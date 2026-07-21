#pragma once

#include <map>
#include <string>


class JsonObject {
public:
    static bool parse(const std::string& text, JsonObject& out);

    bool has(const std::string& key) const;
    std::string getString(const std::string& key, const std::string& fallback = "") const;
    double getDouble(const std::string& key, double fallback = 0.0) const;
    int getInt(const std::string& key, int fallback = 0) const;

private:
    struct Value {
        bool is_string = false;
        std::string s;
        double d = 0.0;
    };
    std::map<std::string, Value> values_;
};
