#pragma once

#include <map>
#include <string>
#include <vector>

class Args {
public:
    static Args parse(int argc, char** argv);

    bool has(const std::string& key) const;
    std::string get(const std::string& key, const std::string& fallback = "") const;
    double getDouble(const std::string& key, double fallback = 0.0) const;
    int getInt(const std::string& key, int fallback = 0) const;

    static void printHelp(const char* program);

private:
    std::map<std::string, std::string> values_;
};

