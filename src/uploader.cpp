#include "uploader.h"
#include "logger.h"
#include "telemetryjson.h"

#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <sstream>

namespace {
constexpr const char* APP_VERSION = "0.1.02";
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
}

Uploader::Uploader(const Config& cfg, Logger& log) : cfg_(cfg), log_(log) {
    last_position_upload_ = std::chrono::steady_clock::now() - std::chrono::seconds(cfg.receiver_position_interval_sec + 1);
}

bool Uploader::sendTelemetry(const TelemetryFrame& frame) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!hasGpsFix(frame)) {
        log_.debug("Rejected frame without GPS fix (lat/lon = 0): type=" + frame.type + " serial=" + frame.serial);
        return false;
    }

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

    std::ostringstream msg;

    msg << "Uploading: " << frame.type << " " << frame.serial
                    << " freq=" << frame.frequency_mhz
                    << " lat=" << frame.lat
                    << " lon=" << frame.lon
                    << " alt=" << frame.alt_m;

                    log_.info(msg.str());

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

bool Uploader::hasGpsFix(const TelemetryFrame& frame) const {
    return TelemetryJson::hasGpsFix(frame);
}

bool Uploader::validTypeSerial(const TelemetryFrame& frame) const {
    return TelemetryJson::validTypeSerial(frame);
}

std::string Uploader::buildTelemetryPostData(const TelemetryFrame& frame) const {
    return TelemetryJson::buildTelemetryJson(frame, cfg_.callsign, APP_VERSION);
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
