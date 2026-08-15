#include "ax25frame.h"

#include <cctype>

namespace {

void encodeAddress(const std::string& callsign_ssid, bool last, bool c_bit, uint8_t out[7]) {
    std::string base = callsign_ssid;
    int ssid = 0;

    size_t dash = callsign_ssid.find('-');
    if (dash != std::string::npos) {
        base = callsign_ssid.substr(0, dash);
        ssid = std::atoi(callsign_ssid.c_str() + dash + 1);
        if (ssid < 0) ssid = 0;
        if (ssid > 15) ssid = 15;
    }
    if (base.size() > 6) base = base.substr(0, 6);

    for (int i = 0; i < 6; ++i) {
        char c = i < static_cast<int>(base.size()) ? base[i] : ' ';
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        out[i] = static_cast<uint8_t>(c << 1);
    }

  
    uint8_t b = 0x60;  
    b |= static_cast<uint8_t>((ssid & 0x0F) << 1);
    if (last) b |= 0x01;
    if (c_bit) b |= 0x80;
    out[6] = b;
}

uint16_t computeFcs(const std::vector<uint8_t>& data) {
    uint16_t fcs = 0xFFFF;
    for (uint8_t byte : data) {
        for (int bit = 0; bit < 8; ++bit) {
            bool toggle = ((fcs ^ byte) & 0x01) != 0;
            fcs >>= 1;
            if (toggle) fcs ^= 0x8408;
            byte >>= 1;
        }
    }
    return static_cast<uint16_t>(~fcs);
}

bool parseTnc2(const std::string& line, std::string& source, std::string& dest, std::vector<std::string>& path,
               std::string& info) {
    size_t gt = line.find('>');
    if (gt == std::string::npos) return false;
    source = line.substr(0, gt);

    size_t colon = line.find(':', gt);
    if (colon == std::string::npos) return false;

    std::string dest_and_path = line.substr(gt + 1, colon - gt - 1);
    size_t comma = dest_and_path.find(',');
    if (comma == std::string::npos) {
        dest = dest_and_path;
    } else {
        dest = dest_and_path.substr(0, comma);
        size_t pos = comma + 1;
        while (pos < dest_and_path.size()) {
            size_t next = dest_and_path.find(',', pos);
            if (next == std::string::npos) {
                path.push_back(dest_and_path.substr(pos));
                break;
            }
            path.push_back(dest_and_path.substr(pos, next - pos));
            pos = next + 1;
        }
    }

    info = line.substr(colon + 1);
    return !source.empty() && !dest.empty();
}

}  // namespace

namespace Ax25 {

std::vector<uint8_t> encodeAxUdpFrame(const std::string& tnc2_line) {
    std::string source, dest, info;
    std::vector<std::string> path;
    if (!parseTnc2(tnc2_line, source, dest, path, info)) return {};

    std::vector<uint8_t> frame;
    frame.reserve(7 * (2 + path.size()) + 2 + info.size() + 2);

    const bool no_digis = path.empty();

    uint8_t addr[7];
    encodeAddress(dest, no_digis, /*c_bit=*/true, addr);
    frame.insert(frame.end(), addr, addr + 7);

    encodeAddress(source, no_digis, /*c_bit=*/false, addr);
    frame.insert(frame.end(), addr, addr + 7);

    for (size_t i = 0; i < path.size(); ++i) {
        bool last = (i + 1 == path.size());
        encodeAddress(path[i], last, /*c_bit=*/false, addr);
        frame.insert(frame.end(), addr, addr + 7);
    }

    frame.push_back(0x03);  
    frame.push_back(0xF0); 

    for (char c : info) frame.push_back(static_cast<uint8_t>(c));

    uint16_t fcs = computeFcs(frame);
    frame.push_back(static_cast<uint8_t>(fcs & 0xFF));         
    frame.push_back(static_cast<uint8_t>((fcs >> 8) & 0xFF)); 

    return frame;
}

}  // namespace Ax25
