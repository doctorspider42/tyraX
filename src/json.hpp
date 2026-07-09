// Minimal JSON parser - just enough for the editor's own project files.
#pragma once

#include <string>
#include <utility>
#include <vector>

namespace json {

struct Value {
    enum class Type { Null, Bool, Number, String, Array, Object };
    Type type = Type::Null;

    bool boolean = false;
    double number = 0.0;
    std::string str;
    std::vector<Value> arr;
    std::vector<std::pair<std::string, Value>> obj;

    // Object member lookup; returns nullptr when absent or not an object.
    const Value* find(const std::string& key) const;

    double numberOr(double fallback) const {
        return type == Type::Number ? number : fallback;
    }
    bool boolOr(bool fallback) const {
        return type == Type::Bool ? boolean : fallback;
    }
    std::string stringOr(const std::string& fallback) const {
        return type == Type::String ? str : fallback;
    }
};

// Parses a complete JSON document. Returns false on malformed input.
bool parse(const std::string& src, Value& out);

}  // namespace json
