#include "args.h"
#include "config.h"
#include "decoderprocess.h"
#include "logger.h"
#include "rs92ephemeris.h"
#include "telemetryparser.h"
#include "uploader.h"
#include "udpout.h"

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
#include <iomanip>
#include <iostream>
#include <memory>
#include <numeric>
#include <optional>
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

static constexpr const char* APP_VERSION = "0.1.04";

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
    static const std::regex re(R"(Baseband\s+power\s*[:=]?\s*(-?(?:[0-9]+(?:[.,][0-9]+)?|inf))\s*dB)", std::regex_constants::icase);
    std::smatch m;
    if (std::regex_search(line, m, re)) {
        std::string v = m[1];
        if (v == "inf" || v == "-inf") return std::nullopt;
        std::replace(v.begin(), v.end(), ',', '.');
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

    std::string line = frame.raw_line;
    const auto brace = line.find('{');
    if (brace != std::string::npos) {
        std::ostringstream field;
        field << "\"wsrx_frequency\":" << std::fixed << std::setprecision(3) << frame.frequency_mhz << ",";
        line.insert(brace + 1, field.str());
    }

    std::filesystem::path file = dir / (safeLogFilename(frame.serial) + ".json");
    std::ofstream out(file, std::ios::app);
    if (!out) return;
    out << line << '\n';
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
    replaceAll(args, "{frequency_hz}", std::to_string(static_cast<long long>(cfg.frequency_mhz * 1e6 + 0.5)));
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
    if (!cfg.rs92_ephemeris_file.empty()) {
        cfg.rs92_ephemeris_file = resolveRelativeToBaseDir(cfg.rs92_ephemeris_file);
    }
    if (cfg.rs92_ephemeris_dir.empty()) cfg.rs92_ephemeris_dir = "ephemeris";
    cfg.rs92_ephemeris_dir = resolveRelativeToBaseDir(cfg.rs92_ephemeris_dir);
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
    if (d.find("s1") != std::string::npos) return "s1";
    if (d.find("rs92") != std::string::npos) return "rs92";
    if (d.find("lms6") != std::string::npos) return "lms6";
    if (d.find("rd94") != std::string::npos || d.find("rd41") != std::string::npos ||
        d.find("dropsonde") != std::string::npos) return "dropsonde";
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
    if (d == "rs92") return "RS92";
    if (d == "lms6") return "LMS6";
    if (d == "s1") return "S1";
    if (d == "dropsonde") return "RD94RD41";
    return decoder;
}

static void validateRequiredDecoderFiles(const Config& cfg) {
    const std::vector<std::string> required = {
        "rs41mod",
        "rs92mod",
        "lms6Xmod",
        "dfm09mod",
        "m10m20mod",
        "imet4iq",
        "meisei100mod",
        "c50iq",
        "windsondmod",
        "rd94rd41drop",
        "dft_detect"
    };

    for (const auto& file : required) {
        const std::string path = joinPath(cfg.decoder_dir, file);
        if (!fileExists(path)) {
            throw std::runtime_error("Required decoder file missing: " + path + " (decoder_dir=" + cfg.decoder_dir + ")");
        }
    }

    if (!cfg.rs92_ephemeris_file.empty() && !fileExists(cfg.rs92_ephemeris_file)) {
        throw std::runtime_error("decoder.rs92_ephemeris_file is set but not found: " + cfg.rs92_ephemeris_file +
                                  " (config.ini). Remove it to let wsrx auto-download RS92 ephemeris data instead.");
    }
}

static std::string decoderCommandPath(const Config& cfg, const std::string& decoder) {
    std::string d = normalizeDecoderName(decoder);
    if (d == "rs41") return joinPath(cfg.decoder_dir, "rs41mod");
    if (d == "rs92") return joinPath(cfg.decoder_dir, "rs92mod");
    if (d == "lms6") return joinPath(cfg.decoder_dir, "lms6Xmod");
    if (d == "dfm") return joinPath(cfg.decoder_dir, "dfm09mod");
    if (d == "m10") return joinPath(cfg.decoder_dir, "m10m20mod");
    if (d == "m20") return joinPath(cfg.decoder_dir, "m10m20mod");
    if (d == "imet") return joinPath(cfg.decoder_dir, "imet4iq");
    if (d == "meisei") return joinPath(cfg.decoder_dir, "meisei100mod");
    if (d == "c34c50") return joinPath(cfg.decoder_dir, "c50iq");
    if (d == "s1") return joinPath(cfg.decoder_dir, "windsondmod");
    if (d == "dropsonde") return joinPath(cfg.decoder_dir, "rd94rd41drop");
    throw std::runtime_error("Unsupported decoder: " + decoder);
}

static std::string decoderArgsFor(const Config& cfg, const std::string& decoder) {
    std::string d = normalizeDecoderName(decoder);
    if (d == "rs41") return expandDecoderArgs("--ecc2 --crc -vx --ptu --json --IQ {iq_offset} - {sample_rate} 16", cfg);
    if (d == "rs92") {
        return expandDecoderArgs(
            "--ecc2 --crc -vx --ptu --json -e " + shellQuote(cfg.rs92_ephemeris_file) +
                " --IQ {iq_offset} - {sample_rate} 16",
            cfg);
    }
    if (d == "lms6") return expandDecoderArgs("--json --vit2 --lpIQ --IQ {iq_offset} - {sample_rate} 16", cfg);
    if (d == "dfm") return expandDecoderArgs("-i -vv --ecc --json --dist --ptu --IQ {iq_offset} - {sample_rate} 16", cfg);
    if (d == "m10") return expandDecoderArgs("-vv --ptu --json --IQ {iq_offset} - {sample_rate} 16", cfg);
    if (d == "m20") return expandDecoderArgs(" -vv --ptu --json --IQ {iq_offset} - {sample_rate} 16", cfg);
    if (d == "imet") return expandDecoderArgs("--json --iq {iq_offset} - {sample_rate} 16", cfg);
    if (d == "meisei") return expandDecoderArgs("--json --dc --IQ {iq_offset} - {sample_rate} 16", cfg);
    if (d == "c34c50") return expandDecoderArgs("--json --ptu --xor-auto --lpIQ --dc --iq {iq_offset} - {sample_rate} 16", cfg);
    if (d == "s1") return expandDecoderArgs("--iq --samplerate {sample_rate} --json --frequency {frequency_hz}", cfg);
    if (d == "dropsonde") return expandDecoderArgs("--json --iq0 --IQ {iq_offset} - {sample_rate} 16", cfg);
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

static std::optional<ScanDetection> parseDftDetectOutput(const std::string& output, double frequency_mhz) {
    if (output.empty()) return std::nullopt;

    std::string first_line;
    std::istringstream iss(output);
    std::getline(iss, first_line);
    while (!first_line.empty() && (first_line.back() == '\n' || first_line.back() == '\r')) first_line.pop_back();
    if (first_line.empty()) return std::nullopt;

    ScanDetection det;
    det.frequency_mhz = frequency_mhz;

    const std::vector<std::string> known = {"RS41", "RS92", "DFM", "M10", "M20", "IMET", "LMS6", "MEISEI", "MRZ", "MTS01", "S1", "RD94RD41"};
    for (const auto& k : known) {
        if (first_line.find(k) != std::string::npos) {
            det.sonde_type = k;
            break;
        }
    }
    if (det.sonde_type.empty()) return std::nullopt;

    std::smatch m;
    static const std::regex score_re(R"(:\s*([+-]?[0-9]+(?:\.[0-9]+)?))");
    if (std::regex_search(first_line, m, score_re)) {
        det.score = std::stod(m[1].str());
    }

    static const std::regex offset_re(R"(,\s*([+-]?[0-9]+(?:\.[0-9]+)?)\s*Hz)", std::regex_constants::icase);
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

static std::vector<SpectrumBin> mergeDuplicateFrequencyBins(std::vector<SpectrumBin> bins, double tolerance_hz) {
    if (bins.size() < 2 || tolerance_hz <= 0.0) return bins;

    std::sort(bins.begin(), bins.end(), [](const SpectrumBin& a, const SpectrumBin& b) {
        return a.frequency_hz < b.frequency_hz;
    });

    std::vector<SpectrumBin> merged;
    merged.reserve(bins.size());

    std::vector<double> group_linear;
    double group_freq_sum = 0.0;
    auto flush = [&]() {
        if (group_linear.empty()) return;
        double power_sum = 0.0;
        for (double p_db : group_linear) power_sum += std::pow(10.0, p_db / 10.0);
        const double mean_power_db = 10.0 * std::log10(power_sum / static_cast<double>(group_linear.size()));
        merged.push_back({group_freq_sum / static_cast<double>(group_linear.size()), mean_power_db});
        group_linear.clear();
        group_freq_sum = 0.0;
    };

    double group_anchor_hz = 0.0;
    for (const auto& b : bins) {
        if (!group_linear.empty() && std::fabs(group_anchor_hz - b.frequency_hz) > tolerance_hz) flush();
        if (group_linear.empty()) group_anchor_hz = b.frequency_hz;
        group_freq_sum += b.frequency_hz;
        group_linear.push_back(b.power_db);
    }
    flush();

    return merged;
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

static constexpr double kScanSmoothMaxWidthHz = 2000.0;

static void smoothNarrowSpikes(std::vector<SpectrumBin>& spectrum, double bin_hz, double max_width_hz) {
    if (spectrum.size() < 3 || bin_hz <= 0.0 || max_width_hz <= 0.0) return;

    int half = static_cast<int>(std::round((max_width_hz / bin_hz) / 2.0));
    if (half < 1) return;

    std::vector<double> smoothed;
    smoothed.reserve(spectrum.size());

    std::vector<double> window;
    for (size_t i = 0; i < spectrum.size(); ++i) {
        size_t lo = (i >= static_cast<size_t>(half)) ? i - static_cast<size_t>(half) : 0;
        size_t hi = std::min(spectrum.size() - 1, i + static_cast<size_t>(half));

        window.clear();
        window.reserve(hi - lo + 1);
        for (size_t j = lo; j <= hi; ++j) window.push_back(spectrum[j].power_db);

        std::nth_element(window.begin(), window.begin() + window.size() / 2, window.end());
        smoothed.push_back(window[window.size() / 2]);
    }

    for (size_t i = 0; i < spectrum.size(); ++i) spectrum[i].power_db = smoothed[i];
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

static void writeVersionJson(const std::string& base_dir, Logger& log) {
    try {
        std::filesystem::path data_dir = std::filesystem::path(base_dir) / "data";
        std::error_code ec;
        std::filesystem::create_directories(data_dir, ec);
        const std::filesystem::path tmp_path = data_dir / "version.json.tmp";
        const std::filesystem::path out_path = data_dir / "version.json";

        std::ofstream out(tmp_path, std::ios::trunc);
        if (!out) {
            log.warn("could not write version file: " + tmp_path.string());
            return;
        }

        out << "{\n";
        out << "  \"software\": \"wsrx\",\n";
        out << "  \"version\": \"" << APP_VERSION << "\"\n";
        out << "}\n";
        out.close();
        std::filesystem::rename(tmp_path, out_path, ec);
        if (ec) {
            std::filesystem::remove(out_path, ec);
            std::filesystem::rename(tmp_path, out_path, ec);
        }
        if (ec) log.warn("could not publish version file: " + out_path.string());
    } catch (const std::exception& e) {
        log.warn(std::string("could not write version file: ") + e.what());
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

struct ScanCandidate {
    double frequency_hz = 0.0;
    const RadioBackend* radio = nullptr;
};

struct BackendScanResult {
    std::vector<SpectrumBin> spectrum;
    double noise_floor = NAN;
    double trigger = NAN;
    std::vector<size_t> peak_idx;
    bool used_fallback = false;
};

static BackendScanResult runKa9qPowerScanForRadio(const Config& cfg, const RadioBackend& radio, Logger& log, bool allow_fallback_candidates) {
    BackendScanResult out;
    const std::string powers = "powers";
    long long start_hz = freqHz(radio.scan_min_mhz);
    long long stop_hz = freqHz(radio.scan_max_mhz);
    double center_hz = (static_cast<double>(start_hz) + static_cast<double>(stop_hz)) / 2.0;
    int bins = static_cast<int>(std::floor((static_cast<double>(stop_hz - start_hz)) / cfg.scan_power_bin_hz)) + 1;
    if (bins < 8) bins = 8;

    const std::string log_path = "/tmp/wsrx_power_" + std::to_string(::getpid()) + "_" + radio.name + ".csv";
    std::string ssrc = std::to_string(static_cast<long long>(std::llround(center_hz / 1000.0))) + "03";

    std::ostringstream cmd;
    cmd << "timeout " << (cfg.scan_spectrum_dwell_sec + 10) << " "
        << powers << " " << shellQuote(radio.ka9q_radio) << " "
        << "-f " << static_cast<long long>(std::llround(center_hz)) << " "
        << "-w " << cfg.scan_power_bin_hz << " "
        << "-b " << bins << " "
        << "-i " << cfg.scan_spectrum_dwell_sec << " "
        << "-s " << ssrc << " "
        << "-c 2 > " << shellQuote(log_path) << " 2>/tmp/wsrx_power_" << ::getpid() << "_" << radio.name << ".err";

    if (cfg.verbose || cfg.decoder_debug) log.debug("scan power command [" + radio.name + "]: " + cmd.str());

    {
        std::lock_guard<std::mutex> powers_lock(g_powers_mutex);
        int rc = std::system(cmd.str().c_str());
        if (rc != 0) {
            std::ostringstream msg;
            msg << "scan powers failed [" << radio.name << "] rc=" << rc << " - is the KA9Q 'powers' binary installed/in PATH, and is " << radio.ka9q_radio << " reachable?";
            log.warn(msg.str());
            return out;
        }
        out.spectrum = readKa9qPowerCsv(log_path, log);
        std::remove(log_path.c_str());
    }
    if (out.spectrum.empty()) {
        log.warn("scan powers produced no spectrum data [" + radio.name + "]");
        return out;
    }

    out.spectrum = mergeDuplicateFrequencyBins(out.spectrum, static_cast<double>(cfg.scan_power_bin_hz) / 2.0);
    smoothNarrowSpikes(out.spectrum, cfg.scan_power_bin_hz, kScanSmoothMaxWidthHz);

    std::vector<SpectrumBin>& spectrum = out.spectrum;
    double nf = medianPower(spectrum);
    double trigger = nf + cfg.scan_threshold_db;
    out.noise_floor = nf;
    out.trigger = trigger;
    std::ostringstream nfmsg;
    nfmsg << "scan [" << radio.name << "] noise_floor=" << nf << " dB threshold=" << cfg.scan_threshold_db << " dB trigger=" << trigger << " dB ";
    log.info(nfmsg.str());

    auto addPeakIndex = [&](std::vector<size_t>& list, size_t idx) {
        const double min_dist_hz = static_cast<double>(cfg.scan_min_distance_hz);
        for (size_t& old_idx : list) {
            if (std::fabs(spectrum[old_idx].frequency_hz - spectrum[idx].frequency_hz) <= min_dist_hz) {
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
            msg << "scan [" << radio.name << "] peak ignored " << (spectrum[idx].frequency_hz / 1e6)
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
            out.peak_idx = peak_idx;
            out.used_fallback = used_fallback;
            return out;
        }
        used_fallback = true;
        std::ostringstream fbmsg;
        fbmsg << "scan [" << radio.name << "] spectrum: no peaks above threshold, checking up to "
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

    for (size_t idx : peak_idx) {
        std::ostringstream msg;
        msg << (used_fallback ? "scan candidate " : "scan peak ") << "[" << radio.name << "] "
            << (spectrum[idx].frequency_hz / 1e6) << " MHz"
            << " level=" << spectrum[idx].power_db << " dB";
        if (cfg.scan_min_peak_width_hz > 0) msg << " width=" << estimatePeakWidthHz(spectrum, idx, trigger) << " Hz";
        if (used_fallback) msg << " snr=" << (spectrum[idx].power_db - nf) << " dB";
        log.info(msg.str());
    }

    out.peak_idx = peak_idx;
    out.used_fallback = used_fallback;
    return out;
}

static std::vector<ScanCandidate> runKa9qPowerScan(const Config& cfg, Logger& log, bool allow_fallback_candidates) {
    std::vector<SpectrumBin> merged_spectrum;
    std::vector<size_t> merged_peak_idx;
    double merged_nf = NAN;
    double merged_trigger = NAN;
    bool any_fallback = false;
    std::vector<ScanCandidate> candidates;

    const double q = static_cast<double>(cfg.scan_quantization_hz);
    std::vector<double> quantized_hz;
    auto appendUnique = [&](double hz, const RadioBackend* radio) {
        if (!std::isfinite(hz) || hz <= 0.0) return;
        if (isBlacklistedFrequencyHz(cfg, hz)) return;
        const double qhz = std::round(hz / q) * q;
        for (double old_qhz : quantized_hz) {
            if (std::fabs(old_qhz - qhz) < q / 2.0) return;
        }
        quantized_hz.push_back(qhz);
        candidates.push_back({hz, radio});
    };

    for (const auto& radio : cfg.radios) {
        BackendScanResult res = runKa9qPowerScanForRadio(cfg, radio, log, allow_fallback_candidates);
        if (res.spectrum.empty()) continue;

        const size_t offset = merged_spectrum.size();
        merged_spectrum.insert(merged_spectrum.end(), res.spectrum.begin(), res.spectrum.end());
        for (size_t idx : res.peak_idx) merged_peak_idx.push_back(idx + offset);
        if (std::isfinite(res.noise_floor) && (!std::isfinite(merged_nf) || res.noise_floor < merged_nf)) merged_nf = res.noise_floor;
        if (std::isfinite(res.trigger) && (!std::isfinite(merged_trigger) || res.trigger < merged_trigger)) merged_trigger = res.trigger;
        any_fallback = any_fallback || res.used_fallback;

        for (size_t idx : res.peak_idx) appendUnique(res.spectrum[idx].frequency_hz, &radio);
    }

    if (!merged_spectrum.empty()) {
        writeScanSpectrumJson(g_base_dir, merged_spectrum, merged_nf, merged_trigger, merged_peak_idx, any_fallback, log);
        writeScanPeaksJson(g_base_dir, merged_spectrum, merged_nf, merged_trigger, merged_peak_idx, any_fallback, log);
    }

    for (double mhz : cfg.scan_whitelist_mhz) {
        if (!std::isfinite(mhz) || mhz <= 0.0) continue;
        if (isBlacklistedFrequencyMhz(cfg, mhz)) continue;
        const RadioBackend* owner = nullptr;
        for (const auto& radio : cfg.radios) {
            if (mhz >= radio.scan_min_mhz && mhz <= radio.scan_max_mhz) { owner = &radio; break; }
        }
        if (!owner) {
            if (cfg.verbose || cfg.decoder_debug) {
                log.debug("scan whitelist_mhz " + std::to_string(mhz) + " MHz falls outside every configured radio range, skipped");
            }
            continue;
        }
        if (cfg.verbose || cfg.decoder_debug) {
            log.debug("scan whitelist_mhz: forcing candidate " + std::to_string(mhz) + " MHz on [" + owner->name + "]");
        }
        appendUnique(mhz * 1e6, owner);
    }

    if (candidates.empty()) log.info("scan spectrum: no usable candidates");
    return candidates;
}

static std::string buildScanTypesList(const Config& cfg, Logger& log) {
    std::vector<std::string> types;
    if (cfg.decoder_type_rs41) types.push_back("RS41");
    if (cfg.decoder_type_rs92) types.push_back("RS92");
    if (cfg.decoder_type_lms6) types.push_back("LMS6");
    if (cfg.decoder_type_dfm9) types.push_back("DFM9");
    if (cfg.decoder_type_m10) types.push_back("M10");
    if (cfg.decoder_type_imet4) types.push_back("IMET4");
    if (cfg.decoder_type_meisei) types.push_back("MEISEI");
    if (cfg.decoder_type_c34c50) types.push_back("C34C50");
    if (cfg.decoder_type_dropsonde) types.push_back("RD94RD41");

    if (types.empty()) {
        log.warn("config.ini [decoder]: all sonde types disabled, falling back to scanning all types");
        types = {"RS41", "DFM9", "M10", "IMET4", "MEISEI", "C34C50", "RD94RD41", "RS92", "LMS6"};
    }

    std::string out;
    for (size_t i = 0; i < types.size(); ++i) {
        if (i) out += ',';
        out += types[i];
    }
    return out;
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
        << " --types " << buildScanTypesList(cfg, log)
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

static std::optional<ScanDetection> runSingleScanDetectionRefined(const Config& cfg, double frequency_mhz, Logger& log) {
    auto det = runSingleScanDetection(cfg, frequency_mhz, log);
    if (!det) return std::nullopt;

    double current_mhz = frequency_mhz;
    for (int iter = 0; iter < 2 && !g_shutdown; ++iter) {
        if (std::fabs(det->frequency_mhz - current_mhz) < 0.0005) break; // < 500 Hz: converged
        current_mhz = det->frequency_mhz;
        auto refined = runSingleScanDetection(cfg, current_mhz, log);
        if (!refined) break;
        if (cfg.verbose) {
            std::ostringstream msg;
            msg << "scan refine " << refined->sonde_type << " re-centered at " << current_mhz
                << " MHz -> " << refined->frequency_mhz << " MHz score=" << refined->score;
            log.debug(msg.str());
        }
        det = refined;
    }
    return det;
}

static void addUniqueOffset(std::vector<double>& offsets, double value) {
    for (double old : offsets) {
        if (std::fabs(old - value) < 0.1) return;
    }
    offsets.push_back(value);
}

static std::vector<double> buildOffsetTrialsHz(const Config& cfg) {
    std::vector<double> offsets;

    addUniqueOffset(offsets, 0.0);

    if (std::fabs(cfg.scan_decoder_offset_hz) > 0.1) {
        addUniqueOffset(offsets, cfg.scan_decoder_offset_hz);
    }

    return offsets;
}

static std::optional<ScanDetection> runScanDetection(const Config& cfg, double peak_mhz, Logger& log) {
    auto offsets = buildOffsetTrialsHz(cfg);
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
        log.debug(msg.str());
    }

    for (double off_hz : offsets) {
        if (g_shutdown) break;
        const double trial_mhz = peak_mhz + off_hz / 1000000.0;
        auto det = runSingleScanDetectionRefined(cfg, trial_mhz, log);
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
            take = std::fabs(candidate_offset_hz) < std::fabs(current_offset_hz);
        }
        if (take) {
            best = det;
            best_score = score;
            best->offset_hz = candidate_offset_hz;
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

    if (normalizeDecoderName(cfg.decoder) == "rs92") {
        const std::string ephemeris_path = Rs92Ephemeris::ensure(cfg, log);
        if (ephemeris_path.empty()) {
            throw std::runtime_error(
                "Could not obtain an RS92 ephemeris file (download failed and no cached file available)");
        }
        cfg.rs92_ephemeris_file = ephemeris_path;
    }

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

static void channelReaderThread(Channel* ch, const Config& base_cfg, Logger& log, Uploader& uploader, UdpSender& udp_sender) {
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

                appendDecoderJsonLog(*frame);
                uploader.sendTelemetry(*frame);
                udp_sender.sendTelemetry(*frame);
            }
        }

        if (!ch->decoder.isRunning()) break;
        if (!read_any) std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }

    ch->reader_exited.store(true);
}

static std::unique_ptr<Channel> startChannelWithReader(Config cfg, const Config& base_cfg, Logger& log, Uploader& uploader, UdpSender& udp_sender) {
    auto ch = startChannelProcess(cfg, log);
    Channel* ptr = ch.get();
    ptr->reader_thread = std::thread(channelReaderThread, ptr, std::cref(base_cfg), std::ref(log), std::ref(uploader), std::ref(udp_sender));
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
                                    std::mutex& channels_mutex, Uploader& uploader, UdpSender& udp_sender) {
    size_t active_count = 0;
    {
        std::lock_guard<std::mutex> lock(channels_mutex);
        active_count = channels.size();
    }
    if (static_cast<int>(active_count) >= cfg.scan_max_channels) return;

    const bool allow_fallback_candidates = (active_count == 0) || cfg.scan_fallback_when_active;
    std::vector<ScanCandidate> peaks = runKa9qPowerScan(cfg, log, allow_fallback_candidates);
    if (peaks.empty()) return;

    struct Candidate {
        double f_mhz;
        const RadioBackend* radio;
    };
    std::vector<Candidate> candidates;
    candidates.reserve(peaks.size());
    for (const auto& peak : peaks) {
        const double f_mhz = peak.frequency_hz / 1e6;
        if (isBlacklistedFrequencyMhz(cfg, f_mhz)) {
            std::ostringstream msg;
            msg << "scan skip never-scan peak " << f_mhz << " MHz";
            log.debug(msg.str());
            continue;
        }
        candidates.push_back({f_mhz, peak.radio});
    }
    if (candidates.empty()) return;

    const int max_parallel = std::max(1, cfg.scan_parallel_detections);

    struct Trial {
        double f_mhz;
        const RadioBackend* radio;
        std::future<std::optional<ScanDetection>> fut;
    };
    std::vector<Trial> inflight;
    size_t next_idx = 0;
    int detections = 0;

    auto tryLaunchMore = [&]() {
        while (next_idx < candidates.size() && inflight.size() < static_cast<size_t>(max_parallel)) {
            const double f_mhz = candidates[next_idx].f_mhz;
            const RadioBackend* radio = candidates[next_idx].radio;
            ++next_idx;
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

            Config scan_cfg = cfg;
            if (radio != nullptr) {
                scan_cfg.ka9q_radio = radio->ka9q_radio;
                scan_cfg.ka9q_pcm = radio->ka9q_pcm;
            }
            inflight.push_back(Trial{
                f_mhz,
                radio,
                std::async(std::launch::async, [scan_cfg, f_mhz, &log]() {
                    return runScanDetection(scan_cfg, f_mhz, log);
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
            const RadioBackend* radio = inflight[i].radio;
            auto det = inflight[i].fut.get();
            inflight.erase(inflight.begin() + static_cast<long>(i));
            progressed = true;

            if (det) {
                ++detections;
                const std::string decoder_name = normalizeDecoderName(det->sonde_type);
                if (decoder_name != "rs41" && decoder_name != "rs92" && decoder_name != "lms6" && decoder_name != "dfm" && decoder_name != "m10" &&
                    decoder_name != "m20" && decoder_name != "imet" && decoder_name != "meisei" &&
                    decoder_name != "s1" && decoder_name != "dropsonde") {
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
                        if (radio != nullptr) {
                            chcfg.ka9q_radio = radio->ka9q_radio;
                            chcfg.ka9q_pcm = radio->ka9q_pcm;
                        }

                        if (decoder_name == "s1") {
                            chcfg.ka9q_low_hz = -30000;
                            chcfg.ka9q_high_hz = 30000;
                        }

                        std::ostringstream hit;
                        hit << "scan detected " << det->sonde_type << " at " << f_mhz << " MHz";
                        if (radio != nullptr) hit << " via [" << radio->name << "]";
                        if (std::fabs(det->offset_hz) > 0.1) {
                            hit << " offset=" << det->offset_hz << " Hz -> " << start_freq << " MHz";
                        }
                        if (!std::isnan(det->score)) hit << " score=" << det->score;
                        log.info(hit.str());

                        auto ch = startChannelWithReader(chcfg, cfg, log, uploader, udp_sender);
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
    std::vector<SpectrumBin> merged_spectrum;

    // cfg.radios is sorted by scan_min_mhz at load time, so appending each
    // backend's spectrum in that order keeps the merged view frequency-ordered.
    for (const auto& radio : cfg.radios) {
        long long start_hz = freqHz(radio.scan_min_mhz);
        long long stop_hz = freqHz(radio.scan_max_mhz);
        double center_hz = (static_cast<double>(start_hz) + static_cast<double>(stop_hz)) / 2.0;
        int bins = static_cast<int>(std::floor((static_cast<double>(stop_hz - start_hz)) / cfg.scan_power_bin_hz)) + 1;
        if (bins < 8) bins = 8;

        const std::string log_path = "/tmp/wsrx_live_power_" + std::to_string(::getpid()) + "_" + radio.name + ".csv";
        std::string ssrc = std::to_string(static_cast<long long>(std::llround(center_hz / 1000.0))) + "13";

        std::ostringstream cmd;
        cmd << "timeout " << (cfg.live_spectrum_dwell_sec + 10) << " "
            << powers << " " << shellQuote(radio.ka9q_radio) << " "
            << "-f " << static_cast<long long>(std::llround(center_hz)) << " "
            << "-w " << cfg.scan_power_bin_hz << " "
            << "-b " << bins << " "
            << "-i " << cfg.live_spectrum_dwell_sec << " "
            << "-s " << ssrc << " "
            << "-c 2 > " << shellQuote(log_path) << " 2>/tmp/wsrx_live_power_" << ::getpid() << "_" << radio.name << ".err";

        std::vector<SpectrumBin> spectrum;
        {
            std::lock_guard<std::mutex> powers_lock(g_powers_mutex);
            int rc = std::system(cmd.str().c_str());
            if (rc != 0) {
                if (cfg.verbose) {
                    std::ostringstream msg;
                    msg << "live spectrum powers failed [" << radio.name << "] rc=" << rc;
                    log.debug(msg.str());
                }
                continue;
            }
            spectrum = readKa9qPowerCsv(log_path, log);
            std::remove(log_path.c_str());
        }
        merged_spectrum.insert(merged_spectrum.end(), spectrum.begin(), spectrum.end());
    }

    if (merged_spectrum.empty()) return;
    double nf = medianPower(merged_spectrum);
    double trigger = nf + cfg.scan_threshold_db;
    writeLiveSpectrumJson(g_base_dir, merged_spectrum, nf, trigger, log);
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
                             std::mutex& channels_mutex, Uploader& uploader, UdpSender& udp_sender) {
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
            scanForChannelsThreaded(cfg, log, channels, channels_mutex, uploader, udp_sender);
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
        UdpSender udp_sender(cfg, log);

        std::signal(SIGINT, handleSignal);
        std::signal(SIGTERM, handleSignal);

        log.info(std::string("wsrx ") + APP_VERSION + " started");
        log.info(std::string("programmed by Jean-Michael Grobel (DO2JMG)"));
        writeVersionJson(g_base_dir, log);
        {
            std::ostringstream msg;
            msg << "auto scan enabled range=" << cfg.scan_min_mhz << "-" << cfg.scan_max_mhz
                << " MHz step=" << cfg.scan_step_khz << " kHz max_channels=" << cfg.scan_max_channels
                << " radios=" << cfg.radios.size();
            log.info(msg.str());
        }
        for (const auto& radio : cfg.radios) {
            std::ostringstream msg;
            msg << "radio [" << radio.name << "] " << radio.scan_min_mhz << "-" << radio.scan_max_mhz
                << " MHz via " << radio.ka9q_radio << " / " << radio.ka9q_pcm;
            log.info(msg.str());
        }

        if (cfg.decoder_type_rs92 && cfg.rs92_ephemeris_file.empty()) {
            log.info("RS92 ephemeris: running startup test download...");
            const std::string test_path = Rs92Ephemeris::ensure(cfg, log);
            if (test_path.empty()) {
                log.warn(
                    "RS92 ephemeris: startup test download failed and no cached file is available. "
                    "RS92 decoding will not work until this succeeds (checked again on next RS92 detection). "
                    "Check internet connectivity / firewall for gssc.esa.int and igs.bkg.bund.de.");
            } else {
                log.info("RS92 ephemeris: startup test OK, using " + test_path);
            }
        }

        uploader.maybeSendReceiverPosition();

        std::vector<std::unique_ptr<Channel>> channels;
        std::mutex channels_mutex;
        std::thread scan_thread;
        std::thread spectrum_thread;

        spectrum_thread = std::thread(spectrumWorkerThread, std::cref(cfg), std::ref(log));
        scan_thread = std::thread(scanWorkerThread, std::cref(cfg), std::ref(log), std::ref(channels),
                                  std::ref(channels_mutex), std::ref(uploader), std::ref(udp_sender));


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
