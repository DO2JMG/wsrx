#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace Ax25 {

std::vector<uint8_t> encodeAxUdpFrame(const std::string& tnc2_line);

}  // namespace Ax25
