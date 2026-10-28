#include "lexer.h"

#include <cstdio>
#include <unordered_map>

namespace cromo {

namespace {

const std::unordered_map<std::string, TokenKind>& keywords() {
    static const std::unordered_map<std::string, TokenKind> table = {
        {"break", TokenKind::KwBreak},       {"case", TokenKind::KwCase},
        {"catch", TokenKind::KwCatch},       {"const", TokenKind::KwConst},
        {"continue", TokenKind::KwContinue}, {"default", TokenKind::KwDefault},
        {"else", TokenKind::KwElse},         {"enum", TokenKind::KwEnum},
        {"extern", TokenKind::KwExtern},     {"false", TokenKind::KwFalse},
        {"finally", TokenKind::KwFinally},   {"for", TokenKind::KwFor},
        {"if", TokenKind::KwIf},             {"import", TokenKind::KwImport},
        {"in", TokenKind::KwIn},             {"interface", TokenKind::KwInterface},
        {"move", TokenKind::KwMove},         {"public", TokenKind::KwPublic},
        {"ref", TokenKind::KwRef},           {"ret", TokenKind::KwRet},
        {"struct", TokenKind::KwStruct},     {"switch", TokenKind::KwSwitch},
        {"throw", TokenKind::KwThrow},       {"true", TokenKind::KwTrue},
        {"try", TokenKind::KwTry},           {"unsafe", TokenKind::KwUnsafe},
        {"void", TokenKind::KwVoid},
    };
    return table;
}

// Tokens after which the statement continues on the next line (D32).
bool continuesOnNextLine(TokenKind kind) {
    switch (kind) {
    case TokenKind::LParen: case TokenKind::LBracket: case TokenKind::Comma: case TokenKind::Dot:
    case TokenKind::Plus: case TokenKind::Minus: case TokenKind::Star: case TokenKind::Slash:
    case TokenKind::Percent: case TokenKind::Assign: case TokenKind::PlusAssign:
    case TokenKind::MinusAssign: case TokenKind::StarAssign: case TokenKind::SlashAssign:
    case TokenKind::PercentAssign: case TokenKind::Equal: case TokenKind::NotEqual:
    case TokenKind::Less: case TokenKind::LessEqual: case TokenKind::Greater:
    case TokenKind::GreaterEqual: case TokenKind::AndAnd: case TokenKind::OrOr: case TokenKind::Not:
        return true;
    default:
        return false;
    }
}

bool isDigit(char c) { return c >= '0' && c <= '9'; }
bool isIdentStart(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_'; }
bool isIdentChar(char c) { return isIdentStart(c) || isDigit(c); }

int hexValue(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

void appendUtf8(std::string& out, uint32_t cp) {
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

class Lexer {
public:
    Lexer(const SourceFile& file, int fileIndex, Diagnostics& diag) : src_(file.text), file_(fileIndex), diag_(diag) {}

    std::vector<Token> run() {
        while (pos_ < src_.size()) {
            char c = src_[pos_];
            if (c == ' ' || c == '\t' || c == '\r') {
                advance();
            } else if (c == '\n') {
                SourceLocation loc = here();
                advance();
                newline(loc);
            } else if (c == '/' && peek(1) == '/') {
                while (pos_ < src_.size() && src_[pos_] != '\n') advance();
            } else if (c == '/' && peek(1) == '*') {
                blockComment();
            } else if (isIdentStart(c)) {
                identifier();
            } else if (isDigit(c)) {
                number();
            } else if (c == '"') {
                string();
            } else if (c == '\'') {
                character();
            } else {
                punctuation();
            }
        }
        SourceLocation end = here();
        newline(end);
        tokens_.push_back(Token{TokenKind::EndOfFile, {}, end, 0});
        return std::move(tokens_);
    }

private:
    char peek(size_t ahead = 0) const {
        return pos_ + ahead < src_.size() ? src_[pos_ + ahead] : '\0';
    }

    SourceLocation here() const { return SourceLocation{line_, column_, file_}; }

    void advance() {
        char c = src_[pos_++];
        if (c == '\n') {
            ++line_;
            column_ = 1;
        } else if ((static_cast<unsigned char>(c) & 0xC0) != 0x80) {
            ++column_;  // UTF-8 continuation bytes do not advance the column
        }
    }

    void push(TokenKind kind, SourceLocation loc, std::string text = {}, uint64_t value = 0) {
        tokens_.push_back(Token{kind, std::move(text), loc, value});
        if (kind == TokenKind::LParen || kind == TokenKind::LBracket) ++depth_;
        if ((kind == TokenKind::RParen || kind == TokenKind::RBracket) && depth_ > 0) --depth_;
    }

    // A newline ends the statement, except inside parentheses or brackets, after
    // a token that asks to continue, or when there is nothing to end.
    void newline(SourceLocation loc) {
        if (depth_ > 0 || tokens_.empty()) return;
        TokenKind last = tokens_.back().kind;
        if (last == TokenKind::Newline || continuesOnNextLine(last)) return;
        tokens_.push_back(Token{TokenKind::Newline, {}, loc, 0});
    }

    void blockComment() {
        SourceLocation start = here();
        advance();
        advance();
        bool sawNewline = false;
        while (pos_ < src_.size() && !(src_[pos_] == '*' && peek(1) == '/')) {
            if (src_[pos_] == '\n') sawNewline = true;
            advance();
        }
        if (pos_ >= src_.size()) {
            diag_.error(start, "unterminated /* comment");
            return;
        }
        advance();
        advance();
        if (sawNewline) newline(here());  // a comment spanning lines counts as a newline
    }

    void identifier() {
        SourceLocation loc = here();
        size_t start = pos_;
        while (pos_ < src_.size() && isIdentChar(src_[pos_])) advance();
        std::string word = src_.substr(start, pos_ - start);
        auto it = keywords().find(word);
        push(it != keywords().end() ? it->second : TokenKind::Identifier, loc, std::move(word));
    }

    void number() {
        SourceLocation loc = here();
        size_t start = pos_;
        int base = 10;
        if (src_[pos_] == '0' && (peek(1) == 'x' || peek(1) == 'X')) base = 16;
        if (src_[pos_] == '0' && (peek(1) == 'b' || peek(1) == 'B')) base = 2;
        if (base != 10) {
            advance();
            advance();
        }

        uint64_t value = 0;
        bool overflow = false;
        bool anyDigit = false;
        bool isFloat = false;
        for (;;) {
            char c = peek();
            if (c == '_') {
                advance();
                continue;
            }
            int digit = base == 16 ? hexValue(c) : (isDigit(c) ? c - '0' : -1);
            if (digit < 0 || digit >= base) break;
            if (value > (UINT64_MAX - static_cast<uint64_t>(digit)) / static_cast<uint64_t>(base)) overflow = true;
            value = value * static_cast<uint64_t>(base) + static_cast<uint64_t>(digit);
            anyDigit = true;
            advance();
        }

        if (base == 10 && peek() == '.' && isDigit(peek(1))) {
            isFloat = true;
            advance();
            while (isDigit(peek()) || peek() == '_') advance();
        }
        if (base == 10 && (peek() == 'e' || peek() == 'E') &&
            (isDigit(peek(1)) || ((peek(1) == '+' || peek(1) == '-') && isDigit(peek(2))))) {
            isFloat = true;
            advance();
            if (peek() == '+' || peek() == '-') advance();
            while (isDigit(peek())) advance();
        }

        std::string text = src_.substr(start, pos_ - start);
        if (!anyDigit) {
            diag_.error(loc, "numeric literal without digits: '" + text + "'");
        } else if (isIdentChar(peek())) {
            diag_.error(here(), "invalid character after the number '" + text + "'");
            while (isIdentChar(peek())) advance();
        } else if (overflow && !isFloat) {
            diag_.error(loc, "integer literal too large: '" + text + "' does not fit in 64 bits");
        }
        push(isFloat ? TokenKind::FloatLiteral : TokenKind::IntegerLiteral, loc, std::move(text), value);
    }

    // Reads an escape sequence after the backslash. Returns the code point, or -1
    // if the sequence is invalid (the error has already been reported).
    long escape(bool allowUnicode) {
        SourceLocation loc = here();
        advance();  // the backslash
        char c = peek();
        if (pos_ < src_.size()) advance();
        switch (c) {
        case 'n': return '\n';
        case 't': return '\t';
        case 'r': return '\r';
        case '0': return 0;
        case '\\': return '\\';
        case '\'': return '\'';
        case '"': return '"';
        case 'x': {
            int hi = hexValue(peek());
            int lo = hexValue(peek(1));
            if (hi < 0 || lo < 0) {
                diag_.error(loc, "invalid \\x escape: two hexadecimal digits are required");
                return -1;
            }
            advance();
            advance();
            return hi * 16 + lo;
        }
        case 'u': {
            if (!allowUnicode) {
                diag_.error(loc, "\\u{...} is not allowed in a character literal, which is one byte");
                return -1;
            }
            if (peek() != '{') {
                diag_.error(loc, "invalid \\u escape: write \\u{code}");
                return -1;
            }
            advance();
            long value = 0;
            int digits = 0;
            while (hexValue(peek()) >= 0 && digits < 6) {
                value = value * 16 + hexValue(peek());
                advance();
                ++digits;
            }
            if (digits == 0 || peek() != '}' || value > 0x10FFFF || (value >= 0xD800 && value <= 0xDFFF)) {
                diag_.error(loc, "invalid \\u{...} escape");
                return -1;
            }
            advance();
            return value;
        }
        default:
            diag_.error(loc, std::string("unknown escape sequence: \\") + c);
            return -1;
        }
    }

    void string() {
        SourceLocation loc = here();
        advance();
        std::string value;
        while (pos_ < src_.size() && src_[pos_] != '"' && src_[pos_] != '\n') {
            if (src_[pos_] == '\\') {
                long cp = escape(true);
                if (cp >= 0) appendUtf8(value, static_cast<uint32_t>(cp));
            } else {
                value += src_[pos_];
                advance();
            }
        }
        if (peek() != '"') {
            diag_.error(loc, "unterminated string");
        } else {
            advance();
        }
        push(TokenKind::StringLiteral, loc, std::move(value));
    }

    // D46: a character literal is one byte, so only ASCII characters are allowed.
    void character() {
        SourceLocation loc = here();
        advance();
        long value = -1;
        unsigned char c = static_cast<unsigned char>(peek());
        if (c == '\'' || c == '\n' || pos_ >= src_.size()) {
            diag_.error(loc, "empty character literal");
        } else if (c == '\\') {
            value = escape(false);
        } else if (c >= 0x80) {
            size_t start = pos_;
            uint32_t cp = 0;
            int extra = c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : 1;
            cp = c & (0x3F >> extra);
            advance();
            for (int i = 0; i < extra && (static_cast<unsigned char>(peek()) & 0xC0) == 0x80; ++i) {
                cp = (cp << 6) | (static_cast<unsigned char>(peek()) & 0x3F);
                advance();
            }
            std::string glyph = src_.substr(start, pos_ - start);
            char hex[16];
            std::snprintf(hex, sizeof hex, "0x%X", cp);
            diag_.error(loc, "the character '" + glyph + "' does not fit in one byte: use the string \"" + glyph +
                                 "\" or the code point " + hex);
        } else {
            value = c;
            advance();
        }
        if (peek() != '\'') {
            diag_.error(loc, "unterminated character literal, or more than one character");
            while (pos_ < src_.size() && src_[pos_] != '\'' && src_[pos_] != '\n') advance();
        }
        if (peek() == '\'') advance();
        push(TokenKind::CharLiteral, loc, {}, value < 0 ? 0 : static_cast<uint64_t>(value));
    }

    void punctuation() {
        SourceLocation loc = here();
        char c = src_[pos_];
        char next = peek(1);
        auto one = [&](TokenKind kind) {
            advance();
            push(kind, loc);
        };
        auto two = [&](TokenKind kind) {
            advance();
            advance();
            push(kind, loc);
        };
        switch (c) {
        case '(': one(TokenKind::LParen); return;
        case ')': one(TokenKind::RParen); return;
        case '{': one(TokenKind::LBrace); return;
        case '}': one(TokenKind::RBrace); return;
        case '[': one(TokenKind::LBracket); return;
        case ']': one(TokenKind::RBracket); return;
        case ',': one(TokenKind::Comma); return;
        case '.': one(TokenKind::Dot); return;
        case ':': one(TokenKind::Colon); return;
        case ';': one(TokenKind::Semicolon); return;
        case '~': one(TokenKind::Tilde); return;
        case '+':
            if (next == '+') two(TokenKind::PlusPlus);
            else if (next == '=') two(TokenKind::PlusAssign);
            else one(TokenKind::Plus);
            return;
        case '-':
            if (next == '-') two(TokenKind::MinusMinus);
            else if (next == '=') two(TokenKind::MinusAssign);
            else one(TokenKind::Minus);
            return;
        case '*': next == '=' ? two(TokenKind::StarAssign) : one(TokenKind::Star); return;
        case '/': next == '=' ? two(TokenKind::SlashAssign) : one(TokenKind::Slash); return;
        case '%': next == '=' ? two(TokenKind::PercentAssign) : one(TokenKind::Percent); return;
        case '=': next == '=' ? two(TokenKind::Equal) : one(TokenKind::Assign); return;
        case '!': next == '=' ? two(TokenKind::NotEqual) : one(TokenKind::Not); return;
        case '<': next == '=' ? two(TokenKind::LessEqual) : one(TokenKind::Less); return;
        case '>': next == '=' ? two(TokenKind::GreaterEqual) : one(TokenKind::Greater); return;
        case '&':
            if (next == '&') {
                two(TokenKind::AndAnd);
                return;
            }
            break;
        case '|':
            if (next == '|') {
                two(TokenKind::OrOr);
                return;
            }
            break;
        default:
            break;
        }

        // Unrecognized character: skip all of it, even if it is UTF-8.
        size_t start = pos_;
        advance();
        while ((static_cast<unsigned char>(peek()) & 0xC0) == 0x80) advance();
        diag_.error(loc, "invalid character: '" + src_.substr(start, pos_ - start) + "'");
    }

    const std::string& src_;
    int file_;  // index of the file, for the locations
    Diagnostics& diag_;
    std::vector<Token> tokens_;
    size_t pos_ = 0;
    int line_ = 1;
    int column_ = 1;
    int depth_ = 0;  // open parentheses and brackets
};

}  // namespace

std::string describe(TokenKind kind) {
    switch (kind) {
    case TokenKind::EndOfFile: return "end of file";
    case TokenKind::Newline: return "a newline";
    case TokenKind::Identifier: return "a name";
    case TokenKind::IntegerLiteral: return "an integer";
    case TokenKind::FloatLiteral: return "a decimal number";
    case TokenKind::StringLiteral: return "a string";
    case TokenKind::CharLiteral: return "a character";
    case TokenKind::LParen: return "'('";
    case TokenKind::RParen: return "')'";
    case TokenKind::LBrace: return "'{'";
    case TokenKind::RBrace: return "'}'";
    case TokenKind::LBracket: return "'['";
    case TokenKind::RBracket: return "']'";
    case TokenKind::Comma: return "','";
    case TokenKind::Dot: return "'.'";
    case TokenKind::Colon: return "':'";
    case TokenKind::Semicolon: return "';'";
    case TokenKind::Tilde: return "'~'";
    case TokenKind::Plus: return "'+'";
    case TokenKind::Minus: return "'-'";
    case TokenKind::Star: return "'*'";
    case TokenKind::Slash: return "'/'";
    case TokenKind::Percent: return "'%'";
    case TokenKind::PlusPlus: return "'++'";
    case TokenKind::MinusMinus: return "'--'";
    case TokenKind::Assign: return "'='";
    case TokenKind::PlusAssign: return "'+='";
    case TokenKind::MinusAssign: return "'-='";
    case TokenKind::StarAssign: return "'*='";
    case TokenKind::SlashAssign: return "'/='";
    case TokenKind::PercentAssign: return "'%='";
    case TokenKind::Equal: return "'=='";
    case TokenKind::NotEqual: return "'!='";
    case TokenKind::Less: return "'<'";
    case TokenKind::LessEqual: return "'<='";
    case TokenKind::Greater: return "'>'";
    case TokenKind::GreaterEqual: return "'>='";
    case TokenKind::AndAnd: return "'&&'";
    case TokenKind::OrOr: return "'||'";
    case TokenKind::Not: return "'!'";
    default:
        for (const auto& [word, k] : keywords()) {
            if (k == kind) return "'" + word + "'";
        }
        return "a token";
    }
}

std::vector<Token> tokenize(const SourceFile& file, int fileIndex, Diagnostics& diag) {
    return Lexer(file, fileIndex, diag).run();
}

}  // namespace cromo
