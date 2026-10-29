#include "format.h"

#include "lexer.h"
#include "parser.h"

#include <algorithm>
#include <set>
#include <vector>

namespace cromo {

namespace {

// A line of the source: its tokens, comments included, and whether a blank line
// came before it.
struct Line {
    std::vector<const Token*> tokens;
    bool blankBefore = false;
};

// A line of the result. A comment at the end of a line of code stays apart, to
// be aligned with the ones of the lines around it (D168).
struct OutputLine {
    std::string code;
    std::string comment;
    bool blankBefore = false;
    bool opens = false;         // it ends with `{`
    bool continuation = false;  // only a comment, under the comment at the end of the line before
};

bool isComment(const Token* token) { return token->kind == TokenKind::Comment; }

bool isLineComment(const Token* token) { return isComment(token) && token->text.compare(0, 2, "//") == 0; }

bool hasComment(const Line& line) { return std::any_of(line.tokens.begin(), line.tokens.end(), isComment); }

// The last token of code of a line, or null if it only has comments.
const Token* lastCode(const Line& line) {
    for (auto it = line.tokens.rbegin(); it != line.tokens.rend(); ++it) {
        if (!isComment(*it)) return *it;
    }
    return nullptr;
}

// A token after which a `-` or `+` is binary.
bool endsOperand(TokenKind kind) {
    switch (kind) {
    case TokenKind::Identifier: case TokenKind::IntegerLiteral: case TokenKind::FloatLiteral:
    case TokenKind::StringLiteral: case TokenKind::CharLiteral: case TokenKind::RParen:
    case TokenKind::RBracket: case TokenKind::KwTrue: case TokenKind::KwFalse:
        return true;
    default:
        return false;
    }
}

std::vector<Line> splitLines(const std::string& source, const std::vector<Token>& tokens) {
    std::vector<Line> lines;
    size_t previousEnd = 0;
    for (const Token& token : tokens) {
        if (token.kind == TokenKind::Newline || token.kind == TokenKind::EndOfFile) continue;
        auto breaks = std::count(source.begin() + static_cast<std::ptrdiff_t>(previousEnd),
                                 source.begin() + static_cast<std::ptrdiff_t>(token.begin), '\n');
        if (lines.empty() || breaks > 0) {
            lines.push_back(Line{});
            lines.back().blankBefore = breaks > 1;
        }
        lines.back().tokens.push_back(&token);
        previousEnd = token.end;
    }
    return lines;
}

// D162: `{`, and `else`, `catch` and `finally` after a `}`, go to the end of the
// line of code before. A comment at the end of that line stays at the end; if
// both lines have comments, or the line before has only comments, nothing
// moves (D168).
void joinBraces(std::vector<Line>& lines) {
    std::vector<Line> joined;
    for (Line& line : lines) {
        if (!joined.empty()) {
            Line& previous = joined.back();
            TokenKind first = line.tokens[0]->kind;
            const Token* last = lastCode(previous);
            bool follows = first == TokenKind::LBrace ||
                           ((first == TokenKind::KwElse || first == TokenKind::KwCatch || first == TokenKind::KwFinally) &&
                            last && last->kind == TokenKind::RBrace);
            bool previousComment = isComment(previous.tokens.back());
            if (last && follows && !(previousComment && hasComment(line))) {
                std::vector<const Token*> tokens(previous.tokens.begin(), previous.tokens.end() - (previousComment ? 1 : 0));
                tokens.insert(tokens.end(), line.tokens.begin(), line.tokens.end());
                if (previousComment) tokens.push_back(previous.tokens.back());
                previous.tokens = std::move(tokens);
                continue;
            }
        }
        joined.push_back(std::move(line));
    }
    lines = std::move(joined);
}

// Whether a space goes between two tokens of the same line (D166).
bool spaceBetween(const Token& a, const Token& b, bool aUnary, const std::set<size_t>& angles) {
    TokenKind x = a.kind;
    TokenKind y = b.kind;
    if (x == TokenKind::Comment || y == TokenKind::Comment) return true;
    if (y == TokenKind::Comma || y == TokenKind::Semicolon) return false;
    if (x == TokenKind::Semicolon) return y != TokenKind::RParen && y != TokenKind::Semicolon;  // `for (;;)`
    if (x == TokenKind::Comma) return true;
    if (y == TokenKind::RParen || y == TokenKind::RBracket || x == TokenKind::LParen || x == TokenKind::LBracket) return false;
    if (x == TokenKind::Dot || y == TokenKind::Dot) return false;
    bool xAngle = angles.count(a.begin) > 0;
    if (angles.count(b.begin)) return false;                     // `list<`, `string>`
    if (xAngle && x == TokenKind::Less) return false;            // `<string`
    if (xAngle) return y != TokenKind::LParen && y != TokenKind::LBracket;  // `list<string> args`, `max<Money>(a, b)`
    if (y == TokenKind::PlusPlus || y == TokenKind::MinusMinus) return false;
    if (x == TokenKind::Not || x == TokenKind::Tilde || aUnary) {
        // `- -x`: without the space it would be `--`.
        return (x == TokenKind::Minus && (y == TokenKind::Minus || y == TokenKind::MinusMinus)) ||
               (x == TokenKind::Plus && (y == TokenKind::Plus || y == TokenKind::PlusPlus));
    }
    if (y == TokenKind::LParen || y == TokenKind::LBracket) {  // a call, an index, `func(i32)`, `[x](i32 a)`
        return !(x == TokenKind::Identifier || x == TokenKind::RParen || x == TokenKind::RBracket || x == TokenKind::KwFunc);
    }
    return true;
}

// Characters, not bytes: the width of a line for the alignment of comments.
size_t width(const std::string& text) {
    size_t start = text.rfind('\n');
    start = start == std::string::npos ? 0 : start + 1;
    size_t count = 0;
    for (size_t i = start; i < text.size(); ++i) {
        if ((static_cast<unsigned char>(text[i]) & 0xC0) != 0x80) ++count;
    }
    return count;
}

std::string layout(const std::string& source, const std::vector<Token>& tokens, const std::set<size_t>& angles) {
    std::vector<Line> lines = splitLines(source, tokens);
    joinBraces(lines);

    std::vector<OutputLine> output;
    // The open braces, parentheses and brackets, each with the level of the lines
    // inside it minus one: a parenthesis takes the level of its line, a brace the
    // level of its statement, or of its line inside parentheses, as a lambda (D183).
    struct Open {
        TokenKind kind;
        int base;
    };
    std::vector<Open> open;
    int statementLevel = 0;    // of the first line of the statement
    bool continues = false;    // the line before asks to continue the statement (D32)
    const Token* previousCode = nullptr;
    int commentColumn = 0;  // in the source, of the comment at the end of the line before
    for (const Line& line : lines) {
        // D168: a comment alone on its line, right under the comment at the end of
        // the line before, goes on in the same column.
        const Token* first = line.tokens[0];
        if (line.tokens.size() == 1 && isLineComment(first) && !line.blankBefore && commentColumn > 0 &&
            first->loc.column == commentColumn) {
            OutputLine out;
            out.comment = first->text;
            out.continuation = true;
            output.push_back(std::move(out));
            continue;
        }
        commentColumn = 0;

        // D167: one level more than what is open, and one more for a statement that
        // continues after an operator; a line that starts by closing has the level
        // of the line that opened.
        bool inBlock = open.empty() || open.back().kind == TokenKind::LBrace;
        int level = open.empty() ? 0 : open.back().base + 1;
        if (continues && inBlock) ++level;
        TokenKind firstKind = first->kind;
        bool closes = firstKind == TokenKind::RBrace || firstKind == TokenKind::RParen || firstKind == TokenKind::RBracket;
        if (closes && !open.empty()) level = open.back().base;
        bool continuation = continues || !inBlock;
        if (!continuation && !isComment(first)) statementLevel = level;
        OutputLine out;
        out.code.assign(static_cast<size_t>(std::max(level, 0)) * 4, ' ');
        const Token* previous = nullptr;
        bool previousUnary = false;
        for (size_t i = 0; i < line.tokens.size(); ++i) {
            const Token* token = line.tokens[i];
            bool trailing = i + 1 == line.tokens.size() && isLineComment(token) && lastCode(line);
            if (trailing) {
                out.comment = token->text;
                commentColumn = token->loc.column;
                break;
            }
            if (previous && spaceBetween(*previous, *token, previousUnary, angles)) out.code += ' ';
            out.code += isComment(token) ? token->text : source.substr(token->begin, token->end - token->begin);
            bool unary = false;
            if (!isComment(token)) {
                TokenKind kind = token->kind;
                if (kind == TokenKind::Minus || kind == TokenKind::Plus) {
                    unary = !previousCode || (!endsOperand(previousCode->kind) && !angles.count(previousCode->begin));
                }
                if (kind == TokenKind::LBrace) {
                    bool inParens = !open.empty() && open.back().kind != TokenKind::LBrace;
                    open.push_back({kind, inParens ? level : statementLevel});
                }
                if (kind == TokenKind::LParen || kind == TokenKind::LBracket) open.push_back({kind, level});
                if ((kind == TokenKind::RBrace || kind == TokenKind::RParen || kind == TokenKind::RBracket) && !open.empty()) {
                    open.pop_back();
                }
                previousCode = token;
            }
            previous = token;
            previousUnary = unary;
        }
        if (const Token* last = lastCode(line)) {
            continues = continuesOnNextLine(last->kind);
            out.opens = last->kind == TokenKind::LBrace;
        }

        // D167: at most one blank line, none at the start or at the end of a block.
        out.blankBefore = line.blankBefore && !output.empty() && !output.back().opens && firstKind != TokenKind::RBrace;
        output.push_back(std::move(out));
    }

    // D168: the comments at the end of consecutive lines of code in a column.
    for (size_t i = 0; i < output.size();) {
        if (output[i].comment.empty()) {
            ++i;
            continue;
        }
        size_t end = i;
        size_t column = 0;
        while (end < output.size() && !output[end].comment.empty() && (end == i || !output[end].blankBefore)) {
            if (!output[end].continuation) column = std::max(column, width(output[end].code));
            ++end;
        }
        for (size_t k = i; k < end; ++k) {
            output[k].code.append(column + 2 - width(output[k].code), ' ');
            output[k].code += output[k].comment;
        }
        i = end;
    }

    std::string newline = source.find("\r\n") != std::string::npos ? "\r\n" : "\n";
    std::string text;
    for (const OutputLine& line : output) {
        if (line.blankBefore) text += newline;
        text += line.code;
        text += newline;
    }
    return text;
}

// The tokens that make the program, without the line breaks that cromo fmt can
// remove: the ones before `{`, `else`, `catch` and `finally` (D162, D169).
std::vector<std::string> programTokens(const std::string& source, const std::vector<Token>& tokens) {
    std::vector<std::string> out;
    for (size_t i = 0; i < tokens.size(); ++i) {
        const Token& token = tokens[i];
        if (token.kind == TokenKind::Newline && i + 1 < tokens.size()) {
            TokenKind next = tokens[i + 1].kind;
            if (next == TokenKind::LBrace || next == TokenKind::KwElse || next == TokenKind::KwCatch ||
                next == TokenKind::KwFinally) {
                continue;
            }
        }
        out.push_back(std::to_string(static_cast<int>(token.kind)) + " " + source.substr(token.begin, token.end - token.begin));
    }
    return out;
}

std::vector<std::string> commentTexts(const std::vector<Token>& tokens) {
    std::vector<std::string> out;
    for (const Token& token : tokens) {
        if (token.kind == TokenKind::Comment) out.push_back(token.text);
    }
    return out;
}

}  // namespace

bool formatSource(const SourceFile& file, Diagnostics& diag, std::string& out, std::string& error) {
    std::vector<Token> tokens = tokenize(file, 0, diag);
    std::set<size_t> angles;
    if (!diag.hasErrors()) parse(tokens, diag, &angles);
    if (diag.hasErrors()) return false;
    std::vector<Token> all = tokenize(file, 0, diag, true);
    out = layout(file.text, all, angles);

    // D169: the result is the same program, without syntax errors, and formatting
    // it again changes nothing.
    SourceFile result{file.path, out};
    Diagnostics quiet(result);
    quiet.setSilent(true);
    std::vector<Token> resultTokens = tokenize(result, 0, quiet);
    std::set<size_t> resultAngles;
    if (!quiet.hasErrors()) parse(resultTokens, quiet, &resultAngles);
    std::vector<Token> resultAll = tokenize(result, 0, quiet, true);
    if (quiet.hasErrors()) {
        error = "the formatted text has syntax errors";
    } else if (programTokens(file.text, tokens) != programTokens(out, resultTokens)) {
        error = "the formatted text would be a different program";
    } else if (commentTexts(all) != commentTexts(resultAll)) {
        error = "the formatted text has different comments";
    } else if (layout(out, resultAll, resultAngles) != out) {
        error = "formatting the result again changes it";
    }
    return error.empty();
}

}  // namespace cromo
