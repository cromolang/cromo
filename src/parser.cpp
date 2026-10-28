#include "parser.h"

#include <optional>

namespace cromo {

namespace {

class Parser {
public:
    Parser(const std::vector<Token>& tokens, Diagnostics& diag) : tokens_(tokens), diag_(diag) {}

    Module parseModule() {
        Module module;
        skipNewlines();
        while (!check(TokenKind::EndOfFile)) {
            if (auto function = parseFunction()) {
                module.functions.push_back(std::move(*function));
            } else {
                skipToNextDeclaration();
            }
            skipNewlines();
        }
        return module;
    }

private:
    const Token& peek(size_t ahead = 0) const {
        size_t index = pos_ + ahead;
        return index < tokens_.size() ? tokens_[index] : tokens_.back();
    }

    bool check(TokenKind kind) const { return peek().kind == kind; }

    const Token& advance() {
        const Token& token = peek();
        if (pos_ < tokens_.size() - 1) ++pos_;
        return token;
    }

    bool match(TokenKind kind) {
        if (!check(kind)) return false;
        advance();
        return true;
    }

    void errorAtCurrent(const std::string& expected) {
        diag_.error(peek().loc, "expected " + expected + ", found " + describe(peek().kind));
    }

    bool expect(TokenKind kind, const std::string& context) {
        if (match(kind)) return true;
        errorAtCurrent(describe(kind) + " " + context);
        return false;
    }

    void skipNewlines() {
        while (match(TokenKind::Newline)) {
        }
    }

    // After an error in a declaration: skip to the end of the block or of the
    // line, at the outermost level.
    void skipToNextDeclaration() {
        int depth = 0;
        while (!check(TokenKind::EndOfFile)) {
            TokenKind kind = advance().kind;
            if (kind == TokenKind::LBrace) ++depth;
            if (kind == TokenKind::RBrace && --depth <= 0) return;
            if (kind == TokenKind::Newline && depth == 0) return;
        }
    }

    // After an error in a statement: skip to the end of the line or of the block.
    void skipToNextStatement() {
        int depth = 0;
        while (!check(TokenKind::EndOfFile)) {
            if (check(TokenKind::RBrace) && depth == 0) return;
            TokenKind kind = advance().kind;
            if (kind == TokenKind::LBrace) ++depth;
            if (kind == TokenKind::RBrace) --depth;
            if (kind == TokenKind::Newline && depth == 0) return;
        }
    }

    bool parseType(TypeName& out) {
        const Token& token = peek();
        if (token.kind == TokenKind::KwVoid) {
            out = TypeName{"void", token.loc};
            advance();
            return true;
        }
        if (token.kind == TokenKind::Identifier) {
            out = TypeName{token.text, token.loc};
            advance();
            return true;
        }
        errorAtCurrent("a type");
        return false;
    }

    // function := type name '(' ')' block
    std::optional<FunctionDecl> parseFunction() {
        FunctionDecl function;
        if (!parseType(function.returnType)) return std::nullopt;

        if (!check(TokenKind::Identifier)) {
            errorAtCurrent("the function name");
            return std::nullopt;
        }
        function.name = peek().text;
        function.loc = peek().loc;
        advance();

        if (!expect(TokenKind::LParen, "after the function name")) return std::nullopt;
        if (!check(TokenKind::RParen)) {
            diag_.error(peek().loc, "function parameters are not supported yet (milestone M1)");
            return std::nullopt;
        }
        advance();

        skipNewlines();  // the '{' may also be on the next line (D32)
        if (!check(TokenKind::LBrace)) {
            errorAtCurrent("'{' to open the function body");
            return std::nullopt;
        }
        function.body = parseBlock();
        return function;
    }

    // block := '{' { statement } '}'
    Block parseBlock() {
        Block block;
        block.open = peek().loc;
        advance();  // '{'
        skipNewlines();
        while (!check(TokenKind::RBrace) && !check(TokenKind::EndOfFile)) {
            if (auto statement = parseStatement()) {
                block.statements.push_back(std::move(statement));
                endOfStatement();
            } else {
                skipToNextStatement();
            }
            skipNewlines();
        }
        block.close = peek().loc;
        if (!match(TokenKind::RBrace)) {
            diag_.error(block.open, "unclosed block: missing '}'");
        }
        return block;
    }

    // A statement must be followed by a newline, unless the block closes right away.
    void endOfStatement() {
        if (match(TokenKind::Newline) || check(TokenKind::RBrace) || check(TokenKind::EndOfFile)) return;
        errorAtCurrent("a newline at the end of the statement (one statement per line)");
        skipToNextStatement();
    }

    std::unique_ptr<Stmt> parseStatement() {
        if (check(TokenKind::KwRet)) {
            SourceLocation loc = advance().loc;
            std::unique_ptr<Expr> value;
            if (!check(TokenKind::Newline) && !check(TokenKind::RBrace) && !check(TokenKind::EndOfFile)) {
                value = parseExpression();
                if (!value) return nullptr;
            }
            return std::make_unique<ReturnStmt>(loc, std::move(value));
        }
        diag_.error(peek().loc, "statement not supported yet: this milestone only has 'ret'");
        return nullptr;
    }

    std::unique_ptr<Expr> parseExpression() { return parseUnary(); }

    std::unique_ptr<Expr> parseUnary() {
        if (check(TokenKind::Minus)) {
            SourceLocation loc = advance().loc;
            auto operand = parseUnary();
            if (!operand) return nullptr;
            return std::make_unique<UnaryExpr>(loc, UnaryOp::Negate, std::move(operand));
        }
        return parsePrimary();
    }

    std::unique_ptr<Expr> parsePrimary() {
        const Token& token = peek();
        // A character literal is one byte (D46): for now it is treated as an integer.
        if (token.kind == TokenKind::IntegerLiteral || token.kind == TokenKind::CharLiteral) {
            advance();
            return std::make_unique<IntegerLiteralExpr>(token.loc, token.intValue);
        }
        if (token.kind == TokenKind::LParen) {
            advance();
            auto inner = parseExpression();
            if (!inner) return nullptr;
            if (!expect(TokenKind::RParen, "to close the parenthesis")) return nullptr;
            return inner;
        }
        errorAtCurrent("an expression");
        return nullptr;
    }

    const std::vector<Token>& tokens_;
    Diagnostics& diag_;
    size_t pos_ = 0;
};

}  // namespace

Module parse(const std::vector<Token>& tokens, Diagnostics& diag) {
    return Parser(tokens, diag).parseModule();
}

}  // namespace cromo
