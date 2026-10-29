#pragma once

#include <string>
#include <vector>

namespace cromo {

// A JSON value, for the messages of the language server (D170).
class Json {
public:
    enum class Kind { Null, Bool, Number, String, Array, Object };

    Json() = default;
    Json(bool value) : kind_(Kind::Bool), bool_(value) {}
    Json(int value) : kind_(Kind::Number), number_(value) {}
    Json(double value) : kind_(Kind::Number), number_(value) {}
    Json(const char* value) : kind_(Kind::String), string_(value) {}
    Json(std::string value) : kind_(Kind::String), string_(std::move(value)) {}

    static Json array() {
        Json json;
        json.kind_ = Kind::Array;
        return json;
    }
    static Json object() {
        Json json;
        json.kind_ = Kind::Object;
        return json;
    }

    Kind kind() const { return kind_; }
    bool isNull() const { return kind_ == Kind::Null; }
    bool asBool() const { return kind_ == Kind::Bool && bool_; }
    double asNumber() const { return kind_ == Kind::Number ? number_ : 0; }
    int asInt() const { return static_cast<int>(asNumber()); }
    const std::string& asString() const { return string_; }
    const std::vector<Json>& items() const { return items_; }

    // A member of an object; a missing member is null.
    const Json& operator[](const std::string& key) const;
    Json& set(const std::string& key, Json value);
    Json& push(Json value);

    std::string dump() const;

    // Reads a JSON text; on an error it returns false.
    static bool parse(const std::string& text, Json& out);

private:
    void dumpTo(std::string& out) const;

    Kind kind_ = Kind::Null;
    bool bool_ = false;
    double number_ = 0;
    std::string string_;
    std::vector<Json> items_;
    std::vector<std::pair<std::string, Json>> members_;  // in the order they were added
};

}  // namespace cromo
