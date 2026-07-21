#include "aprsformat.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <sstream>

namespace {

uint32_t realcard(double x) {
    if (x < 0) return 0;
    return static_cast<uint32_t>(x + 0.5);
}

uint32_t truncc(double r) {
    if (r <= 0.0) return 0UL;
    if (r >= 2.0e9) return 2000000000UL;
    return static_cast<uint32_t>(r);
}

uint32_t dao91(double x) {
    double a = std::fabs(x);
    double frac_deg = a - static_cast<double>(truncc(a));
    return ((truncc(frac_deg * 6.0e5) % 100UL) * 20UL + 11UL) / 22UL;
}

std::string aprsObjectName(const std::string& serial) {
    std::string name = serial.empty() ? "SONDE" : serial;
    if (name.size() > 9) name = name.substr(0, 9);
    while (name.size() < 9) name += ' ';
    return name;
}

std::string firstChar(const std::string& s, char fallback) {
    return s.empty() ? std::string(1, fallback) : std::string(1, s[0]);
}

std::string formatLatMinutes(double lat) {
    int lati = static_cast<int>(std::fabs(static_cast<int>(lat)));
    int latm = static_cast<int>((std::fabs(lat) - lati) * 6000);
    char buf[24];
    std::snprintf(buf, sizeof(buf), "%02d%02d.%02d%c", lati, latm / 100, latm % 100, lat < 0 ? 'S' : 'N');
    return buf;
}

std::string formatLonMinutes(double lon) {
    int loni = static_cast<int>(std::fabs(static_cast<int>(lon)));
    int lonm = static_cast<int>((std::fabs(lon) - loni) * 6000);
    char buf[24];
    std::snprintf(buf, sizeof(buf), "%03d%02d.%02d%c", loni, lonm / 100, lonm % 100, lon < 0 ? 'W' : 'E');
    return buf;
}

}  // namespace

namespace AprsFormat {

std::string formatLatitude(double lat) { return formatLatMinutes(lat); }
std::string formatLongitude(double lon) { return formatLonMinutes(lon); }

std::string buildObjectReport(const AprsConfig& cfg, const JsonObject& frame) {
    const double lat = frame.getDouble("latitude", 0.0);
    const double lon = frame.getDouble("longitude", 0.0);
    const double alt_m = frame.getDouble("altitude", 0.0);
    const double speed_kmh = frame.getDouble("speed", 0.0);
    const double course = frame.getDouble("direction", 0.0);
    const double climb_ms = frame.getDouble("climb", 0.0);
    const std::string type = frame.getString("type");
    const std::string serial = frame.getString("serial");
    const double freq_mhz = frame.getDouble("frequency", 0.0);
    const std::string software = frame.getString("software", "wsrx");
    const std::string version = frame.getString("version");

    constexpr double kFeetPerMeter = 1.0 / 0.3048;
    constexpr double kKmhPerKnot = 1.851984;

    const double speed_kn = speed_kmh / kKmhPerKnot;

    std::ostringstream body;
    body << ";" << aprsObjectName(serial) << "*";

    std::string ts = frame.getString("timestamp");
    if (ts.size() != 6) {
        std::time_t t = std::time(nullptr);
        std::tm tm{};
        gmtime_r(&t, &tm);
        char buf[8];
        std::snprintf(buf, sizeof(buf), "%02d%02d%02d", tm.tm_hour, tm.tm_min, tm.tm_sec);
        ts = buf;
    }
    body << ts << "h";


    body << formatLatMinutes(lat) << firstChar(cfg.object_symbol_table, '/')
         << formatLonMinutes(lon) << firstChar(cfg.object_symbol_code, 'O');

    if (speed_kn > 0.5) {
        char buf[16];
        std::snprintf(buf, sizeof(buf), "%03u/%03u", realcard(std::fmod(course + 360.0, 360.0)), realcard(speed_kn));
        body << buf;
    }

    if (alt_m > 0.5) {
        char buf[16];
        std::snprintf(buf, sizeof(buf), "/A=%06u", realcard(alt_m * kFeetPerMeter));
        body << buf;
    }

    {
        char buf[8];
        std::snprintf(buf, sizeof(buf), "!w%c%c!", static_cast<char>(33 + dao91(lat)), static_cast<char>(33 + dao91(lon)));
        body << buf;
    }

    {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "Clb=%.1fm/s ", climb_ms);
        body << buf;
    }
    if (frame.has("pressure")) {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "p=%.1fhPa ", frame.getDouble("pressure"));
        body << buf;
    }
    if (frame.has("temperature")) {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "t=%.1fC ", frame.getDouble("temperature"));
        body << buf;
    }
    if (frame.has("humidity")) {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "h=%.1f%% ", frame.getDouble("humidity"));
        body << buf;
    }
    {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%.3fMHz Type=%s ", freq_mhz, type.c_str());
        body << buf;
    }
    if (frame.has("burstkilltimer")) {
        int bk = frame.getInt("burstkilltimer");
        if (bk >= 0 && bk < 65535) {  // 65535 = sentinel for "not set"
            char buf[32];
            std::snprintf(buf, sizeof(buf), "TxOff=%dh%02dm ", bk / 3600, (bk % 3600) / 60);
            body << buf;
        }
    }
    if (type.find("DFM") != std::string::npos || type.find("M10") != std::string::npos ||
        type.find("M20") != std::string::npos) {
        body << "ser=" << serial << " ";
    }
    body << software << (version.empty() ? "" : (" " + version));

    std::ostringstream line;
    line << cfg.callsign << ">" << cfg.tocall << ":" << body.str();
    return line.str();
}

std::string buildStationBeacon(const AprsConfig& cfg) {
    std::ostringstream body;
    body << "!" << formatLatMinutes(cfg.station_lat) << firstChar(cfg.station_symbol_table, '/')
         << formatLonMinutes(cfg.station_lon) << firstChar(cfg.station_symbol_code, '-');

    if (cfg.station_alt_m > 0.5) {
        char buf[16];
        std::snprintf(buf, sizeof(buf), "/A=%06u", realcard(cfg.station_alt_m * (1.0 / 0.3048)));
        body << buf;
    }

    body << cfg.station_comment;

    std::ostringstream line;
    line << cfg.callsign << ">" << cfg.tocall << ":" << body.str();
    return line.str();
}

}  // namespace AprsFormat
