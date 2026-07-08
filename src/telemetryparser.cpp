#include "telemetryparser.h"

#include <cmath>
#include <regex>
#include <ctime>
#include <algorithm>
#include <cctype>

namespace {

std::string upperCopy(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return s;
}

bool startsWith(const std::string& s, const std::string& prefix) {
    return s.rfind(prefix, 0) == 0;
}

std::string digitsOnly(const std::string& s) {
    std::string out;
    for (unsigned char c : s) {
        if (std::isdigit(c)) out.push_back(static_cast<char>(c));
    }
    return out;
}

std::string pad5(unsigned long n) {
    n %= 100000UL;
    std::string v = std::to_string(n);
    if (v.size() < 5) v.insert(v.begin(), 5 - v.size(), '0');
    return v;
}

std::string normalizeSubtypeForUpload(std::string type) {
    // dfm09mod can report subtype like "0xC:DFM17". wettersonde.net should get only "DFM17".
    const auto colon = type.find(':');
    if (colon != std::string::npos && colon + 1 < type.size()) {
        type = type.substr(colon + 1);
    }
    return type;
}

std::string normalizeSerialForUpload(std::string serial, const std::string& type) {
    const std::string upper_type = upperCopy(type);
    const std::string upper_serial = upperCopy(serial);

    if (upper_type.find("DFM") != std::string::npos || startsWith(upper_serial, "DFM-")) {
        // dfm09mod JSON gives e.g. "DFM-23052929". wettersonde.net expects "D23052929".
        if (startsWith(upper_serial, "DFM-")) {
            return "D" + serial.substr(4);
        }
    }
    return serial;
}

int secondsOfDayFromHhmmss(const std::string& hhmmss) {
    if (hhmmss.size() != 6) return -1;
    for (char c : hhmmss) {
        if (!std::isdigit(static_cast<unsigned char>(c))) return -1;
    }

    const int hh = std::stoi(hhmmss.substr(0, 2));
    const int mm = std::stoi(hhmmss.substr(2, 2));
    const int ss = std::stoi(hhmmss.substr(4, 2));
    if (hh < 0 || hh > 23 || mm < 0 || mm > 59 || ss < 0 || ss > 60) return -1;
    return hh * 3600 + mm * 60 + ss;
}


std::string padHex(unsigned long n, int width) {
    static const char* hex = "0123456789ABCDEF";
    std::string out;
    do {
        out.push_back(hex[n & 0x0FUL]);
        n >>= 4;
    } while (n > 0);
    while (static_cast<int>(out.size()) < width) out.push_back('0');
    std::reverse(out.begin(), out.end());
    return out;
}

std::string padDec(unsigned long n, int width) {
    std::string v = std::to_string(n);
    if (static_cast<int>(v.size()) < width) v.insert(v.begin(), width - v.size(), '0');
    return v;
}

std::string normalizeMeteomodemSerialForDxlAprs(const std::string& serial, const std::string& type) {
    const std::string upper_type = upperCopy(type);
    const std::string upper_serial = upperCopy(serial);

    // Already dxlAPRS/APRS style.
    if (startsWith(upper_serial, "ME")) return upper_serial;

    // auto_rx / rdzTTGO style for Meteomodem is usually e.g.:
    //   M10: 501-2-10713 -> dxlAPRS ME51222C9
    //   M10: 201-2-12655 -> dxlAPRS ME2122A5F
    // dxlAPRS packs the M10 name as a 7 digit hex value:
    //   ME + %07X((first_digit << 24) | 0x120000 | (middle << 12) | (last_number % 10000))
    // The middle group is normally 0..15 and becomes one hex nibble.
    {
        static const std::regex m10_re(R"(^([0-9]{3})-([0-9]+)-([0-9]+)$)");
        std::smatch m;
        if (upper_type.find("M10") != std::string::npos && std::regex_match(serial, m, m10_re)) {
            const int first_group = std::stoi(m[1]);
            const int middle = std::stoi(m[2]);
            const int number = std::stoi(m[3]);
            const int first_digit = first_group / 100;
            if (first_digit >= 0 && first_digit <= 15 && middle >= 0 && middle <= 15) {
                const unsigned long packed = (static_cast<unsigned long>(first_digit) << 24)
                                           | 0x120000UL
                                           | (static_cast<unsigned long>(middle) << 12)
                                           | static_cast<unsigned long>(number % 10000);
                return "ME" + padHex(packed, 7);
            }
        }
    }

    // auto_rx / rdzTTGO style for M20 seen as e.g.:
    //   510-2-00770 -> dxlAPRS MEC500770
    // M20 uses the C + first digit family prefix and keeps the last numeric block decimal.
    {
        static const std::regex m20_re(R"(^([0-9]{3})-([0-9]+)-([0-9]+)$)");
        std::smatch m;
        if (upper_type.find("M20") != std::string::npos && std::regex_match(serial, m, m20_re)) {
            const int first_group = std::stoi(m[1]);
            const int first_digit = first_group / 100;
            const unsigned long number = std::stoul(m[3]);
            if (first_digit >= 0 && first_digit <= 9) {
                return "ME" + std::string("C") + std::to_string(first_digit) + padDec(number % 100000UL, 5);
            }
        }
    }

    // No safe conversion possible. Return original so uploader blocks non-ME M10/M20.
    return serial;
}

std::string normalizeImetSerialForDxlAprs(const std::string& serial, int frame, const std::string& hhmmss) {
    // dxlAPRS/sondemod does not use the auto_rx / imet decoder hex id as upload name.
    // It generates IMETxxxxx from the estimated launch second of the UTC day:
    //   launch_second = UTC_seconds_of_day - frame, wrapped by 86400
    //   serial = IMET + (launch_second + 10000)
    // Verified examples:
    //   2026-06-29T22:39:23Z frame 590  -> IMET90973
    //   2026-06-29T00:51:02Z frame 8951 -> IMET90511
    if (frame >= 0) {
        const int seconds_of_day = secondsOfDayFromHhmmss(hhmmss);
        if (seconds_of_day >= 0) {
            int launch_second = seconds_of_day - frame;
            while (launch_second < 0) launch_second += 86400;
            launch_second %= 86400;
            const int imet_number = launch_second + 10000;
            return "IMET" + pad5(static_cast<unsigned long>(imet_number));
        }
    }

    // Do not invent a serial when frame/time are missing. Returning the decoder id
    // makes the uploader reject iMet safely instead of creating duplicate sondes.
    return serial;
}

std::string hhmmssFromIsoUtc(const std::string& iso) {
    if (iso.size() >= 19 && iso[10] == 'T') {
        return iso.substr(11, 2) + iso.substr(14, 2) + iso.substr(17, 2);
    }
    return {};
}
}

std::optional<TelemetryFrame> TelemetryParser::parseLine(const std::string& line, double frequency_mhz, const std::string& receiver) {
    // rs1729/OpenWXSDR-style RS41 text output is multi-line. Example:
    // [ 5628] (X1813943)  Sun 2026-06-28 15:17:41.000
    //  lat: 51.81065  lon: 8.42935  alt: 129.06   vH:  0.7  D: 288.6  vV: 0.1
    // The header line contains frame+serial, the position line contains lat/lon/alt.
    {
        static const std::regex hdr_re(R"(^\s*\[\s*([0-9]+)\]\s*\(([A-Za-z0-9_.-]+)\))");
        std::smatch m;
        if (std::regex_search(line, m, hdr_re)) {
            current_frame_ = std::stoi(m[1]);
            current_serial_ = m[2];
            current_type_ = "RS41";
            return std::nullopt;
        }
    }

    TelemetryFrame f;
    f.frequency_mhz = frequency_mhz;
    f.receiver = receiver;
    f.raw_line = line;
    f.timestamp = std::time(nullptr);

    // First handle JSON/single-line output variants.
    auto id = extractString(line, "id");
    auto ser = extractString(line, "serial");
    if (!ser) ser = extractString(line, "ser");
    auto type = extractString(line, "type");
    auto subtype = extractString(line, "subtype");

    if (id) f.serial = *id;
    if (ser) f.serial = *ser;
    if (type) f.type = normalizeSubtypeForUpload(*type);
    if (subtype) f.type = normalizeSubtypeForUpload(*subtype);
    if (auto dt = extractString(line, "datetime")) {
        f.raw_datetime = *dt;
        f.timestamp_hhmmss = hhmmssFromIsoUtc(*dt);
    }

    auto lat = extractNumber(line, "lat");
    auto lon = extractNumber(line, "lon");
    if (!lon) lon = extractNumber(line, "long");
    auto alt = extractNumber(line, "alt");

    if (lat) f.lat = *lat;
    if (lon) f.lon = *lon;
    if (alt) f.alt_m = *alt;

    if (auto v = extractNumber(line, "spd")) f.speed_ms = *v;
    if (auto v = extractNumber(line, "speed")) f.speed_ms = *v;
    if (auto v = extractNumber(line, "vel_h")) f.speed_ms = *v;

    if (auto v = extractNumber(line, "dir")) f.heading_deg = *v;
    if (auto v = extractNumber(line, "heading")) f.heading_deg = *v;

    if (auto v = extractNumber(line, "clb")) f.climb_ms = *v;
    if (auto v = extractNumber(line, "vel_v")) f.climb_ms = *v;

    // PTU and optional fields. Keep these strict: no one-letter aliases here.
    // Otherwise keys like "lat" or "vel_h" can be mistaken for temp/humidity.
    if (auto v = extractNumber(line, "temperature")) f.temperature_c = *v;
    if (auto v = extractNumber(line, "temp")) f.temperature_c = *v;

    if (auto v = extractNumber(line, "humidity")) f.humidity_percent = *v;
    if (auto v = extractNumber(line, "hum")) f.humidity_percent = *v;
    if (auto v = extractNumber(line, "rh")) f.humidity_percent = *v;
    if (auto v = extractNumber(line, "RH")) f.humidity_percent = *v;

    if (auto v = extractNumber(line, "pressure")) f.pressure_hpa = *v;
    if (auto v = extractNumber(line, "press")) f.pressure_hpa = *v;

    if (auto v = extractNumber(line, "batt")) f.battery_v = *v;
    if (auto v = extractNumber(line, "voltage")) f.battery_v = *v;
    if (auto v = extractNumber(line, "ub")) f.battery_v = *v;

    if (auto v = extractNumber(line, "burstkilltimer")) f.burstkilltimer_sec = *v;
    if (auto v = extractNumber(line, "burstkill")) f.burstkilltimer_sec = *v;
    if (auto v = extractNumber(line, "burstkill_timer")) f.burstkilltimer_sec = *v;
    if (auto v = extractNumber(line, "killtimer")) f.killtimer_sec = *v;
    if (auto v = extractNumber(line, "kill_timer")) f.killtimer_sec = *v;

    if (auto x = extractString(line, "xdata")) f.xdata = *x;
    if (auto x = extractString(line, "xdata1")) f.xdata1 = *x;
    if (auto x = extractString(line, "xdata2")) f.xdata2 = *x;
    if (auto x = extractString(line, "xdata3")) f.xdata3 = *x;

    if (auto v = extractNumber(line, "sats")) f.sats = static_cast<int>(*v);
    if (auto v = extractNumber(line, "sat")) f.sats = static_cast<int>(*v);
    if (auto v = extractNumber(line, "frame")) f.frame = static_cast<int>(*v);

    // rs41mod JSON may provide freq/tx_frequency in kHz, e.g. 405300 for 405.300 MHz.
    // Keep the tuned decoder frequency in f.frequency_mhz. Store the reported transmitter
    // frequency separately so the upload can use it and the scan offset cache can learn
    // the difference: tuned_frequency - tx_frequency.
    if (auto v = extractNumber(line, "freq")) f.tx_frequency_mhz = (*v > 1000.0) ? (*v / 1000.0) : *v;
    if (auto v = extractNumber(line, "tx_frequency")) f.tx_frequency_mhz = (*v > 1000.0) ? (*v / 1000.0) : *v;

    // Parse rs1729 verbose RS41 position line.
    if (std::isnan(f.lat) || std::isnan(f.lon) || std::isnan(f.alt_m)) {
        static const std::regex pos_re(
            R"(lat:\s*(-?[0-9]+(?:\.[0-9]+)?)\s+lon:\s*(-?[0-9]+(?:\.[0-9]+)?)\s+alt:\s*(-?[0-9]+(?:\.[0-9]+)?)(?:\s+vH:\s*(-?[0-9]+(?:\.[0-9]+)?))?(?:\s+D:\s*(-?[0-9]+(?:\.[0-9]+)?))?(?:\s+vV:\s*(-?[0-9]+(?:\.[0-9]+)?))?)"
        );
        std::smatch m;
        if (std::regex_search(line, m, pos_re)) {
            f.lat = std::stod(m[1]);
            f.lon = std::stod(m[2]);
            f.alt_m = std::stod(m[3]);
            if (m[4].matched) f.speed_ms = std::stod(m[4]);
            if (m[5].matched) f.heading_deg = std::stod(m[5]);
            if (m[6].matched) f.climb_ms = std::stod(m[6]);
            if (!current_serial_.empty()) f.serial = current_serial_;
            f.type = current_type_.empty() ? "RS41" : current_type_;
            f.frame = current_frame_;
        }
    }

    // Fallback fuer klassische rs1729-Textausgaben: suche typische Serialmuster.
    if (f.serial.empty()) {
        std::regex serial_re("\\b([A-Z][0-9A-Z]{5,10}|D[0-9A-Z]{6,10}|ME[0-9A-Z]{4,10})\\b");
        std::smatch m;
        if (std::regex_search(line, m, serial_re)) {
            f.serial = m[1];
        }
    }

    if (f.type.empty()) {
        if (line.find("RS41") != std::string::npos) f.type = "RS41";
        else if (line.find("DFM") != std::string::npos) f.type = "DFM";
        else if (line.find("M10") != std::string::npos) f.type = "M10";
        else if (line.find("M20") != std::string::npos) f.type = "M20";
        else if (line.find("iMet") != std::string::npos || line.find("IMET") != std::string::npos) f.type = "IMET";
    }

    if (f.serial.empty() || std::isnan(f.lat) || std::isnan(f.lon) || std::isnan(f.alt_m)) {
        return std::nullopt;
    }

    if (f.type.empty()) f.type = "RS41";

    f.type = normalizeSubtypeForUpload(f.type);
    const std::string normalized_type_upper = upperCopy(f.type);
    if (normalized_type_upper.find("IMET") != std::string::npos || startsWith(upperCopy(f.serial), "IMET")) {
        f.type = "IMET";
        f.serial = normalizeImetSerialForDxlAprs(f.serial, f.frame, f.timestamp_hhmmss);
    } else if (normalized_type_upper.find("M10") != std::string::npos || normalized_type_upper.find("M20") != std::string::npos) {
        f.serial = normalizeMeteomodemSerialForDxlAprs(f.serial, f.type);
    } else {
        f.serial = normalizeSerialForUpload(f.serial, f.type);
    }

    return f;
}

std::optional<double> TelemetryParser::extractNumber(const std::string& text, const std::string& key) {
    std::smatch m;

    // Exact JSON key match, e.g. "temperature": 12.3
    // Do not match substrings such as key "t" inside "lat" or key "h" inside "vel_h".
    std::regex quoted_re("\\\"" + key + "\\\"\\s*[:=]\\s*(-?[0-9]+(?:\\.[0-9]+)?)");
    if (std::regex_search(text, m, quoted_re)) {
        return std::stod(m[1]);
    }

    // Exact unquoted key match for simple text formats, e.g. temp=12.3.
    // The key must not be part of a longer identifier.
    std::regex plain_re("(^|[^A-Za-z0-9_])" + key + "\\s*[:=]\\s*(-?[0-9]+(?:\\.[0-9]+)?)");
    if (std::regex_search(text, m, plain_re)) {
        return std::stod(m[2]);
    }

    return std::nullopt;
}

std::optional<std::string> TelemetryParser::extractString(const std::string& text, const std::string& key) {
    std::smatch m;

    // Exact JSON key match, e.g. "id": "X1813943".
    std::regex quoted_re("\\\"" + key + "\\\"\\s*[:=]\\s*\\\"([^\\\"]*)\\\"");
    if (std::regex_search(text, m, quoted_re)) {
        return m[1];
    }

    // Exact unquoted key match for simple text formats, e.g. id=X1813943.
    std::regex plain_re("(^|[^A-Za-z0-9_])" + key + "\\s*[:=]\\s*([A-Za-z0-9_.-]+)");
    if (std::regex_search(text, m, plain_re)) {
        return m[2];
    }

    return std::nullopt;
}
