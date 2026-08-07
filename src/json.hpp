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

// `s` escaped for a JSON string body (no surrounding quotes). Most JSON in this
// editor is built by hand (the project serializer, codegen, the AI prompts), and
// escaping is the one part of that which is easy to get subtly wrong, so it
// exists once.
std::string escape(const std::string& s);

// One value back to compact JSON text. Exists for the case hand-building cannot
// cover: round-tripping a value we did NOT author - the AI Assistant stores a
// model's tool-call arguments with a saved conversation, and it has no schema for
// them. Numbers are written with %.17g so a parse/write/parse cycle is exact.
std::string write(const Value& v);

}  // namespace json
