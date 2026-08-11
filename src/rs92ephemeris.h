#pragma once

#include "config.h"
#include <string>

class Logger;

// Provides rs92mod with a current RINEX-2 GPS navigation (broadcast
// ephemeris) file for its "-e <file>" option, downloading one on demand
// instead of requiring the user to fetch and maintain it by hand.
namespace Rs92Ephemeris {

// Returns a local file path to pass to rs92mod's "-e" option, or an empty
// string if none could be obtained.
//
// Behaviour:
//  - If cfg.rs92_ephemeris_file is set, it is returned unchanged (manual
//    override, no network access, no caching).
//  - Otherwise: a RINEX-2 "brdcDDD0.YYn" file for the current UTC day is
//    looked up in cfg.rs92_ephemeris_dir. If missing, it is downloaded
//    (ESA anonymous FTP first, BKG HTTPS mirror as fallback; the previous
//    UTC day is tried too since the current day's combined file may not
//    be published yet). Once downloaded, the file is reused for the rest
//    of the UTC day without any further network access.
//  - If a fresh download fails but an older cached file from a previous
//    day still exists, that stale file is reused (logged as a warning) --
//    reduced accuracy is preferable to not decoding at all.
std::string ensure(const Config& cfg, Logger& log);

}  // namespace Rs92Ephemeris
