#pragma once

#include "config.h"
#include <string>

class Logger;

namespace Rs92Ephemeris {

std::string ensure(const Config& cfg, Logger& log);

}  // namespace Rs92Ephemeris
