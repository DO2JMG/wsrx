#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <sys/stat.h>
#include <termios.h>
#include <unistd.h>
#include <vector>

static const char* CONFIG_REL_PATH    = "../config.ini";
static const int   WRITE_INTERVAL_SEC = 10;

class SerialPort {
public:
    SerialPort(const std::string& device, int baud) {
        fd_ = ::open(device.c_str(), O_RDONLY | O_NOCTTY);
        if (fd_ < 0)
            throw std::runtime_error("cannot open " + device + ": " + std::strerror(errno));

        termios tty{};
        if (tcgetattr(fd_, &tty) != 0)
            throw std::runtime_error(std::string("tcgetattr: ") + std::strerror(errno));

        speed_t speed = toSpeed(baud);
        cfsetispeed(&tty, speed);
        cfsetospeed(&tty, speed);

        cfmakeraw(&tty);
        tty.c_cflag |= (CLOCAL | CREAD);
        tty.c_cflag &= ~CRTSCTS;
        tty.c_cc[VMIN]  = 1;
        tty.c_cc[VTIME] = 10;

        if (tcsetattr(fd_, TCSANOW, &tty) != 0)
            throw std::runtime_error(std::string("tcsetattr: ") + std::strerror(errno));
        tcflush(fd_, TCIFLUSH);
    }

    ~SerialPort() { if (fd_ >= 0) ::close(fd_); }
    SerialPort(const SerialPort&) = delete;
    SerialPort& operator=(const SerialPort&) = delete;

    bool readLine(std::string& line) {
        line.clear();
        char c;
        while (true) {
            ssize_t n = ::read(fd_, &c, 1);
            if (n < 0) {
                if (errno == EINTR) continue;
                return false;
            }
            if (n == 0) continue;
            if (c == '\n') return true;
            if (c != '\r') line += c;
            if (line.size() > 120) line.clear();
        }
    }

private:
    static speed_t toSpeed(int baud) {
        switch (baud) {
            case 4800:   return B4800;
            case 9600:   return B9600;
            case 19200:  return B19200;
            case 38400:  return B38400;
            case 57600:  return B57600;
            case 115200: return B115200;
            default: throw std::runtime_error("unsupported baud rate");
        }
    }
    int fd_ = -1;
};

static bool checksumOk(const std::string& s) {
    if (s.size() < 4 || s[0] != '$') return false;
    auto star = s.find('*');
    if (star == std::string::npos || star + 2 >= s.size()) return false;
    unsigned char sum = 0;
    for (size_t i = 1; i < star; ++i) sum ^= static_cast<unsigned char>(s[i]);
    unsigned expected = std::strtoul(s.substr(star + 1, 2).c_str(), nullptr, 16);
    return sum == expected;
}

static std::vector<std::string> split(const std::string& s) {
    std::vector<std::string> out;
    std::string body = s.substr(1, s.find('*') - 1);
    std::stringstream ss(body);
    std::string field;
    while (std::getline(ss, field, ',')) out.push_back(field);
    if (!body.empty() && body.back() == ',') out.emplace_back();
    return out;
}

static double toDegrees(const std::string& value, const std::string& hemi) {
    if (value.empty()) return NAN;
    double v = std::stod(value);
    int deg = static_cast<int>(v / 100);
    double result = deg + (v - deg * 100) / 60.0;
    if (hemi == "S" || hemi == "W") result = -result;
    return result;
}

struct GpsFix {
    std::string time;
    std::string date;
    double lat = NAN, lon = NAN, altitude = NAN;
    double speedKmh = NAN, course = NAN;
    int quality = 0, satellites = 0;
    bool valid = false;
};

static void parse(const std::string& line, GpsFix& fix) {
    if (!checksumOk(line)) return;
    auto f = split(line);
    if (f.empty() || f[0].size() < 5) return;
    std::string type = f[0].substr(2);

    try {
        if (type == "GGA" && f.size() >= 10) {
            fix.time       = f[1];
            fix.lat        = toDegrees(f[2], f[3]);
            fix.lon        = toDegrees(f[4], f[5]);
            fix.quality    = f[6].empty() ? 0 : std::stoi(f[6]);
            fix.satellites = f[7].empty() ? 0 : std::stoi(f[7]);
            fix.altitude   = f[9].empty() ? NAN : std::stod(f[9]);
        } else if (type == "RMC" && f.size() >= 10) {
            fix.time     = f[1];
            fix.valid    = (f[2] == "A");
            fix.lat      = toDegrees(f[3], f[4]);
            fix.lon      = toDegrees(f[5], f[6]);
            fix.speedKmh = f[7].empty() ? NAN : std::stod(f[7]) * 1.852;
            fix.course   = f[8].empty() ? NAN : std::stod(f[8]);
            fix.date     = f[9];
        }
    } catch (const std::exception&) {
    }
}

static std::string trim(const std::string& s) {
    auto b = s.find_first_not_of(" \t\r");
    if (b == std::string::npos) return "";
    auto e = s.find_last_not_of(" \t\r");
    return s.substr(b, e - b + 1);
}

static void updateConfig(const std::string& path, double lat, double lon, double alt) {
    std::ifstream in(path);
    if (!in) throw std::runtime_error("cannot read " + path);

    std::vector<std::string> lines;
    for (std::string l; std::getline(in, l);) lines.push_back(l);
    in.close();

    auto fmt = [](double v, int prec) {
        std::ostringstream o;
        o << std::fixed << std::setprecision(prec) << v;
        return o.str();
    };
    struct Entry { const char* key; std::string value; bool done; };
    Entry entries[] = {
        {"lat", fmt(lat, 6), false},
        {"lon", fmt(lon, 6), false},
        {"alt", fmt(std::round(alt), 0), false},
    };

    bool inStation = false, stationFound = false;
    size_t stationEnd = 0;

    for (size_t i = 0; i < lines.size(); ++i) {
        std::string t = trim(lines[i]);
        if (!t.empty() && t.front() == '[') {
            inStation = (t == "[station]");
            if (inStation) { stationFound = true; stationEnd = i + 1; }
            continue;
        }
        if (!inStation) continue;
        if (!t.empty()) stationEnd = i + 1;

        auto eq = lines[i].find('=');
        if (eq == std::string::npos || t[0] == '#' || t[0] == ';') continue;
        std::string key = trim(lines[i].substr(0, eq));
        for (auto& e : entries) {
            if (key == e.key) {
                std::string prefix = lines[i].substr(0, eq + 1);
                lines[i] = prefix + " " + e.value;
                e.done = true;
            }
        }
    }

    if (!stationFound) {
        if (!lines.empty() && !trim(lines.back()).empty()) lines.emplace_back();
        lines.emplace_back("[station]");
        stationEnd = lines.size();
    }
    for (auto& e : entries) {
        if (!e.done) {
            lines.insert(lines.begin() + stationEnd, std::string(e.key) + " = " + e.value);
            ++stationEnd;
        }
    }

    std::string tmp = path + ".tmp";
    {
        std::ofstream out(tmp, std::ios::trunc);
        if (!out) throw std::runtime_error("cannot write " + tmp);
        for (const auto& l : lines) out << l << '\n';
        if (!out) throw std::runtime_error("write error on " + tmp);
    }
    struct stat st{};
    if (::stat(path.c_str(), &st) == 0) ::chmod(tmp.c_str(), st.st_mode & 07777);
    if (std::rename(tmp.c_str(), path.c_str()) != 0)
        throw std::runtime_error("cannot replace " + path + ": " + std::strerror(errno));
}

static std::string configPathFromExe() {
    char buf[4096];
    ssize_t n = ::readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (n <= 0) return CONFIG_REL_PATH;
    std::string exe(buf, n);
    std::string dir = exe.substr(0, exe.find_last_of('/'));
    return dir + "/" + CONFIG_REL_PATH;
}

int main(int argc, char* argv[]) {
    std::string device = "/dev/ttyACM0";
    int baud = 9600;
    bool raw = false;
    int minSats = 4;

    std::vector<std::string> positional;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--raw") raw = true;
        else if (a == "--min-sats" && i + 1 < argc) minSats = std::atoi(argv[++i]);
        else positional.push_back(a);
    }
    if (positional.size() > 0) device = positional[0];
    if (positional.size() > 1) baud = std::atoi(positional[1].c_str());

    std::string configPath;
    if (!raw) {
        std::vector<std::string> candidates = {configPathFromExe(), CONFIG_REL_PATH};
        for (const auto& c : candidates) {
            if (::access(c.c_str(), F_OK) == 0) { configPath = c; break; }
        }
        if (configPath.empty()) {
            std::cerr << "Error: config.ini not found. Searched:\n";
            for (const auto& c : candidates) {
                char abs[4096];
                std::cerr << "  " << (::realpath(c.c_str(), abs) ? abs : c) << '\n';
            }
            return 1;
        }
        char abs[4096];
        if (::realpath(configPath.c_str(), abs)) configPath = abs;

        std::string dir = configPath.substr(0, configPath.find_last_of('/'));
        if (::access(configPath.c_str(), W_OK) != 0)
            std::cerr << "WARNING: no write permission on " << configPath << '\n';
        if (::access(dir.c_str(), W_OK) != 0)
            std::cerr << "WARNING: no write permission in directory " << dir
                      << " (needed for config.ini.tmp)\n";
    }

    try {
        SerialPort port(device, baud);
        std::cout << "Reading from " << device << " at " << baud << " baud ...\n";
        if (!raw)
            std::cout << "config.ini: " << configPath << "\n"
                      << "Waiting for fix with at least " << minSats << " satellites, write interval "
                      << WRITE_INTERVAL_SEC << " s.\n";

        using Clock = std::chrono::steady_clock;
        Clock::time_point lastWrite{};
        bool written = false;
        bool sawGGA = false, sawRMC = false;
        int ggaCount = 0;

        GpsFix fix;
        std::string line;
        while (port.readLine(line)) {
            if (raw) { std::cout << line << '\n'; continue; }
            if (line.size() < 6 || line[0] != '$') continue;

            parse(line, fix);
            bool isGGA = line.compare(3, 3, "GGA") == 0;
            bool isRMC = line.compare(3, 3, "RMC") == 0;
            if (isGGA) { sawGGA = true; ++ggaCount; }
            if (isRMC) sawRMC = true;

            bool ggaOnly = !sawRMC && ggaCount > 3;
            if (ggaOnly && isGGA) fix.valid = fix.quality > 0;

            if (isRMC || (ggaOnly && isGGA)) {
                std::cout << std::fixed << std::setprecision(6)
                          << "UTC " << fix.time << "  "
                          << (fix.valid ? "FIX   " : "NO FIX")
                          << "  Lat " << fix.lat << "  Lon " << fix.lon
                          << std::setprecision(1)
                          << "  Alt " << fix.altitude << " m"
                          << "  " << fix.speedKmh << " km/h"
                          << "  Sats " << fix.satellites;

                std::string reason;
                if (!fix.valid)                         reason = "receiver reports no valid fix";
                else if (!sawGGA)                       reason = "no GGA sentences (altitude/satellites missing)";
                else if (fix.quality == 0)              reason = "GGA fix quality 0";
                else if (fix.satellites < minSats)      reason = "not enough satellites";
                else if (std::isnan(fix.lat) || std::isnan(fix.lon)) reason = "no position";
                else if (std::isnan(fix.altitude))      reason = "no altitude";
                if (!reason.empty()) std::cout << "  -> not written: " << reason;
                std::cout << std::endl;

                bool due = !written ||
                           Clock::now() - lastWrite >= std::chrono::seconds(WRITE_INTERVAL_SEC);

                if (reason.empty() && due) {
                    try {
                        updateConfig(configPath, fix.lat, fix.lon, fix.altitude);
                        written = true;
                        lastWrite = Clock::now();
                        std::cout << ">> " << configPath << " updated: lat="
                                  << std::setprecision(6) << fix.lat << " lon=" << fix.lon
                                  << " alt=" << std::setprecision(0) << std::round(fix.altitude)
                                  << std::endl;
                    } catch (const std::exception& e) {
                        std::cerr << "Write error: " << e.what() << std::endl;
                        lastWrite = Clock::now();
                        written = true;
                    }
                }
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << '\n';
        return 1;
    }
    return 0;
}