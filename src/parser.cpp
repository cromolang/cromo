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
    // ---- Token helpers ----

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

    // ---- Declarations ----

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

    // function := type name '(' [param {',' param}] ')' block
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
            do {
                if (check(TokenKind::KwRef) || check(TokenKind::KwMove)) {
                    diag_.error(peek().loc, "'ref' and 'move' parameters arrive in milestone M3");
                    return std::nullopt;
                }
                Param param;
                if (!parseType(param.type)) return std::nullopt;
                if (!check(TokenKind::Identifier)) {
                    errorAtCurrent("the parameter name");
                    return std::nullopt;
                }
                param.name = peek().text;
                param.loc = peek().loc;
                advance();
                function.params.push_back(std::move(param));
            } while (match(TokenKind::Comma));
        }
        if (!expect(TokenKind::RParen, "to close the parameter list")) return std::nullopt;

        skipNewlines();  // the '{' may also be on the next line (D32)
        if (!check(TokenKind::LBrace)) {
            errorAtCurrent("'{' to open the function body");
            return std::nullopt;
        }
        function.body = parseBlock();
        return function;
    }

    // ---- Statements ----

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

    // Parses a block that must follow `if`, `else` or `for`: braces are mandatory (D32).
    bool parseBody(Block& out, const char* owner) {
        skipNewlines();
        if (!check(TokenKind::LBrace)) {
            errorAtCurrent(std::string("'{' after ") + owner + " (braces are mandatory)");
            return false;
        }
        out = parseBlock();
        return true;
    }

    // A statement must be followed by a newline, unless the block closes right away.
    void endOfStatement() {
        if (match(TokenKind::Newline) || check(TokenKind::RBrace) || check(TokenKind::EndOfFile)) return;
        errorAtCurrent("a newline at the end of the statement (one statement per line)");
        skipToNextStatement();
    }

    bool startsDeclaration() const {
        return check(TokenKind::KwConst) ||
               (check(TokenKind::Identifier) && peek(1).kind == TokenKind::Identifier);
    }

    StmtPtr parseStatement() {
        switch (peek().kind) {
        case TokenKind::KwRet: {
            SourceLocation loc = advance().loc;
            ExprPtr value;
            if (!check(TokenKind::Newline) && !check(TokenKind::RBrace) && !check(TokenKind::EndOfFile)) {
                value = parseExpression();
                if (!value) return nullptr;
            }
            return std::make_unique<ReturnStmt>(loc, std::move(value));
        }
        case TokenKind::KwIf:
            return parseIf();
        case TokenKind::KwFor:
            return parseFor();
        case TokenKind::KwBreak:
            return std::make_unique<BreakStmt>(advance().loc);
        case TokenKind::KwContinue:
            return std::make_unique<ContinueStmt>(advance().loc);
        default:
            if (startsDeclaration()) return parseVarDecl();
            return parseSimpleStatement();
        }
    }

    // declaration := ['const'] type name ['=' expression]
    StmtPtr parseVarDecl() {
        auto decl = std::make_unique<VarDeclStmt>(peek().loc);
        decl->isConst = match(TokenKind::KwConst);
        if (!parseType(decl->typeName)) return nullptr;
        if (!check(TokenKind::Identifier)) {
            errorAtCurrent("the variable name");
            return nullptr;
        }
        decl->name = peek().text;
        decl->nameLoc = peek().loc;
        advance();
        if (match(TokenKind::Assign)) {
            decl->init = parseExpression();
            if (!decl->init) return nullptr;
        }
        return decl;
    }

    // Assignment, ++/-- or expression (a call).
    StmtPtr parseSimpleStatement() {
        if (check(TokenKind::Identifier)) {
            TokenKind next = peek(1).kind;
            std::optional<AssignOp> op;
            switch (next) {
            case TokenKind::Assign: op = AssignOp::Assign; break;
            case TokenKind::PlusAssign: op = AssignOp::Add; break;
            case TokenKind::MinusAssign: op = AssignOp::Sub; break;
            case TokenKind::StarAssign: op = AssignOp::Mul; break;
            case TokenKind::SlashAssign: op = AssignOp::Div; break;
            case TokenKind::PercentAssign: op = AssignOp::Rem; break;
            default: break;
            }
            if (op) {
                auto assign = std::make_unique<AssignStmt>(peek().loc);
                assign->target = peek().text;
                assign->targetLoc = peek().loc;
                assign->op = *op;
                advance();
                advance();
                assign->value = parseExpression();
                if (!assign->value) return nullptr;
                return assign;
            }
            if (next == TokenKind::PlusPlus || next == TokenKind::MinusMinus) {
                const Token& name = advance();
                advance();
                return std::make_unique<IncDecStmt>(name.loc, name.text, next == TokenKind::PlusPlus);
            }
        }
        SourceLocation loc = peek().loc;
        ExprPtr expr = parseExpression();
        if (!expr) return nullptr;
        if (check(TokenKind::PlusPlus) || check(TokenKind::MinusMinus)) {
            diag_.error(peek().loc, "'++' and '--' can only follow a variable name, as a statement on its own (D32)");
            return nullptr;
        }
        return std::make_unique<ExprStmt>(loc, std::move(expr));
    }

    // if := 'if' '(' expression ')' block ['else' (if | block)]
    StmtPtr parseIf() {
        auto stmt = std::make_unique<IfStmt>(advance().loc);
        if (!expect(TokenKind::LParen, "after 'if'")) return nullptr;
        stmt->condition = parseExpression();
        if (!stmt->condition) return nullptr;
        if (!expect(TokenKind::RParen, "to close the condition")) return nullptr;
        if (!parseBody(stmt->thenBlock, "the condition of 'if'")) return nullptr;

        // `else` may also start on the next line (D32): look past the newlines
        // without consuming them unless an `else` is really there.
        size_t ahead = 0;
        while (peek(ahead).kind == TokenKind::Newline) ++ahead;
        if (peek(ahead).kind != TokenKind::KwElse) return stmt;
        pos_ += ahead;
        SourceLocation elseLoc = advance().loc;

        auto elseBlock = std::make_unique<Block>();
        elseBlock->open = elseLoc;
        if (check(TokenKind::KwIf)) {
            StmtPtr nested = parseIf();
            if (!nested) return nullptr;
            elseBlock->close = nested->loc;
            elseBlock->statements.push_back(std::move(nested));
        } else if (!parseBody(*elseBlock, "'else'")) {
            return nullptr;
        }
        stmt->elseBlock = std::move(elseBlock);
        return stmt;
    }

    // for := 'for' block
    //      | 'for' '(' expression ')' block
    //      | 'for' '(' [init] ';' [expression] ';' [step] ')' block
    StmtPtr parseFor() {
        auto stmt = std::make_unique<ForStmt>(advance().loc);
        skipNewlines();
        if (check(TokenKind::LBrace)) {
            stmt->form = ForForm::Infinite;
            stmt->body = parseBlock();
            return stmt;
        }
        if (!expect(TokenKind::LParen, "or '{' after 'for'")) return nullptr;

        if (isForEach()) {
            diag_.error(peek().loc, "'for' over the elements of a list arrives with lists (milestone M5)");
            return nullptr;
        }

        if (hasSemicolonBeforeClose()) {
            stmt->form = ForForm::Classic;
            if (!check(TokenKind::Semicolon)) {
                stmt->init = startsDeclaration() ? parseVarDecl() : parseSimpleStatement();
                if (!stmt->init) return nullptr;
            }
            if (!expect(TokenKind::Semicolon, "after the first part of 'for'")) return nullptr;
            if (!check(TokenKind::Semicolon)) {
                stmt->condition = parseExpression();
                if (!stmt->condition) return nullptr;
            }
            if (!expect(TokenKind::Semicolon, "after the condition of 'for'")) return nullptr;
            if (!check(TokenKind::RParen)) {
                if (startsDeclaration()) {
                    diag_.error(peek().loc, "the last part of 'for' cannot declare a variable");
                    return nullptr;
                }
                stmt->step = parseSimpleStatement();
                if (!stmt->step) return nullptr;
            }
        } else {
            stmt->form = ForForm::Condition;
            stmt->condition = parseExpression();
            if (!stmt->condition) return nullptr;
        }
        if (!expect(TokenKind::RParen, "to close the 'for' header")) return nullptr;
        if (!parseBody(stmt->body, "the 'for' header")) return nullptr;
        return stmt;
    }

    bool isForEach() const {
        size_t i = 0;
        if (peek(i).kind == TokenKind::KwRef) ++i;
        return peek(i).kind == TokenKind::Identifier &&
               (peek(i + 1).kind == TokenKind::KwIn ||
                (peek(i + 1).kind == TokenKind::Identifier && peek(i + 2).kind == TokenKind::KwIn));
    }

    // True if the 'for' header contains a ';' before its closing parenthesis.
    bool hasSemicolonBeforeClose() const {
        int depth = 0;
        for (size_t i = 0;; ++i) {
            TokenKind kind = peek(i).kind;
            if (kind == TokenKind::EndOfFile || kind == TokenKind::LBrace) return false;
            if (kind == TokenKind::LParen) ++depth;
            if (kind == TokenKind::RParen && depth-- == 0) return false;
            if (kind == TokenKind::Semicolon && depth == 0) return true;
        }
    }

    // ---- Expressions, from the lowest precedence ----

    ExprPtr parseExpression() { return parseOr(); }

    ExprPtr binary(BinaryOp op, SourceLocation loc, ExprPtr lhs, ExprPtr rhs) {
        if (!lhs || !rhs) return nullptr;
        return std::make_unique<BinaryExpr>(loc, op, std::move(lhs), std::move(rhs));
    }

    ExprPtr parseOr() {
        ExprPtr lhs = parseAnd();
        while (lhs && check(TokenKind::OrOr)) {
            SourceLocation loc = advance().loc;
            lhs = binary(BinaryOp::Or, loc, std::move(lhs), parseAnd());
        }
        return lhs;
    }

    ExprPtr parseAnd() {
        ExprPtr lhs = parseEquality();
        while (lhs && check(TokenKind::AndAnd)) {
            SourceLocation loc = advance().loc;
            lhs = binary(BinaryOp::And, loc, std::move(lhs), parseEquality());
        }
        return lhs;
    }

    ExprPtr parseEquality() {
        ExprPtr lhs = parseRelational();
        while (lhs && (check(TokenKind::Equal) || check(TokenKind::NotEqual))) {
            BinaryOp op = check(TokenKind::Equal) ? BinaryOp::Eq : BinaryOp::Ne;
            SourceLocation loc = advance().loc;
            lhs = binary(op, loc, std::move(lhs), parseRelational());
        }
        return lhs;
    }

    ExprPtr parseRelational() {
        ExprPtr lhs = parseAdditive();
        for (;;) {
            std::optional<BinaryOp> op;
            switch (peek().kind) {
            case TokenKind::Less: op = BinaryOp::Lt; break;
            case TokenKind::LessEqual: op = BinaryOp::Le; break;
            case TokenKind::Greater: op = BinaryOp::Gt; break;
            case TokenKind::GreaterEqual: op = BinaryOp::Ge; break;
            default: break;
            }
            if (!lhs || !op) return lhs;
            SourceLocation loc = advance().loc;
            lhs = binary(*op, loc, std::move(lhs), parseAdditive());
        }
    }

    ExprPtr parseAdditive() {
        ExprPtr lhs = parseMultiplicative();
        while (lhs && (check(TokenKind::Plus) || check(TokenKind::Minus))) {
            BinaryOp op = check(TokenKind::Plus) ? BinaryOp::Add : BinaryOp::Sub;
            SourceLocation loc = advance().loc;
            lhs = binary(op, loc, std::move(lhs), parseMultiplicative());
        }
        return lhs;
    }

    ExprPtr parseMultiplicative() {
        ExprPtr lhs = parseUnary();
        for (;;) {
            std::optional<BinaryOp> op;
            switch (peek().kind) {
            case TokenKind::Star: op = BinaryOp::Mul; break;
            case TokenKind::Slash: op = BinaryOp::Div; break;
            case TokenKind::Percent: op = BinaryOp::Rem; break;
            default: break;
            }
            if (!lhs || !op) return lhs;
            SourceLocation loc = advance().loc;
            lhs = binary(*op, loc, std::move(lhs), parseUnary());
        }
    }

    ExprPtr parseUnary() {
        if (check(TokenKind::Minus) || check(TokenKind::Not)) {
            UnaryOp op = check(TokenKind::Minus) ? UnaryOp::Negate : UnaryOp::Not;
            SourceLocation loc = advance().loc;
            ExprPtr operand = parseUnary();
            if (!operand) return nullptr;
            return std::make_unique<UnaryExpr>(loc, op, std::move(operand));
        }
        return parsePostfix();
    }

    // A name followed by '(' is a call; a type name followed by '(' is a conversion (D20).
    ExprPtr parsePostfix() {
        if (check(TokenKind::Identifier) && peek(1).kind == TokenKind::LParen) {
            const Token& name = advance();
            auto call = std::make_unique<CallExpr>(name.loc, name.text);
            advance();  // '('
            if (!check(TokenKind::RParen)) {
                do {
                    ExprPtr arg = parseExpression();
                    if (!arg) return nullptr;
                    call->args.push_back(std::move(arg));
                } while (match(TokenKind::Comma));
            }
            if (!expect(TokenKind::RParen, "to close the arguments")) return nullptr;
            return call;
        }
        return parsePrimary();
    }

    ExprPtr parsePrimary() {
        const Token& token = peek();
        switch (token.kind) {
        case TokenKind::IntegerLiteral:
            advance();
            return std::make_unique<IntLiteralExpr>(token.loc, token.intValue, false);
        case TokenKind::CharLiteral:
            advance();
            return std::make_unique<IntLiteralExpr>(token.loc, token.intValue, true);
        case TokenKind::FloatLiteral: {
            advance();
            std::string text;
            for (char c : token.text) {
                if (c != '_') text += c;
            }
            return std::make_unique<FloatLiteralExpr>(token.loc, std::move(text));
        }
        case TokenKind::KwTrue:
        case TokenKind::KwFalse:
            advance();
            return std::make_unique<BoolLiteralExpr>(token.loc, token.kind == TokenKind::KwTrue);
        case TokenKind::StringLiteral:
            advance();
            return std::make_unique<StringLiteralExpr>(token.loc, token.text);
        case TokenKind::Identifier:
            advance();
            return std::make_unique<NameExpr>(token.loc, token.text);
        case TokenKind::LParen: {
            advance();
            ExprPtr inner = parseExpression();
            if (!inner) return nullptr;
            if (!expect(TokenKind::RParen, "to close the parenthesis")) return nullptr;
            return inner;
        }
        default:
            errorAtCurrent("an expression");
            return nullptr;
        }
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
