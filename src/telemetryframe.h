#pragma once

#include <cmath>
#include <ctime>
#include <string>

struct TelemetryFrame {
    std::string serial;
    std::string type;
    std::string receiver;

    double frequency_mhz = 0.0;
    double tx_frequency_mhz = NAN;
    double lat = NAN;
    double lon = NAN;
    double alt_m = NAN;
    double speed_ms = NAN;
    double heading_deg = NAN;
    double climb_ms = NAN;
    double temperature_c = NAN;
    double humidity_percent = NAN;
    double pressure_hpa = NAN;
    double battery_v = NAN;
    double burstkilltimer_sec = NAN;
    double killtimer_sec = NAN;
    std::string aux;

    int frame = -1;
    int sats = -1;
    double rssi_db = NAN;

    std::time_t timestamp = 0;
    std::string timestamp_hhmmss;
    std::string raw_datetime;
    std::string raw_line;
};

