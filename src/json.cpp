#include "json.hpp"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace json {

const Value* Value::find(const std::string& key) const {
    if (type != Type::Object) return nullptr;
    for (const auto& [k, v] : obj)
        if (k == key) return &v;
    return nullptr;
}

namespace {

// One \uXXXX escape's code unit, or -1 when the four hex digits are not there.
// `p` points at the 'u'.
int hex4(const char* p, const char* end) {
    if (end - p < 5) return -1;
    int v = 0;
    for (int i = 1; i <= 4; ++i) {
        const char c = p[i];
        int d;
        if (c >= '0' && c <= '9') d = c - '0';
        else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
        else return -1;
        v = v * 16 + d;
    }
    return v;
}

// A code point appended as UTF-8 - what the rest of the editor speaks. This
// used to write a literal '?' for every \u escape, which was harmless while the
// only JSON here was our own (we never emit them) and silently mangled every
// accented letter the moment a chat backend answered with escaped text.
void appendUtf8(std::string& out, unsigned cp) {
    if (cp < 0x80) {
        out += (char)cp;
    } else if (cp < 0x800) {
        out += (char)(0xC0 | (cp >> 6));
        out += (char)(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        out += (char)(0xE0 | (cp >> 12));
        out += (char)(0x80 | ((cp >> 6) & 0x3F));
        out += (char)(0x80 | (cp & 0x3F));
    } else {
        out += (char)(0xF0 | (cp >> 18));
        out += (char)(0x80 | ((cp >> 12) & 0x3F));
        out += (char)(0x80 | ((cp >> 6) & 0x3F));
        out += (char)(0x80 | (cp & 0x3F));
    }
}

struct Parser {
    const char* p;
    const char* end;

    void skipWs() {
        while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) ++p;
    }

    bool consume(char c) {
        skipWs();
        if (p < end && *p == c) {
            ++p;
            return true;
        }
        return false;
    }

    bool parseString(std::string& out) {
        skipWs();
        if (p >= end || *p != '"') return false;
        ++p;
        out.clear();
        while (p < end && *p != '"') {
            if (*p == '\\') {
                ++p;
                if (p >= end) return false;
                switch (*p) {
                    case '"': out += '"'; break;
                    case '\\': out += '\\'; break;
                    case '/': out += '/'; break;
                    case 'n': out += '\n'; break;
                    case 't': out += '\t'; break;
                    case 'r': out += '\r'; break;
                    case 'b': out += '\b'; break;
                    case 'f': out += '\f'; break;
                    case 'u': {
                        const int hi = hex4(p, end);
                        if (hi < 0) return false;
                        p += 4;
                        unsigned cp = (unsigned)hi;
                        // A surrogate PAIR is one code point in two escapes;
                        // a lone surrogate is not encodable, so it becomes the
                        // replacement character rather than invalid UTF-8.
                        if (cp >= 0xD800 && cp <= 0xDBFF) {
                            const int lo = (end - p >= 7 && p[1] == '\\' && p[2] == 'u')
                                               ? hex4(p + 2, end)
                                               : -1;
                            if (lo >= 0xDC00 && lo <= 0xDFFF) {
                                cp = 0x10000 + ((cp - 0xD800) << 10) +
                                     ((unsigned)lo - 0xDC00);
                                p += 6;
                            } else {
                                cp = 0xFFFD;
                            }
                        } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
                            cp = 0xFFFD;
                        }
                        appendUtf8(out, cp);
                        break;
                    }
                    default: return false;
                }
                ++p;
            } else {
                out += *p++;
            }
        }
        if (p >= end) return false;
        ++p;  // closing quote
        return true;
    }

    bool parseValue(Value& out) {
        skipWs();
        if (p >= end) return false;

        switch (*p) {
            case '{': {
                ++p;
                out.type = Value::Type::Object;
                skipWs();
                if (consume('}')) return true;
                for (;;) {
                    std::string key;
                    if (!parseString(key)) return false;
                    if (!consume(':')) return false;
                    Value v;
                    if (!parseValue(v)) return false;
                    out.obj.emplace_back(std::move(key), std::move(v));
                    if (consume(',')) continue;
                    return consume('}');
                }
            }
            case '[': {
                ++p;
                out.type = Value::Type::Array;
                skipWs();
                if (consume(']')) return true;
                for (;;) {
                    Value v;
                    if (!parseValue(v)) return false;
                    out.arr.push_back(std::move(v));
                    if (consume(',')) continue;
                    return consume(']');
                }
            }
            case '"':
                out.type = Value::Type::String;
                return parseString(out.str);
            case 't':
                if (end - p >= 4 && strncmp(p, "true", 4) == 0) {
                    out.type = Value::Type::Bool;
                    out.boolean = true;
                    p += 4;
                    return true;
                }
                return false;
            case 'f':
                if (end - p >= 5 && strncmp(p, "false", 5) == 0) {
                    out.type = Value::Type::Bool;
                    out.boolean = false;
                    p += 5;
                    return true;
                }
                return false;
            case 'n':
                if (end - p >= 4 && strncmp(p, "null", 4) == 0) {
                    out.type = Value::Type::Null;
                    p += 4;
                    return true;
                }
                return false;
            default: {
                char* numEnd = nullptr;
                double d = strtod(p, &numEnd);
                if (numEnd == p || numEnd > end) return false;
                out.type = Value::Type::Number;
                out.number = d;
                p = numEnd;
                return true;
            }
        }
    }
};

}  // namespace

bool parse(const std::string& src, Value& out) {
    // A FAILED parse used to leave whatever it had already read in `out`, and
    // the members of a second parse into the same value were APPENDED to them -
    // so find() answered with the abandoned first attempt's copy of a key. That
    // only became visible once something retried (the AI reply repair, which
    // parses, fails, fixes the text and parses again), and it looked exactly
    // like the repair not working.
    out = Value{};
    const char* begin = src.data();
    const char* end = src.data() + src.size();
    // Tolerate a UTF-8 BOM (common when files are edited on Windows)
    if (src.size() >= 3 && (unsigned char)begin[0] == 0xEF && (unsigned char)begin[1] == 0xBB &&
        (unsigned char)begin[2] == 0xBF)
        begin += 3;
    Parser parser{begin, end};
    if (!parser.parseValue(out)) return false;
    parser.skipWs();
    return parser.p == parser.end;
}

std::string escape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (unsigned char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                } else {
                    out += (char)c;
                }
        }
    }
    return out;
}

std::string write(const Value& v) {
    switch (v.type) {
        case Value::Type::Null: return "null";
        case Value::Type::Bool: return v.boolean ? "true" : "false";
        case Value::Type::Number: {
            char buf[40];
            // %.17g round-trips an IEEE double exactly; the trailing ".0" a
            // whole number would get from other formats is not JSON's problem.
            std::snprintf(buf, sizeof(buf), "%.17g", v.number);
            return buf;
        }
        case Value::Type::String: return "\"" + escape(v.str) + "\"";
        case Value::Type::Array: {
            std::string s = "[";
            for (size_t i = 0; i < v.arr.size(); ++i)
                s += (i ? "," : "") + write(v.arr[i]);
            return s + "]";
        }
        case Value::Type::Object: {
            std::string s = "{";
            for (size_t i = 0; i < v.obj.size(); ++i)
                s += std::string(i ? "," : "") + "\"" + escape(v.obj[i].first) +
                     "\":" + write(v.obj[i].second);
            return s + "}";
        }
    }
    return "null";
}

}  // namespace json
