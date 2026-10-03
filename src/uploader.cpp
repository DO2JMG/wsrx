#include "uploader.h"
#include "httpclient.h"
#include "logger.h"
#include "telemetryjson.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <iomanip>
#include <sstream>

#ifndef WSRX_API_BASE
#define WSRX_API_BASE "http://api.wettersonde.net"
#endif

namespace {

constexpr const char* TELEMETRY_URL = WSRX_API_BASE "/telemetrie.php";
constexpr const char* POSITION_URL = WSRX_API_BASE "/position.php";
constexpr const char* ENCRYPT_URL = WSRX_API_BASE "/encrypt.php";

constexpr int kUploadWorkers = 2;
constexpr int kHttpTimeoutSec = 10;
constexpr auto kForgetSondeAfter = std::chrono::hours(1);
constexpr auto kPruneInterval = std::chrono::minutes(5);

std::string upperCopy(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return s;
}

bool isRs41Sgm(const std::string& type) {
    return upperCopy(type).find("RS41-SGM") != std::string::npos;
}

std::string urlEncode(const std::string& s) {
    static const char* hex = "0123456789ABCDEF";
    std::string out;
    out.reserve(s.size());
    for (unsigned char c : s) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            out += static_cast<char>(c);
        } else {
            out += '%';
            out += hex[(c >> 4) & 0xF];
            out += hex[c & 0xF];
        }
    }
    return out;
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

}  // namespace

Uploader::Uploader(const Config& cfg, Logger& log) : cfg_(cfg), log_(log) {
    last_position_upload_ = Clock::now() - std::chrono::seconds(cfg.receiver_position_interval_sec + 1);
    last_prune_ = Clock::now();

    HttpClient::globalInit();
    if (cfg_.upload_enabled && !cfg_.dry_run) {
        log_.info(std::string("upload via ") + HttpClient::backendName());
    }
    for (int i = 0; i < kUploadWorkers; ++i) {
        workers_.emplace_back(&Uploader::workerLoop, this);
    }
}

Uploader::~Uploader() {
    stop();
}

void Uploader::stop() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stopping_ = true;
    }
    abort_transfers_.store(true);
    cv_.notify_all();
    for (auto& t : workers_) {
        if (t.joinable()) t.join();
    }
    workers_.clear();
}

int Uploader::minUploadIntervalMs(const TelemetryFrame& frame) {
    static constexpr int DEFAULT_MIN_UPLOAD_INTERVAL_MS = 20000;
    static constexpr int FAST_MIN_UPLOAD_INTERVAL_MS = 1000;
    static constexpr double FAST_MAX_ALTITUDE_M = 1000.0;
    static constexpr double FAST_MIN_SPEED_KMH = 3.0;

    const double speed_kmh = std::isnan(frame.speed_ms) ? 0.0 : frame.speed_ms * 3.6;
    const bool fast_mode = !std::isnan(frame.alt_m) && frame.alt_m < FAST_MAX_ALTITUDE_M && speed_kmh > FAST_MIN_SPEED_KMH;
    return fast_mode ? FAST_MIN_UPLOAD_INTERVAL_MS : DEFAULT_MIN_UPLOAD_INTERVAL_MS;
}

void Uploader::queueIfDueLocked(const std::string& serial, SondeState& st, Clock::time_point now) {
    if (!st.has_unsent || st.queued || st.in_flight) return;
    if (st.ever_uploaded) {
        const auto since_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - st.last_upload).count();
        if (since_ms < minUploadIntervalMs(st.latest)) return;
    }
    st.queued = true;
    ready_.push_back(serial);
    cv_.notify_one();
}

void Uploader::pruneLocked(Clock::time_point now) {
    if (now - last_prune_ < kPruneInterval) return;
    last_prune_ = now;
    for (auto it = sondes_.begin(); it != sondes_.end();) {
        const SondeState& st = it->second;
        if (!st.queued && !st.in_flight && now - st.latest_received > kForgetSondeAfter) {
            it = sondes_.erase(it);
        } else {
            ++it;
        }
    }
}

void Uploader::sendTelemetry(const TelemetryFrame& frame) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (stopping_) return;
        notifyEncryptedRs41SgmLocked(frame);
    }

    if (!TelemetryJson::hasGpsFix(frame)) {
        log_.debug("Rejected frame without GPS fix (lat/lon = 0): type=" + frame.type + " serial=" + frame.serial);
        return;
    }

    if (!TelemetryJson::validTypeSerial(frame)) {
        log_.warn("Rejected frame by type/serial rule: type=" + frame.type + " serial=" + frame.serial);
        return;
    }

    bool rate_limited = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (stopping_) return;

        const auto now = Clock::now();
        SondeState& st = sondes_[frame.serial];
        st.latest = frame;
        st.latest_received = now;
        st.has_unsent = true;
        queueIfDueLocked(frame.serial, st, now);
        rate_limited = !st.queued && !st.in_flight;
        pruneLocked(now);
    }

    if (rate_limited) log_.debug("Rate-limited telemetry for " + frame.serial);
}

void Uploader::maybeSendReceiverPosition() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (stopping_) return;

    const auto now = Clock::now();
    const auto diff = std::chrono::duration_cast<std::chrono::seconds>(now - last_position_upload_).count();
    if (diff < cfg_.receiver_position_interval_sec) return;

    std::ostringstream oss;
    bool first = true;
    oss << '{';
    addString(oss, first, "callsign", cfg_.callsign);
    addNumberRaw(oss, first, "latitude", cfg_.station_lat);
    addNumberRaw(oss, first, "longitude", cfg_.station_lon);
    addNumber1(oss, first, "altitude", cfg_.station_alt);
    addString(oss, first, "software", "wsrx");
    addString(oss, first, "version", WSRX_VERSION);
    oss << '}';

    last_position_upload_ = now;

    if (cfg_.dry_run || !cfg_.upload_enabled) {
        log_.info("DRY-RUN receiver position JSON: " + oss.str());
        return;
    }

    requests_.push_back(Request{POSITION_URL, oss.str()});
    cv_.notify_one();
}

void Uploader::notifyEncryptedRs41SgmLocked(const TelemetryFrame& frame) {
    if (!isRs41Sgm(frame.type)) return;
    if (frame.serial.empty()) return;
    if (!TelemetryJson::validTypeSerial(frame)) return;

    if (encrypt_notified_serials_.find(frame.serial) != encrypt_notified_serials_.end()) return;
    encrypt_notified_serials_.insert(frame.serial);

    const double freq_mhz = !std::isnan(frame.tx_frequency_mhz) ? frame.tx_frequency_mhz : frame.frequency_mhz;

    std::ostringstream freq_oss;
    freq_oss << std::fixed << std::setprecision(3) << freq_mhz;

    std::ostringstream url;
    url << ENCRYPT_URL
        << "?serial=" << urlEncode(frame.serial)
        << "&frequency=" << urlEncode(freq_oss.str())
        << "&callsign=" << urlEncode(cfg_.callsign);

    if (cfg_.dry_run || !cfg_.upload_enabled) {
        log_.info("DRY-RUN RS41-SGM encrypt notification: " + url.str());
        return;
    }

    log_.info("Notifying wettersonde.net of encrypted RS41-SGM: serial=" + frame.serial +
              " freq=" + freq_oss.str() + "MHz");

    requests_.push_back(Request{url.str(), std::string()});
    cv_.notify_one();
}

void Uploader::workerLoop() {
    HttpClient http;
    http.setAbortFlag(&abort_transfers_);

    while (true) {
        std::unique_lock<std::mutex> lock(mutex_);
        cv_.wait(lock, [this] { return stopping_ || !requests_.empty() || !ready_.empty(); });
        if (stopping_) return;

        if (!requests_.empty()) {
            Request req = std::move(requests_.front());
            requests_.pop_front();
            lock.unlock();

            const bool is_get = req.body.empty();
            if (cfg_.verbose) {
                log_.debug(is_get ? "upload GET " + req.url : "upload JSON POST " + req.url + " data: " + req.body);
            }
            std::string error;
            const bool ok = is_get ? http.get(req.url, kHttpTimeoutSec, error)
                                   : http.postJson(req.url, req.body, kHttpTimeoutSec, error);
            if (!ok) {
                log_.warn("Upload failed: " + req.url + " (" + error + ")");
            } else if (cfg_.verbose) {
                log_.debug("Upload OK: " + req.url);
            }
            continue;
        }

        const std::string serial = std::move(ready_.front());
        ready_.pop_front();
        auto it = sondes_.find(serial);
        if (it == sondes_.end()) continue;
        SondeState& st = it->second;
        st.queued = false;
        if (!st.has_unsent) continue;

        const auto now = Clock::now();
        const TelemetryFrame frame = st.latest;
        const double waited_s = std::chrono::duration<double>(now - st.latest_received).count();
        st.has_unsent = false;
        st.in_flight = true;
        st.ever_uploaded = true;
        st.last_upload = now;
        lock.unlock();

        const std::string data = TelemetryJson::buildTelemetryJson(frame, cfg_.callsign, WSRX_VERSION);
        if (cfg_.dry_run || !cfg_.upload_enabled) {
            log_.info("DRY-RUN telemetry JSON: " + data);
        } else {
            std::ostringstream msg;
            msg << "Uploading: " << frame.type << " " << frame.serial
                << " freq=" << frame.frequency_mhz
                << " lat=" << frame.lat
                << " lon=" << frame.lon
                << " alt=" << frame.alt_m
                << " wait=" << std::fixed << std::setprecision(1) << waited_s << "s";
            log_.info(msg.str());
            if (cfg_.verbose) {
                log_.debug("upload JSON POST " + std::string(TELEMETRY_URL) + " data: " + data);
            }

            std::string error;
            if (http.postJson(TELEMETRY_URL, data, kHttpTimeoutSec, error)) {
                if (cfg_.verbose) log_.debug("Upload OK: " + std::string(TELEMETRY_URL));
            } else {
                log_.warn("Upload failed: " + std::string(TELEMETRY_URL) + " (" + error + ")");
                if (cfg_.verbose) log_.debug("failed JSON POST data: " + data);
            }
        }

        lock.lock();
        auto again = sondes_.find(serial);
        if (again != sondes_.end()) {
            again->second.in_flight = false;
            queueIfDueLocked(serial, again->second, Clock::now());
        }
    }
}
