#include "jsonlite.h"

#include <cstdlib>

namespace {

void skipWs(const std::string& s, size_t& i) {
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r')) ++i;
}

bool parseString(const std::string& s, size_t& i, std::string& out) {
    if (i >= s.size() || s[i] != '"') return false;
    ++i;
    out.clear();
    while (i < s.size() && s[i] != '"') {
        char c = s[i];
        if (c == '\\' && i + 1 < s.size()) {
            char n = s[i + 1];
            switch (n) {
                case '"': out += '"'; break;
                case '\\': out += '\\'; break;
                case '/': out += '/'; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case 'n': out += '\n'; break;
                case 'r': out += '\r'; break;
                case 't': out += '\t'; break;
                case 'u':
                    // Skip \uXXXX, not needed for our flat ASCII schema.
                    if (i + 5 < s.size()) i += 4;
                    break;
                default: out += n; break;
            }
            i += 2;
        } else {
            out += c;
            ++i;
        }
    }
    if (i >= s.size() || s[i] != '"') return false;
    ++i;  // closing quote
    return true;
}

bool parseNumber(const std::string& s, size_t& i, double& out) {
    size_t start = i;
    if (i < s.size() && (s[i] == '-' || s[i] == '+')) ++i;
    bool any_digit = false;
    while (i < s.size() && (isdigit(static_cast<unsigned char>(s[i])) || s[i] == '.' || s[i] == 'e' || s[i] == 'E' ||
                             s[i] == '+' || s[i] == '-')) {
        if (isdigit(static_cast<unsigned char>(s[i]))) any_digit = true;
        ++i;
    }
    if (!any_digit) return false;
    out = std::strtod(s.substr(start, i - start).c_str(), nullptr);
    return true;
}

}  // namespace

bool JsonObject::parse(const std::string& text, JsonObject& out) {
    out.values_.clear();
    size_t i = 0;
    skipWs(text, i);
    if (i >= text.size() || text[i] != '{') return false;
    ++i;
    skipWs(text, i);

    if (i < text.size() && text[i] == '}') return true;  // empty object

    while (true) {
        skipWs(text, i);
        std::string key;
        if (!parseString(text, i, key)) return false;
        skipWs(text, i);
        if (i >= text.size() || text[i] != ':') return false;
        ++i;
        skipWs(text, i);

        Value v;
        if (i < text.size() && text[i] == '"') {
            std::string s;
            if (!parseString(text, i, s)) return false;
            v.is_string = true;
            v.s = s;
        } else if (i < text.size() && (text[i] == 't' || text[i] == 'f' || text[i] == 'n')) {
            // true / false / null - skip token, not used in our schema.
            while (i < text.size() && isalpha(static_cast<unsigned char>(text[i]))) ++i;
            v.is_string = false;
            v.d = 0.0;
        } else {
            double d = 0.0;
            if (!parseNumber(text, i, d)) return false;
            v.is_string = false;
            v.d = d;
        }
        out.values_[key] = v;

        skipWs(text, i);
        if (i < text.size() && text[i] == ',') {
            ++i;
            continue;
        }
        break;
    }

    skipWs(text, i);
    if (i >= text.size() || text[i] != '}') return false;
    return true;
}

bool JsonObject::has(const std::string& key) const {
    return values_.find(key) != values_.end();
}

std::string JsonObject::getString(const std::string& key, const std::string& fallback) const {
    auto it = values_.find(key);
    if (it == values_.end()) return fallback;
    if (it->second.is_string) return it->second.s;
    // Numbers requested as strings (shouldn't normally happen for our keys).
    char buf[64];
    snprintf(buf, sizeof(buf), "%g", it->second.d);
    return buf;
}

double JsonObject::getDouble(const std::string& key, double fallback) const {
    auto it = values_.find(key);
    if (it == values_.end()) return fallback;
    if (!it->second.is_string) return it->second.d;
    return std::strtod(it->second.s.c_str(), nullptr);
}

int JsonObject::getInt(const std::string& key, int fallback) const {
    return static_cast<int>(getDouble(key, static_cast<double>(fallback)));
}
