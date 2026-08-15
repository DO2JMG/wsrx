#include "rs92ephemeris.h"
#include "logger.h"

#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <system_error>
#include <unistd.h>
#include <vector>

namespace {

std::string shellQuote(const std::string& value) {
    std::string out = "'";
    for (char c : value) {
        if (c == '\'') out += "'\\''";
        else out += c;
    }
    out += "'";
    return out;
}

bool fileNonEmpty(const std::string& path) {
    std::error_code ec;
    return std::filesystem::exists(path, ec) && std::filesystem::file_size(path, ec) > 0;
}

struct YearDay {
    int year = 0;   // e.g. 2026
    int yy = 0;     // 2-digit year, e.g. 26
    int doy = 0;    // day of year, 1-366
};

YearDay utcDateMinusDays(int days_back) {
    std::time_t t = std::time(nullptr) - static_cast<std::time_t>(days_back) * 86400;
    std::tm tm{};
    gmtime_r(&t, &tm);
    YearDay yd;
    yd.year = tm.tm_year + 1900;
    yd.yy = yd.year % 100;
    yd.doy = tm.tm_yday + 1;  // tm_yday is 0-based
    return yd;
}

std::string pad(int value, int width) {
    std::ostringstream oss;
    oss << std::setfill('0') << std::setw(width) << value;
    return oss.str();
}

std::string rnxFileName(const YearDay& yd) {
    return "brdc" + pad(yd.doy, 3) + "0." + pad(yd.yy, 2) + "n";
}

bool runQuiet(const std::string& cmd) {
    std::string full = cmd + " >/dev/null 2>&1";
    return std::system(full.c_str()) == 0;
}

bool commandExists(const std::string& name) {
    return runQuiet("command -v " + shellQuote(name));
}

bool runCapture(const std::string& cmd, std::string& output) {
    output.clear();
    char tmpl[] = "/tmp/wsrx-rs92eph-XXXXXX";
    int fd = mkstemp(tmpl);
    if (fd < 0) return std::system((cmd + " >/dev/null 2>&1").c_str()) == 0;
    close(fd);

    std::string full = cmd + " >" + shellQuote(tmpl) + " 2>&1";
    int rc = std::system(full.c_str());

    std::ifstream in(tmpl);
    if (in) {
        std::ostringstream oss;
        oss << in.rdbuf();
        output = oss.str();
        if (output.size() > 300) output = output.substr(0, 300) + "...";
        while (!output.empty() && (output.back() == '\n' || output.back() == '\r')) output.pop_back();
    }
    std::remove(tmpl);
    return rc == 0;
}

bool looksLikeRinex2GpsNav(const std::string& path) {
    std::ifstream in(path);
    if (!in) return false;

    std::string first_line;
    if (!std::getline(in, first_line)) return false;

    if (first_line.find("RINEX VERSION") == std::string::npos) return false;

    std::istringstream iss(first_line);
    std::string version_token;
    iss >> version_token;
    if (version_token.empty() || version_token[0] != '2') return false;

    return true;
}

bool tryDownload(const YearDay& yd, const std::string& rnx_name, const std::string& dest_dir, Logger& log,
                  bool verbose) {
    const std::string yyyy = std::to_string(yd.year);
    const std::string ddd = pad(yd.doy, 3);

    const std::vector<std::string> compressed_names = {rnx_name + ".gz", rnx_name + ".Z"};

    const std::string tmp_decompressed = dest_dir + "/." + rnx_name;
    const std::string dest_path = dest_dir + "/" + rnx_name;

    for (const auto& compressed_name : compressed_names) {
        const std::string url =
            "ftp://anonymous:anonymous@gssc.esa.int/gnss/data/daily/" + yyyy + "/" + ddd + "/" + compressed_name;
        const std::string tmp_compressed = dest_dir + "/." + compressed_name;

        std::filesystem::remove(tmp_compressed);
        std::filesystem::remove(tmp_decompressed);

        std::string curl_output;
        std::string cmd = "curl -fsS -m 20 -o " + shellQuote(tmp_compressed) + " " + shellQuote(url);
        bool curl_ok = runCapture(cmd, curl_output) && fileNonEmpty(tmp_compressed);
        if (!curl_ok) {
            log.warn("RS92 ephemeris: download failed from " + url +
                      (curl_output.empty() ? "" : " (" + curl_output + ")"));
            std::filesystem::remove(tmp_compressed);
            continue;
        }

        bool decompressed = false;
        if (compressed_name.size() > 3 && compressed_name.substr(compressed_name.size() - 3) == ".gz") {
            decompressed = runQuiet("gzip -dc " + shellQuote(tmp_compressed) + " > " + shellQuote(tmp_decompressed)) &&
                            fileNonEmpty(tmp_decompressed);
        }
        if (!decompressed) {
            decompressed = runQuiet("uncompress -f " + shellQuote(tmp_compressed)) && fileNonEmpty(tmp_decompressed);
        }
        if (!decompressed) {
            decompressed = runQuiet("gzip -dc " + shellQuote(tmp_compressed) + " > " + shellQuote(tmp_decompressed)) &&
                            fileNonEmpty(tmp_decompressed);
        }
        if (!decompressed) {
            decompressed = runQuiet("zcat " + shellQuote(tmp_compressed) + " > " + shellQuote(tmp_decompressed)) &&
                            fileNonEmpty(tmp_decompressed);
        }
        std::filesystem::remove(tmp_compressed);

        if (!decompressed) {
            std::filesystem::remove(tmp_decompressed);
            log.warn("RS92 ephemeris: decompression failed for " + url +
                      " (tried gzip, uncompress, zcat -- is one of them installed?)");
            continue;
        }

        if (!looksLikeRinex2GpsNav(tmp_decompressed)) {
            log.warn("RS92 ephemeris: downloaded file from " + url +
                      " does not look like a RINEX-2 GPS nav file, discarding it");
            std::filesystem::remove(tmp_decompressed);
            continue;
        }

        std::error_code ec;
        std::filesystem::rename(tmp_decompressed, dest_path, ec);
        if (ec) {
            // Cross-filesystem fallback.
            std::filesystem::copy_file(tmp_decompressed, dest_path, std::filesystem::copy_options::overwrite_existing, ec);
            std::filesystem::remove(tmp_decompressed);
        }
        if (ec || !fileNonEmpty(dest_path)) {
            if (verbose) log.debug("RS92 ephemeris: could not place downloaded file at " + dest_path);
            continue;
        }

        log.info("RS92 ephemeris: downloaded " + rnx_name + " from " + url);
        return true;
    }
    return false;
}

std::string mostRecentCachedFile(const std::string& dest_dir) {
    std::error_code ec;
    std::string best_path;
    std::filesystem::file_time_type best_time{};
    bool have_best = false;

    if (!std::filesystem::is_directory(dest_dir, ec)) return {};

    for (const auto& entry : std::filesystem::directory_iterator(dest_dir, ec)) {
        if (ec) break;
        if (!entry.is_regular_file()) continue;
        const std::string name = entry.path().filename().string();
        if (name.rfind("brdc", 0) != 0 || name.size() < 2 || name.back() != 'n') continue;
        if (!fileNonEmpty(entry.path().string())) continue;

        std::error_code time_ec;
        auto mtime = std::filesystem::last_write_time(entry.path(), time_ec);
        if (time_ec) continue;
        if (!have_best || mtime > best_time) {
            best_time = mtime;
            best_path = entry.path().string();
            have_best = true;
        }
    }
    return best_path;
}

}  // namespace

namespace Rs92Ephemeris {

std::string ensure(const Config& cfg, Logger& log) {
    if (!cfg.rs92_ephemeris_file.empty()) {
        return cfg.rs92_ephemeris_file;  // manual override, used as-is
    }

    const std::string dir = cfg.rs92_ephemeris_dir;
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    if (ec) {
        log.warn("RS92 ephemeris: could not create cache directory " + dir + ": " + ec.message());
        return {};
    }

    const YearDay today = utcDateMinusDays(0);
    const std::string today_name = rnxFileName(today);
    const std::string today_path = dir + "/" + today_name;

    if (fileNonEmpty(today_path)) {
        return today_path;  // already have today's file, no network needed
    }

    if (!commandExists("curl")) {
        log.warn("RS92 ephemeris: 'curl' is not installed/not in PATH -- cannot download ephemeris data. "
                  "Install curl, or set decoder.rs92_ephemeris_file manually.");
        return mostRecentCachedFile(dir);
    }
    if (!commandExists("uncompress") && !commandExists("gzip") && !commandExists("zcat")) {
        log.warn("RS92 ephemeris: none of 'uncompress', 'gzip', 'zcat' are installed -- cannot decompress "
                  "downloaded ephemeris data. Install one of them, or set decoder.rs92_ephemeris_file manually.");
        return mostRecentCachedFile(dir);
    }

    for (int days_back : {0, 1}) {
        const YearDay yd = utcDateMinusDays(days_back);
        const std::string name = rnxFileName(yd);
        const std::string path = dir + "/" + name;
        if (fileNonEmpty(path)) return path;
        if (tryDownload(yd, name, dir, log, cfg.verbose)) return path;
    }

    const std::string stale = mostRecentCachedFile(dir);
    if (!stale.empty()) {
        log.warn("RS92 ephemeris: download failed, reusing stale cached file " + stale);
        return stale;
    }

    log.warn("RS92 ephemeris: download failed and no cached file is available; RS92 decoding will be skipped");
    return {};
}

}  // namespace Rs92Ephemeris
