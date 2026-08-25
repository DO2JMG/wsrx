#include <arpa/inet.h>
#include <cctype>
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
#include <atomic>
#include <cmath>
#include <mutex>
#include <optional>
#include <set>
#include <array>

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

static std::mutex g_file_write_mutex;

static bool write_file_atomic(const std::string &path, const std::string &content) {
    std::lock_guard<std::mutex> lock(g_file_write_mutex);
    std::error_code ec;
    std::filesystem::create_directories(dirname_of(path), ec);
    std::string tmp = path + ".tmp";
    std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out.write(content.data(), static_cast<std::streamsize>(content.size()));
    out.close();
    if (!out) return false;
    if (std::rename(tmp.c_str(), path.c_str()) != 0) return false;
    return true;
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

static std::atomic<bool> g_update_running{false};

static bool run_detached_to_log(const std::string &cmd, const std::string &log_path) {
    std::error_code ec;
    std::filesystem::create_directories(dirname_of(log_path), ec);
    std::string full = cmd + " > " + shell_quote(log_path) + " 2>&1 < /dev/null &";
    return std::system(full.c_str()) == 0;
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
    std::string spectrum_file;
    std::string peaks_file;
    std::string sondes_dir;
    std::string cpu_file;
    std::string version_file;
    std::string update_script;
    std::string update_log;
    std::string web_auth_user;
    std::string web_auth_pass;
    double station_lat = std::numeric_limits<double>::quiet_NaN();
    double station_lon = std::numeric_limits<double>::quiet_NaN();
    double station_alt = 0.0;
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



struct CpuTimes {
    unsigned long long idle = 0;
    unsigned long long total = 0;
};

static CpuTimes read_cpu_times() {
    CpuTimes t;
    std::ifstream f("/proc/stat");
    std::string line;
    if (!std::getline(f, line)) return t;
    std::istringstream iss(line);
    std::string cpu_label;
    unsigned long long user = 0, nice = 0, system = 0, idle = 0, iowait = 0,
                        irq = 0, softirq = 0, steal = 0, guest = 0, guest_nice = 0;
    iss >> cpu_label >> user >> nice >> system >> idle >> iowait >> irq >> softirq >> steal >> guest >> guest_nice;
    t.idle = idle + iowait;
    t.total = user + nice + system + idle + iowait + irq + softirq + steal;
    return t;
}

static double cpu_usage_percent(const CpuTimes &prev, const CpuTimes &cur) {
    if (cur.total <= prev.total) return 0.0;
    unsigned long long d_total = cur.total - prev.total;
    unsigned long long d_idle = (cur.idle >= prev.idle) ? (cur.idle - prev.idle) : 0;
    double usage = (1.0 - static_cast<double>(d_idle) / static_cast<double>(d_total)) * 100.0;
    if (usage < 0.0) usage = 0.0;
    if (usage > 100.0) usage = 100.0;
    return usage;
}

static void cpu_monitor_thread(App app) {
    std::error_code ec;
    std::filesystem::create_directories(dirname_of(app.cpu_file), ec);

    CpuTimes prev = read_cpu_times();
    std::vector<double> samples;

    while (!g_stop) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        if (g_stop) break;

        CpuTimes cur = read_cpu_times();
        double pct = cpu_usage_percent(prev, cur);
        prev = cur;
        samples.push_back(pct);

        if (samples.size() >= 3) {
            double avg = 0.0;
            for (double s : samples) avg += s;
            avg /= static_cast<double>(samples.size());
            samples.clear();

            std::ostringstream js;
            js << "{\"cpu_percent\":" << std::fixed << std::setprecision(1) << avg
               << ",\"timestamp\":" << static_cast<long long>(time(nullptr)) << "}";

            std::string tmp = app.cpu_file + ".tmp";
            std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
            if (out) {
                out << js.str();
                out.close();
                std::rename(tmp.c_str(), app.cpu_file.c_str());
            }
        }
    }
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

static int hex_val(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
    if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
    return -1;
}

static void append_utf8(std::string &out, unsigned int cp) {
    if (cp <= 0x7F) {
        out.push_back(static_cast<char>(cp));
    } else if (cp <= 0x7FF) {
        out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp <= 0xFFFF) {
        out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
}

static std::optional<unsigned int> read_unicode_escape(const std::string &line, size_t &p) {
    if (p + 5 >= line.size() || line[p] != '\\' || line[p + 1] != 'u') return std::nullopt;
    int h0 = hex_val(line[p + 2]), h1 = hex_val(line[p + 3]), h2 = hex_val(line[p + 4]), h3 = hex_val(line[p + 5]);
    if (h0 < 0 || h1 < 0 || h2 < 0 || h3 < 0) return std::nullopt;
    unsigned int cp = (static_cast<unsigned int>(h0) << 12) | (static_cast<unsigned int>(h1) << 8) |
                       (static_cast<unsigned int>(h2) << 4) | static_cast<unsigned int>(h3);
    p += 5;
    return cp;
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
    for (; p < line.size(); ++p) {
        char c = line[p];
        if (c == '"') return out;
        if (c != '\\' || p + 1 >= line.size()) {
            out.push_back(c);
            continue;
        }

        size_t up = p;
        if (auto cp = read_unicode_escape(line, up)) {
            unsigned int codepoint = *cp;
            // Combine a UTF-16 surrogate pair into a single code point.
            if (codepoint >= 0xD800 && codepoint <= 0xDBFF) {
                size_t np = up + 1;
                if (auto low = read_unicode_escape(line, np); low && *low >= 0xDC00 && *low <= 0xDFFF) {
                    codepoint = 0x10000 + ((codepoint - 0xD800) << 10) + (*low - 0xDC00);
                    up = np;
                }
            }
            append_utf8(out, codepoint);
            p = up;
            continue;
        }

        char next = line[p + 1];
        switch (next) {
            case 'n': out.push_back('\n'); break;
            case 't': out.push_back('\t'); break;
            case 'r': out.push_back('\r'); break;
            case 'b': out.push_back('\b'); break;
            case 'f': out.push_back('\f'); break;
            default: out.push_back(next); break; // handles \" \\ \/ etc.
        }
        ++p;
    }
    return std::nullopt;
}

static std::string strip_json_ext(const std::string &name) {
    if (name.size() > 5 && name.substr(name.size() - 5) == ".json") return name.substr(0, name.size() - 5);
    return name;
}

static double haversine_km(double lat1, double lon1, double lat2, double lon2) {
    constexpr double R = 6371.0088; // mean Earth radius, km
    constexpr double kPi = 3.14159265358979323846;
    const double dlat = (lat2 - lat1) * kPi / 180.0;
    const double dlon = (lon2 - lon1) * kPi / 180.0;
    const double a = std::sin(dlat / 2) * std::sin(dlat / 2) +
                      std::cos(lat1 * kPi / 180.0) * std::cos(lat2 * kPi / 180.0) *
                      std::sin(dlon / 2) * std::sin(dlon / 2);
    const double c = 2 * std::atan2(std::sqrt(a), std::sqrt(1 - a));
    return R * c;
}

// Initial great-circle bearing from (lat1,lon1) to (lat2,lon2), in degrees,
// normalized to [0, 360). 0 = north, 90 = east, etc.
static double bearing_deg(double lat1, double lon1, double lat2, double lon2) {
    constexpr double kPi = 3.14159265358979323846;
    const double phi1 = lat1 * kPi / 180.0;
    const double phi2 = lat2 * kPi / 180.0;
    const double dlon = (lon2 - lon1) * kPi / 180.0;
    const double y = std::sin(dlon) * std::cos(phi2);
    const double x = std::cos(phi1) * std::sin(phi2) - std::sin(phi1) * std::cos(phi2) * std::cos(dlon);
    double deg = std::atan2(y, x) * 180.0 / kPi;
    if (deg < 0) deg += 360.0;
    return deg;
}

static double elevation_deg(double lat1, double lon1, double alt1, double lat2, double lon2, double alt2) {
    constexpr double kPi = 3.14159265358979323846;
    constexpr double kEarthRadius = 6371000.0; // meters
    const double phi1 = lat1 * kPi / 180.0;
    const double phi2 = lat2 * kPi / 180.0;
    const double dlon = (lon2 - lon1) * kPi / 180.0;
    const double central_angle = std::acos(
        std::max(-1.0, std::min(1.0,
            std::sin(phi1) * std::sin(phi2) + std::cos(phi1) * std::cos(phi2) * std::cos(dlon))));

    const double ta = kEarthRadius + alt1;
    const double tb = kEarthRadius + alt2;
    const double ea = std::cos(central_angle) * tb - ta;
    const double eb = std::sin(central_angle) * tb;
    return std::atan2(ea, eb) * 180.0 / kPi;
}

static std::string sanitize_serial(const std::string &s) {
    std::string out;
    for (unsigned char c : s) {
        if (std::isalnum(c) || c == '_' || c == '-') out.push_back(static_cast<char>(c));
        else out.push_back('_');
    }
    if (out.empty()) out = "unknown";
    return out;
}

static std::mutex g_launchsite_mutex;
static std::map<std::string, std::string> g_launchsite_cache; // serial -> launchsite name ("" = looked up, none found)
static std::set<std::string> g_launchsite_pending;

static void fetchLaunchsiteBackground(std::string serial) {
    const std::string safe_serial = sanitize_serial(serial);
    const std::string url = "http://api.wettersonde.net/sonde.php?sonde=" + safe_serial + "&wsrx=1";
    const std::string body = run_cmd("curl -fsS -m 6 " + shell_quote(url));
    std::string site;
    if (auto s = extract_json_string(body, "launchsite")) site = *s;

    std::lock_guard<std::mutex> lock(g_launchsite_mutex);
    g_launchsite_cache[serial] = site;
    g_launchsite_pending.erase(serial);
}

static std::optional<std::string> getLaunchsiteCached(const std::string &serial) {
    std::lock_guard<std::mutex> lock(g_launchsite_mutex);
    auto it = g_launchsite_cache.find(serial);
    if (it != g_launchsite_cache.end()) return it->second;
    if (g_launchsite_pending.insert(serial).second) {
        std::thread(fetchLaunchsiteBackground, serial).detach();
    }
    return std::nullopt;
}

static std::optional<std::string> json_parse_string_at(const std::string &s, size_t &pos) {
    if (pos >= s.size() || s[pos] != '"') return std::nullopt;
    size_t p = pos + 1;
    std::string out;
    for (; p < s.size(); ++p) {
        char c = s[p];
        if (c == '"') { pos = p + 1; return out; }
        if (c != '\\' || p + 1 >= s.size()) {
            out.push_back(c);
            continue;
        }

        size_t up = p;
        if (auto cp = read_unicode_escape(s, up)) {
            unsigned int codepoint = *cp;
            if (codepoint >= 0xD800 && codepoint <= 0xDBFF) {
                size_t np = up + 1;
                if (auto low = read_unicode_escape(s, np); low && *low >= 0xDC00 && *low <= 0xDFFF) {
                    codepoint = 0x10000 + ((codepoint - 0xD800) << 10) + (*low - 0xDC00);
                    up = np;
                }
            }
            append_utf8(out, codepoint);
            p = up;
            continue;
        }

        char next = s[p + 1];
        switch (next) {
            case 'n': out.push_back('\n'); break;
            case 't': out.push_back('\t'); break;
            case 'r': out.push_back('\r'); break;
            case 'b': out.push_back('\b'); break;
            case 'f': out.push_back('\f'); break;
            case '/': out.push_back('/'); break;
            default: out.push_back(next); break; // handles \" \\ etc.
        }
        ++p;
    }
    return std::nullopt; // unterminated string
}

struct JsonValue {
    enum class Type { Null, Bool, Number, String, Array, Object } type = Type::Null;
    bool b = false;
    double num = 0.0;
    std::string str;
    std::vector<JsonValue> arr;
    std::map<std::string, JsonValue> obj;

    bool isArray() const { return type == Type::Array; }
    bool isObject() const { return type == Type::Object; }
    bool isNumber() const { return type == Type::Number; }
    bool isString() const { return type == Type::String; }

    const JsonValue *get(const std::string &key) const {
        if (type != Type::Object) return nullptr;
        auto it = obj.find(key);
        return it == obj.end() ? nullptr : &it->second;
    }
};

class JsonParser {
public:
    explicit JsonParser(const std::string &s) : s_(s) {}

    std::optional<JsonValue> parse() {
        skipWs();
        return parseValue();
    }

private:
    const std::string &s_;
    size_t p_ = 0;

    void skipWs() {
        while (p_ < s_.size() && std::isspace(static_cast<unsigned char>(s_[p_]))) ++p_;
    }

    std::optional<JsonValue> parseValue() {
        skipWs();
        if (p_ >= s_.size()) return std::nullopt;
        char c = s_[p_];
        if (c == '{') return parseObject();
        if (c == '[') return parseArray();
        if (c == '"') return parseStringValue();
        if (c == 't' || c == 'f') return parseBool();
        if (c == 'n') return parseNull();
        return parseNumber();
    }

    std::optional<JsonValue> parseStringValue() {
        auto str = json_parse_string_at(s_, p_);
        if (!str) return std::nullopt;
        JsonValue v;
        v.type = JsonValue::Type::String;
        v.str = *str;
        return v;
    }

    std::optional<JsonValue> parseBool() {
        if (s_.compare(p_, 4, "true") == 0) { p_ += 4; JsonValue v; v.type = JsonValue::Type::Bool; v.b = true; return v; }
        if (s_.compare(p_, 5, "false") == 0) { p_ += 5; JsonValue v; v.type = JsonValue::Type::Bool; v.b = false; return v; }
        return std::nullopt;
    }

    std::optional<JsonValue> parseNull() {
        if (s_.compare(p_, 4, "null") == 0) { p_ += 4; JsonValue v; v.type = JsonValue::Type::Null; return v; }
        return std::nullopt;
    }

    std::optional<JsonValue> parseNumber() {
        size_t start = p_;
        if (p_ < s_.size() && (s_[p_] == '-' || s_[p_] == '+')) ++p_;
        while (p_ < s_.size() && (std::isdigit(static_cast<unsigned char>(s_[p_])) || s_[p_] == '.' ||
               s_[p_] == 'e' || s_[p_] == 'E' || s_[p_] == '+' || s_[p_] == '-')) ++p_;
        if (p_ == start) return std::nullopt;
        try {
            JsonValue v;
            v.type = JsonValue::Type::Number;
            v.num = std::stod(s_.substr(start, p_ - start));
            return v;
        } catch (...) {
            return std::nullopt;
        }
    }

    std::optional<JsonValue> parseArray() {
        ++p_; // consume '['
        JsonValue v;
        v.type = JsonValue::Type::Array;
        skipWs();
        if (p_ < s_.size() && s_[p_] == ']') { ++p_; return v; }
        while (true) {
            auto elem = parseValue();
            if (!elem) return std::nullopt;
            v.arr.push_back(std::move(*elem));
            skipWs();
            if (p_ < s_.size() && s_[p_] == ',') { ++p_; continue; }
            if (p_ < s_.size() && s_[p_] == ']') { ++p_; break; }
            return std::nullopt;
        }
        return v;
    }

    std::optional<JsonValue> parseObject() {
        ++p_; // consume '{'
        JsonValue v;
        v.type = JsonValue::Type::Object;
        skipWs();
        if (p_ < s_.size() && s_[p_] == '}') { ++p_; return v; }
        while (true) {
            skipWs();
            if (p_ >= s_.size() || s_[p_] != '"') return std::nullopt;
            auto key = json_parse_string_at(s_, p_);
            if (!key) return std::nullopt;
            skipWs();
            if (p_ >= s_.size() || s_[p_] != ':') return std::nullopt;
            ++p_;
            auto val = parseValue();
            if (!val) return std::nullopt;
            v.obj[*key] = std::move(*val);
            skipWs();
            if (p_ < s_.size() && s_[p_] == ',') { ++p_; continue; }
            if (p_ < s_.size() && s_[p_] == '}') { ++p_; break; }
            return std::nullopt;
        }
        return v;
    }
};

struct SondeFrame {
    double lat = std::numeric_limits<double>::quiet_NaN();
    double lon = std::numeric_limits<double>::quiet_NaN();
    double alt = std::numeric_limits<double>::quiet_NaN();
    double v_speed = std::numeric_limits<double>::quiet_NaN();
    std::string datetime;
};

static std::vector<SondeFrame> load_sonde_frames(const App &app, const std::string &serial_raw) {
    std::vector<SondeFrame> frames;
    const std::string serial = sanitize_serial(serial_raw);
    const std::string path = app.sondes_dir + "/" + serial + ".json";
    std::ifstream f(path);
    if (!f) return frames;

    std::string line;
    while (std::getline(f, line)) {
        if (line.find('{') == std::string::npos) continue;
        SondeFrame fr;
        if (auto v = extract_json_number(line, "lat")) fr.lat = *v;
        else if (auto v2 = extract_json_number(line, "latitude")) fr.lat = *v2;
        if (auto v = extract_json_number(line, "lon")) fr.lon = *v;
        else if (auto v2 = extract_json_number(line, "longitude")) fr.lon = *v2;
        if (auto v = extract_json_number(line, "alt")) fr.alt = *v;
        else if (auto v2 = extract_json_number(line, "altitude")) fr.alt = *v2;
        if (auto v = extract_json_number(line, "vel_v")) fr.v_speed = *v;
        if (auto v = extract_json_string(line, "datetime")) fr.datetime = *v;
        else if (auto v2 = extract_json_string(line, "time")) fr.datetime = *v2;
        frames.push_back(std::move(fr));
    }
    return frames;
}

static bool sonde_is_descending(const std::vector<SondeFrame> &frames) {
    if (frames.empty()) return false;

    size_t start = frames.size() > 6 ? frames.size() - 6 : 0;
    double firstAlt = std::numeric_limits<double>::quiet_NaN();
    double lastAlt = std::numeric_limits<double>::quiet_NaN();
    int seen = 0;
    for (size_t i = start; i < frames.size(); ++i) {
        if (!std::isfinite(frames[i].alt)) continue;
        if (!std::isfinite(firstAlt)) firstAlt = frames[i].alt;
        lastAlt = frames[i].alt;
        ++seen;
    }
    if (seen >= 2 && std::isfinite(firstAlt) && std::isfinite(lastAlt)) {
        return lastAlt < firstAlt - 1.0;
    }
    if (std::isfinite(frames.back().v_speed)) return frames.back().v_speed < 0;
    return false;
}

static double burst_descent_factor(double burst_calc) {
    double f = 0.0;
    if (burst_calc < 30000) f = ((burst_calc - 20000) * (0.15 - 0.3) / (30000 - 20000)) + 0.3;
    if (burst_calc < 20000) f = ((burst_calc - 10000) * (0.3 - 0.5) / (20000 - 10000)) + 0.5;
    if (burst_calc < 10000) f = ((burst_calc - 8000) * (0.5 - 0.6) / (10000 - 8000)) + 0.6;
    if (burst_calc < 8000)  f = ((burst_calc - 6000) * (0.6 - 0.75) / (8000 - 6000)) + 0.75;
    if (burst_calc < 6000)  f = ((burst_calc - 3000) * (0.75 - 0.95) / (6000 - 3000)) + 0.95;
    if (burst_calc < 3000)  f = ((burst_calc - 2000) * (0.95 - 0.98) / (3000 - 2000)) + 0.98;
    if (burst_calc < 2000)  f = (burst_calc - 1000) * (0.98 - 1) / (2000 - 1000) + 1;
    return f;
}

static std::string predict_cache_path(const App &app, const std::string &serial) {
    return app.base_dir + "/data/prediction/" + sanitize_serial(serial) + ".json";
}

static void update_prediction_for_sonde(const App &app, const std::string &serial) {
    auto frames = load_sonde_frames(app, serial);
    if (frames.empty()) return;
    const SondeFrame &last = frames.back();
    if (!std::isfinite(last.lat) || !std::isfinite(last.lon) || !std::isfinite(last.alt) || !std::isfinite(last.v_speed)) return;
    if (last.alt >= 25000.0) return; // matches realtime.php's $predictionMaxAltitude
    if (!sonde_is_descending(frames)) return;

    double longitude = last.lon < 0 ? last.lon + 360.0 : last.lon;
    double burst_calc = last.alt - 42.0;
    double burst_faktor = burst_descent_factor(burst_calc);
    double v_speed_abs = std::abs(std::round(last.v_speed));

    double ascent_rate_req = std::max(0.1, v_speed_abs);
    double descent_rate_req = std::max(0.1, std::round(v_speed_abs * burst_faktor));
    double burst_altitude_req = std::max(200.0, std::round(burst_calc));
    double launch_altitude_req = std::max(0.0, std::round(last.alt - 49.0));

    char dtbuf[64];
    time_t t = time(nullptr);
    struct tm tm{};
    gmtime_r(&t, &tm);
    strftime(dtbuf, sizeof(dtbuf), "%Y-%m-%dT%H:%M:%SZ", &tm);

    std::ostringstream q;
    q << "https://predict.wettersonde.net/?launch_latitude=" << std::fixed << std::setprecision(6) << last.lat
      << "&launch_longitude=" << longitude
      << "&launch_altitude=" << std::setprecision(0) << launch_altitude_req
      << "&launch_datetime=" << dtbuf
      << "&ascent_rate=" << std::setprecision(2) << ascent_rate_req
      << "&burst_altitude=" << std::setprecision(0) << burst_altitude_req
      << "&descent_rate=" << std::setprecision(2) << descent_rate_req;

    std::string body = run_cmd("curl -fsS -m 15 " + shell_quote(q.str()));
    if (body.empty()) return;

    JsonParser parser(body);
    auto parsed = parser.parse();
    if (!parsed || !parsed->isObject() || !parsed->get("prediction") || !parsed->get("prediction")->isArray()) {
        std::cerr << "wsrx-web: prediction request failed for " << serial << " (query=" << q.str() << ")\n";
        return;
    }

    write_file_atomic(predict_cache_path(app, serial), body);
}

struct PredictionOutput {
    std::vector<std::array<double, 3>> track; // lat, lon, alt (all stages, ascent+descent)
    bool has_landing = false;
    double landing_lat = std::numeric_limits<double>::quiet_NaN();
    double landing_lon = std::numeric_limits<double>::quiet_NaN();
    double landing_alt = std::numeric_limits<double>::quiet_NaN();
    std::string landing_time;
};

static std::optional<PredictionOutput> extract_prediction_output(const JsonValue &root) {
    const JsonValue *stages = root.get("prediction");
    if (!stages || !stages->isArray()) return std::nullopt;

    PredictionOutput out;

    for (const auto &stage : stages->arr) {
        const JsonValue *traj = stage.get("trajectory");
        if (!traj || !traj->isArray()) continue;
        for (const auto &pt : traj->arr) {
            const JsonValue *lat = pt.get("latitude");
            const JsonValue *lon = pt.get("longitude");
            const JsonValue *alt = pt.get("altitude");
            if (!lat || !lon || !lat->isNumber() || !lon->isNumber()) continue;
            double lo = lon->num;
            if (lo > 180) lo -= 360;
            double al = (alt && alt->isNumber()) ? alt->num : std::numeric_limits<double>::quiet_NaN();
            out.track.push_back({lat->num, lo, al});
        }
    }

    for (const auto &stage : stages->arr) {
        const JsonValue *stageName = stage.get("stage");
        if (!stageName || !stageName->isString() || stageName->str != "descent") continue;
        const JsonValue *traj = stage.get("trajectory");
        if (!traj || !traj->isArray() || traj->arr.empty()) continue;
        const JsonValue &pt = traj->arr.back();
        const JsonValue *lat = pt.get("latitude");
        const JsonValue *lon = pt.get("longitude");
        if (lat && lon && lat->isNumber() && lon->isNumber()) {
            out.has_landing = true;
            out.landing_lat = lat->num;
            out.landing_lon = lon->num > 180 ? lon->num - 360 : lon->num;
            const JsonValue *alt = pt.get("altitude");
            out.landing_alt = (alt && alt->isNumber()) ? alt->num : std::numeric_limits<double>::quiet_NaN();
            const JsonValue *dtv = pt.get("datetime");
            if (dtv && dtv->isString()) out.landing_time = dtv->str;
        }
        break;
    }

    if (out.track.empty()) return std::nullopt;
    return out;
}

static std::string prediction_json_for_serial(const App &app, const std::string &serial) {
    std::string body = read_file_full(predict_cache_path(app, serial), 2 * 1024 * 1024);
    if (body.empty()) return "null";

    JsonParser parser(body);
    auto parsed = parser.parse();
    if (!parsed) return "null";
    auto out = extract_prediction_output(*parsed);
    if (!out) return "null";

    std::ostringstream js;
    js << "{\"track\":[";
    for (size_t i = 0; i < out->track.size(); ++i) {
        if (i) js << ",";
        const auto &p = out->track[i];
        js << "[" << std::fixed << std::setprecision(6) << p[0] << "," << p[1] << ",";
        if (std::isfinite(p[2])) js << std::setprecision(1) << p[2]; else js << "null";
        js << "]";
    }
    js << "]";
    js << ",\"landing\":";
    if (out->has_landing) {
        js << "{\"lat\":" << std::fixed << std::setprecision(6) << out->landing_lat
           << ",\"lon\":" << out->landing_lon
           << ",\"alt\":";
        if (std::isfinite(out->landing_alt)) js << std::setprecision(1) << out->landing_alt; else js << "null";
        js << ",\"time\":\"" << json_escape(out->landing_time) << "\"}";
    } else {
        js << "null";
    }
    js << "}";
    return js.str();
}

static bool serial_is_undecoded(const std::string &serial) {
    int run = 0;
    for (unsigned char c : serial) {
        if (c == 'x' || c == 'X') {
            if (++run >= 4) return true;
        } else {
            run = 0;
        }
    }
    return false;
}

static void prediction_scheduler_thread(App app) {
    namespace fs = std::filesystem;
    const long long PREDICTION_ACTIVE_MAX_AGE_SEC = 180;
    const long long PREDICT_MIN_INTERVAL_SEC = 30;

    while (!g_stop) {
        std::this_thread::sleep_for(std::chrono::seconds(5));
        if (g_stop) break;

        std::error_code ec;
        if (!fs::exists(app.sondes_dir, ec) || !fs::is_directory(app.sondes_dir, ec)) continue;

        const std::time_t now = std::time(nullptr);

        for (const auto &entry : fs::directory_iterator(app.sondes_dir, ec)) {
            if (ec) break;
            if (g_stop) break;
            if (!entry.is_regular_file(ec)) continue;
            auto path = entry.path();
            if (path.extension() != ".json") continue;

            std::error_code mtime_ec;
            auto ftime = entry.last_write_time(mtime_ec);
            if (mtime_ec) continue;
            auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
                ftime - fs::file_time_type::clock::now() + std::chrono::system_clock::now());
            std::time_t modified = std::chrono::system_clock::to_time_t(sctp);
            if ((now - modified) > PREDICTION_ACTIVE_MAX_AGE_SEC) continue;

            std::string serial = strip_json_ext(path.filename().string());
            if (serial_is_undecoded(serial)) continue;

            struct stat cache_st{};
            std::string cache_path = predict_cache_path(app, serial);
            bool have_cache = stat(cache_path.c_str(), &cache_st) == 0;
            if (have_cache && (now - cache_st.st_mtime) < PREDICT_MIN_INTERVAL_SEC) continue;

            update_prediction_for_sonde(app, serial);
        }
    }
}

static bool looksLikeSondeSubtypeName(const std::string& s) {
    if (s.empty()) return false;
    for (unsigned char c : s) {
        if (c < '0' || c > '9') return true;
    }
    return false;
}

static std::string radiosondes_json(const App &app, long long max_age_sec, bool include_track = false) {
    namespace fs = std::filesystem;
    struct TrackPoint {
        double lat;
        double lon;
        double alt;
        std::string time;
    };
    struct Item {
        std::string serial;
        std::string type;
        bool type_is_subtype = false;
        std::string first_time;
        std::string last_time;
        double first_alt = std::numeric_limits<double>::quiet_NaN();
        double last_alt = std::numeric_limits<double>::quiet_NaN();
        double last_lat = std::numeric_limits<double>::quiet_NaN();
        double last_lon = std::numeric_limits<double>::quiet_NaN();
        double frequency_mhz = std::numeric_limits<double>::quiet_NaN();
        double last_vel_h = std::numeric_limits<double>::quiet_NaN();
        double last_vel_v = std::numeric_limits<double>::quiet_NaN();
        uintmax_t size = 0;
        long frames = 0;
        std::time_t modified = 0;
        std::vector<TrackPoint> track;
    };

    std::vector<Item> items;
    std::error_code ec;
    if (!fs::exists(app.sondes_dir, ec) || !fs::is_directory(app.sondes_dir, ec)) {
        return "{\"radiosondes\":[],\"message\":\"logs/sondes not found yet\"}";
    }

    const std::time_t now = std::time(nullptr);

    for (const auto &entry : fs::directory_iterator(app.sondes_dir, ec)) {
        if (ec) break;
        if (!entry.is_regular_file(ec)) continue;
        auto path = entry.path();
        if (path.extension() != ".json") continue;

        Item item;
        item.serial = strip_json_ext(path.filename().string());
        if (serial_is_undecoded(item.serial)) continue; // e.g. DFM's "Dxxxxxx" placeholder, real serial not decoded yet
        item.size = entry.file_size(ec);

        auto ftime = entry.last_write_time(ec);
        if (!ec) {
            auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
                ftime - fs::file_time_type::clock::now() + std::chrono::system_clock::now());
            item.modified = std::chrono::system_clock::to_time_t(sctp);
        }

        if (max_age_sec > 0 && item.modified > 0 && (now - item.modified) > max_age_sec) continue;

        std::ifstream f(path);
        std::string line;
        bool first_valid = true;
        while (std::getline(f, line)) {
            if (line.find('{') == std::string::npos) continue;
            item.frames++;
            if (auto t = extract_json_string(line, "subtype"); t && looksLikeSondeSubtypeName(*t)) {
                item.type = *t;
                item.type_is_subtype = true;
            } else if (!item.type_is_subtype && item.type.empty()) {
                if (auto t2 = extract_json_string(line, "type")) item.type = *t2;
            }
            auto alt = extract_json_number(line, "alt");
            if (!alt) alt = extract_json_number(line, "altitude");
            auto dt = extract_json_string(line, "datetime");
            if (!dt) dt = extract_json_string(line, "time");
            auto lat = extract_json_number(line, "lat");
            if (!lat) lat = extract_json_number(line, "latitude");
            auto lon = extract_json_number(line, "lon");
            if (!lon) lon = extract_json_number(line, "longitude");
            auto freq = extract_json_number(line, "wsrx_frequency");
            auto vel_h = extract_json_number(line, "vel_h");
            auto vel_v = extract_json_number(line, "vel_v");

            if (first_valid) {
                if (alt) item.first_alt = *alt;
                if (dt) item.first_time = *dt;
                first_valid = false;
            }
            if (alt) item.last_alt = *alt;
            if (dt) item.last_time = *dt;
            if (lat) item.last_lat = *lat;
            if (lon) item.last_lon = *lon;
            if (freq) item.frequency_mhz = *freq;
            if (vel_h) item.last_vel_h = *vel_h;
            if (vel_v) item.last_vel_v = *vel_v;

            if (include_track && lat && lon) {
                TrackPoint tp;
                tp.lat = *lat;
                tp.lon = *lon;
                tp.alt = alt ? *alt : std::numeric_limits<double>::quiet_NaN();
                tp.time = dt ? *dt : "";
                item.track.push_back(tp);
            }
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
        js << ",\"last_latitude\":";
        if (std::isfinite(it.last_lat)) js << std::fixed << std::setprecision(6) << it.last_lat; else js << "null";
        js << ",\"last_longitude\":";
        if (std::isfinite(it.last_lon)) js << std::fixed << std::setprecision(6) << it.last_lon; else js << "null";
        js << ",\"frequency\":";
        if (std::isfinite(it.frequency_mhz)) js << std::fixed << std::setprecision(3) << it.frequency_mhz; else js << "null";
        js << ",\"vel_h\":";
        if (std::isfinite(it.last_vel_h)) js << std::fixed << std::setprecision(2) << it.last_vel_h; else js << "null";
        js << ",\"vel_v\":";
        if (std::isfinite(it.last_vel_v)) js << std::fixed << std::setprecision(2) << it.last_vel_v; else js << "null";
        js << ",\"distance_km\":";
        if (std::isfinite(it.last_lat) && std::isfinite(it.last_lon) &&
            std::isfinite(app.station_lat) && std::isfinite(app.station_lon)) {
            js << std::fixed << std::setprecision(1)
               << haversine_km(app.station_lat, app.station_lon, it.last_lat, it.last_lon);
        } else {
            js << "null";
        }
        js << ",\"bearing_deg\":";
        if (std::isfinite(it.last_lat) && std::isfinite(it.last_lon) &&
            std::isfinite(app.station_lat) && std::isfinite(app.station_lon)) {
            js << std::fixed << std::setprecision(0)
               << bearing_deg(app.station_lat, app.station_lon, it.last_lat, it.last_lon);
        } else {
            js << "null";
        }
        js << ",\"elevation_deg\":";
        if (std::isfinite(it.last_lat) && std::isfinite(it.last_lon) && std::isfinite(it.last_alt) &&
            std::isfinite(app.station_lat) && std::isfinite(app.station_lon)) {
            js << std::fixed << std::setprecision(1)
               << elevation_deg(app.station_lat, app.station_lon, app.station_alt,
                                 it.last_lat, it.last_lon, it.last_alt);
        } else {
            js << "null";
        }
        js << ",\"first_time\":\"" << json_escape(it.first_time) << "\"";
        js << ",\"last_time\":\"" << json_escape(it.last_time) << "\"";
        js << ",\"modified\":" << static_cast<long long>(it.modified);
        js << ",\"launchsite\":";
        if (std::isfinite(it.last_lat) && std::isfinite(it.last_lon)) {
            auto site = getLaunchsiteCached(it.serial);
            if (site && !site->empty()) js << "\"" << json_escape(*site) << "\""; else js << "null";
        } else {
            js << "null";
        }
        if (include_track) {
            js << ",\"track\":[";
            for (size_t j = 0; j < it.track.size(); ++j) {
                if (j) js << ",";
                const auto &tp = it.track[j];
                js << "[" << std::fixed << std::setprecision(6) << tp.lat << "," << tp.lon << ",";
                if (std::isfinite(tp.alt)) js << std::setprecision(1) << tp.alt; else js << "null";
                js << ",\"" << json_escape(tp.time) << "\"]";
            }
            js << "]";
            js << ",\"prediction\":" << prediction_json_for_serial(app, it.serial);
        }
        js << "}";
    }
    js << "]}";
    return js.str();
}

struct OzoneXdata {
    int instrument_type = 0;
    int instrument_number = 0;
    double pump_temperature_c = 0.0;
    double ozone_current_ua = 0.0;
    double battery_voltage_v = 0.0;
    int pump_current_ma = 0;
    double external_voltage_v = 0.0;
};

static std::optional<OzoneXdata> decodeOzoneXdata(const std::string &aux_raw) {
    std::string x;
    x.reserve(aux_raw.size());
    for (unsigned char c : aux_raw) x.push_back(static_cast<char>(std::toupper(c)));
    while (!x.empty() && std::isspace(static_cast<unsigned char>(x.front()))) x.erase(x.begin());
    while (!x.empty() && std::isspace(static_cast<unsigned char>(x.back()))) x.pop_back();

    if (x.size() != 20) return std::nullopt;
    for (unsigned char c : x) {
        if (!std::isxdigit(c)) return std::nullopt;
    }

    auto hexAt = [&](size_t pos, size_t len) -> long {
        return std::strtol(x.substr(pos, len).c_str(), nullptr, 16);
    };

    OzoneXdata d;
    d.instrument_type = static_cast<int>(hexAt(0, 2));
    if (d.instrument_type != 5) return std::nullopt; // only instrument type 5 (OIF411/ozone) is decoded

    d.instrument_number = static_cast<int>(hexAt(2, 2));
    d.pump_temperature_c = hexAt(4, 4) / 100.0;
    d.ozone_current_ua = hexAt(8, 5) / 10000.0;
    d.battery_voltage_v = hexAt(13, 2) / 10.0;
    d.pump_current_ma = static_cast<int>(hexAt(15, 3));
    d.external_voltage_v = hexAt(18, 2) / 10.0;
    return d;
}

static std::string radiosonde_detail_json(const App &app, const std::string &serial_raw) {
    namespace fs = std::filesystem;
    const std::string serial = sanitize_serial(serial_raw);
    const fs::path path = fs::path(app.sondes_dir) / (serial + ".json");

    std::error_code ec;
    if (!fs::exists(path, ec) || !fs::is_regular_file(path, ec)) {
        return "{\"error\":\"no log found for serial " + json_escape(serial) + "\"}";
    }

    std::map<std::string, std::string> str_fields;
    std::map<std::string, double> num_fields;
    static const std::vector<std::string> string_keys = {
        "type", "subtype", "id", "serial", "datetime", "time", "aux", "ref_datetime", "ref_position",
        "rs41_mainboard"
    };
    static const std::vector<std::string> number_keys = {
        "frame", "lat", "lon", "alt", "altitude", "vel_h", "heading", "vel_v", "sats", "sat",
        "bt", "batt", "temp", "humidity", "pressure", "rssi", "burstkilltimer", "killtimer",
        "wsrx_frequency", "rs41_mainboard_fw", "tx_power_raw"
    };
    bool encrypted = false;
    bool saw_encrypted = false;

    double first_alt = std::numeric_limits<double>::quiet_NaN();
    std::string first_time;
    long frames = 0;
    bool first_valid = true;

    std::ifstream f(path);
    std::string line;
    while (std::getline(f, line)) {
        if (line.find('{') == std::string::npos) continue;
        frames++;

        for (const auto &k : string_keys) {
            if (auto v = extract_json_string(line, k)) str_fields[k] = *v;
        }
        for (const auto &k : number_keys) {
            if (auto v = extract_json_number(line, k)) num_fields[k] = *v;
        }
        if (line.find("\"encrypted\"") != std::string::npos) {
            saw_encrypted = true;
            encrypted = line.find("\"encrypted\": true") != std::string::npos ||
                        line.find("\"encrypted\":true") != std::string::npos;
        }

        auto alt = extract_json_number(line, "alt");
        if (!alt) alt = extract_json_number(line, "altitude");
        auto dt = extract_json_string(line, "datetime");
        if (!dt) dt = extract_json_string(line, "time");
        if (first_valid) {
            if (alt) first_alt = *alt;
            if (dt) first_time = *dt;
            first_valid = false;
        }
    }

    if (frames == 0) {
        return "{\"error\":\"no valid frames found for serial " + json_escape(serial) + "\"}";
    }

    std::time_t modified = 0;
    auto ftime = fs::last_write_time(path, ec);
    if (!ec) {
        auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
            ftime - fs::file_time_type::clock::now() + std::chrono::system_clock::now());
        modified = std::chrono::system_clock::to_time_t(sctp);
    }

    std::string display_type = str_fields.count("subtype") ? str_fields["subtype"]
                              : (str_fields.count("type") ? str_fields["type"] : "");
    std::string display_serial = str_fields.count("id") ? str_fields["id"]
                                : (str_fields.count("serial") ? str_fields["serial"] : serial);

    std::ostringstream js;
    js << "{";
    js << "\"serial\":\"" << json_escape(display_serial) << "\"";
    js << ",\"type\":\"" << json_escape(display_type) << "\"";
    js << ",\"frames\":" << frames;
    js << ",\"modified\":" << static_cast<long long>(modified);
    js << ",\"first_time\":\"" << json_escape(first_time) << "\"";
    js << ",\"first_altitude\":";
    if (std::isfinite(first_alt)) js << std::fixed << std::setprecision(1) << first_alt; else js << "null";
    if (saw_encrypted) js << ",\"encrypted\":" << (encrypted ? "true" : "false");

    for (const auto &k : string_keys) {
        if (k == "type" || k == "subtype" || k == "id" || k == "serial") continue; // already surfaced above
        auto it = str_fields.find(k);
        js << ",\"" << k << "\":";
        if (it != str_fields.end()) js << "\"" << json_escape(it->second) << "\""; else js << "null";
    }
    for (const auto &k : number_keys) {
        auto it = num_fields.find(k);
        js << ",\"" << k << "\":";
        if (it != num_fields.end()) js << std::fixed << std::setprecision(6) << it->second << std::defaultfloat;
        else js << "null";
    }

    {
        auto aux_it = str_fields.find("aux");
        std::optional<OzoneXdata> ozone = (aux_it != str_fields.end())
            ? decodeOzoneXdata(aux_it->second) : std::nullopt;
        if (ozone) {
            js << ",\"o3_instrument_number\":" << ozone->instrument_number;
            js << ",\"o3_pump_temperature_c\":" << std::fixed << std::setprecision(2)
               << ozone->pump_temperature_c << std::defaultfloat;
            js << ",\"o3_current_ua\":" << std::fixed << std::setprecision(4)
               << ozone->ozone_current_ua << std::defaultfloat;
            js << ",\"o3_battery_v\":" << std::fixed << std::setprecision(1)
               << ozone->battery_voltage_v << std::defaultfloat;
            js << ",\"o3_pump_current_ma\":" << ozone->pump_current_ma;
            js << ",\"o3_external_v\":" << std::fixed << std::setprecision(1)
               << ozone->external_voltage_v << std::defaultfloat;
        }
    }

    if (num_fields.count("lat") && num_fields.count("lon") &&
        std::isfinite(app.station_lat) && std::isfinite(app.station_lon)) {
        js << ",\"distance_km\":" << std::fixed << std::setprecision(1)
           << haversine_km(app.station_lat, app.station_lon, num_fields["lat"], num_fields["lon"]);
        js << ",\"bearing_deg\":" << std::fixed << std::setprecision(0)
           << bearing_deg(app.station_lat, app.station_lon, num_fields["lat"], num_fields["lon"]);

        double target_alt = num_fields.count("alt") ? num_fields["alt"]
                           : (num_fields.count("altitude") ? num_fields["altitude"]
                           : std::numeric_limits<double>::quiet_NaN());
        if (std::isfinite(target_alt)) {
            js << ",\"elevation_deg\":" << std::fixed << std::setprecision(1)
               << elevation_deg(app.station_lat, app.station_lon, app.station_alt,
                                 num_fields["lat"], num_fields["lon"], target_alt);
        } else {
            js << ",\"elevation_deg\":null";
        }
    } else {
        js << ",\"distance_km\":null";
        js << ",\"bearing_deg\":null";
        js << ",\"elevation_deg\":null";
    }

    js << "}";
    return js.str();
}

static const size_t kMaxRequestBytes = 4 * 1024 * 1024;

static bool read_full_request(int fd, std::string &out) {
    char buf[8192];
    size_t header_end = std::string::npos;
    while (header_end == std::string::npos) {
        ssize_t n = recv(fd, buf, sizeof(buf), 0);
        if (n <= 0) return false;
        out.append(buf, static_cast<size_t>(n));
        if (out.size() > kMaxRequestBytes) return false;
        header_end = out.find("\r\n\r\n");
    }

    long long content_length = 0;
    std::string cl = get_header(out, "Content-Length");
    if (!cl.empty()) {
        try { content_length = std::stoll(cl); } catch (...) { content_length = 0; }
    }
    if (content_length < 0) content_length = 0;
    if (static_cast<size_t>(content_length) > kMaxRequestBytes) return false;

    size_t body_start = header_end + 4;
    while (out.size() - body_start < static_cast<size_t>(content_length)) {
        ssize_t n = recv(fd, buf, sizeof(buf), 0);
        if (n <= 0) break; // client hung up early; handle whatever we got
        out.append(buf, static_cast<size_t>(n));
        if (out.size() > kMaxRequestBytes) return false;
    }
    return true;
}

static std::string request_body(const std::string &request) {
    size_t pos = request.find("\r\n\r\n");
    if (pos == std::string::npos) return "";
    std::string body = request.substr(pos + 4);
    std::string cl = get_header(request, "Content-Length");
    if (!cl.empty()) {
        try {
            size_t len = static_cast<size_t>(std::stoll(cl));
            if (len <= body.size()) body.resize(len);
        } catch (...) {}
    }
    return body;
}

static void handle_client(int fd, const App &app) {
    std::string request_str;
    if (!read_full_request(fd, request_str)) { close(fd); return; }

    if (!check_auth(app, request_str)) {
        send_unauthorized(fd);
        close(fd);
        return;
    }

    std::istringstream req(request_str);
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
        if (method == "POST") {
            std::string body = request_body(request_str);
            if (body.size() > 512 * 1024) {
                send_response(fd, 413, "text/plain", "config.ini too large (max 512 KB)\n");
            } else if (write_file_atomic(app.config_file, body)) {
                send_response(fd, 200, "text/plain", "OK\n");
            } else {
                send_response(fd, 500, "text/plain", "failed to write config.ini\n");
            }
        } else {
            send_response(fd, 200, "text/plain", read_file(app.config_file, 256 * 1024));
        }
    } else if (path == "/api/whitelist") {
        if (method == "POST") {
            std::string body = request_body(request_str);
            if (body.size() > 512 * 1024) {
                send_response(fd, 413, "text/plain", "whitelist too large (max 512 KB)\n");
            } else if (write_file_atomic(app.whitelist_file, body)) {
                send_response(fd, 200, "text/plain", "OK\n");
            } else {
                send_response(fd, 500, "text/plain", "failed to write whitelist\n");
            }
        } else {
            send_response(fd, 200, "text/plain", read_file(app.whitelist_file, 256 * 1024));
        }
    } else if (path == "/api/blacklist") {
        if (method == "POST") {
            std::string body = request_body(request_str);
            if (body.size() > 512 * 1024) {
                send_response(fd, 413, "text/plain", "blacklist too large (max 512 KB)\n");
            } else if (write_file_atomic(app.blacklist_file, body)) {
                send_response(fd, 200, "text/plain", "OK\n");
            } else {
                send_response(fd, 500, "text/plain", "failed to write blacklist\n");
            }
        } else {
            send_response(fd, 200, "text/plain", read_file(app.blacklist_file, 256 * 1024));
        }
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
    } else if (path == "/api/cpu") {
        std::string t = read_file(app.cpu_file, 4096);
        if (t.empty()) {
            t = "{\"error\":\"cpu_load.json not found yet\",\"cpu_percent\":null}";
        }
        send_response(fd, 200, "application/json", t);
    } else if (path == "/api/version") {
        std::string t = read_file(app.version_file, 4096);
        if (t.empty()) {
            t = "{\"version\":null}";
        }
        send_response(fd, 200, "application/json", t);
    } else if (path == "/api/radiosondes") {
        auto qm = parse_query(query);
        long long max_age_sec = 0;
        if (qm.count("active_sec")) {
            try { max_age_sec = std::stoll(qm["active_sec"]); } catch (...) { max_age_sec = 0; }
        } else if (qm.count("hours")) {
            try { max_age_sec = static_cast<long long>(std::stod(qm["hours"]) * 3600.0); } catch (...) { max_age_sec = 0; }
        }
        bool include_track = qm.count("tracks") && qm["tracks"] != "0" && qm["tracks"] != "false";
        send_response(fd, 200, "application/json", radiosondes_json(app, max_age_sec, include_track));
    } else if (path == "/api/radiosonde") {
        auto qm = parse_query(query);
        std::string serial = qm.count("serial") ? qm["serial"] : "";
        if (serial.empty()) {
            send_response(fd, 400, "application/json", "{\"error\":\"missing serial parameter\"}");
        } else {
            send_response(fd, 200, "application/json", radiosonde_detail_json(app, serial));
        }
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
    } else if (path == "/api/update") {
        if (method != "POST") {
            send_response(fd, 405, "text/plain", "POST required\n");
        } else if (!file_exists(app.update_script)) {
            send_response(fd, 404, "text/plain", "update.sh not found in " + app.base_dir + "\n");
        } else if (g_update_running.exchange(true)) {
            send_response(fd, 409, "text/plain", "An update is already running.\n");
        } else {
            bool launched = run_detached_to_log(shell_quote(app.update_script), app.update_log);
            if (!launched) {
                g_update_running = false;
                send_response(fd, 500, "text/plain", "failed to start update.sh\n");
            } else {
                send_response(fd, 200, "text/plain",
                    "Update started in the background.\n"
                    "update.sh stops wsrx AND this web interface, pulls, rebuilds, then restarts both - "
                    "so this page will go quiet for a bit and then pick back up on its own once it's done. "
                    "No need to reload. This can take a few minutes.\n"
                    "If the rebuild fails, wsrx and the web interface stay stopped until you fix it "
                    "and run ./wsrx.sh start by hand.\n");
            }
        }
    } else if (path == "/api/updatelog") {
        int lines = 400;
        auto qm = parse_query(query);
        if (qm.count("lines")) lines = std::max(1, std::min(5000, atoi(qm["lines"].c_str())));
        std::string t = read_file(app.update_log, 1024 * 1024);
        if (t.empty() && !file_exists(app.update_log)) t = "No update has been run yet.\n";
        send_response(fd, 200, "text/plain", tail_lines(t, lines));
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
        std::string lat_str = ini_value(ini_text, "lat");
        std::string lon_str = ini_value(ini_text, "lon");
        std::string alt_str = ini_value(ini_text, "alt");
        try { if (!lat_str.empty()) app.station_lat = std::stod(lat_str); } catch (...) {}
        try { if (!lon_str.empty()) app.station_lon = std::stod(lon_str); } catch (...) {}
        try { if (!alt_str.empty()) app.station_alt = std::stod(alt_str); } catch (...) {}
    }
    app.spectrum_file = app.base_dir + "/data/spectrum_live.json";
    app.peaks_file = app.base_dir + "/data/scan_peaks.json";
    app.sondes_dir = app.base_dir + "/logs/sondes";
    app.cpu_file = app.base_dir + "/data/cpu_load.json";
    app.version_file = app.base_dir + "/data/version.json";
    app.update_script = app.base_dir + "/update.sh";
    app.update_log = app.base_dir + "/logs/update.log";

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

    std::thread(cpu_monitor_thread, app).detach();
    std::thread(prediction_scheduler_thread, app).detach();

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