#include "config.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace {

std::string trim(const std::string& s) {
    size_t a = 0;
    while (a < s.size() && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    size_t b = s.size();
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return s.substr(a, b - a);
}

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

bool parseBool(const std::string& value, bool fallback) {
    std::string v = lower(trim(value));
    if (v == "1" || v == "true" || v == "yes" || v == "on") return true;
    if (v == "0" || v == "false" || v == "no" || v == "off") return false;
    return fallback;
}

std::unordered_map<std::string, std::string> readIniFile(const std::string& path) {
    std::unordered_map<std::string, std::string> out;
    std::ifstream in(path);
    if (!in) {
        throw std::runtime_error("Missing config.ini next to wsrx: " + path);
    }

    std::string section;
    std::string line;
    while (std::getline(in, line)) {
        size_t comment = line.find_first_of("#;");
        if (comment != std::string::npos) line = line.substr(0, comment);
        line = trim(line);
        if (line.empty()) continue;

        if (line.front() == '[' && line.back() == ']') {
            section = lower(trim(line.substr(1, line.size() - 2)));
            continue;
        }

        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = lower(trim(line.substr(0, eq)));
        std::string value = trim(line.substr(eq + 1));
        if (!section.empty()) key = section + "." + key;
        out[key] = value;
    }

    return out;
}

std::string iniGet(const std::unordered_map<std::string, std::string>& ini, const std::string& key, const std::string& fallback) {
    auto it = ini.find(lower(key));
    return it == ini.end() ? fallback : it->second;
}

double iniDouble(const std::unordered_map<std::string, std::string>& ini, const std::string& key, double fallback) {
    auto it = ini.find(lower(key));
    return it == ini.end() ? fallback : std::stod(it->second);
}

int iniInt(const std::unordered_map<std::string, std::string>& ini, const std::string& key, int fallback) {
    auto it = ini.find(lower(key));
    return it == ini.end() ? fallback : std::stoi(it->second);
}

bool iniBool(const std::unordered_map<std::string, std::string>& ini, const std::string& key, bool fallback) {
    auto it = ini.find(lower(key));
    return it == ini.end() ? fallback : parseBool(it->second, fallback);
}

std::vector<double> parseFrequencyListMhz(const std::string& value) {
    std::vector<double> out;
    std::string cur;
    auto flush = [&]() {
        std::string v = trim(cur);
        cur.clear();
        if (v.empty()) return;
        try {
            double mhz = std::stod(v);
            if (std::isfinite(mhz) && mhz > 0.0) out.push_back(mhz);
        } catch (...) {
        }
    };
    for (char c : value) {
        if (c == ',' || std::isspace(static_cast<unsigned char>(c))) flush();
        else cur.push_back(c);
    }
    flush();
    return out;
}

std::vector<double> iniFrequencyListMhz(const std::unordered_map<std::string, std::string>& ini, const std::string& key) {
    auto it = ini.find(lower(key));
    return it == ini.end() ? std::vector<double>{} : parseFrequencyListMhz(it->second);
}

std::vector<double> loadFrequencyListFile(const std::string& path) {
    std::vector<double> out;
    if (path.empty()) return out;

    std::ifstream in(path);
    if (!in) {
        throw std::runtime_error("Could not open frequency list file: " + path);
    }

    std::string line;
    while (std::getline(in, line)) {
        size_t comment = line.find('#');
        if (comment != std::string::npos) line = line.substr(0, comment);
        line = trim(line);
        if (line.empty()) continue;
        for (double mhz : parseFrequencyListMhz(line)) out.push_back(mhz);
    }
    return out;
}

std::vector<double> mergeUniqueFrequenciesMhz(std::vector<double> base, const std::vector<double>& extra) {
    for (double mhz : extra) {
        bool present = false;
        for (double existing : base) {
            if (std::fabs(existing - mhz) < 1e-6) { present = true; break; }
        }
        if (!present) base.push_back(mhz);
    }
    return base;
}

}

Config Config::load(const Args& args, const std::string& config_file) {
    if (args.has("config")) {
        throw std::runtime_error("-config is not supported. config.ini must be next to wsrx.");
    }
    if (args.has("telemetry-url") || args.has("position-url")) {
        throw std::runtime_error("Upload URLs are fixed in the code and cannot be changed by parameter or config.ini.");
    }

    Config cfg;

    auto ini = readIniFile(config_file);
    cfg.config_file = config_file;

    cfg.callsign = iniGet(ini, "station.callsign", cfg.callsign);
    cfg.station_lat = iniDouble(ini, "station.lat", cfg.station_lat);
    cfg.station_lon = iniDouble(ini, "station.lon", cfg.station_lon);
    cfg.station_alt = iniDouble(ini, "station.alt", cfg.station_alt);

    cfg.ka9q_radio = iniGet(ini, "ka9q.radio_name", cfg.ka9q_radio);
    cfg.ka9q_pcm = iniGet(ini, "ka9q.pcm_name", cfg.ka9q_pcm);

    cfg.decoder_dir = iniGet(ini, "decoder.decoder_dir", cfg.decoder_dir);
    cfg.wav_file = iniGet(ini, "decoder.wav_file", cfg.wav_file);
    cfg.sample_rate = iniInt(ini, "decoder.sample_rate", cfg.sample_rate);
    cfg.ka9q_low_hz = iniInt(ini, "decoder.ka9q_low_hz", cfg.ka9q_low_hz);
    cfg.ka9q_high_hz = iniInt(ini, "decoder.ka9q_high_hz", cfg.ka9q_high_hz);
    cfg.iq_offset_hz = iniDouble(ini, "decoder.iq_offset_hz", cfg.iq_offset_hz);

    cfg.decoder_type_rs41 = iniBool(ini, "decoder.rs41", cfg.decoder_type_rs41);
    cfg.decoder_type_dfm9 = iniBool(ini, "decoder.dfm9", cfg.decoder_type_dfm9);
    cfg.decoder_type_m10 = iniBool(ini, "decoder.m10", cfg.decoder_type_m10);
    cfg.decoder_type_imet4 = iniBool(ini, "decoder.imet4", cfg.decoder_type_imet4);
    cfg.decoder_type_meisei = iniBool(ini, "decoder.meisei", cfg.decoder_type_meisei);
    cfg.decoder_type_c34c50 = iniBool(ini, "decoder.c34c50", cfg.decoder_type_c34c50);
    cfg.decoder_type_s1 = iniBool(ini, "decoder.s1", cfg.decoder_type_s1);

    cfg.scan_enabled = true;
    cfg.scan_min_mhz = iniDouble(ini, "scan.min_mhz", cfg.scan_min_mhz);
    cfg.scan_max_mhz = iniDouble(ini, "scan.max_mhz", cfg.scan_max_mhz);
    cfg.scan_step_khz = iniInt(ini, "scan.step_khz", cfg.scan_step_khz);
    cfg.scan_threshold_db = iniDouble(ini, "scan.threshold_db", cfg.scan_threshold_db);
    cfg.scan_interval_sec = iniInt(ini, "scan.interval_sec", cfg.scan_interval_sec);
    cfg.scan_tune_timeout_sec = iniInt(ini, "scan.tune_timeout_sec", cfg.scan_tune_timeout_sec);
    cfg.scan_detect_dwell_sec = iniInt(ini, "scan.detect_dwell_sec", cfg.scan_detect_dwell_sec);
    cfg.scan_spectrum_dwell_sec = iniInt(ini, "scan.spectrum_dwell_sec", cfg.scan_spectrum_dwell_sec);
    cfg.live_spectrum_interval_sec = iniInt(ini, "scan.live_spectrum_interval_sec", cfg.live_spectrum_interval_sec);
    cfg.live_spectrum_dwell_sec = iniInt(ini, "scan.live_spectrum_dwell_sec", cfg.live_spectrum_dwell_sec);
    cfg.scan_power_bin_hz = iniInt(ini, "scan.power_bin_hz", cfg.scan_power_bin_hz);
    cfg.scan_quantization_hz = iniInt(ini, "scan.quantization_hz", cfg.scan_quantization_hz);
    cfg.scan_min_distance_hz = iniInt(ini, "scan.min_distance_hz", cfg.scan_min_distance_hz);
    cfg.scan_min_peak_width_hz = iniInt(ini, "scan.min_peak_width_hz", cfg.scan_min_peak_width_hz);
    cfg.scan_whitelist_mhz = iniFrequencyListMhz(ini, "scan.whitelist_mhz");
    cfg.scan_whitelist_file = iniGet(ini, "scan.whitelist_file", cfg.scan_whitelist_file);
    cfg.scan_blacklist_mhz = iniFrequencyListMhz(ini, "scan.blacklist_mhz");
    cfg.scan_blacklist_file = iniGet(ini, "scan.blacklist_file", cfg.scan_blacklist_file);
    cfg.scan_blacklist_width_khz = iniDouble(ini, "scan.blacklist_width_khz", cfg.scan_blacklist_width_khz);
    cfg.scan_active_skip_width_khz = iniDouble(ini, "scan.active_skip_width_khz", cfg.scan_active_skip_width_khz);
    cfg.scan_max_peaks = iniInt(ini, "scan.max_peaks", cfg.scan_max_peaks);
    cfg.scan_parallel_detections = iniInt(ini, "scan.parallel_detections", cfg.scan_parallel_detections);
    cfg.scan_accept_score = iniDouble(ini, "scan.accept_score", cfg.scan_accept_score);
    cfg.scan_fallback_when_active = iniBool(ini, "scan.fallback_when_active", cfg.scan_fallback_when_active);
    cfg.scan_fallback_candidates = iniInt(ini, "scan.fallback_candidates", cfg.scan_fallback_candidates);
    cfg.scan_fallback_min_snr_db = iniDouble(ini, "scan.fallback_min_snr_db", cfg.scan_fallback_min_snr_db);
    cfg.scan_decoder_offset_hz = iniDouble(ini, "scan.decoder_offset_hz", cfg.scan_decoder_offset_hz);
    cfg.scan_offset_search_hz = iniInt(ini, "scan.offset_search_hz", cfg.scan_offset_search_hz);
    cfg.scan_offset_step_hz = iniInt(ini, "scan.offset_step_hz", cfg.scan_offset_step_hz);
    cfg.scan_max_channels = iniInt(ini, "scan.max_channels", cfg.scan_max_channels);
    cfg.channel_timeout_sec = iniInt(ini, "scan.channel_timeout_sec", cfg.channel_timeout_sec);

    cfg.upload_enabled = iniBool(ini, "upload.enabled", cfg.upload_enabled);
    cfg.receiver_position_interval_sec = iniInt(ini, "upload.receiver_position_interval_sec", cfg.receiver_position_interval_sec);

    cfg.udp_enabled = iniBool(ini, "udp.enabled", cfg.udp_enabled);
    cfg.udp_host = iniGet(ini, "udp.host", cfg.udp_host);
    cfg.udp_port = iniInt(ini, "udp.port", cfg.udp_port);

    cfg.dry_run = iniBool(ini, "runtime.dry_run", cfg.dry_run);
    cfg.verbose = iniBool(ini, "runtime.verbose", cfg.verbose);
    cfg.decoder_debug = iniBool(ini, "runtime.decoder_debug", cfg.decoder_debug);
    cfg.callsign = args.get("callsign", cfg.callsign);
    cfg.station_lat = args.getDouble("station-lat", cfg.station_lat);
    cfg.station_lon = args.getDouble("station-lon", cfg.station_lon);
    cfg.station_alt = args.getDouble("station-alt", cfg.station_alt);

    cfg.ka9q_radio = args.get("ka9q-radio", cfg.ka9q_radio);
    cfg.ka9q_pcm = args.get("ka9q-pcm", cfg.ka9q_pcm);
    cfg.decoder_dir = args.get("decoder-dir", cfg.decoder_dir);
    cfg.wav_file = args.get("wav", cfg.wav_file);

    cfg.sample_rate = args.getInt("sample-rate", cfg.sample_rate);
    cfg.ka9q_low_hz = args.getInt("ka9q-low", cfg.ka9q_low_hz);
    cfg.ka9q_high_hz = args.getInt("ka9q-high", cfg.ka9q_high_hz);
    cfg.iq_offset_hz = args.getDouble("iq-offset", cfg.iq_offset_hz);
    if (args.has("dry-run")) cfg.dry_run = true;
    if (args.has("verbose")) cfg.verbose = true;
    if (args.has("decoder-debug")) cfg.decoder_debug = true;
    if (args.has("upload")) cfg.upload_enabled = true;
    if (args.has("udp")) cfg.udp_enabled = true;
    cfg.udp_host = args.get("udp-host", cfg.udp_host);
    cfg.udp_port = args.getInt("udp-port", cfg.udp_port);
    cfg.scan_enabled = true;
    cfg.scan_min_mhz = args.getDouble("scan-min", cfg.scan_min_mhz);
    cfg.scan_max_mhz = args.getDouble("scan-max", cfg.scan_max_mhz);
    cfg.scan_step_khz = args.getInt("scan-step-khz", cfg.scan_step_khz);
    cfg.scan_threshold_db = args.getDouble("scan-threshold", cfg.scan_threshold_db);
    cfg.scan_interval_sec = args.getInt("scan-interval", cfg.scan_interval_sec);
    cfg.scan_tune_timeout_sec = args.getInt("scan-tune-timeout", cfg.scan_tune_timeout_sec);
    cfg.scan_detect_dwell_sec = args.getInt("scan-detect-dwell", cfg.scan_detect_dwell_sec);
    cfg.scan_spectrum_dwell_sec = args.getInt("scan-spectrum-dwell", cfg.scan_spectrum_dwell_sec);
    cfg.live_spectrum_interval_sec = args.getInt("live-spectrum-interval", cfg.live_spectrum_interval_sec);
    cfg.live_spectrum_dwell_sec = args.getInt("live-spectrum-dwell", cfg.live_spectrum_dwell_sec);
    cfg.scan_power_bin_hz = args.getInt("scan-power-bin-hz", cfg.scan_power_bin_hz);
    cfg.scan_quantization_hz = args.getInt("scan-quantization-hz", cfg.scan_quantization_hz);
    cfg.scan_min_distance_hz = args.getInt("scan-min-distance-hz", cfg.scan_min_distance_hz);
    cfg.scan_min_peak_width_hz = args.getInt("scan-min-peak-width-hz", cfg.scan_min_peak_width_hz);
    if (args.has("scan-whitelist-mhz")) cfg.scan_whitelist_mhz = parseFrequencyListMhz(args.get("scan-whitelist-mhz", ""));
    cfg.scan_whitelist_file = args.get("scan-whitelist-file", cfg.scan_whitelist_file);
    if (args.has("scan-blacklist-mhz")) cfg.scan_blacklist_mhz = parseFrequencyListMhz(args.get("scan-blacklist-mhz", ""));
    cfg.scan_blacklist_file = args.get("scan-blacklist-file", cfg.scan_blacklist_file);
    cfg.scan_blacklist_width_khz = args.getDouble("scan-blacklist-width-khz", cfg.scan_blacklist_width_khz);
    cfg.scan_active_skip_width_khz = args.getDouble("scan-active-skip-width-khz", cfg.scan_active_skip_width_khz);
    cfg.scan_max_peaks = args.getInt("scan-max-peaks", cfg.scan_max_peaks);
    cfg.scan_parallel_detections = args.getInt("scan-parallel-detections", cfg.scan_parallel_detections);
    cfg.scan_accept_score = args.getDouble("scan-accept-score", cfg.scan_accept_score);
    if (args.has("scan-fallback-when-active")) cfg.scan_fallback_when_active = true;
    cfg.scan_fallback_candidates = args.getInt("scan-fallback-candidates", cfg.scan_fallback_candidates);
    cfg.scan_fallback_min_snr_db = args.getDouble("scan-fallback-min-snr", cfg.scan_fallback_min_snr_db);
    cfg.scan_decoder_offset_hz = args.getDouble("scan-decoder-offset-hz", cfg.scan_decoder_offset_hz);
    cfg.scan_offset_search_hz = args.getInt("scan-offset-search-hz", cfg.scan_offset_search_hz);
    cfg.scan_offset_step_hz = args.getInt("scan-offset-step-hz", cfg.scan_offset_step_hz);
    cfg.scan_max_channels = args.getInt("scan-max-channels", cfg.scan_max_channels);
    cfg.channel_timeout_sec = args.getInt("channel-timeout", cfg.channel_timeout_sec);
    cfg.receiver_position_interval_sec = args.getInt("position-interval", cfg.receiver_position_interval_sec);
    if (cfg.dry_run) cfg.upload_enabled = false;

    if (!cfg.scan_whitelist_file.empty()) {
        cfg.scan_whitelist_mhz = mergeUniqueFrequenciesMhz(cfg.scan_whitelist_mhz, loadFrequencyListFile(cfg.scan_whitelist_file));
    }
    if (!cfg.scan_blacklist_file.empty()) {
        cfg.scan_blacklist_mhz = mergeUniqueFrequenciesMhz(cfg.scan_blacklist_mhz, loadFrequencyListFile(cfg.scan_blacklist_file));
    }

    if (cfg.callsign.empty()) {
        throw std::runtime_error("Missing callsign. Set station.callsign in config.ini or use -callsign.");
    }
    if (cfg.scan_enabled) {
        if (cfg.scan_min_mhz <= 0.0 || cfg.scan_max_mhz <= cfg.scan_min_mhz) {
            throw std::runtime_error("Invalid scan range. Set scan.min_mhz and scan.max_mhz in config.ini.");
        }
        if (cfg.scan_step_khz <= 0) {
            throw std::runtime_error("Invalid scan.step_khz in config.ini.");
        }
        if (cfg.scan_max_channels < 1) cfg.scan_max_channels = 1;
        if (cfg.channel_timeout_sec < 30) cfg.channel_timeout_sec = 30;
        if (cfg.scan_interval_sec < 1) cfg.scan_interval_sec = 1;
        if (cfg.scan_tune_timeout_sec < 1) cfg.scan_tune_timeout_sec = 1;
        if (cfg.scan_detect_dwell_sec < 1) cfg.scan_detect_dwell_sec = 1;
        if (cfg.scan_spectrum_dwell_sec < 1) cfg.scan_spectrum_dwell_sec = 1;
        if (cfg.live_spectrum_interval_sec < 1) cfg.live_spectrum_interval_sec = 1;
        if (cfg.live_spectrum_dwell_sec < 1) cfg.live_spectrum_dwell_sec = 1;
        if (cfg.scan_power_bin_hz < 100) cfg.scan_power_bin_hz = 100;
        if (cfg.scan_quantization_hz < 1000) cfg.scan_quantization_hz = 1000;
        if (cfg.scan_min_distance_hz < 100) cfg.scan_min_distance_hz = 100;
        if (cfg.scan_min_peak_width_hz < 0) cfg.scan_min_peak_width_hz = 0;
        if (cfg.scan_blacklist_width_khz < 0.0) cfg.scan_blacklist_width_khz = 0.0;
        if (cfg.scan_blacklist_width_khz > 100.0) cfg.scan_blacklist_width_khz = 100.0;
        if (cfg.scan_active_skip_width_khz < 0.0) cfg.scan_active_skip_width_khz = 0.0;
        if (cfg.scan_active_skip_width_khz > 100.0) cfg.scan_active_skip_width_khz = 100.0;
        if (cfg.scan_max_peaks < 1) cfg.scan_max_peaks = 1;
        if (cfg.scan_parallel_detections < 1) cfg.scan_parallel_detections = 1;
        if (cfg.scan_parallel_detections > 32) cfg.scan_parallel_detections = 32;
        const unsigned int cpu_count = std::thread::hardware_concurrency();
        if (cpu_count > 0 && cfg.scan_parallel_detections > static_cast<int>(cpu_count)) {
            cfg.scan_parallel_detections = static_cast<int>(cpu_count);
        }
        if (cfg.scan_fallback_candidates < 0) cfg.scan_fallback_candidates = 0;
        if (cfg.scan_accept_score < 0.0) cfg.scan_accept_score = 0.0;
        if (cfg.scan_accept_score > 1.0) cfg.scan_accept_score = 1.0;
        if (std::fabs(cfg.scan_decoder_offset_hz) > 25000.0) {
            throw std::runtime_error("Invalid scan.decoder_offset_hz. Expected value within +/-25000 Hz.");
        }
        if (cfg.scan_offset_search_hz < 0) cfg.scan_offset_search_hz = 0;
        if (cfg.scan_offset_search_hz > 25000) cfg.scan_offset_search_hz = 25000;
        if (cfg.scan_offset_step_hz < 100) cfg.scan_offset_step_hz = 100;
        if (cfg.scan_offset_step_hz > cfg.scan_offset_search_hz && cfg.scan_offset_search_hz > 0) cfg.scan_offset_step_hz = cfg.scan_offset_search_hz;
    }
    if (cfg.station_lat == 0.0 && cfg.station_lon == 0.0) {
        throw std::runtime_error("Missing station position. Set station.lat/station.lon in config.ini or use -station-lat/-station-lon.");
    }

    for (int i = 1; i <= 16; ++i) {
        const std::string prefix = "radio" + std::to_string(i) + ".";
        if (ini.find(prefix + "radio_name") == ini.end()) continue;

        RadioBackend rb;
        rb.name = iniGet(ini, prefix + "name", "radio" + std::to_string(i));
        rb.ka9q_radio = iniGet(ini, prefix + "radio_name", "");
        rb.ka9q_pcm = iniGet(ini, prefix + "pcm_name", "");
        rb.scan_min_mhz = iniDouble(ini, prefix + "min_mhz", cfg.scan_min_mhz);
        rb.scan_max_mhz = iniDouble(ini, prefix + "max_mhz", cfg.scan_max_mhz);

        if (rb.ka9q_radio.empty() || rb.ka9q_pcm.empty()) {
            throw std::runtime_error("[radio" + std::to_string(i) + "] needs both radio_name and pcm_name in config.ini.");
        }
        if (rb.scan_min_mhz <= 0.0 || rb.scan_max_mhz <= rb.scan_min_mhz) {
            throw std::runtime_error("[radio" + std::to_string(i) + "] has an invalid min_mhz/max_mhz range in config.ini.");
        }
        cfg.radios.push_back(rb);
    }

    if (cfg.radios.empty()) {
        RadioBackend rb;
        rb.name = "default";
        rb.ka9q_radio = cfg.ka9q_radio;
        rb.ka9q_pcm = cfg.ka9q_pcm;
        rb.scan_min_mhz = cfg.scan_min_mhz;
        rb.scan_max_mhz = cfg.scan_max_mhz;
        cfg.radios.push_back(rb);
    } else {
        for (size_t a = 0; a < cfg.radios.size(); ++a) {
            for (size_t b = a + 1; b < cfg.radios.size(); ++b) {
                const auto& ra = cfg.radios[a];
                const auto& rb = cfg.radios[b];
                const bool overlap = ra.scan_min_mhz < rb.scan_max_mhz && rb.scan_min_mhz < ra.scan_max_mhz;
                if (overlap) {
                    throw std::runtime_error(
                        "[radio" + std::to_string(a + 1) + "] and [radio" + std::to_string(b + 1) +
                        "] scan ranges overlap in config.ini. Give each SDR its own non-overlapping min_mhz/max_mhz.");
                }
            }
        }

        double combined_min = cfg.radios.front().scan_min_mhz;
        double combined_max = cfg.radios.front().scan_max_mhz;
        for (const auto& rb : cfg.radios) {
            combined_min = std::min(combined_min, rb.scan_min_mhz);
            combined_max = std::max(combined_max, rb.scan_max_mhz);
        }
        cfg.scan_min_mhz = combined_min;
        cfg.scan_max_mhz = combined_max;
    }

    std::sort(cfg.radios.begin(), cfg.radios.end(), [](const RadioBackend& a, const RadioBackend& b) {
        return a.scan_min_mhz < b.scan_min_mhz;
    });

    return cfg;
}

