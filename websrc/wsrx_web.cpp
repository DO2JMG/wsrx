#include <arpa/inet.h>
#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fcntl.h>
#include <fstream>
#include <limits>
#include <iomanip>
#include <filesystem>
#include <iostream>
#include <map>
#include <netinet/in.h>
#include <sstream>
#include <string>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <vector>
#include <algorithm>
#include <cmath>
#include <optional>

static volatile sig_atomic_t g_stop = 0;

static void on_signal(int) { g_stop = 1; }

static std::string dirname_of(const std::string &path) {
    size_t pos = path.find_last_of('/');
    if (pos == std::string::npos) return ".";
    if (pos == 0) return "/";
    return path.substr(0, pos);
}

static std::string get_exe_dir(const char *argv0) {
    char buf[4096];
    ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (n > 0) {
        buf[n] = '\0';
        return dirname_of(buf);
    }
    char cwd[4096];
    if (getcwd(cwd, sizeof(cwd))) return std::string(cwd);
    return dirname_of(argv0 ? argv0 : ".");
}

static bool file_exists(const std::string &path) {
    struct stat st{};
    return stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}

static bool path_is_safe_static(const std::string &path) {
    if (path.empty()) return false;
    if (path[0] != '/') return false;
    if (path.find("..") != std::string::npos) return false;
    return true;
}

static std::string read_file(const std::string &path, size_t max_bytes = 512 * 1024) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return "";
    f.seekg(0, std::ios::end);
    std::streamoff len = f.tellg();
    if (len < 0) len = 0;
    std::streamoff start = 0;
    if (static_cast<size_t>(len) > max_bytes) start = len - static_cast<std::streamoff>(max_bytes);
    f.seekg(start, std::ios::beg);
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

static std::string ini_value(const std::string &ini_text, const std::string &key) {
    std::istringstream in(ini_text);
    std::string line;
    while (std::getline(in, line)) {
        size_t comment = line.find_first_of("#;");
        if (comment != std::string::npos) line = line.substr(0, comment);
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string k = line.substr(0, eq);
        size_t a = k.find_first_not_of(" \t");
        size_t b = k.find_last_not_of(" \t");
        if (a == std::string::npos) continue;
        k = k.substr(a, b - a + 1);
        if (k != key) continue;
        std::string v = line.substr(eq + 1);
        a = v.find_first_not_of(" \t");
        b = v.find_last_not_of(" \t");
        if (a == std::string::npos) return "";
        return v.substr(a, b - a + 1);
    }
    return "";
}

static std::string resolve_config_path(const std::string &base_dir, const std::string &value, const std::string &fallback_filename) {
    std::string v = value.empty() ? fallback_filename : value;
    if (!v.empty() && v[0] == '/') return v;
    return base_dir + "/" + v;
}

static std::string read_file_full(const std::string &path, size_t max_bytes = 1024 * 1024) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return "";
    f.seekg(0, std::ios::end);
    std::streamoff len = f.tellg();
    if (len < 0 || static_cast<size_t>(len) > max_bytes) return "";
    f.seekg(0, std::ios::beg);
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

static std::string shell_quote(const std::string &s) {
    std::string out = "'";
    for (char c : s) {
        if (c == '\'') out += "'\\''";
        else out += c;
    }
    out += "'";
    return out;
}

static std::string run_cmd(const std::string &cmd, int *exit_code = nullptr) {
    std::string data;
    FILE *fp = popen((cmd + " 2>&1").c_str(), "r");
    if (!fp) {
        if (exit_code) *exit_code = -1;
        return "popen failed";
    }
    char buf[4096];
    while (fgets(buf, sizeof(buf), fp)) data += buf;
    int rc = pclose(fp);
    if (exit_code) {
        if (WIFEXITED(rc)) *exit_code = WEXITSTATUS(rc);
        else *exit_code = rc;
    }
    return data;
}

static std::string json_escape(const std::string &s) {
    std::string out;
    out.reserve(s.size() + 16);
    for (unsigned char c : s) {
        switch (c) {
            case '\\': out += "\\\\"; break;
            case '"': out += "\\\""; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 32) {
                    char tmp[8];
                    snprintf(tmp, sizeof(tmp), "\\u%04x", c);
                    out += tmp;
                } else out += static_cast<char>(c);
        }
    }
    return out;
}

static std::map<std::string, std::string> parse_query(const std::string &q) {
    std::map<std::string, std::string> m;
    size_t pos = 0;
    while (pos < q.size()) {
        size_t amp = q.find('&', pos);
        std::string part = q.substr(pos, amp == std::string::npos ? std::string::npos : amp - pos);
        size_t eq = part.find('=');
        if (eq != std::string::npos) m[part.substr(0, eq)] = part.substr(eq + 1);
        else if (!part.empty()) m[part] = "";
        if (amp == std::string::npos) break;
        pos = amp + 1;
    }
    return m;
}

static std::string active_channels_from_log(const std::string &log) {
    std::vector<std::string> channels;
    std::istringstream iss(log);
    std::string line;
    while (std::getline(iss, line)) {
        const std::string start = "starting decoder channel ";
        const std::string stop = "stopping decoder channel ";
        size_t p = line.find(start);
        if (p != std::string::npos) {
            p += start.size();
            size_t e = line.find(' ', p);
            std::string freq = line.substr(p, e == std::string::npos ? std::string::npos : e - p);
            channels.erase(std::remove(channels.begin(), channels.end(), freq), channels.end());
            channels.push_back(freq);
        }
        p = line.find(stop);
        if (p != std::string::npos) {
            p += stop.size();
            size_t e = line.find(' ', p);
            std::string freq = line.substr(p, e == std::string::npos ? std::string::npos : e - p);
            channels.erase(std::remove(channels.begin(), channels.end(), freq), channels.end());
        }
    }
    std::ostringstream js;
    js << "[";
    for (size_t i = 0; i < channels.size(); ++i) {
        if (i) js << ",";
        js << "\"" << json_escape(channels[i]) << "\"";
    }
    js << "]";
    return js.str();
}

struct App {
    std::string base_dir;
    std::string web_dir;
    std::string script;
    std::string log_file;
    std::string config_file;
    std::string whitelist_file;
    std::string blacklist_file;
    std::string offset_file;
    std::string spectrum_file;
    std::string peaks_file;
    std::string sondes_dir;
    std::string web_auth_user;
    std::string web_auth_pass;
};

static std::string http_date() {
    char buf[128];
    time_t t = time(nullptr);
    struct tm tm{};
    gmtime_r(&t, &tm);
    strftime(buf, sizeof(buf), "%a, %d %b %Y %H:%M:%S GMT", &tm);
    return buf;
}

static void send_response(int fd, int code, const std::string &type, const std::string &body) {
    std::string status = code == 200 ? "OK" : (code == 404 ? "Not Found" : (code == 405 ? "Method Not Allowed" : "Error"));
    std::ostringstream h;
    h << "HTTP/1.1 " << code << " " << status << "\r\n";
    h << "Date: " << http_date() << "\r\n";
    h << "Server: wsrx-web\r\n";
    h << "Content-Type: " << type << "; charset=utf-8\r\n";
    h << "Content-Length: " << body.size() << "\r\n";
    h << "Cache-Control: no-store\r\n";
    h << "Connection: close\r\n\r\n";
    std::string out = h.str() + body;
    send(fd, out.data(), out.size(), MSG_NOSIGNAL);
}

static std::string mime_type(const std::string &path) {
    if (path.size() >= 5 && path.substr(path.size() - 5) == ".html") return "text/html";
    if (path.size() >= 4 && path.substr(path.size() - 4) == ".css") return "text/css";
    if (path.size() >= 3 && path.substr(path.size() - 3) == ".js") return "application/javascript";
    if (path.size() >= 5 && path.substr(path.size() - 5) == ".json") return "application/json";
    if (path.size() >= 4 && path.substr(path.size() - 4) == ".svg") return "image/svg+xml";
    if (path.size() >= 4 && path.substr(path.size() - 4) == ".png") return "image/png";
    return "text/plain";
}

static std::string base64_decode(const std::string &in) {
    static const std::string chars =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::vector<int> lut(256, -1);
    for (int i = 0; i < 64; ++i) lut[static_cast<unsigned char>(chars[i])] = i;
    std::string out;
    int val = 0, valb = -8;
    for (unsigned char c : in) {
        if (lut[c] == -1) break;
        val = (val << 6) + lut[c];
        valb += 6;
        if (valb >= 0) {
            out.push_back(static_cast<char>((val >> valb) & 0xFF));
            valb -= 8;
        }
    }
    return out;
}

static std::string get_header(const std::string &request, const std::string &header_name) {
    std::istringstream in(request);
    std::string line;
    std::getline(in, line); // request line, not a header
    std::string lower_name = header_name;
    std::transform(lower_name.begin(), lower_name.end(), lower_name.begin(),
                    [](unsigned char c) { return std::tolower(c); });
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) break; // end of headers
        size_t colon = line.find(':');
        if (colon == std::string::npos) continue;
        std::string name = line.substr(0, colon);
        std::transform(name.begin(), name.end(), name.begin(),
                        [](unsigned char c) { return std::tolower(c); });
        if (name == lower_name) {
            std::string val = line.substr(colon + 1);
            size_t a = val.find_first_not_of(" \t");
            return a == std::string::npos ? "" : val.substr(a);
        }
    }
    return "";
}

static bool check_auth(const App &app, const std::string &request) {
    if (app.web_auth_user.empty()) return true; // auth disabled: no user configured
    std::string auth_header = get_header(request, "Authorization");
    const std::string prefix = "Basic ";
    if (auth_header.compare(0, prefix.size(), prefix) != 0) return false;
    std::string decoded = base64_decode(auth_header.substr(prefix.size()));
    size_t colon = decoded.find(':');
    if (colon == std::string::npos) return false;
    std::string user = decoded.substr(0, colon);
    std::string pass = decoded.substr(colon + 1);
    return user == app.web_auth_user && pass == app.web_auth_pass;
}

static void send_unauthorized(int fd) {
    const std::string body = "401 Unauthorized\n";
    std::ostringstream h;
    h << "HTTP/1.1 401 Unauthorized\r\n";
    h << "Date: " << http_date() << "\r\n";
    h << "Server: wsrx-web\r\n";
    h << "WWW-Authenticate: Basic realm=\"wsrx\"\r\n";
    h << "Content-Type: text/plain; charset=utf-8\r\n";
    h << "Content-Length: " << body.size() << "\r\n";
    h << "Cache-Control: no-store\r\n";
    h << "Connection: close\r\n\r\n";
    std::string out = h.str() + body;
    send(fd, out.data(), out.size(), MSG_NOSIGNAL);
}

static bool send_static_file(int fd, const App &app, const std::string &request_path) {
    if (!path_is_safe_static(request_path)) return false;
    std::string rel = request_path == "/" ? "/index.html" : request_path;
    std::string full = app.web_dir + rel;
    if (!file_exists(full)) return false;
    std::string body = read_file_full(full, 2 * 1024 * 1024);
    send_response(fd, 200, mime_type(full), body);
    return true;
}

static std::string tail_lines(const std::string &text, int lines) {
    if (lines <= 0) return text;
    int count = 0;
    for (auto it = text.rbegin(); it != text.rend(); ++it) {
        if (*it == '\n') {
            count++;
            if (count > lines) {
                return std::string(it.base(), text.end());
            }
        }
    }
    return text;
}



static std::optional<double> extract_json_number(const std::string &line, const std::string &key) {
    std::string needle = "\"" + key + "\"";
    size_t p = line.find(needle);
    if (p == std::string::npos) return std::nullopt;
    p = line.find(':', p + needle.size());
    if (p == std::string::npos) return std::nullopt;
    ++p;
    while (p < line.size() && std::isspace(static_cast<unsigned char>(line[p]))) ++p;
    size_t e = p;
    while (e < line.size() && (std::isdigit(static_cast<unsigned char>(line[e])) || line[e] == '-' || line[e] == '+' || line[e] == '.')) ++e;
    if (e == p) return std::nullopt;
    try { return std::stod(line.substr(p, e - p)); } catch (...) { return std::nullopt; }
}

static std::optional<std::string> extract_json_string(const std::string &line, const std::string &key) {
    std::string needle = "\"" + key + "\"";
    size_t p = line.find(needle);
    if (p == std::string::npos) return std::nullopt;
    p = line.find(':', p + needle.size());
    if (p == std::string::npos) return std::nullopt;
    p = line.find('"', p + 1);
    if (p == std::string::npos) return std::nullopt;
    ++p;
    std::string out;
    bool esc = false;
    for (; p < line.size(); ++p) {
        char c = line[p];
        if (esc) { out.push_back(c); esc = false; continue; }
        if (c == '\\') { esc = true; continue; }
        if (c == '"') return out;
        out.push_back(c);
    }
    return std::nullopt;
}

static std::string strip_json_ext(const std::string &name) {
    if (name.size() > 5 && name.substr(name.size() - 5) == ".json") return name.substr(0, name.size() - 5);
    return name;
}

static std::string radiosondes_json(const App &app) {
    namespace fs = std::filesystem;
    struct Item {
        std::string serial;
        std::string type;
        std::string first_time;
        std::string last_time;
        double first_alt = std::numeric_limits<double>::quiet_NaN();
        double last_alt = std::numeric_limits<double>::quiet_NaN();
        uintmax_t size = 0;
        long frames = 0;
        std::time_t modified = 0;
    };

    std::vector<Item> items;
    std::error_code ec;
    if (!fs::exists(app.sondes_dir, ec) || !fs::is_directory(app.sondes_dir, ec)) {
        return "{\"radiosondes\":[],\"message\":\"logs/sondes not found yet\"}";
    }

    for (const auto &entry : fs::directory_iterator(app.sondes_dir, ec)) {
        if (ec) break;
        if (!entry.is_regular_file(ec)) continue;
        auto path = entry.path();
        if (path.extension() != ".json") continue;

        Item item;
        item.serial = strip_json_ext(path.filename().string());
        item.size = entry.file_size(ec);

        auto ftime = entry.last_write_time(ec);
        if (!ec) {
            auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
                ftime - fs::file_time_type::clock::now() + std::chrono::system_clock::now());
            item.modified = std::chrono::system_clock::to_time_t(sctp);
        }

        std::ifstream f(path);
        std::string line;
        bool first_valid = true;
        while (std::getline(f, line)) {
            if (line.find('{') == std::string::npos) continue;
            item.frames++;
            if (item.type.empty()) {
                if (auto t = extract_json_string(line, "subtype")) item.type = *t;
                else if (auto t2 = extract_json_string(line, "type")) item.type = *t2;
            }
            auto alt = extract_json_number(line, "alt");
            if (!alt) alt = extract_json_number(line, "altitude");
            auto dt = extract_json_string(line, "datetime");
            if (!dt) dt = extract_json_string(line, "time");

            if (first_valid) {
                if (alt) item.first_alt = *alt;
                if (dt) item.first_time = *dt;
                first_valid = false;
            }
            if (alt) item.last_alt = *alt;
            if (dt) item.last_time = *dt;
        }
        if (item.frames > 0) items.push_back(item);
    }

    std::sort(items.begin(), items.end(), [](const Item &a, const Item &b) {
        return a.modified > b.modified;
    });

    std::ostringstream js;
    js << "{\"radiosondes\":[";
    for (size_t i = 0; i < items.size(); ++i) {
        const auto &it = items[i];
        if (i) js << ",";
        js << "{\"serial\":\"" << json_escape(it.serial) << "\"";
        js << ",\"type\":\"" << json_escape(it.type) << "\"";
        js << ",\"frames\":" << it.frames;
        js << ",\"size\":" << it.size;
        js << ",\"first_altitude\":";
        if (std::isfinite(it.first_alt)) js << std::fixed << std::setprecision(1) << it.first_alt; else js << "null";
        js << ",\"last_altitude\":";
        if (std::isfinite(it.last_alt)) js << std::fixed << std::setprecision(1) << it.last_alt; else js << "null";
        js << ",\"first_time\":\"" << json_escape(it.first_time) << "\"";
        js << ",\"last_time\":\"" << json_escape(it.last_time) << "\"";
        js << ",\"modified\":" << static_cast<long long>(it.modified);
        js << "}";
    }
    js << "]}";
    return js.str();
}

static void handle_client(int fd, const App &app) {
    char buf[8192];
    ssize_t n = recv(fd, buf, sizeof(buf) - 1, 0);
    if (n <= 0) { close(fd); return; }
    buf[n] = '\0';
    std::string request_str(buf, static_cast<size_t>(n));

    if (!check_auth(app, request_str)) {
        send_unauthorized(fd);
        close(fd);
        return;
    }

    std::istringstream req(buf);
    std::string method, target, version;
    req >> method >> target >> version;
    std::string path = target;
    std::string query;
    size_t qpos = target.find('?');
    if (qpos != std::string::npos) {
        path = target.substr(0, qpos);
        query = target.substr(qpos + 1);
    }

    if (path.rfind("/api/", 0) != 0 && method == "GET") {
        if (send_static_file(fd, app, path)) { close(fd); return; }
    }

    if (path == "/api/status") {
        int rc = 0;
        std::string raw = run_cmd(shell_quote(app.script) + " status", &rc);
        bool running = rc == 0;
        std::string pid;
        std::string pidfile = app.base_dir + "/pidfiles/wsrx.pid";
        if (file_exists(pidfile)) pid = read_file(pidfile, 64);
        pid.erase(std::remove(pid.begin(), pid.end(), '\n'), pid.end());
        std::string log = read_file(app.log_file, 512 * 1024);
        std::ostringstream js;
        js << "{\"running\":" << (running ? "true" : "false")
           << ",\"pid\":\"" << json_escape(pid) << "\""
           << ",\"base_dir\":\"" << json_escape(app.base_dir) << "\""
           << ",\"web_dir\":\"" << json_escape(app.web_dir) << "\""
           << ",\"raw\":\"" << json_escape(raw) << "\""
           << ",\"channels\":" << active_channels_from_log(log) << "}";
        send_response(fd, 200, "application/json", js.str());
    } else if (path == "/api/log") {
        int lines = 300;
        auto qm = parse_query(query);
        if (qm.count("lines")) lines = std::max(1, std::min(2000, atoi(qm["lines"].c_str())));
        send_response(fd, 200, "text/plain", tail_lines(read_file(app.log_file, 1024 * 1024), lines));
    } else if (path == "/api/config") {
        send_response(fd, 200, "text/plain", read_file(app.config_file, 256 * 1024));
    } else if (path == "/api/whitelist") {
        std::string t = read_file(app.whitelist_file, 256 * 1024);
        if (t.empty() && !file_exists(app.whitelist_file)) t = app.whitelist_file + " not found\n";
        send_response(fd, 200, "text/plain", t);
    } else if (path == "/api/blacklist") {
        std::string t = read_file(app.blacklist_file, 256 * 1024);
        if (t.empty() && !file_exists(app.blacklist_file)) t = app.blacklist_file + " not found\n";
        send_response(fd, 200, "text/plain", t);
    } else if (path == "/api/offsets") {
        std::string t = read_file(app.offset_file, 256 * 1024);
        if (t.empty() && !file_exists(app.offset_file)) t = "offset_cache.txt not found yet\n";
        send_response(fd, 200, "text/plain", t);
    } else if (path == "/api/spectrum") {
        std::string t = read_file(app.spectrum_file, 2 * 1024 * 1024);
        if (t.empty() && !file_exists(app.spectrum_file)) {
            std::string legacy = app.base_dir + "/data/scan_spectrum.json";
            t = read_file(legacy, 2 * 1024 * 1024);
        }
        if (t.empty()) {
            t = "{\"error\":\"spectrum_live.json not found yet\",\"points\":[]}";
        }
        send_response(fd, 200, "application/json", t);
    } else if (path == "/api/peaks") {
        std::string t = read_file(app.peaks_file, 512 * 1024);
        if (t.empty()) {
            std::string legacy = app.base_dir + "/data/scan_spectrum.json";
            t = read_file(legacy, 512 * 1024);
        }
        if (t.empty()) {
            t = "{\"error\":\"scan_peaks.json not found yet\",\"peaks\":[]}";
        }
        send_response(fd, 200, "application/json", t);
    } else if (path == "/api/radiosondes") {
        send_response(fd, 200, "application/json", radiosondes_json(app));
    } else if (path == "/api/clearlogs") {
        if (method != "POST") send_response(fd, 405, "text/plain", "POST required\n");
        else {
            int rc = 0;
            std::string out = run_cmd(shell_quote(app.script) + " clearlogs", &rc);
            send_response(fd, rc == 0 ? 200 : 500, "text/plain", out);
        }
    } else if (path == "/api/start" || path == "/api/stop" || path == "/api/restart") {
        if (method != "POST") send_response(fd, 405, "text/plain", "POST required\n");
        else {
            std::string action = path.substr(std::string("/api/").size());
            int rc = 0;

            std::string out = run_cmd(shell_quote(app.script) + " " + action + " wsrx", &rc);
            send_response(fd, rc == 0 ? 200 : 500, "text/plain", out);
        }
    } else {
        send_response(fd, 404, "text/plain", "not found\n");
    }
    close(fd);
}

int main(int argc, char **argv) {
    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);

    App app;
    app.base_dir = get_exe_dir(argv[0]);
    int port = 8073;
    std::string bind_addr = "0.0.0.0";

    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "-port" && i + 1 < argc) port = atoi(argv[++i]);
        else if (a == "-bind" && i + 1 < argc) bind_addr = argv[++i];
        else if (a == "-dir" && i + 1 < argc) app.base_dir = argv[++i];
        else if (a == "-h" || a == "-help") {
            std::cout << "Usage: wsrx-web [-bind 0.0.0.0] [-port 8073] [-dir /path/to/wsrx]\n";
            return 0;
        }
    }

    app.web_dir = app.base_dir + "/web";
    app.script = app.base_dir + "/wsrx.sh";
    app.log_file = app.base_dir + "/logs/wsrx.log";
    app.config_file = app.base_dir + "/config.ini";
    {
        std::string ini_text = read_file(app.config_file, 256 * 1024);
        app.whitelist_file = resolve_config_path(app.base_dir, ini_value(ini_text, "whitelist_file"), "whitelist.txt");
        app.blacklist_file = resolve_config_path(app.base_dir, ini_value(ini_text, "blacklist_file"), "blacklist.txt");
        app.web_auth_user = ini_value(ini_text, "web_auth_user");
        app.web_auth_pass = ini_value(ini_text, "web_auth_password");
    }
    app.offset_file = app.base_dir + "/offset_cache.txt";
    app.spectrum_file = app.base_dir + "/data/spectrum_live.json";
    app.peaks_file = app.base_dir + "/data/scan_peaks.json";
    app.sondes_dir = app.base_dir + "/logs/sondes";

    int s = socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) { perror("socket"); return 1; }
    int yes = 1;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(port));
    if (inet_pton(AF_INET, bind_addr.c_str(), &addr.sin_addr) != 1) {
        std::cerr << "invalid bind address: " << bind_addr << "\n";
        return 1;
    }
    if (bind_addr == "0.0.0.0") addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(s, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) < 0) {
        perror("bind");
        return 1;
    }
    if (listen(s, 16) < 0) { perror("listen"); return 1; }

    std::cout << "wsrx-web listening on http://" << bind_addr << ":" << port << "\n";
    std::cout << "base_dir=" << app.base_dir << "\n";
    std::cout << "web_dir=" << app.web_dir << "\n";
    if (!app.web_auth_user.empty()) {
        std::cout << "web auth: enabled (user=" << app.web_auth_user << ")\n";
    } else {
        std::cout << "web auth: disabled (set web_auth_user/web_auth_password in config.ini [web] section to enable)\n";
    }

    while (!g_stop) {
        sockaddr_in caddr{};
        socklen_t clen = sizeof(caddr);
        int c = accept(s, reinterpret_cast<sockaddr *>(&caddr), &clen);
        if (c < 0) {
            if (errno == EINTR) continue;
            perror("accept");
            break;
        }
        std::thread(handle_client, c, app).detach();
    }
    close(s);
    return 0;
}

