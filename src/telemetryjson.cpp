#include "telemetryjson.h"

#include <cmath>
#include <cstdlib>
#include <ctime>
#include <iomanip>
#include <regex>
#include <sstream>

namespace {

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

void addInt(std::ostringstream& oss, bool& first, const std::string& key, int value) {
    addComma(oss, first);
    oss << '"' << key << "\":" << value;
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
    const auto& type = frame.type;
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

    if (type == "S1") {
        static const std::regex s1_serial_re(R"(^S1-s?[0-9]+$)");
        return std::regex_match(serial, s1_serial_re);
    }

    return true;
}

std::string buildTelemetryJson(const TelemetryFrame& frame, const std::string& callsign,
                                const std::string& app_version) {
    std::ostringstream oss;
    bool first = true;
    const std::string ts = frame.timestamp_hhmmss.empty() ? nowTimestampUtc() : frame.timestamp_hhmmss;

    oss << '{';
    addString(oss, first, "timestamp", ts);

    const int upload_frame = (frame.type.find("RS41") != std::string::npos)
        ? (frame.frame >= 0 ? frame.frame : 0)
        : 0;
    addInt(oss, first, "frame", upload_frame);
    addNumberRaw(oss, first, "latitude", frame.lat);
    addNumberRaw(oss, first, "longitude", frame.lon);
    addNumber1(oss, first, "altitude", frame.alt_m);
    const double speed_kmh = std::isnan(frame.speed_ms) ? 0.0 : frame.speed_ms * 3.6;
    addNumber1(oss, first, "speed", speed_kmh);
    addNumber1(oss, first, "direction", std::isnan(frame.heading_deg) ? 0.0 : frame.heading_deg);
    const double upload_frequency_mhz = !std::isnan(frame.tx_frequency_mhz) ? frame.tx_frequency_mhz : frame.frequency_mhz;
    addNumber1(oss, first, "frequency", upload_frequency_mhz);
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
    if (!std::isnan(frame.burstkilltimer_sec)) addInt(oss, first, "burstkilltimer", static_cast<int>(std::llround(frame.burstkilltimer_sec)));
    if (!std::isnan(frame.killtimer_sec)) addInt(oss, first, "killtimer", static_cast<int>(std::llround(frame.killtimer_sec)));
    if (frame.sats >= 0) addInt(oss, first, "sat", frame.sats);
    if (!frame.aux.empty()) addString(oss, first, "aux", frame.aux);
    if (!frame.raw_datetime.empty()) addString(oss, first, "datetime", frame.raw_datetime);

    oss << '}';
    return oss.str();
}

}  // namespace TelemetryJson
