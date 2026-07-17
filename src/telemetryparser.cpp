#include "telemetryparser.h"

#include <cmath>
#include <regex>
#include <ctime>
#include <algorithm>
#include <cctype>
#include <cstring>
#include <vector>

namespace {

std::string upperCopy(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return s;
}

bool startsWith(const std::string& s, const std::string& prefix) {
    return s.rfind(prefix, 0) == 0;
}

std::string pad5(unsigned long n) {
    n %= 100000UL;
    std::string v = std::to_string(n);
    if (v.size() < 5) v.insert(v.begin(), 5 - v.size(), '0');
    return v;
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

//---------------- Replace Serial like dxlAPRS ---------------------------------------

std::string normalizeDFM(std::string type) {    
    const auto colon = type.find(':');
    if (colon != std::string::npos && colon + 1 < type.size()) {
        type = type.substr(colon + 1);
    }
    return type;
}

std::string normalizeDFMSerial(std::string serial, const std::string& type) { 
    const std::string upper_type = upperCopy(type);
    const std::string upper_serial = upperCopy(serial);

    if (upper_type.find("DFM") != std::string::npos || startsWith(upper_serial, "DFM-")) {
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

int hexDigitValue(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
    return -1;
}

std::string normalizeM20Serial(const std::string& working_serial) {
    static const std::regex m20_re(R"(^([0-9]{3})-([0-9]+)-([0-9]+)$)");
    std::smatch m;
    if (!std::regex_match(working_serial, m, m20_re)) return working_serial;

    const int first_group = std::stoi(m[1]);
    const int middle = std::stoi(m[2]);
    const unsigned long number = std::stoul(m[3]);

    // first_group = (tmp/12)*100 + (tmp%12 + 1)  =>  Umkehrung:
    const int hundreds = first_group / 100;      // = tmp/12
    const int remainder = first_group % 100;     // = tmp%12 + 1
    if (remainder < 1 || remainder > 12) return working_serial;
    if (middle < 1 || middle > 8) return working_serial;

    const int tmp = hundreds * 12 + (remainder - 1);   // = data[18] & 0x7F
    const int bit7 = (middle - 1) & 1;                 // = data[18] >> 7
    const unsigned int byte18 = static_cast<unsigned int>(tmp)
                               | (static_cast<unsigned int>(bit7) << 7);

    return "ME" + padHex(byte18, 2) + padDec(number % 100000UL, 5);
}

std::string normalizeM10Serial(const std::string& working_serial) {
    static const std::regex m10_re(R"(^([0-9A-Fa-f])([0-9]{2})-([0-9A-Fa-f])-([0-9])([0-9]{4})$)");
    std::smatch m;
    if (!std::regex_match(working_serial, m, m10_re)) return working_serial;

    const int h   = hexDigitValue(m[1].str()[0]);
    const int dd  = std::stoi(m[2]);                    // 0..15
    const int mid = hexDigitValue(m[3].str()[0]);        // 0..15
    const int q   = std::stoi(m[4]);                    // 0..7
    const unsigned long r = std::stoul(m[5]);            // 0..8191 (4-stellig)

    if (h < 0 || mid < 0 || dd < 0 || dd > 15) return working_serial;

    const unsigned long id16 = (static_cast<unsigned long>(q) << 13) | (r & 0x1FFFUL);

    const unsigned long packed = (static_cast<unsigned long>(h)   << 24)
                                | (static_cast<unsigned long>(dd)  << 20)
                                | (static_cast<unsigned long>(mid) << 16)
                                | (id16 & 0xFFFFUL);

    return "ME" + padHex(packed, 7);
}

std::string normalizeM10M20(const std::string& serial, const std::string& type) {
    const std::string upper_type = upperCopy(type);
    std::string upper_serial = upperCopy(serial);

    if (startsWith(upper_serial, "ME")) return upper_serial;

    std::string working_serial = serial;
    for (const char* prefix : {"M10-", "M20-", "M10_", "M20_"}) {
        if (startsWith(upper_serial, prefix)) {
            working_serial = working_serial.substr(std::strlen(prefix));
            upper_serial = upper_serial.substr(std::strlen(prefix));
            break;
        }
    }

    if (upper_type.find("M20") != std::string::npos) {
        return normalizeM20Serial(working_serial);
    }
    if (upper_type.find("M10") != std::string::npos) {
        return normalizeM10Serial(working_serial);
    }
    return serial;
}

std::string normalizeMeisei(const std::string& serial) {
    const auto dash = serial.rfind('-');
    if (dash == std::string::npos || dash + 1 >= serial.size()) return serial;

    const std::string suffix = serial.substr(dash + 1);
    for (unsigned char c : suffix) {
        if (!std::isdigit(c)) {
            return serial;
        }
    }

    unsigned long meisei_id = 0;
    try {
        meisei_id = std::stoul(suffix);
    } catch (const std::exception&) {
        return serial;
    }

    std::string hex_suffix = padHex(meisei_id, 1); // kein Padding, wie Python hex()
    if (hex_suffix.size() > 6) {
        hex_suffix = hex_suffix.substr(hex_suffix.size() - 6);
    }

    return "IMS" + hex_suffix;
}

std::string normalizeSRSC50(const std::string& serial) {
    std::vector<std::string> parts;
    size_t start = 0;
    for (size_t i = 0; i <= serial.size(); ++i) {
        if (i == serial.size() || serial[i] == '-') {
            parts.push_back(serial.substr(start, i - start));
            start = i + 1;
        }
    }

    if (parts.size() < 2) return serial;

    const std::string& segment = parts[1];
    for (unsigned char c : segment) {
        if (!std::isdigit(c)) return serial; 
    }

    unsigned long id_val = 0;
    try {
        id_val = std::stoul(segment);
    } catch (const std::exception&) {
        return serial;
    }

    std::string hex_suffix = padHex(id_val, 4); 
    if (hex_suffix.size() > 4) {
        hex_suffix = hex_suffix.substr(hex_suffix.size() - 4);
    }

    return "C50" + hex_suffix;
}

std::string normalizeImet(const std::string& serial, int frame, const std::string& hhmmss) {
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

    return serial;
}

//---------------------------------------------------------------------------------------------------------------

std::string hhmmssFromIsoUtc(const std::string& iso) {
    // Full ISO-8601 timestamp, e.g. 2026-07-11T21:49:14.000Z
    if (iso.size() >= 19 && iso[10] == 'T' && iso[13] == ':' && iso[16] == ':') {
        return iso.substr(11, 2) + iso.substr(14, 2) + iso.substr(17, 2);
    }

    // imet4iq uses a time-only UTC value, e.g. 21:49:14Z.
    if (iso.size() >= 8 && iso[2] == ':' && iso[5] == ':') {
        const std::string hhmmss = iso.substr(0, 2) + iso.substr(3, 2) + iso.substr(6, 2);
        if (secondsOfDayFromHhmmss(hhmmss) >= 0) return hhmmss;
    }

    return {};
}
}

std::optional<TelemetryFrame> TelemetryParser::parseLine(const std::string& line, double frequency_mhz, const std::string& receiver) {
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
    if (type) f.type = normalizeDFM(*type);

    if (subtype) {
        const std::string upper_type_for_subtype = upperCopy(f.type);
        if (upper_type_for_subtype.find("RS41") != std::string::npos) {
            f.type = normalizeDFM(*subtype);
        }
    }
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

    if (auto v = extractNumber(line, "vel_h")) f.speed_ms = *v;
    if (auto v = extractNumber(line, "heading")) f.heading_deg = *v;
    if (auto v = extractNumber(line, "vel_v")) f.climb_ms = *v;
    if (auto v = extractNumber(line, "temp")) f.temperature_c = *v;
    if (auto v = extractNumber(line, "humidity")) f.humidity_percent = *v;
    if (auto v = extractNumber(line, "pressure")) f.pressure_hpa = *v;
    if (auto v = extractNumber(line, "batt")) f.battery_v = *v;
    if (auto v = extractNumber(line, "bt")) f.burstkilltimer_sec = *v;
    if (auto x = extractString(line, "aux")) f.aux = *x;


    if (auto v = extractNumber(line, "sats")) f.sats = static_cast<int>(*v);
    if (auto v = extractNumber(line, "sat")) f.sats = static_cast<int>(*v);
    if (auto v = extractNumber(line, "frame")) f.frame = static_cast<int>(*v);

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

    f.type = normalizeDFM(f.type);
    const std::string normalized_type_upper = upperCopy(f.type);
    if (normalized_type_upper.find("IMET") != std::string::npos || startsWith(upperCopy(f.serial), "IMET")) {
        f.type = "IMET";
        f.serial = normalizeImet(f.serial, f.frame, f.timestamp_hhmmss);
    } else if (normalized_type_upper.find("M10") != std::string::npos || normalized_type_upper.find("M20") != std::string::npos) {

        if (auto aprsid = extractString(line, "aprsid"); aprsid && !aprsid->empty()) {
            f.serial = *aprsid;
        } else {
            f.serial = normalizeM10M20(f.serial, f.type);
        }
    } else if (normalized_type_upper.find("MEISEI") != std::string::npos
               || normalized_type_upper.find("IMS100") != std::string::npos
               || normalized_type_upper.find("RS11G") != std::string::npos) {
        f.serial = normalizeMeisei(f.serial);
    } else if (normalized_type_upper.find("C50") != std::string::npos) {
        f.serial = normalizeSRSC50(f.serial);
    } else {
        f.serial = normalizeDFMSerial(f.serial, f.type);
    }

    return f;
}

std::optional<double> TelemetryParser::extractNumber(const std::string& text, const std::string& key) {
    std::smatch m;

    std::regex quoted_re("\\\"" + key + "\\\"\\s*[:=]\\s*(-?[0-9]+(?:\\.[0-9]+)?)");
    if (std::regex_search(text, m, quoted_re)) {
        return std::stod(m[1]);
    }

    std::regex plain_re("(^|[^A-Za-z0-9_])" + key + "\\s*[:=]\\s*(-?[0-9]+(?:\\.[0-9]+)?)");
    if (std::regex_search(text, m, plain_re)) {
        return std::stod(m[2]);
    }

    return std::nullopt;
}

std::optional<std::string> TelemetryParser::extractString(const std::string& text, const std::string& key) {
    std::smatch m;

    std::regex quoted_re("\\\"" + key + "\\\"\\s*[:=]\\s*\\\"([^\\\"]*)\\\"");
    if (std::regex_search(text, m, quoted_re)) {
        return m[1];
    }

    std::regex plain_re("(^|[^A-Za-z0-9_])" + key + "\\s*[:=]\\s*([A-Za-z0-9_.-]+)");
    if (std::regex_search(text, m, plain_re)) {
        return m[2];
    }

    return std::nullopt;
}
