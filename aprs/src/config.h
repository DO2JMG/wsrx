#pragma once

#include <string>

struct AprsConfig {
    // [aprs-is] - target APRS-IS server
    std::string server = "rotate.aprs2.net";
    int server_port = 14580;
    std::string callsign;     
    std::string passcode;    
    std::string tocall = "APWSRX";
    std::string filter;       
    int reconnect_min_sec = 5;
    int reconnect_max_sec = 300;

    double station_lat = 0.0;
    double station_lon = 0.0;
    double station_alt_m = 0.0;
    std::string station_comment = "wsrx APRS gateway";
    std::string station_symbol_table = "/";
    std::string station_symbol_code = "-";  
    int station_beacon_interval_sec = 1800;  

    std::string object_symbol_table = "/";
    std::string object_symbol_code = "O";  

    std::string udp_listen_host = "0.0.0.0";
    int udp_listen_port = 18000;

    bool verbose = false;

    static AprsConfig load(const std::string& config_file);
};
