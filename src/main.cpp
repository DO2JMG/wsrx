#include "args.h"
#include "config.h"
#include "decoderprocess.h"
#include "logger.h"
#include "telemetryparser.h"
#include "uploader.h"

#include <algorithm>
#include <atomic>
#include <csignal>
#include <chrono>
#include <ctime>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <memory>
#include <numeric>
#include <optional>
#include <map>
#include <mutex>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
#include <utility>
#include <unistd.h>
#include <sys/stat.h>

static std::atomic<bool> g_shutdown{false};
static std::string g_base_dir = ".";
static std::mutex g_powers_mutex;
static std::atomic<unsigned int> g_scan_ssrc_sequence{0};

static constexpr const char* APP_VERSION = "0.1.01";

static bool startsWith(const std::string& s, const std::string& prefix) {
    return s.rfind(prefix, 0) == 0;
}

static bool containsJsonObject(const std::string& line) {
    return line.find('{') != std::string::npos && line.find('}') != std::string::npos;
}

static bool shouldLogDecoderLine(const std::string& line, bool decoder_debug) {
    if (decoder_debug) return true;
    if (containsJsonObject(line)) return true;
    if (startsWith(line, "[pipeline]")) return true;
    if (startsWith(line, "IF:")) return true;
    if (startsWith(line, "dec:")) return true;
    if (line.find("Baseband power") != std::string::npos) return true;
    return false;
}

static std::optional<double> extractBasebandPowerDb(const std::string& line) {
    static const std::regex re(R"(Baseband\s+power\s*[:=]?\s*(-?(?:[0-9]+(?:\.[0-9]+)?|inf))\s*dB)", std::regex_constants::icase);
    std::smatch m;
    if (std::regex_search(line, m, re)) {
        std::string v = m[1];
        if (v == "inf" || v == "-inf") return std::nullopt;
        return std::stod(v);
    }
    return std::nullopt;
}

static void handleSignal(int) {
    g_shutdown = true;
}

static std::string safeLogFilename(std::string serial) {
    std::string out;
    for (unsigned char c : serial) {
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-') {
            out.push_back(static_cast<char>(c));
        } else {
            out.push_back('_');
        }
    }
    if (out.empty()) out = "unknown";
    return out;
}

static void appendDecoderJsonLog(const TelemetryFrame& frame) {
    if (frame.raw_line.empty() || !containsJsonObject(frame.raw_line) || frame.serial.empty()) return;

    std::filesystem::path dir = std::filesystem::path(g_base_dir) / "logs" / "sondes";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    if (ec) return;

    std::filesystem::path file = dir / (safeLogFilename(frame.serial) + ".json");
    std::ofstream out(file, std::ios::app);
    if (!out) return;
    out << frame.raw_line << '\n';
}

static bool fileExists(const std::string& path) {
    std::error_code ec;
    return std::filesystem::exists(path, ec);
}

static std::string shellQuote(const std::string& value) {
    std::string out = "'";
    for (char c : value) {
        if (c == '\'') out += "'\\''";
        else out += c;
    }
    out += "'";
    return out;
}

static std::string joinPath(const std::string& dir, const std::string& file) {
    if (dir.empty()) return file;
    if (dir.back() == '/') return dir + file;
    return dir + "/" + file;
}

static long long freqHz(double mhz) {
    return static_cast<long long>(std::llround(mhz * 1000000.0));
}

static std::string ka9qSsrc(double mhz, int suffix = 1) {
    long long khz = static_cast<long long>(std::llround(freqHz(mhz) / 1000.0));
    std::ostringstream oss;
    oss << khz;
    if (suffix < 10) oss << '0';
    oss << suffix;
    return oss.str();
}

static std::string buildKa9qTuneCommandFor(const Config& cfg, double frequency_mhz, const std::string& ssrc, int timeout_sec) {
    long long hz = freqHz(frequency_mhz);
    std::ostringstream cmd;
    cmd << "timeout " << timeout_sec << " tune "
        << "--samprate " << cfg.sample_rate << " "
        << "--mode iq "
        << "--low " << cfg.ka9q_low_hz << " --high " << cfg.ka9q_high_hz << " "
        << "--frequency " << hz << " "
        << "--ssrc " << ssrc << " "
        << "--radio " << shellQuote(cfg.ka9q_radio);
    return cmd.str();
}

static std::string buildKa9qTuneCommand(const Config& cfg) {
    return buildKa9qTuneCommandFor(cfg, cfg.frequency_mhz, ka9qSsrc(cfg.frequency_mhz, 1), 5);
}

static void closeKa9qSsrc(const Config& cfg, const std::string& ssrc, Logger& log) {
    std::ostringstream cmd;
    cmd << "timeout 5 tune --samprate " << cfg.sample_rate << " --mode iq "
        << "--frequency 0 "
        << "--ssrc " << ssrc << " "
        << "--radio " << shellQuote(cfg.ka9q_radio)
        << " >/dev/null 2>&1";
    log.debug("closing KA9Q channel: " + cmd.str());

    const int rc = std::system(cmd.str().c_str());
    if (rc != 0 && cfg.verbose) {
        log.debug(
            "closing KA9Q channel failed for SSRC " +
            ssrc +
            ", rc=" +
            std::to_string(rc)
        );
    }

}

static void closeKa9qChannel(const Config& cfg, Logger& log) {
    closeKa9qSsrc(cfg, ka9qSsrc(cfg.frequency_mhz, 1), log);
}

static std::string expandDecoderArgs(std::string args, const Config& cfg) {
    auto replaceAll = [](std::string& text, const std::string& key, const std::string& value) {
        size_t pos = 0;
        while ((pos = text.find(key, pos)) != std::string::npos) {
            text.replace(pos, key.size(), value);
            pos += value.size();
        }
    };
    replaceAll(args, "{iq_offset}", std::to_string(cfg.iq_offset_hz));
    replaceAll(args, "{sample_rate}", std::to_string(cfg.sample_rate));
    return args;
}

static int effectiveDecoderSampleRate(const Config& cfg, const std::string& decoder) {
    (void)decoder;
    return cfg.sample_rate;
}

static std::string makeTempKa9qScript(const Config& cfg, const std::string& decoder_cmd, const std::string& decoder_label, const std::string& decoder_args) {
    long long hz = freqHz(cfg.frequency_mhz);
    std::string ssrc = ka9qSsrc(cfg.frequency_mhz, 1);

    char tmpl[] = "/tmp/wsrx-ka9q-XXXXXX.sh";
    int fd = mkstemps(tmpl, 3);
    if (fd < 0) {
        throw std::runtime_error("Could not create temporary KA9Q pipeline script");
    }
    close(fd);

    std::ofstream script(tmpl, std::ios::trunc);
    if (!script) {
        throw std::runtime_error("Could not write temporary KA9Q pipeline script");
    }

    script << "#!/bin/sh\n"
           << "set -u\n"
           << "echo '[pipeline] opening KA9Q channel ssrc=" << ssrc
           << " freq_hz=" << hz
           << " low=" << cfg.ka9q_low_hz
           << " high=" << cfg.ka9q_high_hz
           << " sample_rate=" << cfg.sample_rate << "' >&2\n"
           << buildKa9qTuneCommand(cfg) << " 2>&1\n"
           << "echo '[pipeline] starting pcmrecord -> " << decoder_label << " JSON raw IQ' >&2\n"
           << "echo '[pipeline] " << decoder_label << " options: " << decoder_args << "' >&2\n"
           << "if command -v stdbuf >/dev/null 2>&1; then DEC_PREFIX='stdbuf -oL'; else DEC_PREFIX=''; fi\n";

    script << "pcmrecord --ssrc " << ssrc
           << " --catmode --raw " << shellQuote(cfg.ka9q_pcm);

    script << " | $DEC_PREFIX " << shellQuote(decoder_cmd) << " " << decoder_args << "\n";

    script.close();
    chmod(tmpl, 0700);
    return tmpl;
}

static std::string buildKa9qDecoderPipeline(const Config& cfg, const std::string& decoder_cmd, const std::string& decoder_label, const std::string& decoder_args, Logger& log) {
    (void)log;
    std::string script = makeTempKa9qScript(cfg, decoder_cmd, decoder_label, decoder_args);
    std::ostringstream info;
    info << decoder_label << " KA9Q mode: pcmrecord raw IQ -> " << decoder_label
         << " JSON args=[" << decoder_args << "]"
         << " sample_rate=" << cfg.sample_rate
         << " channel=[" << cfg.ka9q_low_hz << "," << cfg.ka9q_high_hz << "]";
    return "/bin/sh " + shellQuote(script);
}

static std::string executableDir(const char* argv0) {
    std::error_code ec;
    std::filesystem::path exe_path;
#ifdef __linux__
    char buf[4096];
    ssize_t len = ::readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (len > 0) {
        buf[len] = '\0';
        exe_path = std::filesystem::path(buf);
    }
#endif
    if (exe_path.empty()) exe_path = std::filesystem::absolute(argv0, ec);
    if (exe_path.empty()) return ".";
    return exe_path.parent_path().string();
}

static bool isAbsolutePath(const std::string& path) {
    return std::filesystem::path(path).is_absolute();
}

static std::string resolveRelativeToBaseDir(const std::string& path) {
    if (path.empty() || isAbsolutePath(path)) return path;
    return (std::filesystem::path(g_base_dir) / path).string();
}

static void applyRuntimeDefaults(Config& cfg) {
    if (cfg.decoder_dir.empty()) cfg.decoder_dir = "decoder";
    cfg.decoder_dir = resolveRelativeToBaseDir(cfg.decoder_dir);
}

static std::string lowerCopy(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

static std::string normalizeDecoderName(const std::string& decoder) {
    std::string d = lowerCopy(decoder);
    if (d.find("rs41") != std::string::npos) return "rs41";
    if (d.find("dfm") != std::string::npos) return "dfm";
    if (d.find("m10") != std::string::npos) return "m10";
    if (d.find("m20") != std::string::npos) return "m20";
    if (d.find("imet") != std::string::npos) return "imet";
    if (d.find("meisei") != std::string::npos) return "meisei";
    if (d.find("c34c50") != std::string::npos) return "c34c50";
    return d;
}

static std::string decoderLabel(const std::string& decoder) {
    std::string d = normalizeDecoderName(decoder);
    if (d == "rs41") return "RS41";
    if (d == "dfm") return "DFM";
    if (d == "m10") return "M10";
    if (d == "m20") return "M20";
    if (d == "imet") return "IMET";
    if (d == "meisei") return "IMS100";
    if (d == "c34c50") return "c34c50";
    return decoder;
}

static void validateRequiredDecoderFiles(const Config& cfg) {
    const std::vector<std::string> required = {
        "rs41mod",
        "dfm09mod",
        "m10m20mod",
        "imet4iq",
        "meisei100mod",
        "c50iq",
        "dft_detect"
    };

    for (const auto& file : required) {
        const std::string path = joinPath(cfg.decoder_dir, file);
        if (!fileExists(path)) {
            throw std::runtime_error("Required decoder file missing: " + path + " (decoder_dir=" + cfg.decoder_dir + ")");
        }
    }
}

static std::string decoderCommandPath(const Config& cfg, const std::string& decoder) {
    std::string d = normalizeDecoderName(decoder);
    if (d == "rs41") return joinPath(cfg.decoder_dir, "rs41mod");
    if (d == "dfm") return joinPath(cfg.decoder_dir, "dfm09mod");
    if (d == "m10") return joinPath(cfg.decoder_dir, "m10m20mod");
    if (d == "m20") return joinPath(cfg.decoder_dir, "m10m20mod");
    if (d == "imet") return joinPath(cfg.decoder_dir, "imet4iq");
    if (d == "meisei") return joinPath(cfg.decoder_dir, "meisei100mod");
    if (d == "c34c50") return joinPath(cfg.decoder_dir, "c50iq");
    throw std::runtime_error("Unsupported decoder: " + decoder);
}

static std::string decoderArgsFor(const Config& cfg, const std::string& decoder) {
    std::string d = normalizeDecoderName(decoder);
    if (d == "rs41") return expandDecoderArgs("--ecc2 --crc -vx --ptu --json --IQ {iq_offset} - {sample_rate} 16", cfg);
    if (d == "dfm") return expandDecoderArgs("-i -vv --ecc --json --dist --ptu --IQ {iq_offset} - {sample_rate} 16", cfg);
    if (d == "m10") return expandDecoderArgs("-vv --ptu --json --IQ {iq_offset} - {sample_rate} 16", cfg);
    if (d == "m20") return expandDecoderArgs(" -vv --ptu --json --IQ {iq_offset} - {sample_rate} 16", cfg);
    if (d == "imet") return expandDecoderArgs("--json --iq {iq_offset} - {sample_rate} 16", cfg);
    if (d == "meisei") return expandDecoderArgs("--json --dc --IQ {iq_offset} - {sample_rate} 16", cfg);
    if (d == "c34c50") return expandDecoderArgs("--json --ptu --xor-auto --lpIQ --dc --iq {iq_offset} - {sample_rate} 16", cfg);
    throw std::runtime_error("Unsupported decoder: " + decoder);
}

static std::string buildDecoderCommand(const Config& cfg, Logger& log) {
    const std::string decoder_name = normalizeDecoderName(cfg.decoder);
    const std::string label = decoderLabel(decoder_name);
    const std::string cmd_path = decoderCommandPath(cfg, decoder_name);
    const std::string args = decoderArgsFor(cfg, decoder_name);

    if (!fileExists(cmd_path)) {
        throw std::runtime_error(label + " decoder not found: " + cmd_path + " (expected in decoder/ next to wsrx)");
    }
    if (!cfg.wav_file.empty()) {
        std::string prefix;
        if (std::system("command -v stdbuf >/dev/null 2>&1") == 0) prefix = "stdbuf -oL ";
        return prefix + shellQuote(cmd_path) + " " + args + " " + shellQuote(cfg.wav_file);
    }

    return buildKa9qDecoderPipeline(cfg, cmd_path, label, args, log);
}

struct ScanDetection {
    double frequency_mhz = 0.0;
    std::string sonde_type;
    double offset_hz = 0.0;
    double score = NAN;
};

struct LearnedOffset {
    double offset_hz = NAN;
    std::time_t updated = 0;
};

class OffsetCache {
public:
    OffsetCache() = default;

    void setPath(const std::string& path) {
        path_ = path;
    }

    void load(Logger& log) {
        std::lock_guard<std::mutex> lock(mutex_);
        cache_.clear();
        if (path_.empty()) return;
        std::ifstream in(path_);
        if (!in) {
            log.debug("offset cache not found yet: " + path_);
            return;
        }

        std::string line;
        int count = 0;
        int skipped_stale = 0;
        const std::time_t now = std::time(nullptr);
        while (std::getline(in, line)) {
            if (line.empty() || line[0] == '#') continue;
            std::istringstream iss(line);
            long long key = 0;
            double offset = NAN;
            long long ts = 0;
            if (!(iss >> key >> offset >> ts)) continue;
            if (!std::isfinite(offset)) continue;
            if (std::fabs(offset) > 25000.0) continue;
            if (now - static_cast<std::time_t>(ts) > MAX_AGE_SEC) {
                ++skipped_stale;
                continue;
            }
            cache_[key] = LearnedOffset{offset, static_cast<std::time_t>(ts)};
            ++count;
        }
        if (count > 0 || skipped_stale > 0) {
            std::ostringstream msg;
            msg << "loaded " << count << " learned frequency offset(s) from " << path_
                << " (" << skipped_stale << " stale entries dropped)";
        }
    }

    std::optional<double> get(double mhz, int quantization_hz) const {
        std::lock_guard<std::mutex> lock(mutex_);
        long long key = keyFor(mhz, quantization_hz);
        auto it = cache_.find(key);
        if (it == cache_.end()) return std::nullopt;
        if (!std::isfinite(it->second.offset_hz)) return std::nullopt;
        const std::time_t age_sec = std::time(nullptr) - it->second.updated;
        if (age_sec > MAX_AGE_SEC) return std::nullopt;
        return it->second.offset_hz;
    }

    void update(double tx_mhz, double tuned_mhz, int quantization_hz, Logger& log) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!std::isfinite(tx_mhz) || !std::isfinite(tuned_mhz) || tx_mhz <= 0.0 || tuned_mhz <= 0.0) return;
        double offset_hz = (tuned_mhz - tx_mhz) * 1000000.0;
        if (!std::isfinite(offset_hz) || std::fabs(offset_hz) > 25000.0) return;

        long long key = keyFor(tx_mhz, quantization_hz);
        auto now = std::time(nullptr);
        auto it = cache_.find(key);
        bool changed = true;
        if (it != cache_.end() && std::fabs(it->second.offset_hz - offset_hz) < 100.0) {
            changed = false;
            it->second.offset_hz = offset_hz;
            it->second.updated = now;
        } else {
            cache_[key] = LearnedOffset{offset_hz, now};
        }

        std::ostringstream msg;
        msg << (changed ? "learned" : "refreshed") << " offset for "
            << (static_cast<double>(key) / 1000000.0)
            << " MHz: " << offset_hz << " Hz (tuned=" << tuned_mhz
            << " MHz tx=" << tx_mhz << " MHz)";
        if (changed) log.info(msg.str()); else log.debug(msg.str());

        const auto steady_now = std::chrono::steady_clock::now();
        const bool due_for_periodic_save = last_save_.time_since_epoch().count() == 0 ||
            std::chrono::duration_cast<std::chrono::seconds>(steady_now - last_save_).count() >= SAVE_MIN_INTERVAL_SEC;
        if (changed || due_for_periodic_save) {
            save(log);
            last_save_ = steady_now;
        }
    }

private:
    static constexpr long long MAX_AGE_SEC = 7 * 24 * 3600;
    static constexpr int SAVE_MIN_INTERVAL_SEC = 30;

    static long long keyFor(double mhz, int quantization_hz) {
        long long hz = static_cast<long long>(std::llround(mhz * 1000000.0));
        long long q = std::max(1000, quantization_hz);
        return static_cast<long long>(std::llround(static_cast<double>(hz) / static_cast<double>(q))) * q;
    }

    void save(Logger& log) const {
        if (path_.empty()) return;
        std::ofstream out(path_, std::ios::trunc);
        if (!out) {
            log.warn("could not write offset cache: " + path_);
            return;
        }
        out << "# wsrx learned frequency offsets\n";
        out << "# format: quantized_frequency_hz offset_hz unix_timestamp\n";
        for (const auto& [key, value] : cache_) {
            if (!std::isfinite(value.offset_hz)) continue;
            out << key << ' ' << value.offset_hz << ' ' << static_cast<long long>(value.updated) << "\n";
        }
    }

    std::string path_;
    std::map<long long, LearnedOffset> cache_;
    mutable std::mutex mutex_;
    std::chrono::steady_clock::time_point last_save_{};
};

static std::optional<ScanDetection> parseDftDetectOutput(const std::string& output, double frequency_mhz) {
    if (output.empty()) return std::nullopt;

    std::string first_line;
    std::istringstream iss(output);
    std::getline(iss, first_line);
    while (!first_line.empty() && (first_line.back() == '\n' || first_line.back() == '\r')) first_line.pop_back();
    if (first_line.empty()) return std::nullopt;

    ScanDetection det;
    det.frequency_mhz = frequency_mhz;

    const std::vector<std::string> known = {"RS41", "RS92", "DFM", "M10", "M20", "IMET", "LMS6", "MEISEI", "MRZ", "MTS01"};
    for (const auto& k : known) {
        if (first_line.find(k) != std::string::npos) {
            det.sonde_type = k;
            break;
        }
    }
    if (det.sonde_type.empty()) return std::nullopt;

    std::smatch m;
    static const std::regex score_re(R"(:\s*(-?[0-9]+(?:\.[0-9]+)?))");
    if (std::regex_search(first_line, m, score_re)) {
        det.score = std::stod(m[1].str());
    }

    static const std::regex offset_re(R"(,\s*(-?[0-9]+(?:\.[0-9]+)?)\s*Hz)", std::regex_constants::icase);
    if (std::regex_search(first_line, m, offset_re)) {
        det.offset_hz = std::stod(m[1].str());
    }

    return det;
}


struct SpectrumBin {
    double frequency_hz = 0.0;
    double power_db = 0.0;
};

static std::vector<std::string> splitCsvLine(const std::string& line, size_t maxsplit = 0) {
    std::vector<std::string> out;
    std::string cur;
    size_t splits = 0;
    for (char c : line) {
        if (c == ',' && (maxsplit == 0 || splits < maxsplit)) {
            out.push_back(cur);
            cur.clear();
            ++splits;
        } else {
            cur.push_back(c);
        }
    }
    out.push_back(cur);
    return out;
}

static std::vector<SpectrumBin> readKa9qPowerCsv(const std::string& path, Logger& log) {
    std::vector<SpectrumBin> bins;
    std::ifstream in(path);
    if (!in) return bins;

    std::string line;
    bool first_line = true;
    while (std::getline(in, line)) {
        if (line.empty()) continue;

        if (first_line) {
            first_line = false;
            if (line.find("start") != std::string::npos || line.find("freq") != std::string::npos) continue;
        }

        auto fields = splitCsvLine(line, 5);
        if (fields.size() < 6) continue;

        try {
            double start_hz = std::stod(fields[1]);
            double stop_hz = std::stod(fields[2]);
            int n_samples = std::stoi(fields[4]);
            auto samples = splitCsvLine(fields[5]);
            if (n_samples <= 0 || samples.empty()) continue;

            const size_t n = samples.size();
            for (size_t i = 0; i < n; ++i) {
                std::string v = samples[i];
                if (v.empty()) continue;
                double pwr = std::stod(v);
                if (!std::isfinite(pwr)) continue;
                double f_hz = (n > 1) ? start_hz + (stop_hz - start_hz) * static_cast<double>(i) / static_cast<double>(n - 1) : start_hz;
                bins.push_back({f_hz, pwr});
            }
        } catch (const std::exception& e) {
            log.debug(std::string("scan power CSV parse skipped line: ") + e.what());
            continue;
        }
    }

    return bins;
}

static double medianPower(std::vector<SpectrumBin> bins) {
    if (bins.empty()) return NAN;
    std::vector<double> values;
    values.reserve(bins.size());
    for (const auto& b : bins) values.push_back(b.power_db);
    std::sort(values.begin(), values.end());
    const size_t mid = values.size() / 2;
    if (values.size() % 2) return values[mid];
    return (values[mid - 1] + values[mid]) / 2.0;
}

static double estimatePeakWidthHz(const std::vector<SpectrumBin>& spectrum, size_t peak_idx, double trigger_db) {
    if (spectrum.empty() || peak_idx >= spectrum.size()) return 0.0;

    size_t left = peak_idx;
    while (left > 0 && spectrum[left - 1].power_db >= trigger_db) --left;

    size_t right = peak_idx;
    while (right + 1 < spectrum.size() && spectrum[right + 1].power_db >= trigger_db) ++right;

    if (right <= left) {
        if (spectrum.size() >= 2) {
            if (peak_idx > 0) return std::fabs(spectrum[peak_idx].frequency_hz - spectrum[peak_idx - 1].frequency_hz);
            return std::fabs(spectrum[peak_idx + 1].frequency_hz - spectrum[peak_idx].frequency_hz);
        }
        return 0.0;
    }

    return std::fabs(spectrum[right].frequency_hz - spectrum[left].frequency_hz);
}


static void writeScanSpectrumJson(const std::string& base_dir,
                                  const std::vector<SpectrumBin>& spectrum,
                                  double noise_floor,
                                  double trigger,
                                  const std::vector<size_t>& peak_idx,
                                  bool used_fallback,
                                  Logger& log) {
    if (spectrum.empty()) return;
    try {
        std::filesystem::path data_dir = std::filesystem::path(base_dir) / "data";
        std::error_code ec;
        std::filesystem::create_directories(data_dir, ec);
        const std::filesystem::path tmp_path = data_dir / "scan_spectrum.json.tmp";
        const std::filesystem::path out_path = data_dir / "scan_spectrum.json";

        std::ofstream out(tmp_path, std::ios::trunc);
        if (!out) {
            log.warn("could not write scan spectrum: " + tmp_path.string());
            return;
        }

        std::time_t now = std::time(nullptr);
        char ts[64];
        std::tm tm{};
        gmtime_r(&now, &tm);
        std::strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S UTC", &tm);

        out << "{\n";
        out << "  \"timestamp\": \"" << ts << "\",\n";
        out << "  \"unix\": " << static_cast<long long>(now) << ",\n";
        out << "  \"noise_floor_db\": " << noise_floor << ",\n";
        out << "  \"trigger_db\": " << trigger << ",\n";
        out << "  \"fallback\": " << (used_fallback ? "true" : "false") << ",\n";
        out << "  \"points\": [";
        for (size_t i = 0; i < spectrum.size(); ++i) {
            if (i) out << ",";
            out << "[" << (spectrum[i].frequency_hz / 1000000.0) << "," << spectrum[i].power_db << "]";
        }
        out << "],\n";
        out << "  \"peaks\": [";
        for (size_t i = 0; i < peak_idx.size(); ++i) {
            if (i) out << ",";
            const auto& b = spectrum[peak_idx[i]];
            out << "[" << (b.frequency_hz / 1000000.0) << "," << b.power_db << "]";
        }
        out << "]\n";
        out << "}\n";
        out.close();
        std::filesystem::rename(tmp_path, out_path, ec);
        if (ec) {
            std::filesystem::remove(out_path, ec);
            std::filesystem::rename(tmp_path, out_path, ec);
        }
        if (ec) log.warn("could not publish scan spectrum: " + out_path.string());
    } catch (const std::exception& e) {
        log.warn(std::string("could not write scan spectrum: ") + e.what());
    }
}



static bool isBlacklistedFrequencyHz(const Config& cfg, double frequency_hz) {
    const double width_hz = std::max(0.0, cfg.scan_active_skip_width_khz) * 1000.0;
    for (double mhz : cfg.scan_blacklist_mhz) {
        if (!std::isfinite(mhz) || mhz <= 0.0) continue;
        if (std::fabs(frequency_hz - mhz * 1000000.0) <= width_hz) return true;
    }
    return false;
}

static bool isBlacklistedFrequencyMhz(const Config& cfg, double frequency_mhz) {
    return isBlacklistedFrequencyHz(cfg, frequency_mhz * 1000000.0);
}
static void writeLiveSpectrumJson(const std::string& base_dir,
                                  const std::vector<SpectrumBin>& spectrum,
                                  double noise_floor,
                                  double trigger,
                                  Logger& log) {
    if (spectrum.empty()) return;
    try {
        std::filesystem::path data_dir = std::filesystem::path(base_dir) / "data";
        std::error_code ec;
        std::filesystem::create_directories(data_dir, ec);
        const std::filesystem::path tmp_path = data_dir / "spectrum_live.json.tmp";
        const std::filesystem::path out_path = data_dir / "spectrum_live.json";
        std::ofstream out(tmp_path, std::ios::trunc);
        if (!out) {
            log.warn("could not write live spectrum: " + tmp_path.string());
            return;
        }
        std::time_t now = std::time(nullptr);
        char ts[64];
        std::tm tm{};
        gmtime_r(&now, &tm);
        std::strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S UTC", &tm);
        out << "{\n";
        out << "  \"timestamp\": \"" << ts << "\",\n";
        out << "  \"unix\": " << static_cast<long long>(now) << ",\n";
        out << "  \"noise_floor_db\": " << noise_floor << ",\n";
        out << "  \"trigger_db\": " << trigger << ",\n";
        out << "  \"points\": [";
        for (size_t i = 0; i < spectrum.size(); ++i) {
            if (i) out << ",";
            out << "[" << (spectrum[i].frequency_hz / 1000000.0) << "," << spectrum[i].power_db << "]";
        }
        out << "],\n";
        out << "  \"source\": \"live\"\n";
        out << "}\n";
        out.close();
        std::filesystem::rename(tmp_path, out_path, ec);
        if (ec) {
            std::filesystem::remove(out_path, ec);
            std::filesystem::rename(tmp_path, out_path, ec);
        }
        if (ec) log.warn("could not publish live spectrum: " + out_path.string());
    } catch (const std::exception& e) {
        log.warn(std::string("could not write live spectrum: ") + e.what());
    }
}

static void writeScanPeaksJson(const std::string& base_dir,
                               const std::vector<SpectrumBin>& spectrum,
                               double noise_floor,
                               double trigger,
                               const std::vector<size_t>& peak_idx,
                               bool used_fallback,
                               Logger& log) {
    try {
        std::filesystem::path data_dir = std::filesystem::path(base_dir) / "data";
        std::error_code ec;
        std::filesystem::create_directories(data_dir, ec);
        const std::filesystem::path tmp_path = data_dir / "scan_peaks.json.tmp";
        const std::filesystem::path out_path = data_dir / "scan_peaks.json";
        std::ofstream out(tmp_path, std::ios::trunc);
        if (!out) {
            log.warn("could not write scan peaks: " + tmp_path.string());
            return;
        }
        std::time_t now = std::time(nullptr);
        char ts[64];
        std::tm tm{};
        gmtime_r(&now, &tm);
        std::strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S UTC", &tm);
        out << "{\n";
        out << "  \"timestamp\": \"" << ts << "\",\n";
        out << "  \"unix\": " << static_cast<long long>(now) << ",\n";
        out << "  \"noise_floor_db\": " << noise_floor << ",\n";
        out << "  \"trigger_db\": " << trigger << ",\n";
        out << "  \"fallback\": " << (used_fallback ? "true" : "false") << ",\n";
        out << "  \"peaks\": [";
        for (size_t i = 0; i < peak_idx.size(); ++i) {
            if (i) out << ",";
            const auto& b = spectrum[peak_idx[i]];
            out << "[" << (b.frequency_hz / 1000000.0) << "," << b.power_db << "]";
        }
        out << "]\n";
        out << "}\n";
        out.close();
        std::filesystem::rename(tmp_path, out_path, ec);
        if (ec) {
            std::filesystem::remove(out_path, ec);
            std::filesystem::rename(tmp_path, out_path, ec);
        }
        if (ec) log.warn("could not publish scan peaks: " + out_path.string());
    } catch (const std::exception& e) {
        log.warn(std::string("could not write scan peaks: ") + e.what());
    }
}

static std::vector<double> runKa9qPowerScanInner(const Config& cfg, Logger& log, bool allow_fallback_candidates) {
    const std::string powers = "powers";
    long long start_hz = freqHz(cfg.scan_min_mhz);
    long long stop_hz = freqHz(cfg.scan_max_mhz);
    double center_hz = (static_cast<double>(start_hz) + static_cast<double>(stop_hz)) / 2.0;
    int bins = static_cast<int>(std::floor((static_cast<double>(stop_hz - start_hz)) / cfg.scan_power_bin_hz)) + 1;
    if (bins < 8) bins = 8;

    const std::string log_path = "/tmp/wsrx_power_" + std::to_string(::getpid()) + ".csv";
    std::string ssrc = std::to_string(static_cast<long long>(std::llround(center_hz / 1000.0))) + "03";

    std::ostringstream cmd;
    cmd << "timeout " << (cfg.scan_spectrum_dwell_sec + 10) << " "
        << powers << " " << shellQuote(cfg.ka9q_radio) << " "
        << "-f " << static_cast<long long>(std::llround(center_hz)) << " "
        << "-w " << cfg.scan_power_bin_hz << " "
        << "-b " << bins << " "
        << "-i " << cfg.scan_spectrum_dwell_sec << " "
        << "-s " << ssrc << " "
        << "-c 2 > " << shellQuote(log_path) << " 2>/tmp/wsrx_power_" << ::getpid() << ".err";

    if (cfg.verbose || cfg.decoder_debug) log.debug("scan power command: " + cmd.str());

    std::vector<SpectrumBin> spectrum;
    {
        std::lock_guard<std::mutex> powers_lock(g_powers_mutex);
        int rc = std::system(cmd.str().c_str());
        if (rc != 0) {
            std::ostringstream msg;
            msg << "scan powers failed rc=" << rc << " - is the KA9Q 'powers' binary installed/in PATH?";
            log.warn(msg.str());
            return {};
        }
        spectrum = readKa9qPowerCsv(log_path, log);
        std::remove(log_path.c_str());
    }
    if (spectrum.empty()) {
        log.warn("scan powers produced no spectrum data");
        return {};
    }

    double nf = medianPower(spectrum);
    double trigger = nf + cfg.scan_threshold_db;
    std::ostringstream nfmsg;

    nfmsg << "scan noise_floor=" << nf << " dB threshold=" << cfg.scan_threshold_db << " dB trigger=" << trigger << " dB ";
    log.info(nfmsg.str());

    auto addPeakIndex = [&](std::vector<size_t>& list, size_t idx) {
        const double min_dist_hz = static_cast<double>(cfg.scan_min_distance_hz);
        for (size_t& old_idx : list) {
            if (std::fabs(spectrum[old_idx].frequency_hz - spectrum[idx].frequency_hz) < min_dist_hz) {
                if (spectrum[idx].power_db > spectrum[old_idx].power_db) old_idx = idx;
                return;
            }
        }
        list.push_back(idx);
    };

    auto peakWidthOk = [&](size_t idx) {
        if (cfg.scan_min_peak_width_hz <= 0) return true;
        const double width_hz = estimatePeakWidthHz(spectrum, idx, trigger);
        if (width_hz >= static_cast<double>(cfg.scan_min_peak_width_hz)) return true;
        if (cfg.verbose || cfg.decoder_debug) {
            std::ostringstream msg;
            msg << "scan peak ignored " << (spectrum[idx].frequency_hz / 1e6)
                << " MHz width=" << width_hz << " Hz min=" << cfg.scan_min_peak_width_hz << " Hz";
            log.debug(msg.str());
        }
        return false;
    };

    std::vector<size_t> peak_idx;
    for (size_t i = 1; i + 1 < spectrum.size(); ++i) {
        if (isBlacklistedFrequencyHz(cfg, spectrum[i].frequency_hz)) continue;
        if (spectrum[i].power_db < trigger) continue;
        if (spectrum[i].power_db < spectrum[i - 1].power_db) continue;
        if (spectrum[i].power_db < spectrum[i + 1].power_db) continue;
        if (!peakWidthOk(i)) continue;
        addPeakIndex(peak_idx, i);
    }

    std::sort(peak_idx.begin(), peak_idx.end(), [&](size_t a, size_t b) {
        return spectrum[a].power_db > spectrum[b].power_db;
    });

    bool used_fallback = false;
    if (peak_idx.empty()) {
        if (!allow_fallback_candidates || cfg.scan_fallback_candidates <= 0) {
            writeScanSpectrumJson(g_base_dir, spectrum, nf, trigger, peak_idx, used_fallback, log);
            writeScanPeaksJson(g_base_dir, spectrum, nf, trigger, peak_idx, used_fallback, log);
            return {};
        }
        used_fallback = true;
        std::ostringstream fbmsg;
        fbmsg << "scan spectrum: no peaks above threshold, checking up to "
              << cfg.scan_fallback_candidates << " strongest candidate(s) with snr >= "
              << cfg.scan_fallback_min_snr_db << " dB";
        log.info(fbmsg.str());
        std::vector<size_t> all_idx;
        all_idx.reserve(spectrum.size());
        for (size_t i = 0; i < spectrum.size(); ++i) all_idx.push_back(i);
        std::sort(all_idx.begin(), all_idx.end(), [&](size_t a, size_t b) {
            return spectrum[a].power_db > spectrum[b].power_db;
        });
        for (size_t idx : all_idx) {
            if (isBlacklistedFrequencyHz(cfg, spectrum[idx].frequency_hz)) continue;
            const double snr = spectrum[idx].power_db - nf;
            if (snr < cfg.scan_fallback_min_snr_db) break;
            if (!peakWidthOk(idx)) continue;
            addPeakIndex(peak_idx, idx);
            if (static_cast<int>(peak_idx.size()) >= cfg.scan_fallback_candidates) break;
        }
        std::sort(peak_idx.begin(), peak_idx.end(), [&](size_t a, size_t b) {
            return spectrum[a].power_db > spectrum[b].power_db;
        });
    }

    if (static_cast<int>(peak_idx.size()) > cfg.scan_max_peaks) peak_idx.resize(static_cast<size_t>(cfg.scan_max_peaks));

    writeScanSpectrumJson(g_base_dir, spectrum, nf, trigger, peak_idx, used_fallback, log);
    writeScanPeaksJson(g_base_dir, spectrum, nf, trigger, peak_idx, used_fallback, log);

    std::vector<double> peaks_hz;
    std::vector<double> quantized_hz;
    for (size_t idx : peak_idx) {
        if (isBlacklistedFrequencyHz(cfg, spectrum[idx].frequency_hz)) continue;
        double q = static_cast<double>(cfg.scan_quantization_hz);
        double raw_f = spectrum[idx].frequency_hz;
        double q_f = std::round(raw_f / q) * q;
        if (q_f < start_hz - q / 2.0 || q_f > stop_hz + q / 2.0) continue;
        if (std::find_if(quantized_hz.begin(), quantized_hz.end(), [&](double old) { return std::fabs(old - q_f) < q / 2.0; }) != quantized_hz.end()) continue;
        quantized_hz.push_back(q_f);
        peaks_hz.push_back(raw_f);
        std::ostringstream msg;
        msg << (used_fallback ? "scan candidate " : "scan peak ")
            << (raw_f / 1e6) << " MHz";
        if (std::fabs(raw_f - q_f) >= 100.0) {
            msg << " channel=" << (q_f / 1e6) << " MHz";
        }
        msg << " level=" << spectrum[idx].power_db << " dB";
        if (cfg.scan_min_peak_width_hz > 0) {
            msg << " width=" << estimatePeakWidthHz(spectrum, idx, trigger) << " Hz";
        }
        if (used_fallback) msg << " snr=" << (spectrum[idx].power_db - nf) << " dB";
        log.info(msg.str());
    }

    std::vector<double> ordered_hz;
    std::vector<double> ordered_quantized_hz;
    auto appendUnique = [&](double hz) {
        if (!std::isfinite(hz) || hz <= 0.0) return;
        if (isBlacklistedFrequencyHz(cfg, hz)) return;
        const double q = static_cast<double>(cfg.scan_quantization_hz);
        const double qhz = std::round(hz / q) * q;
        for (double old_qhz : ordered_quantized_hz) {
            if (std::fabs(old_qhz - qhz) < q / 2.0) return;
        }
        ordered_quantized_hz.push_back(qhz);
        ordered_hz.push_back(hz);
    };
    for (double mhz : cfg.scan_whitelist_mhz) appendUnique(mhz * 1e6);
    for (double hz : peaks_hz) appendUnique(hz);

    if (ordered_hz.empty()) log.info("scan spectrum: no usable candidates");
    return ordered_hz;
}

static std::vector<double> runKa9qPowerScan(const Config& cfg, Logger& log, bool allow_fallback_candidates) {
    std::vector<double> result = runKa9qPowerScanInner(cfg, log, allow_fallback_candidates);

    if (cfg.scan_whitelist_mhz.empty()) return result;

    const double q = static_cast<double>(cfg.scan_quantization_hz);
    auto alreadyPresent = [&](double hz) {
        for (double existing : result) {
            if (std::fabs(existing - hz) < q / 2.0) return true;
        }
        return false;
    };

    for (double mhz : cfg.scan_whitelist_mhz) {
        if (!std::isfinite(mhz) || mhz <= 0.0) continue;
        const double hz = mhz * 1e6;
        if (isBlacklistedFrequencyMhz(cfg, mhz)) continue;
        if (alreadyPresent(hz)) continue;
        if (cfg.verbose || cfg.decoder_debug) {
            log.debug("scan whitelist_mhz: forcing candidate " + std::to_string(mhz) + " MHz");
        }
        result.push_back(hz);
    }

    return result;
}

static std::optional<ScanDetection> runSingleScanDetection(Config cfg, double frequency_mhz, Logger& log) {
    cfg.frequency_mhz = frequency_mhz;

    const int suffix = 20 + static_cast<int>(g_scan_ssrc_sequence.fetch_add(1, std::memory_order_relaxed) % 70u);
    const std::string ssrc = ka9qSsrc(frequency_mhz, suffix);
    const std::string dft_detect = joinPath(cfg.decoder_dir, "dft_detect");

    if (!fileExists(dft_detect)) {
        throw std::runtime_error("Required decoder file missing: " + dft_detect + " (decoder_dir=" + cfg.decoder_dir + ")");
    }

    if (cfg.decoder_debug) {
        std::ostringstream msg;
        msg << "scan detect trial " << frequency_mhz << " MHz";
        log.debug(msg.str());
    }

    std::string tune_cmd = buildKa9qTuneCommandFor(cfg, frequency_mhz, ssrc, cfg.scan_tune_timeout_sec);

    const int tune_rc = std::system((tune_cmd + " >/dev/null 2>&1").c_str());
    if (tune_rc != 0) {
        if (cfg.decoder_debug) {
            std::ostringstream msg;
            msg << "scan tune failed " << frequency_mhz << " MHz rc=" << tune_rc;
            log.debug(msg.str());
        }
        closeKa9qSsrc(cfg, ssrc, log);
        return std::nullopt;
    }

    std::ostringstream cmd;
    cmd << "timeout " << (cfg.scan_detect_dwell_sec * 2 + 2) << " "
        << "pcmrecord --ssrc " << ssrc
        << " --catmode --raw " << shellQuote(cfg.ka9q_pcm)
        << " | " << shellQuote(dft_detect)
        << " -t " << cfg.scan_detect_dwell_sec
        << " --types RS41,DFM9,M10,IMET4,MEISEI,C34C50"
        << " --iq --bw 15 --dc - "
        << cfg.sample_rate << " 16 2>/dev/null";

    if (cfg.decoder_debug) {
        log.debug("scan command: " + cmd.str());
    }

    FILE* fp = popen(cmd.str().c_str(), "r");
    if (!fp) {
        closeKa9qSsrc(cfg, ssrc, log);
        return std::nullopt;
    }

    std::string output;
    char buf[1024];
    while (!g_shutdown && fgets(buf, sizeof(buf), fp)) {
        output += buf;
    }
    int rc = pclose(fp);
    closeKa9qSsrc(cfg, ssrc, log);

    if (cfg.decoder_debug && !output.empty()) {
        std::string one = output;
        while (!one.empty() && (one.back() == '\n' || one.back() == '\r')) one.pop_back();
        log.debug("scan dft_detect output: " + one);
    }

    auto det = parseDftDetectOutput(output, frequency_mhz);
    if (!det) {
        if (cfg.decoder_debug && rc != 0) {
            std::ostringstream msg;
            msg << "scan no sonde " << frequency_mhz << " MHz rc=" << rc;
            log.debug(msg.str());
        }
        return std::nullopt;
    }

    if (std::fabs(det->offset_hz) > 0.1) {
        det->frequency_mhz = frequency_mhz + det->offset_hz / 1000000.0;
    } else {
        det->frequency_mhz = frequency_mhz;
    }
    det->frequency_mhz = std::round(det->frequency_mhz * 1000.0) / 1000.0;
    return det;
}

static void addUniqueOffset(std::vector<double>& offsets, double value) {
    for (double old : offsets) {
        if (std::fabs(old - value) < 0.1) return;
    }
    offsets.push_back(value);
}

static std::vector<double> buildOffsetTrialsHz(const Config& cfg, std::optional<double> learned_offset_hz) {
    std::vector<double> offsets;

    if (learned_offset_hz && std::isfinite(*learned_offset_hz) && std::fabs(*learned_offset_hz) <= 25000.0) {
        addUniqueOffset(offsets, *learned_offset_hz);
    }

    addUniqueOffset(offsets, 0.0);

    const int max_hz = std::max(0, cfg.scan_offset_search_hz);
    const int step_hz = std::max(100, cfg.scan_offset_step_hz);
    for (int hz = step_hz; hz <= max_hz; hz += step_hz) {
        addUniqueOffset(offsets, static_cast<double>(hz));
        addUniqueOffset(offsets, static_cast<double>(-hz));
    }

    if (std::fabs(cfg.scan_decoder_offset_hz) > 0.1) {
        addUniqueOffset(offsets, cfg.scan_decoder_offset_hz);
    }

    return offsets;
}

static std::optional<ScanDetection> runScanDetection(const Config& cfg, double peak_mhz, Logger& log, const OffsetCache& offset_cache) {
    auto learned = offset_cache.get(peak_mhz, cfg.scan_quantization_hz);
    auto offsets = buildOffsetTrialsHz(cfg, learned);
    std::optional<ScanDetection> best;
    double best_score = -1.0;

    if (cfg.verbose) {
        std::ostringstream msg;
        msg << "scan detect around " << peak_mhz << " MHz offsets=";
        for (size_t i = 0; i < offsets.size(); ++i) {
            if (i) msg << ",";
            msg << offsets[i];
        }
        msg << " Hz";
        if (learned) msg << " learned=" << *learned << " Hz";
        log.debug(msg.str());
    }

    for (double off_hz : offsets) {
        if (g_shutdown) break;
        const double trial_mhz = peak_mhz + off_hz / 1000000.0;
        auto det = runSingleScanDetection(cfg, trial_mhz, log);
        if (!det) continue;

        double score = std::isnan(det->score) ? 0.0 : std::fabs(det->score);
        double candidate_offset_hz = (det->frequency_mhz - peak_mhz) * 1000000.0;
        double current_offset_hz = best ? best->offset_hz : 1e99;
        const double score_margin = 0.01;
        bool take = false;
        if (!best) {
            take = true;
        } else if (score > best_score + score_margin) {
            take = true;
        } else if (std::fabs(score - best_score) <= score_margin) {
            if (learned) {
                take = std::fabs(candidate_offset_hz - *learned) < std::fabs(current_offset_hz - *learned);
            } else {
                take = std::fabs(candidate_offset_hz) < std::fabs(current_offset_hz);
            }
        }
        if (take) {
            best = det;
            best_score = score;
            best->offset_hz = candidate_offset_hz;
        }

        const bool is_learned_trial = learned.has_value() && std::fabs(off_hz - *learned) < 0.5;
        if (is_learned_trial && best_score >= cfg.scan_accept_score) {
            if (cfg.verbose) {
                std::ostringstream msg;
                msg << "scan early-accept " << best->sonde_type << " near " << peak_mhz
                    << " MHz score=" << best_score << " >= accept_score=" << cfg.scan_accept_score
                    << " (learned offset confirmed, skipped remaining offset trials)";
                log.debug(msg.str());
            }
            break;
        }
    }

    if (best && cfg.verbose) {
        std::ostringstream msg;
        msg << "scan best " << best->sonde_type << " near " << peak_mhz
            << " MHz offset=" << best->offset_hz << " Hz score=" << best->score
            << " -> " << best->frequency_mhz << " MHz";
        log.debug(msg.str());
    }

    return best;
}

struct Channel {
    Config cfg;
    DecoderProcess decoder;
    TelemetryParser parser;
    double latest_rssi_db = NAN;
    bool got_frame = false;
    std::chrono::steady_clock::time_point started;
    std::chrono::steady_clock::time_point last_frame;
    std::atomic<bool> stop_requested{false};
    std::atomic<bool> reader_exited{false};
    std::thread reader_thread;
    std::mutex state_mutex;
};

static bool frequencyAlreadyActiveLocked(const std::vector<std::unique_ptr<Channel>>& channels, double mhz, double window_khz) {
    const double window_mhz = window_khz / 1000.0;
    for (const auto& ch : channels) {
        if (std::fabs(ch->cfg.frequency_mhz - mhz) <= window_mhz) return true;
    }
    return false;
}

static std::unique_ptr<Channel> startChannelProcess(Config cfg, Logger& log) {
    cfg.sample_rate = effectiveDecoderSampleRate(cfg, cfg.decoder);

    auto ch = std::make_unique<Channel>();
    ch->cfg = cfg;
    ch->started = std::chrono::steady_clock::now();
    ch->last_frame = ch->started;

    std::string command = buildDecoderCommand(ch->cfg, log);
    std::ostringstream msg;
    msg << "starting decoder channel " << ch->cfg.frequency_mhz << " MHz: ";
    log.info(msg.str());

    if (!ch->decoder.start(command)) {
        throw std::runtime_error("Could not start decoder process");
    }
    return ch;
}

static void channelReaderThread(Channel* ch, const Config& base_cfg, Logger& log, Uploader& uploader, OffsetCache& offset_cache) {
    while (!g_shutdown && !ch->stop_requested.load()) {
        bool read_any = false;
        while (!g_shutdown && !ch->stop_requested.load()) {
            auto line = ch->decoder.readLine();
            if (!line) break;
            read_any = true;

            if (base_cfg.verbose && shouldLogDecoderLine(*line, base_cfg.decoder_debug)) {
                std::ostringstream prefix;
                prefix << "decoder " << ch->cfg.frequency_mhz << ": " << *line;
                log.debug(prefix.str());
            }
            if (auto rssi = extractBasebandPowerDb(*line)) ch->latest_rssi_db = *rssi;

            auto frame = ch->parser.parseLine(*line, ch->cfg.frequency_mhz, base_cfg.callsign);
            if (frame) {
                if (!std::isnan(ch->latest_rssi_db)) frame->rssi_db = ch->latest_rssi_db;
                {
                    std::lock_guard<std::mutex> lock(ch->state_mutex);
                    ch->got_frame = true;
                    ch->last_frame = std::chrono::steady_clock::now();
                }

                std::ostringstream msg;
                msg << frame->type << " " << frame->serial
                    << " freq=" << frame->frequency_mhz
                    << " lat=" << frame->lat
                    << " lon=" << frame->lon
                    << " alt=" << frame->alt_m;

                if (base_cfg.scan_enabled && std::isfinite(frame->tx_frequency_mhz) && frame->tx_frequency_mhz > 0.0) {
                    offset_cache.update(frame->tx_frequency_mhz, ch->cfg.frequency_mhz, base_cfg.scan_quantization_hz, log);
                } else if (base_cfg.scan_enabled && base_cfg.verbose) {
                    log.debug("offset learning skipped: decoder JSON did not contain tx_frequency/freq");
                }

                appendDecoderJsonLog(*frame);
                uploader.sendTelemetry(*frame);
            }
        }

        if (!ch->decoder.isRunning()) break;
        if (!read_any) std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }

    ch->reader_exited.store(true);
}

static std::unique_ptr<Channel> startChannelWithReader(Config cfg, const Config& base_cfg, Logger& log, Uploader& uploader, OffsetCache& offset_cache) {
    auto ch = startChannelProcess(cfg, log);
    Channel* ptr = ch.get();
    ptr->reader_thread = std::thread(channelReaderThread, ptr, std::cref(base_cfg), std::ref(log), std::ref(uploader), std::ref(offset_cache));
    return ch;
}

static void stopChannel(Channel& ch, Logger& log) {
    std::ostringstream msg;
    msg << "stopping decoder channel " << ch.cfg.frequency_mhz << " MHz";
    log.info(msg.str());
    ch.stop_requested.store(true);
    ch.decoder.stop();
    if (ch.reader_thread.joinable()) ch.reader_thread.join();
    closeKa9qChannel(ch.cfg, log);
}

static void scanForChannelsThreaded(const Config& cfg, Logger& log, std::vector<std::unique_ptr<Channel>>& channels,
                                    std::mutex& channels_mutex, Uploader& uploader, OffsetCache& offset_cache) {
    size_t active_count = 0;
    {
        std::lock_guard<std::mutex> lock(channels_mutex);
        active_count = channels.size();
    }
    if (static_cast<int>(active_count) >= cfg.scan_max_channels) return;

    const bool allow_fallback_candidates = (active_count == 0) || cfg.scan_fallback_when_active;
    std::vector<double> peak_hz = runKa9qPowerScan(cfg, log, allow_fallback_candidates);
    if (peak_hz.empty()) return;

    std::vector<double> candidates;
    candidates.reserve(peak_hz.size());
    for (double peak : peak_hz) {
        const double f_mhz = peak / 1e6;
        if (isBlacklistedFrequencyMhz(cfg, f_mhz)) {
            std::ostringstream msg;
            msg << "scan skip never-scan peak " << f_mhz << " MHz";
            log.debug(msg.str());
            continue;
        }
        candidates.push_back(f_mhz);
    }
    if (candidates.empty()) return;

    const int max_parallel = std::max(1, cfg.scan_parallel_detections);

    struct Trial {
        double f_mhz;
        std::future<std::optional<ScanDetection>> fut;
    };
    std::vector<Trial> inflight;
    size_t next_idx = 0;
    int detections = 0;

    auto tryLaunchMore = [&]() {
        while (next_idx < candidates.size() && inflight.size() < static_cast<size_t>(max_parallel)) {
            const double f_mhz = candidates[next_idx++];
            {
                std::lock_guard<std::mutex> lock(channels_mutex);
                if (static_cast<int>(channels.size()) >= cfg.scan_max_channels) return;
                if (frequencyAlreadyActiveLocked(channels, f_mhz, cfg.scan_active_skip_width_khz)) {
                    std::ostringstream msg;
                    msg << "scan skip active peak " << f_mhz << " MHz";
                    log.debug(msg.str());
                    continue;
                }
            }
            inflight.push_back(Trial{
                f_mhz,
                std::async(std::launch::async, [&cfg, f_mhz, &log, &offset_cache]() {
                    return runScanDetection(cfg, f_mhz, log, offset_cache);
                })
            });
        }
    };

    tryLaunchMore();

    while (!inflight.empty()) {
        if (g_shutdown) break;

        bool progressed = false;
        for (size_t i = 0; i < inflight.size(); ++i) {
            if (inflight[i].fut.wait_for(std::chrono::milliseconds(20)) != std::future_status::ready) continue;

            const double f_mhz = inflight[i].f_mhz;
            auto det = inflight[i].fut.get();
            inflight.erase(inflight.begin() + static_cast<long>(i));
            progressed = true;

            if (det) {
                ++detections;
                const std::string decoder_name = normalizeDecoderName(det->sonde_type);
                if (decoder_name != "rs41" && decoder_name != "dfm" && decoder_name != "m10" &&
                    decoder_name != "m20" && decoder_name != "imet" && decoder_name != "meisei") {
                    std::ostringstream unsupported;
                    unsupported << "scan detected unsupported sonde " << det->sonde_type
                                << " near " << f_mhz << " MHz";
                    log.warn(unsupported.str());
                } else {
                    const double start_freq = det->frequency_mhz;
                    std::lock_guard<std::mutex> lock(channels_mutex);
                    if (static_cast<int>(channels.size()) < cfg.scan_max_channels &&
                        !frequencyAlreadyActiveLocked(channels, start_freq, cfg.scan_active_skip_width_khz)) {
                        Config chcfg = cfg;
                        chcfg.scan_enabled = false;
                        chcfg.frequency_mhz = start_freq;
                        chcfg.decoder = decoder_name;

                        std::ostringstream hit;
                        hit << "scan detected " << det->sonde_type << " at " << f_mhz << " MHz";
                        if (std::fabs(det->offset_hz) > 0.1) {
                            hit << " offset=" << det->offset_hz << " Hz -> " << start_freq << " MHz";
                        }
                        if (!std::isnan(det->score)) hit << " score=" << det->score;
                        log.info(hit.str());

                        auto ch = startChannelWithReader(chcfg, cfg, log, uploader, offset_cache);
                        channels.push_back(std::move(ch));
                    }
                }
            }

            tryLaunchMore();
            break;
        }

        if (!progressed) std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    if (detections == 0) {
        log.info("scan finished: no radiosonde signatures detected on peaks");
    }
}

static void updateLiveSpectrumOnce(const Config& cfg, Logger& log) {
    const std::string powers = "powers";
    long long start_hz = freqHz(cfg.scan_min_mhz);
    long long stop_hz = freqHz(cfg.scan_max_mhz);
    double center_hz = (static_cast<double>(start_hz) + static_cast<double>(stop_hz)) / 2.0;
    int bins = static_cast<int>(std::floor((static_cast<double>(stop_hz - start_hz)) / cfg.scan_power_bin_hz)) + 1;
    if (bins < 8) bins = 8;

    const std::string log_path = "/tmp/wsrx_live_power_" + std::to_string(::getpid()) + ".csv";
    std::string ssrc = std::to_string(static_cast<long long>(std::llround(center_hz / 1000.0))) + "13";

    std::ostringstream cmd;
    cmd << "timeout " << (cfg.live_spectrum_dwell_sec + 10) << " "
        << powers << " " << shellQuote(cfg.ka9q_radio) << " "
        << "-f " << static_cast<long long>(std::llround(center_hz)) << " "
        << "-w " << cfg.scan_power_bin_hz << " "
        << "-b " << bins << " "
        << "-i " << cfg.live_spectrum_dwell_sec << " "
        << "-s " << ssrc << " "
        << "-c 2 > " << shellQuote(log_path) << " 2>/tmp/wsrx_live_power_" << ::getpid() << ".err";

    std::vector<SpectrumBin> spectrum;
    {
        std::lock_guard<std::mutex> powers_lock(g_powers_mutex);
        int rc = std::system(cmd.str().c_str());
        if (rc != 0) {
            if (cfg.verbose) {
                std::ostringstream msg;
                msg << "live spectrum powers failed rc=" << rc;
                log.debug(msg.str());
            }
            return;
        }
        spectrum = readKa9qPowerCsv(log_path, log);
        std::remove(log_path.c_str());
    }
    if (spectrum.empty()) return;
    double nf = medianPower(spectrum);
    double trigger = nf + cfg.scan_threshold_db;
    writeLiveSpectrumJson(g_base_dir, spectrum, nf, trigger, log);
}

static void spectrumWorkerThread(const Config& cfg, Logger& log) {
    auto last = std::chrono::steady_clock::now() - std::chrono::seconds(cfg.live_spectrum_interval_sec + 1);
    while (!g_shutdown) {
        auto now = std::chrono::steady_clock::now();
        auto since = std::chrono::duration_cast<std::chrono::seconds>(now - last).count();
        if (since >= cfg.live_spectrum_interval_sec) {
            last = now;
            updateLiveSpectrumOnce(cfg, log);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
    log.info("live spectrum thread stopped");
}

static void scanWorkerThread(const Config& cfg, Logger& log, std::vector<std::unique_ptr<Channel>>& channels,
                             std::mutex& channels_mutex, Uploader& uploader, OffsetCache& offset_cache) {
    auto last_scan = std::chrono::steady_clock::now() - std::chrono::seconds(cfg.scan_interval_sec + 1);
    while (!g_shutdown) {
        auto now = std::chrono::steady_clock::now();
        auto since_scan = std::chrono::duration_cast<std::chrono::seconds>(now - last_scan).count();
        size_t active_count = 0;
        {
            std::lock_guard<std::mutex> lock(channels_mutex);
            active_count = channels.size();
        }
        if (since_scan >= cfg.scan_interval_sec && static_cast<int>(active_count) < cfg.scan_max_channels) {
            last_scan = now;
            scanForChannelsThreaded(cfg, log, channels, channels_mutex, uploader, offset_cache);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
    log.info("scan thread stopped");
}

int main(int argc, char** argv) {
    try {
        Args args = Args::parse(argc, argv);
        if (args.has("help")) {
            Args::printHelp(argv[0]);
            return 0;
        }

        g_base_dir = executableDir(argv[0]);
        const std::string config_path = joinPath(g_base_dir, "config.ini");
        Config cfg = Config::load(args, config_path);
        applyRuntimeDefaults(cfg);
        validateRequiredDecoderFiles(cfg);
        Logger log("", cfg.verbose);
        Uploader uploader(cfg, log);
        OffsetCache offset_cache;
        offset_cache.setPath(joinPath(g_base_dir, "offset_cache.txt"));
        log.debug("offset cache path: " + joinPath(g_base_dir, "offset_cache.txt"));
        offset_cache.load(log);

        std::signal(SIGINT, handleSignal);
        std::signal(SIGTERM, handleSignal);

        log.info(std::string("wsrx ") + APP_VERSION + " started");
        log.info(std::string("programmed by Jean-Michael Grobel (DO2JMG)"));
        {
            std::ostringstream msg;
            msg << "auto scan enabled range=" << cfg.scan_min_mhz << "-" << cfg.scan_max_mhz
                << " MHz step=" << cfg.scan_step_khz << " kHz max_channels=" << cfg.scan_max_channels;
            log.info(msg.str());
        }

        uploader.maybeSendReceiverPosition();

        std::vector<std::unique_ptr<Channel>> channels;
        std::mutex channels_mutex;
        std::thread scan_thread;
        std::thread spectrum_thread;

        spectrum_thread = std::thread(spectrumWorkerThread, std::cref(cfg), std::ref(log));
        scan_thread = std::thread(scanWorkerThread, std::cref(cfg), std::ref(log), std::ref(channels),
                                  std::ref(channels_mutex), std::ref(uploader), std::ref(offset_cache));


        while (!g_shutdown) {
            uploader.maybeSendReceiverPosition();

            {
                std::lock_guard<std::mutex> lock(channels_mutex);
                for (auto it = channels.begin(); it != channels.end();) {
                    Channel& ch = **it;
                    bool remove = false;
                    bool got_frame = false;
                    std::chrono::steady_clock::time_point started;
                    std::chrono::steady_clock::time_point last_frame;
                    {
                        std::lock_guard<std::mutex> state_lock(ch.state_mutex);
                        got_frame = ch.got_frame;
                        started = ch.started;
                        last_frame = ch.last_frame;
                    }

                    if (ch.reader_exited.load()) {
                        log.warn("decoder reader stopped");
                        remove = true;
                    } else {
                        auto now = std::chrono::steady_clock::now();
                        auto age = std::chrono::duration_cast<std::chrono::seconds>(now - last_frame).count();
                        auto noframe_age = std::chrono::duration_cast<std::chrono::seconds>(now - started).count();
                        if (got_frame && age > cfg.channel_timeout_sec) {
                            std::ostringstream msg;
                            msg << "channel timeout " << ch.cfg.frequency_mhz << " MHz: no frame for " << age << "s";
                            log.warn(msg.str());
                            remove = true;
                        } else if (!got_frame && noframe_age > cfg.channel_timeout_sec) {
                            std::ostringstream msg;
                            msg << "channel timeout " << ch.cfg.frequency_mhz << " MHz: no valid frame after " << noframe_age << "s";
                            log.warn(msg.str());
                            remove = true;
                        }
                    }

                    if (remove) {
                        stopChannel(ch, log);
                        it = channels.erase(it);
                    } else {
                        ++it;
                    }
                }
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(250));
        }

        g_shutdown = true;
        if (scan_thread.joinable()) scan_thread.join();
        if (spectrum_thread.joinable()) spectrum_thread.join();

        {
            std::lock_guard<std::mutex> lock(channels_mutex);
            for (auto& ch : channels) stopChannel(*ch, log);
            channels.clear();
        }

        log.info("wsrx stopped");
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "ERROR: " << ex.what() << std::endl;
        return 1;
    }
}

