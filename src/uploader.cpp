#include "uploader.h"
#include "logger.h"

#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <sstream>
#include <regex>

namespace {
constexpr const char* APP_VERSION = "0.2.29";
constexpr const char* TELEMETRY_URL = "http://api.wettersonde.net/telemetrie.php";
constexpr const char* POSITION_URL = "http://api.wettersonde.net/position.php";

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

std::string shellQuote(const std::string& s) {
    std::string out = "'";
    for (char c : s) {
        if (c == '\'') out += "'\\''";
        else out += c;
    }
    out += "'";
    return out;
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
}

Uploader::Uploader(const Config& cfg, Logger& log) : cfg_(cfg), log_(log) {
    last_position_upload_ = std::chrono::steady_clock::now() - std::chrono::seconds(cfg.receiver_position_interval_sec + 1);
}

bool Uploader::sendTelemetry(const TelemetryFrame& frame) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!validTypeSerial(frame)) {
        log_.warn("Rejected frame by type/serial rule: type=" + frame.type + " serial=" + frame.serial);
        return false;
    }

    if (!allowedByRateLimit(frame.serial)) {
        log_.debug("Rate-limited telemetry for " + frame.serial);
        return false;
    }

    std::string data = buildTelemetryPostData(frame);

    if (cfg_.dry_run || !cfg_.upload_enabled) {
        log_.info("DRY-RUN telemetry JSON: " + data);
        return true;
    }

    return postWithCurl(TELEMETRY_URL, data);
}

bool Uploader::maybeSendReceiverPosition() {
    std::lock_guard<std::mutex> lock(mutex_);
    auto now = std::chrono::steady_clock::now();
    auto diff = std::chrono::duration_cast<std::chrono::seconds>(now - last_position_upload_).count();
    if (diff < cfg_.receiver_position_interval_sec) return false;

    std::ostringstream oss;
    bool first = true;
    oss << '{';
    addString(oss, first, "callsign", cfg_.callsign);
    addNumberRaw(oss, first, "latitude", cfg_.station_lat);
    addNumberRaw(oss, first, "longitude", cfg_.station_lon);
    addNumber1(oss, first, "altitude", cfg_.station_alt);
    addString(oss, first, "software", "wsrx");
    addString(oss, first, "version", APP_VERSION);
    oss << '}';

    last_position_upload_ = now;

    if (cfg_.dry_run || !cfg_.upload_enabled) {
        log_.info("DRY-RUN receiver position JSON: " + oss.str());
        return true;
    }

    return postWithCurl(POSITION_URL, oss.str());
}

bool Uploader::allowedByRateLimit(const std::string& serial) {
    static constexpr int MIN_UPLOAD_INTERVAL_PER_SONDE_SEC = 10;

    auto now = std::chrono::steady_clock::now();
    auto it = last_upload_.find(serial);
    if (it != last_upload_.end()) {
        auto diff = std::chrono::duration_cast<std::chrono::seconds>(now - it->second).count();
        if (diff < MIN_UPLOAD_INTERVAL_PER_SONDE_SEC) return false;
    }
    last_upload_[serial] = now;
    return true;
}

bool Uploader::validTypeSerial(const TelemetryFrame& frame) const {
    const auto& type = frame.type;
    const auto& serial = frame.serial;

    if (type.find("RS41") != std::string::npos) {
        return serial.find('D') == std::string::npos && serial.find("ME") == std::string::npos;
    }

    if (type.find("DFM") != std::string::npos) {
        // Upload DFM only after dfm09mod has resolved the real serial.
        // Reject placeholders such as DFM-xxxxxxxx / Dxxxxxxxx.
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

    return true;
}

std::string Uploader::buildTelemetryPostData(const TelemetryFrame& frame) const {
    std::ostringstream oss;
    bool first = true;
    const std::string ts = frame.timestamp_hhmmss.empty() ? nowTimestampUtc() : frame.timestamp_hhmmss;

    oss << '{';
    addString(oss, first, "timestamp", ts);
    // Only RS41 has a real power-up/frame counter compatible with the API.
    // DFM/M10/M20 decoder frame values can be timestamps or internal counters,
    // so send 0 for all non-RS41 types.
    const int upload_frame = (frame.type.find("RS41") != std::string::npos)
        ? (frame.frame >= 0 ? frame.frame : 0)
        : 0;
    addInt(oss, first, "frame", upload_frame);
    addNumberRaw(oss, first, "latitude", frame.lat);
    addNumberRaw(oss, first, "longitude", frame.lon);
    addNumber1(oss, first, "altitude", frame.alt_m);
    // rs41mod vel_h/vH is m/s; wettersonde.net API expects speed in km/h.
    const double speed_kmh = std::isnan(frame.speed_ms) ? 0.0 : frame.speed_ms * 3.6;
    addNumber1(oss, first, "speed", speed_kmh);
    addNumber1(oss, first, "direction", std::isnan(frame.heading_deg) ? 0.0 : frame.heading_deg);
    const double upload_frequency_mhz = !std::isnan(frame.tx_frequency_mhz) ? frame.tx_frequency_mhz : frame.frequency_mhz;
    addNumber1(oss, first, "frequency", upload_frequency_mhz);
    addString(oss, first, "type", frame.type);
    addString(oss, first, "serial", frame.serial);
    addString(oss, first, "callsign", cfg_.callsign);
    addNumber1(oss, first, "climb", std::isnan(frame.climb_ms) ? 0.0 : frame.climb_ms);
    addString(oss, first, "software", "wsrx");
    addString(oss, first, "version", APP_VERSION);

    if (!std::isnan(frame.temperature_c)) addNumber1(oss, first, "temperature", frame.temperature_c);
    if (!std::isnan(frame.humidity_percent)) addNumber1(oss, first, "humidity", frame.humidity_percent);
    if (!std::isnan(frame.pressure_hpa)) addNumber1(oss, first, "pressure", frame.pressure_hpa);
    if (!std::isnan(frame.battery_v)) addNumber1(oss, first, "voltage", frame.battery_v);
    if (!std::isnan(frame.rssi_db)) addNumber1(oss, first, "rssi", frame.rssi_db);
    if (!std::isnan(frame.burstkilltimer_sec)) addInt(oss, first, "burstkilltimer", static_cast<int>(std::llround(frame.burstkilltimer_sec)));
    if (!std::isnan(frame.killtimer_sec)) addInt(oss, first, "killtimer", static_cast<int>(std::llround(frame.killtimer_sec)));
    if (frame.sats >= 0) addInt(oss, first, "sat", frame.sats);
    if (!frame.xdata.empty()) addString(oss, first, "xdata", frame.xdata);
    if (!frame.xdata1.empty()) addString(oss, first, "xdata1", frame.xdata1);
    if (!frame.xdata2.empty()) addString(oss, first, "xdata2", frame.xdata2);
    if (!frame.xdata3.empty()) addString(oss, first, "xdata3", frame.xdata3);
    if (!frame.raw_datetime.empty()) addString(oss, first, "datetime", frame.raw_datetime);

    oss << '}';
    return oss.str();
}

bool Uploader::postWithCurl(const std::string& url, const std::string& data) {
    if (cfg_.verbose) {
        log_.debug("upload JSON POST " + url + " data: " + data);
    }

    std::string cmd = "curl -fsS -m 10 -X POST -H 'Content-Type: application/json' --data " + shellQuote(data) + " " + shellQuote(url) + " >/dev/null";
    int rc = std::system(cmd.c_str());
    if (rc != 0) {
        log_.warn("Upload failed: " + url);
        if (cfg_.verbose) {
            log_.debug("failed JSON POST data: " + data);
        }
        return false;
    }
    if (cfg_.verbose) {
        log_.debug("Upload OK: " + url);
    }
    return true;
}
