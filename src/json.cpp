#include "json.h"

#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

namespace cromo {

namespace {

const Json kNull;

class Parser {
public:
    explicit Parser(const std::string& text) : text_(text) {}

    bool run(Json& out) {
        if (!value(out)) return false;
        space();
        return pos_ == text_.size();
    }

private:
    void space() {
        while (pos_ < text_.size() && (text_[pos_] == ' ' || text_[pos_] == '\t' || text_[pos_] == '\n' || text_[pos_] == '\r')) {
            ++pos_;
        }
    }

    bool literal(const char* word) {
        size_t length = std::char_traits<char>::length(word);
        if (text_.compare(pos_, length, word) != 0) return false;
        pos_ += length;
        return true;
    }

    bool value(Json& out) {
        if (++depth_ > 200) return false;
        space();
        if (pos_ >= text_.size()) return false;
        char c = text_[pos_];
        bool ok = false;
        if (c == '{') {
            ok = object(out);
        } else if (c == '[') {
            ok = array(out);
        } else if (c == '"') {
            std::string text;
            ok = string(text);
            out = Json(std::move(text));
        } else if (literal("true")) {
            out = Json(true), ok = true;
        } else if (literal("false")) {
            out = Json(false), ok = true;
        } else if (literal("null")) {
            out = Json(), ok = true;
        } else {
            ok = number(out);
        }
        --depth_;
        return ok;
    }

    bool object(Json& out) {
        out = Json::object();
        ++pos_;
        space();
        if (pos_ < text_.size() && text_[pos_] == '}') {
            ++pos_;
            return true;
        }
        for (;;) {
            space();
            std::string key;
            if (pos_ >= text_.size() || text_[pos_] != '"' || !string(key)) return false;
            space();
            if (pos_ >= text_.size() || text_[pos_++] != ':') return false;
            Json item;
            if (!value(item)) return false;
            out.set(key, std::move(item));
            space();
            if (pos_ >= text_.size()) return false;
            char c = text_[pos_++];
            if (c == '}') return true;
            if (c != ',') return false;
        }
    }

    bool array(Json& out) {
        out = Json::array();
        ++pos_;
        space();
        if (pos_ < text_.size() && text_[pos_] == ']') {
            ++pos_;
            return true;
        }
        for (;;) {
            Json item;
            if (!value(item)) return false;
            out.push(std::move(item));
            space();
            if (pos_ >= text_.size()) return false;
            char c = text_[pos_++];
            if (c == ']') return true;
            if (c != ',') return false;
        }
    }

    bool hex4(unsigned& out) {
        if (pos_ + 4 > text_.size()) return false;
        out = 0;
        for (int i = 0; i < 4; ++i) {
            char c = text_[pos_++];
            out <<= 4;
            if (c >= '0' && c <= '9') out |= static_cast<unsigned>(c - '0');
            else if (c >= 'a' && c <= 'f') out |= static_cast<unsigned>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') out |= static_cast<unsigned>(c - 'A' + 10);
            else return false;
        }
        return true;
    }

    static void appendUtf8(std::string& out, unsigned cp) {
        if (cp < 0x80) {
            out += static_cast<char>(cp);
        } else if (cp < 0x800) {
            out += static_cast<char>(0xC0 | (cp >> 6));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        } else if (cp < 0x10000) {
            out += static_cast<char>(0xE0 | (cp >> 12));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        } else {
            out += static_cast<char>(0xF0 | (cp >> 18));
            out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        }
    }

    bool string(std::string& out) {
        ++pos_;  // '"'
        while (pos_ < text_.size()) {
            char c = text_[pos_++];
            if (c == '"') return true;
            if (c != '\\') {
                out += c;
                continue;
            }
            if (pos_ >= text_.size()) return false;
            char e = text_[pos_++];
            switch (e) {
            case '"': out += '"'; break;
            case '\\': out += '\\'; break;
            case '/': out += '/'; break;
            case 'b': out += '\b'; break;
            case 'f': out += '\f'; break;
            case 'n': out += '\n'; break;
            case 'r': out += '\r'; break;
            case 't': out += '\t'; break;
            case 'u': {
                unsigned cp = 0;
                if (!hex4(cp)) return false;
                if (cp >= 0xD800 && cp < 0xDC00 && text_.compare(pos_, 2, "\\u") == 0) {  // a surrogate pair
                    pos_ += 2;
                    unsigned low = 0;
                    if (!hex4(low)) return false;
                    cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
                }
                appendUtf8(out, cp);
                break;
            }
            default: return false;
            }
        }
        return false;
    }

    bool number(Json& out) {
        size_t start = pos_;
        while (pos_ < text_.size() && (std::isdigit(static_cast<unsigned char>(text_[pos_])) || text_[pos_] == '-' ||
                                       text_[pos_] == '+' || text_[pos_] == '.' || text_[pos_] == 'e' || text_[pos_] == 'E')) {
            ++pos_;
        }
        if (start == pos_) return false;
        out = Json(std::strtod(text_.substr(start, pos_ - start).c_str(), nullptr));
        return true;
    }

    const std::string& text_;
    size_t pos_ = 0;
    int depth_ = 0;
};

void dumpString(const std::string& text, std::string& out) {
    out += '"';
    for (unsigned char c : text) {
        switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (c < 0x20) {
                char buffer[8];
                std::snprintf(buffer, sizeof buffer, "\\u%04x", c);
                out += buffer;
            } else {
                out += static_cast<char>(c);  // UTF-8 goes as it is
            }
        }
    }
    out += '"';
}

}  // namespace

const Json& Json::operator[](const std::string& key) const {
    for (const auto& [name, value] : members_) {
        if (name == key) return value;
    }
    return kNull;
}

Json& Json::set(const std::string& key, Json value) {
    kind_ = Kind::Object;
    for (auto& [name, existing] : members_) {
        if (name == key) {
            existing = std::move(value);
            return *this;
        }
    }
    members_.emplace_back(key, std::move(value));
    return *this;
}

Json& Json::push(Json value) {
    kind_ = Kind::Array;
    items_.push_back(std::move(value));
    return *this;
}

std::string Json::dump() const {
    std::string out;
    dumpTo(out);
    return out;
}

void Json::dumpTo(std::string& out) const {
    switch (kind_) {
    case Kind::Null: out += "null"; break;
    case Kind::Bool: out += bool_ ? "true" : "false"; break;
    case Kind::Number: {
        char buffer[32];
        if (std::floor(number_) == number_ && std::fabs(number_) < 1e15) {
            std::snprintf(buffer, sizeof buffer, "%lld", static_cast<long long>(number_));
        } else {
            std::snprintf(buffer, sizeof buffer, "%.17g", number_);
        }
        out += buffer;
        break;
    }
    case Kind::String: dumpString(string_, out); break;
    case Kind::Array:
        out += '[';
        for (size_t i = 0; i < items_.size(); ++i) {
            if (i) out += ',';
            items_[i].dumpTo(out);
        }
        out += ']';
        break;
    case Kind::Object:
        out += '{';
        for (size_t i = 0; i < members_.size(); ++i) {
            if (i) out += ',';
            dumpString(members_[i].first, out);
            out += ':';
            members_[i].second.dumpTo(out);
        }
        out += '}';
        break;
    }
}

bool Json::parse(const std::string& text, Json& out) { return Parser(text).run(out); }

}  // namespace cromo
