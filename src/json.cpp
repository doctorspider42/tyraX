#include "json.hpp"

#include <cctype>
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
                    case 'u':
                        if (end - p < 5) return false;
                        p += 4;  // non-ASCII escapes not needed for our files
                        out += '?';
                        break;
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

}  // namespace json
