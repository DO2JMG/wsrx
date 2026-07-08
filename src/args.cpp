#include "args.h"

#include <cstdlib>
#include <iostream>
#include <stdexcept>

Args Args::parse(int argc, char** argv) {
    Args args;

    for (int i = 1; i < argc; ++i) {
        std::string key = argv[i];

        if (key.rfind("--", 0) == 0) {
            throw std::runtime_error("Invalid parameter \"" + key + "\". Use \"-" + key.substr(2) + "\" instead.");
        }

        if (key.empty() || key[0] != '-') {
            throw std::runtime_error("Invalid argument \"" + key + "\". Parameters must start with one '-' character.");
        }

        if (key == "-h" || key == "-help") {
            args.values_["help"] = "1";
            continue;
        }

        key = key.substr(1);

        if (i + 1 < argc && argv[i + 1][0] != '-') {
            args.values_[key] = argv[++i];
        } else {
            args.values_[key] = "1";
        }
    }

    return args;
}

bool Args::has(const std::string& key) const {
    return values_.find(key) != values_.end();
}

std::string Args::get(const std::string& key, const std::string& fallback) const {
    auto it = values_.find(key);
    return it == values_.end() ? fallback : it->second;
}

double Args::getDouble(const std::string& key, double fallback) const {
    if (!has(key)) return fallback;
    return std::stod(get(key));
}

int Args::getInt(const std::string& key, int fallback) const {
    if (!has(key)) return fallback;
    return std::stoi(get(key));
}

void Args::printHelp(const char* program) {
    std::cout << "Usage:\n"
              << "  " << program << "\n"
              << "  " << program << " -scan-min 400.000 -scan-max 406.000 -dry-run -verbose\n\n"
              << "config.ini must be in the same directory as wsrx.\n"
              << "Parameters use one '-' only and override config.ini values:\n"
              << "  -scan-min <MHz>\n"
              << "  -scan-max <MHz>\n"
              << "  -scan-step-khz <kHz>\n"
              << "  -scan-detect-dwell <sec> seconds for dft_detect per frequency\n"
              << "  -scan-offset-search-hz <Hz>  try +/- this range around each peak\n"
              << "  -scan-offset-step-hz <Hz>    offset search step\n"
              << "  -scan-accept-score <0..1>    early accept score for dft_detect\n"
              << "  -scan-fallback-when-active   test weak fallback candidates even with active channels\n"
              << "  -scan-max-channels <n>\n"
              << "  -channel-timeout <sec>\n"
              << "  -callsign <CALL>\n"
              << "  -station-lat <deg>\n"
              << "  -station-lon <deg>\n"
              << "  -station-alt <m>\n"
              << "  -ka9q-radio <name>\n"
              << "  -ka9q-pcm <name>\n"
              << "  -decoder-dir <path>      default: /home/ultron/RS/demod/mod\n"
              << "  -rs41-command <path>     optional direct override\n"
              << "  -dfm-command <path>      optional DFM decoder override\n"
              << "  -m10-command <path>      optional M10 decoder override\n"
              << "  -m20-command <path>      optional M20 decoder override\n"
              << "  -wav <file>              offline WAV test input\n"
              << "  -sample-rate <Hz>        KA9Q/decoder sample rate, default 48000\n"
              << "  -ka9q-low <Hz>           KA9Q channel low edge, default -20000\n"
              << "  -ka9q-high <Hz>          KA9Q channel high edge, default 20000\n"
              << "  -iq-offset <Hz>          rs41mod --IQ offset, default 0.0\n"
              << "  -upload\n"
              << "  -dry-run\n"
              << "  -verbose                 show cleaned runtime debug output\n"
              << "  -decoder-debug           show every raw decoder/scan line\n";
}
