#include "telemetryjson.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <iomanip>
#include <regex>
#include <sstream>

namespace {

std::string upperCopy(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return s;
}

std::string jsonEscape(const std::string& s) {
    std::ostringstream out;
    for (unsigned char c : s) {
        switch (c) {
            case '"': out << "\\\""; break;
            case '\\': out << "\\\\"; break;
            case '\b': out << "\\b"; break;
            case '\f': out << "\\f"; break;
            case '\n': out << "\\n"; break;
            case '\r': out << "\\r"; break;
            case '\t': out << "\\t"; break;
            default:
                if (c < 0x20) {
                    out << "\\u" << std::hex << std::setw(4) << std::setfill('0') << static_cast<int>(c) << std::dec;
                } else {
                    out << static_cast<char>(c);
                }
        }
    }
    return out.str();
}

std::string nowTimestampUtc() {
    std::time_t t = std::time(nullptr);
    std::tm tm{};
    gmtime_r(&t, &tm);
    char buf[7]{};
    std::strftime(buf, sizeof(buf), "%H%M%S", &tm);
    return std::string(buf);
}

void addComma(std::ostringstream& oss, bool& first) {
    if (!first) oss << ',';
    first = false;
}

void addString(std::ostringstream& oss, bool& first, const std::string& key, const std::string& value) {
    addComma(oss, first);
    oss << '"' << key << "\":\"" << jsonEscape(value) << '"';
}

void addNumberRaw(std::ostringstream& oss, bool& first, const std::string& key, double value) {
    addComma(oss, first);
    oss << '"' << key << "\":" << std::setprecision(10) << value;
}

void addNumber1(std::ostringstream& oss, bool& first, const std::string& key, double value) {
    addComma(oss, first);
    oss << '"' << key << "\":" << std::fixed << std::setprecision(1) << value << std::defaultfloat;
}

// Frequencies need kHz resolution (3 decimals in MHz), 1 decimal (100 kHz
// steps) is too coarse and causes the uploaded frequency to drift from the
// actual sonde frequency.
void addNumber3(std::ostringstream& oss, bool& first, const std::string& key, double value) {
    addComma(oss, first);
    oss << '"' << key << "\":" << std::fixed << std::setprecision(3) << value << std::defaultfloat;
}

void addInt(std::ostringstream& oss, bool& first, const std::string& key, int value) {
    addComma(oss, first);
    oss << '"' << key << "\":" << value;
}

constexpr double kDegToRad = 3.14159265358979323846 / 180.0;
constexpr double kEarthRadiusM = 6371000.0;

struct LookAngles {
    double azimuth_deg;
    double elevation_deg;
};

LookAngles calculateLookAngles(double aLatDeg, double aLonDeg, double aAltM,
                                double bLatDeg, double bLonDeg, double bAltM) {
    const double aLat = aLatDeg * kDegToRad;
    const double aLon = aLonDeg * kDegToRad;
    const double bLat = bLatDeg * kDegToRad;
    const double bLon = bLonDeg * kDegToRad;

    const double dLon = bLon - aLon;
    const double sa = std::cos(bLat) * std::sin(dLon);
    const double sb = (std::cos(aLat) * std::sin(bLat)) - (std::sin(aLat) * std::cos(bLat) * std::cos(dLon));
    double bearing = std::atan2(sa, sb);
    const double aa = std::sqrt(sa * sa + sb * sb);
    const double ab = (std::sin(aLat) * std::sin(bLat)) + (std::cos(aLat) * std::cos(bLat) * std::cos(dLon));
    const double angleAtCentre = std::atan2(aa, ab);

    const double ta = kEarthRadiusM + aAltM;
    const double tb = kEarthRadiusM + bAltM;
    const double ea = (std::cos(angleAtCentre) * tb) - ta;
    const double eb = std::sin(angleAtCentre) * tb;
    const double elevation_deg = std::atan2(ea, eb) / kDegToRad;

    if (bearing < 0) bearing += 2 * 3.14159265358979323846;
    const double azimuth_deg = bearing / kDegToRad;

    return {azimuth_deg, elevation_deg};
}

}  // namespace

namespace TelemetryJson {

bool hasGpsFix(const TelemetryFrame& frame) {
    if (std::isnan(frame.lat) || std::isnan(frame.lon)) return false;

    constexpr double kEpsilon = 1e-6;
    if (std::fabs(frame.lat) < kEpsilon && std::fabs(frame.lon) < kEpsilon) return false;

    return true;
}

bool validTypeSerial(const TelemetryFrame& frame) {
    // Gross-/Kleinschreibung-unabhaengig, da subtype-Werte (z.B. "iMet-4",
    // "iMet-1-RS", "RS41-SGP") den Typ 1:1 ersetzen und dabei nicht zwingend
    // durchgehend Grossbuchstaben verwenden.
    const std::string type = upperCopy(frame.type);
    const auto& serial = frame.serial;

    if (type.find("RS41") != std::string::npos) {
        return serial.find('D') == std::string::npos && serial.find("ME") == std::string::npos;
    }

    if (type.find("DFM") != std::string::npos) {
        static const std::regex dfm_serial_re(R"(^D[0-9]{6,10}$)");
        return std::regex_match(serial, dfm_serial_re);
    }

    if (type.find("M10") != std::string::npos || type.find("M20") != std::string::npos) {
        return serial.rfind("ME", 0) == 0;
    }

    if (type.find("IMET") != std::string::npos) {
        static const std::regex imet_serial_re(R"(^IMET[0-9]{5}$)");
        return std::regex_match(serial, imet_serial_re);
    }

    if (type.find("MEISEI") != std::string::npos || type.find("IMS100") != std::string::npos || type.find("RS11G") != std::string::npos) {
        static const std::regex meisei_serial_re(R"(^IMS[0-9A-F]{1,6}$)");
        return std::regex_match(serial, meisei_serial_re);
    }

    if (type.find("C50") != std::string::npos) {
        static const std::regex c50_serial_re(R"(^C50[0-9A-F]{4}$)");
        return std::regex_match(serial, c50_serial_re);
    }

    if (type.find("LMS") != std::string::npos) {
        static const std::regex lms6_serial_re(R"(^LMS6[0-9A-F]{5}$)");
        return std::regex_match(serial, lms6_serial_re);
    }

    if (type == "S1") {
        static const std::regex s1_serial_re(R"(^WS[0-9]+$)");
        return std::regex_match(serial, s1_serial_re);
    }

    if (type.find("RD94") != std::string::npos || type.find("RD41") != std::string::npos) {
        return serial != "000000000";
    }

    if (type.find("MTS01") != std::string::npos) {
        static const std::regex mts01_serial_re(R"(^MTS[0-9A-FX]{1,6}$)");
        return std::regex_match(serial, mts01_serial_re);
    }

    if (type.find("MRZ") != std::string::npos) {
        static const std::regex mrz_serial_re(R"(^MRZ[0-9A-F]{6}$)");
        return std::regex_match(serial, mrz_serial_re);
    }

    if (type.find("CF6") != std::string::npos) {
        static const std::regex cf6_serial_re(R"(^CF6[0-9]{8}$)");
        return std::regex_match(serial, cf6_serial_re);
    }

    if (type.find("GTH") != std::string::npos) {
        static const std::regex gth_serial_re(R"(^GTH[0-9A-F]{8}$)");
        return std::regex_match(serial, gth_serial_re);
    }

    return true;
}

std::string buildTelemetryJson(const TelemetryFrame& frame, const std::string& callsign,
                                const std::string& app_version, const StationLocation* station) {
    std::ostringstream oss;
    bool first = true;
    const std::string ts = frame.timestamp_hhmmss.empty() ? nowTimestampUtc() : frame.timestamp_hhmmss;

    oss << '{';
    addString(oss, first, "timestamp", ts);

    const bool has_reliable_frame_number = frame.type.find("RS41") != std::string::npos ||
                                            frame.type.find("RS92") != std::string::npos ||
                                            frame.type.find("LMS") != std::string::npos;
    const int upload_frame = (has_reliable_frame_number && frame.frame >= 0) ? frame.frame : 0;
    addInt(oss, first, "frame", upload_frame);
    addNumberRaw(oss, first, "latitude", frame.lat);
    addNumberRaw(oss, first, "longitude", frame.lon);
    addNumber1(oss, first, "altitude", frame.alt_m);
    const double speed_kmh = std::isnan(frame.speed_ms) ? 0.0 : frame.speed_ms * 3.6;
    addNumber1(oss, first, "speed", speed_kmh);
    addNumber1(oss, first, "direction", std::isnan(frame.heading_deg) ? 0.0 : frame.heading_deg);
    const double upload_frequency_mhz = !std::isnan(frame.tx_frequency_mhz) ? frame.tx_frequency_mhz : frame.frequency_mhz;
    addNumber3(oss, first, "frequency", upload_frequency_mhz);
    addString(oss, first, "type", frame.type);
    addString(oss, first, "serial", frame.serial);
    addString(oss, first, "callsign", callsign);
    addNumber1(oss, first, "climb", std::isnan(frame.climb_ms) ? 0.0 : frame.climb_ms);
    addString(oss, first, "software", "wsrx");
    addString(oss, first, "version", app_version);

    if (!std::isnan(frame.temperature_c)) addNumber1(oss, first, "temperature", frame.temperature_c);
    if (!std::isnan(frame.humidity_percent)) addNumber1(oss, first, "humidity", frame.humidity_percent);
    if (!std::isnan(frame.pressure_hpa)) addNumber1(oss, first, "pressure", frame.pressure_hpa);
    if (!std::isnan(frame.battery_v)) addNumber1(oss, first, "voltage", frame.battery_v);
    if (!std::isnan(frame.rssi_db)) addNumber1(oss, first, "rssi", frame.rssi_db);
    if (!std::isnan(frame.tx_power_raw)) addInt(oss, first, "tx_power", static_cast<int>(std::llround(frame.tx_power_raw)));
    if (!std::isnan(frame.burstkilltimer_sec)) addInt(oss, first, "burstkilltimer", static_cast<int>(std::llround(frame.burstkilltimer_sec)));
    if (!std::isnan(frame.killtimer_sec)) addInt(oss, first, "killtimer", static_cast<int>(std::llround(frame.killtimer_sec)));
    if (frame.sats >= 0) addInt(oss, first, "sat", frame.sats);
    if (!frame.aux.empty()) addString(oss, first, "aux", frame.aux);
    if (!frame.raw_datetime.empty()) addString(oss, first, "datetime", frame.raw_datetime);

    if (station != nullptr && !std::isnan(frame.lat) && !std::isnan(frame.lon) && !std::isnan(frame.alt_m)) {
        const LookAngles look = calculateLookAngles(station->lat, station->lon, station->alt_m,
                                                      frame.lat, frame.lon, frame.alt_m);
        addNumber1(oss, first, "az", look.azimuth_deg);
        addNumber1(oss, first, "el", look.elevation_deg);
    }

    oss << '}';
    return oss.str();
}

}  // namespace TelemetryJson
