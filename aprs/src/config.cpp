#include "config.h"

#include <cstdlib>
#include <fstream>
#include <map>
#include <sstream>
#include <stdexcept>

namespace {

std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

using IniMap = std::map<std::string, std::string>;

IniMap parseIni(const std::string& path) {
    std::ifstream in(path);
    if (!in) throw std::runtime_error("could not open config file: " + path);

    IniMap map;
    std::string section;
    std::string line;
    while (std::getline(in, line)) {
        std::string t = trim(line);
        if (t.empty() || t[0] == '#' || t[0] == ';') continue;
        if (t.front() == '[' && t.back() == ']') {
            section = trim(t.substr(1, t.size() - 2));
            continue;
        }
        size_t eq = t.find('=');
        if (eq == std::string::npos) continue;
        std::string key = trim(t.substr(0, eq));
        std::string value = trim(t.substr(eq + 1));
        // Strip inline comments starting with ' ;' or ' #'
        size_t c = value.find(" ;");
        if (c != std::string::npos) value = trim(value.substr(0, c));
        c = value.find(" #");
        if (c != std::string::npos) value = trim(value.substr(0, c));

        std::string full_key = section.empty() ? key : (section + "." + key);
        map[full_key] = value;
    }
    return map;
}

std::string iniGet(const IniMap& ini, const std::string& key, const std::string& fallback) {
    auto it = ini.find(key);
    return it == ini.end() ? fallback : it->second;
}

int iniInt(const IniMap& ini, const std::string& key, int fallback) {
    auto it = ini.find(key);
    if (it == ini.end() || it->second.empty()) return fallback;
    return std::atoi(it->second.c_str());
}

double iniDouble(const IniMap& ini, const std::string& key, double fallback) {
    auto it = ini.find(key);
    if (it == ini.end() || it->second.empty()) return fallback;
    return std::strtod(it->second.c_str(), nullptr);
}

bool iniBool(const IniMap& ini, const std::string& key, bool fallback) {
    auto it = ini.find(key);
    if (it == ini.end() || it->second.empty()) return fallback;
    std::string v = it->second;
    for (auto& c : v) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
    return v == "1" || v == "true" || v == "yes" || v == "on";
}

}  // namespace

AprsConfig AprsConfig::load(const std::string& config_file) {
    AprsConfig cfg;
    IniMap ini = parseIni(config_file);

    cfg.server = iniGet(ini, "aprs-is.server", cfg.server);
    cfg.server_port = iniInt(ini, "aprs-is.port", cfg.server_port);
    cfg.callsign = iniGet(ini, "aprs-is.callsign", cfg.callsign);
    cfg.passcode = iniGet(ini, "aprs-is.passcode", cfg.passcode);
    cfg.tocall = iniGet(ini, "aprs-is.tocall", cfg.tocall);
    cfg.filter = iniGet(ini, "aprs-is.filter", cfg.filter);
    cfg.reconnect_min_sec = iniInt(ini, "aprs-is.reconnect_min_sec", cfg.reconnect_min_sec);
    cfg.reconnect_max_sec = iniInt(ini, "aprs-is.reconnect_max_sec", cfg.reconnect_max_sec);

    cfg.station_lat = iniDouble(ini, "station.latitude", cfg.station_lat);
    cfg.station_lon = iniDouble(ini, "station.longitude", cfg.station_lon);
    cfg.station_alt_m = iniDouble(ini, "station.altitude", cfg.station_alt_m);
    cfg.station_comment = iniGet(ini, "station.comment", cfg.station_comment);
    cfg.station_symbol_table = iniGet(ini, "station.symbol_table", cfg.station_symbol_table);
    cfg.station_symbol_code = iniGet(ini, "station.symbol_code", cfg.station_symbol_code);
    cfg.station_beacon_interval_sec = iniInt(ini, "station.beacon_interval_sec", cfg.station_beacon_interval_sec);

    cfg.object_symbol_table = iniGet(ini, "object.symbol_table", cfg.object_symbol_table);
    cfg.object_symbol_code = iniGet(ini, "object.symbol_code", cfg.object_symbol_code);

    cfg.udp_listen_host = iniGet(ini, "udp.listen_host", cfg.udp_listen_host);
    cfg.udp_listen_port = iniInt(ini, "udp.listen_port", cfg.udp_listen_port);

    cfg.verbose = iniBool(ini, "general.verbose", cfg.verbose);

    if (cfg.callsign.empty()) throw std::runtime_error("aprs.ini: [aprs-is] callsign is required");
    if (cfg.passcode.empty()) throw std::runtime_error("aprs.ini: [aprs-is] passcode is required");

    return cfg;
}
