#pragma once

#include "args.h"
#include <string>
#include <vector>

struct RadioBackend {
    std::string name = "default";
    std::string ka9q_radio;
    std::string ka9q_pcm;
    double scan_min_mhz = 0.0;
    double scan_max_mhz = 0.0;
};

struct Config {
    std::string config_file = "config.ini";
    std::string callsign;
    double station_lat = 0.0;
    double station_lon = 0.0;
    double station_alt = 0.0;


    double frequency_mhz = 0.0;
    std::string decoder = "rs41";

    std::string ka9q_radio = "wettersonde.local";
    std::string ka9q_pcm = "wettersonde-pcm.local";

    std::vector<RadioBackend> radios;

    std::string decoder_dir = "decoder";
    std::string wav_file;

    int sample_rate = 48000;
    int ka9q_low_hz = -20000;
    int ka9q_high_hz = 20000;
    double iq_offset_hz = 0.0;

    // Which sonde types dft_detect should scan for (used to build its
    // --types argument). Default: all enabled, matching prior behaviour.
    bool decoder_type_rs41 = true;
    bool decoder_type_dfm9 = true;
    bool decoder_type_m10 = true;
    bool decoder_type_imet4 = true;
    bool decoder_type_meisei = true;
    bool decoder_type_c34c50 = true;
    // Windsond S1 is explicitly marked experimental in the README, so it
    // stays opt-in unlike the other types.
    bool decoder_type_s1 = false;

    bool scan_enabled = true;
    double scan_min_mhz = 400.0;
    double scan_max_mhz = 406.0;
    int scan_step_khz = 10;
    double scan_threshold_db = 8.0;
    int scan_interval_sec = 20;
    int scan_tune_timeout_sec = 3;
    int scan_detect_dwell_sec = 5;
    int scan_spectrum_dwell_sec = 5;
    int live_spectrum_interval_sec = 5;
    int live_spectrum_dwell_sec = 2;
    int scan_power_bin_hz = 800;
    int scan_quantization_hz = 10000;
    int scan_min_distance_hz = 1000;
    int scan_min_peak_width_hz = 5000;
    std::vector<double> scan_whitelist_mhz;
    std::string scan_whitelist_file;
    std::vector<double> scan_blacklist_mhz;
    std::string scan_blacklist_file;
    double scan_blacklist_width_khz = 5.0;
    double scan_active_skip_width_khz = 5.0;
    int scan_max_peaks = 10;
    int scan_parallel_detections = 3;
    double scan_accept_score = 0.90;
    bool scan_fallback_when_active = false;
    int scan_fallback_candidates = 0;
    double scan_fallback_min_snr_db = 8.0;
    double scan_decoder_offset_hz = 0.0;
    int scan_offset_search_hz = 5000;
    int scan_offset_step_hz = 1000;
    int scan_max_channels = 4;
    int channel_timeout_sec = 180;

    bool dry_run = false;
    bool verbose = false;
    bool decoder_debug = false;
    bool upload_enabled = false;
    int receiver_position_interval_sec = 1800;

    bool udp_enabled = false;
    std::string udp_host = "127.0.0.1";
    int udp_port = 18000;


    static Config load(const Args& args, const std::string& config_file);
};

