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
            if (check(TokenKind::KwStruct)) {
                if (auto decl = parseStruct()) {
                    module.structs.push_back(std::move(*decl));
                } else {
                    skipToNextDeclaration();
                }
            } else if (auto function = parseFunction()) {
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

    // params := '(' [param {',' param}] ')'
    // param  := ['ref' | 'move' ['const']] type name   (D22)
    bool parseParams(FunctionDecl& function) {
        if (!expect(TokenKind::LParen, "after the function name")) return false;
        if (!check(TokenKind::RParen)) {
            do {
                Param param;
                if (match(TokenKind::KwRef)) {
                    param.mode = ParamMode::Ref;
                } else if (match(TokenKind::KwMove)) {
                    param.mode = ParamMode::Move;
                    param.isConst = match(TokenKind::KwConst);
                }
                if (check(TokenKind::KwConst)) {
                    diag_.error(peek().loc, param.mode == ParamMode::Ref
                                                ? "'ref const' does not exist: a read parameter is already read-only (D22)"
                                                : "only a 'move' parameter can be const, written 'move const' (D22)");
                    return false;
                }
                if (!parseType(param.type)) return false;
                if (!check(TokenKind::Identifier)) {
                    errorAtCurrent("the parameter name");
                    return false;
                }
                param.name = peek().text;
                param.loc = peek().loc;
                advance();
                function.params.push_back(std::move(param));
            } while (match(TokenKind::Comma));
        }
        return expect(TokenKind::RParen, "to close the parameter list");
    }

    bool parseFunctionBody(FunctionDecl& function) {
        skipNewlines();  // the '{' may also be on the next line (D32)
        if (!check(TokenKind::LBrace)) {
            errorAtCurrent("'{' to open the function body");
            return false;
        }
        function.body = parseBlock();
        return true;
    }

    // function := type name params ['const'] block
    std::optional<FunctionDecl> parseFunction(bool inStruct = false) {
        FunctionDecl function;
        if (!parseType(function.returnType)) return std::nullopt;
        if (!check(TokenKind::Identifier)) {
            errorAtCurrent(inStruct ? "the name of the field or method" : "the function name");
            return std::nullopt;
        }
        function.name = peek().text;
        function.loc = peek().loc;
        advance();
        if (!parseParams(function)) return std::nullopt;
        if (check(TokenKind::KwConst)) {
            if (!inStruct) {
                diag_.error(peek().loc, "only methods can be const (D24)");
                return std::nullopt;
            }
            function.isConst = true;
            advance();
        }
        if (!parseFunctionBody(function)) return std::nullopt;
        return function;
    }

    // struct := 'struct' name '{' { member } '}'
    // member := field | constructor | destructor | method
    std::optional<StructDecl> parseStruct() {
        StructDecl decl;
        advance();  // 'struct'
        if (!check(TokenKind::Identifier)) {
            errorAtCurrent("the name of the struct");
            return std::nullopt;
        }
        decl.name = peek().text;
        decl.loc = peek().loc;
        advance();
        if (check(TokenKind::Colon)) {
            diag_.error(peek().loc, "interfaces arrive in milestone M4");
            return std::nullopt;
        }
        skipNewlines();
        if (!expect(TokenKind::LBrace, "to open the struct")) return std::nullopt;
        skipNewlines();
        while (!check(TokenKind::RBrace) && !check(TokenKind::EndOfFile)) {
            if (!parseMember(decl)) skipToNextStatement();
            if (!match(TokenKind::Newline) && !check(TokenKind::RBrace)) {
                errorAtCurrent("a newline after the member");
                skipToNextStatement();
            }
            skipNewlines();
        }
        if (!expect(TokenKind::RBrace, "to close the struct")) return std::nullopt;
        return decl;
    }

    bool parseMember(StructDecl& decl) {
        // ~Name() { ... }
        if (check(TokenKind::Tilde)) {
            FunctionDecl destructor;
            destructor.kind = FunctionKind::Destructor;
            destructor.loc = advance().loc;
            if (!check(TokenKind::Identifier) || peek().text != decl.name) {
                errorAtCurrent("'" + decl.name + "' after '~'");
                return false;
            }
            advance();
            destructor.name = "~" + decl.name;
            destructor.returnType = TypeName{"void", destructor.loc};
            if (!parseParams(destructor) || !parseFunctionBody(destructor)) return false;
            decl.members.push_back(std::move(destructor));
            return true;
        }
        // Name(params) { ... }
        if (check(TokenKind::Identifier) && peek().text == decl.name && peek(1).kind == TokenKind::LParen) {
            FunctionDecl constructor;
            constructor.kind = FunctionKind::Constructor;
            constructor.name = decl.name;
            constructor.loc = advance().loc;
            constructor.returnType = TypeName{"void", constructor.loc};
            if (!parseParams(constructor) || !parseFunctionBody(constructor)) return false;
            decl.members.push_back(std::move(constructor));
            return true;
        }
        // type name ['=' literal]   or   type name params ['const'] block
        if (peek(2).kind == TokenKind::LParen) {
            auto method = parseFunction(true);
            if (!method) return false;
            method->kind = FunctionKind::Method;
            decl.members.push_back(std::move(*method));
            return true;
        }
        FieldDecl field;
        if (!parseType(field.type)) return false;
        if (!check(TokenKind::Identifier)) {
            errorAtCurrent("the name of the field");
            return false;
        }
        field.name = peek().text;
        field.loc = peek().loc;
        advance();
        if (match(TokenKind::Assign)) {
            field.init = parseExpression();
            if (!field.init) return false;
        }
        decl.fields.push_back(std::move(field));
        return true;
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

    // Assignment, ++/-- or expression (a call). The target of an assignment is an
    // expression too: sema checks that it is a variable or a field.
    StmtPtr parseSimpleStatement() {
        SourceLocation loc = peek().loc;
        ExprPtr expr = parseExpression();
        if (!expr) return nullptr;

        std::optional<AssignOp> op;
        switch (peek().kind) {
        case TokenKind::Assign: op = AssignOp::Assign; break;
        case TokenKind::PlusAssign: op = AssignOp::Add; break;
        case TokenKind::MinusAssign: op = AssignOp::Sub; break;
        case TokenKind::StarAssign: op = AssignOp::Mul; break;
        case TokenKind::SlashAssign: op = AssignOp::Div; break;
        case TokenKind::PercentAssign: op = AssignOp::Rem; break;
        default: break;
        }
        if (op) {
            advance();
            auto assign = std::make_unique<AssignStmt>(loc, std::move(expr), *op);
            assign->value = parseExpression();
            if (!assign->value) return nullptr;
            return assign;
        }
        if (check(TokenKind::PlusPlus) || check(TokenKind::MinusMinus)) {
            bool increment = advance().kind == TokenKind::PlusPlus;
            return std::make_unique<IncDecStmt>(loc, std::move(expr), increment);
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
        // `ref x` and `move x`: sema checks where they can be written (D22, D23).
        if (check(TokenKind::KwRef) || check(TokenKind::KwMove)) {
            bool isRef = check(TokenKind::KwRef);
            SourceLocation loc = advance().loc;
            ExprPtr operand = parsePostfix();
            if (!operand) return nullptr;
            if (isRef) return std::make_unique<RefExpr>(loc, std::move(operand));
            return std::make_unique<MoveExpr>(loc, std::move(operand));
        }
        if (check(TokenKind::Minus) || check(TokenKind::Not)) {
            UnaryOp op = check(TokenKind::Minus) ? UnaryOp::Negate : UnaryOp::Not;
            SourceLocation loc = advance().loc;
            ExprPtr operand = parseUnary();
            if (!operand) return nullptr;
            return std::make_unique<UnaryExpr>(loc, op, std::move(operand));
        }
        return parsePostfix();
    }

    bool parseArgs(std::vector<ExprPtr>& args) {
        advance();  // '('
        if (!check(TokenKind::RParen)) {
            do {
                ExprPtr arg = parseExpression();
                if (!arg) return false;
                args.push_back(std::move(arg));
            } while (match(TokenKind::Comma));
        }
        return expect(TokenKind::RParen, "to close the arguments");
    }

    // A name followed by '(' is a call: of a function, of a constructor, or a
    // conversion when the name is a type (D20). Then any number of `.field` and
    // `.method(args)`.
    ExprPtr parsePostfix() {
        ExprPtr expr;
        if (check(TokenKind::Identifier) && peek(1).kind == TokenKind::LParen) {
            const Token& name = advance();
            auto call = std::make_unique<CallExpr>(name.loc, name.text);
            if (!parseArgs(call->args)) return nullptr;
            expr = std::move(call);
        } else {
            expr = parsePrimary();
        }
        while (expr && check(TokenKind::Dot)) {
            advance();
            if (!check(TokenKind::Identifier)) {
                errorAtCurrent("the name of a field or method after '.'");
                return nullptr;
            }
            const Token& name = advance();
            if (check(TokenKind::LParen)) {
                auto call = std::make_unique<MethodCallExpr>(name.loc, std::move(expr), name.text);
                if (!parseArgs(call->args)) return nullptr;
                expr = std::move(call);
            } else {
                expr = std::make_unique<FieldExpr>(name.loc, std::move(expr), name.text);
            }
        }
        return expr;
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
