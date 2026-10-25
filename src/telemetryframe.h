#pragma once

#include <cmath>
#include <string>

struct TelemetryFrame {
    std::string serial;
    std::string type;

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
    std::string aux;

    int frame = -1;
    int sats = -1;

    double tx_power_raw = NAN;


    std::string mainboard;
    int mainboard_fw = -1;

    std::string timestamp_hhmmss;
    std::string raw_datetime;
    std::string raw_line;
};

