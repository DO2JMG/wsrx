#pragma once

#include <cstdint>
#include <string>
#include <vector>

// Encodes a TNC2-style APRS line ("SRC>DST[,DIGI1,DIGI2,...]:info") into a
// raw AX.25 UI frame with trailing FCS (CRC-16/X-25), i.e. the AXUDP wire
// format used by the dxlAPRS toolchain (udpbox, udpgate4, udpflex, and
// aprsmap's "RF port" input) as well as other AXUDP-speaking software
// (soundmodem, direwolf -X, svxlink, ...).
//
// AXUDP == RFC 1226 "IP encapsulation of AX.25 frames": HDLC flags and bit
// stuffing are omitted (the UDP datagram already delimits the frame), but
// the address field, control byte, PID byte, info field and the 16-bit
// CRC-CCITT FCS are all present exactly as on the air.
namespace Ax25 {

// Builds the AXUDP frame bytes for the given TNC2 line. Returns an empty
// vector if the line could not be parsed (missing '>' or ':').
std::vector<uint8_t> encodeAxUdpFrame(const std::string& tnc2_line);

}  // namespace Ax25
