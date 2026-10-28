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
            } else if (check(TokenKind::KwEnum)) {
                if (auto decl = parseEnum()) {
                    module.structs.push_back(std::move(*decl));
                } else {
                    skipToNextDeclaration();
                }
            } else if (check(TokenKind::KwInterface)) {
                if (auto decl = parseInterface()) {
                    module.interfaces.push_back(std::move(*decl));
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

    // type := 'void' | name ['<' type {',' type} '>']
    bool parseType(TypeName& out) {
        const Token& token = peek();
        if (token.kind == TokenKind::KwVoid) {
            out = TypeName{"void", token.loc};
            advance();
            return true;
        }
        if (token.kind != TokenKind::Identifier) {
            errorAtCurrent("a type");
            return false;
        }
        out = TypeName{token.text, token.loc};
        advance();
        if (match(TokenKind::Less)) {
            do {
                TypeName arg;
                if (!parseType(arg)) return false;
                out.args.push_back(std::move(arg));
            } while (match(TokenKind::Comma));
            if (!expect(TokenKind::Greater, "to close the type arguments")) return false;
        }
        return true;
    }

    // Looks ahead without consuming: the position after the type that starts at
    // `i`, or 0 if there is no type there.
    size_t skipType(size_t i) const {
        if (peek(i).kind == TokenKind::KwVoid) return i + 1;
        if (peek(i).kind != TokenKind::Identifier) return 0;
        if (peek(i + 1).kind != TokenKind::Less) return i + 1;
        return skipTypeArgs(i + 1);
    }

    // The position after the type arguments that start with the '<' at `i`, or 0.
    size_t skipTypeArgs(size_t i) const {
        for (++i;;) {
            i = skipType(i);
            if (i == 0) return 0;
            if (peek(i).kind == TokenKind::Greater) return i + 1;
            if (peek(i).kind != TokenKind::Comma) return 0;
            ++i;
        }
    }

    // A generic type before a `.`: `Result<i32, string>.Ok(5)`.
    bool isGenericType() const {
        if (!check(TokenKind::Identifier) || peek(1).kind != TokenKind::Less) return false;
        size_t after = skipTypeArgs(1);
        return after && peek(after).kind == TokenKind::Dot;
    }

    // A call with type arguments: `max<Money>(a, b)`.
    bool isGenericCall() const {
        if (!check(TokenKind::Identifier) || peek(1).kind != TokenKind::Less) return false;
        size_t after = skipTypeArgs(1);
        return after && peek(after).kind == TokenKind::LParen;
    }

    // typeParams := '<' name [':' type {'+' type}] {',' ...} '>'   (D52, D88)
    bool parseTypeParams(std::vector<TypeParam>& out) {
        advance();  // '<'
        do {
            if (!check(TokenKind::Identifier)) {
                errorAtCurrent("the name of a type parameter");
                return false;
            }
            TypeParam param;
            param.name = peek().text;
            param.loc = peek().loc;
            advance();
            if (match(TokenKind::Colon)) {
                do {
                    TypeName constraint;
                    if (!parseType(constraint)) return false;
                    param.constraints.push_back(std::move(constraint));
                } while (match(TokenKind::Plus));
            }
            out.push_back(std::move(param));
        } while (match(TokenKind::Comma));
        return expect(TokenKind::Greater, "to close the type parameters");
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

    // signature := type name [typeParams] params ['const']
    bool parseSignature(FunctionDecl& function, bool inStruct) {
        if (!parseType(function.returnType)) return false;
        if (!check(TokenKind::Identifier)) {
            errorAtCurrent(inStruct ? "the name of the field or method" : "the function name");
            return false;
        }
        function.name = peek().text;
        function.loc = peek().loc;
        advance();
        if (check(TokenKind::Less) && !parseTypeParams(function.typeParams)) return false;
        if (!parseParams(function)) return false;
        if (check(TokenKind::KwConst)) {
            if (!inStruct) {
                diag_.error(peek().loc, "only methods can be const (D24)");
                return false;
            }
            function.isConst = true;
            advance();
        }
        return true;
    }

    // function := signature block
    std::optional<FunctionDecl> parseFunction(bool inStruct = false) {
        FunctionDecl function;
        if (!parseSignature(function, inStruct)) return std::nullopt;
        if (!parseFunctionBody(function)) return std::nullopt;
        return function;
    }

    // enum := 'enum' name [typeParams] [':' interfaces] '{' { case | method } '}'
    // case := name ['(' type name {',' type name} ')']   (D104)
    std::optional<StructDecl> parseEnum() {
        StructDecl decl;
        decl.isEnum = true;
        advance();  // 'enum'
        if (!check(TokenKind::Identifier)) {
            errorAtCurrent("the name of the enum");
            return std::nullopt;
        }
        decl.name = peek().text;
        decl.loc = peek().loc;
        advance();
        if (check(TokenKind::Less) && !parseTypeParams(decl.typeParams)) return std::nullopt;
        if (match(TokenKind::Colon)) {
            do {
                TypeName name;
                if (!parseType(name)) return std::nullopt;
                decl.interfaces.push_back(std::move(name));
            } while (match(TokenKind::Comma));
        }
        skipNewlines();
        if (!expect(TokenKind::LBrace, "to open the enum")) return std::nullopt;
        skipNewlines();
        while (!check(TokenKind::RBrace) && !check(TokenKind::EndOfFile)) {
            if (!parseEnumMember(decl)) {
                skipToNextStatement();  // it also takes the newline
                skipNewlines();
                continue;
            }
            if (!match(TokenKind::Newline) && !check(TokenKind::RBrace)) {
                errorAtCurrent("a newline after the case");
                skipToNextStatement();
            }
            skipNewlines();
        }
        if (!expect(TokenKind::RBrace, "to close the enum")) return std::nullopt;
        return decl;
    }

    bool parseEnumMember(StructDecl& decl) {
        if (check(TokenKind::Tilde) || (check(TokenKind::Identifier) && peek().text == decl.name)) {
            diag_.error(peek().loc, "an enum has no constructor or destructor (D110)");
            return false;
        }
        size_t afterType = skipType(0);
        if (afterType && peek(afterType).kind == TokenKind::Identifier) {  // a method
            auto method = parseFunction(true);
            if (!method) return false;
            method->kind = FunctionKind::Method;
            decl.members.push_back(std::move(*method));
            return true;
        }
        if (!check(TokenKind::Identifier)) {
            errorAtCurrent("a case or a method");
            return false;
        }
        EnumCase item;
        item.name = peek().text;
        item.loc = advance().loc;
        if (check(TokenKind::LParen)) {
            FunctionDecl data;  // the data are written like parameters (D104)
            if (!parseParams(data)) return false;
            for (Param& param : data.params) {
                if (param.mode != ParamMode::Read) {
                    diag_.error(param.loc, "the data of a case have no 'ref' or 'move' (D104)");
                    return false;
                }
                FieldDecl field;
                field.type = std::move(param.type);
                field.name = std::move(param.name);
                field.loc = param.loc;
                item.fields.push_back(std::move(field));
            }
        }
        decl.cases.push_back(std::move(item));
        return true;
    }

    // interface := 'interface' name '{' { signature } '}'   (D49, D90)
    std::optional<InterfaceDecl> parseInterface() {
        InterfaceDecl decl;
        advance();  // 'interface'
        if (!check(TokenKind::Identifier)) {
            errorAtCurrent("the name of the interface");
            return std::nullopt;
        }
        decl.name = peek().text;
        decl.loc = peek().loc;
        advance();
        if (check(TokenKind::Less)) {
            diag_.error(peek().loc, "an interface has no type parameters: its methods use Self (D84)");
            return std::nullopt;
        }
        if (check(TokenKind::Colon)) {
            diag_.error(peek().loc, "an interface cannot extend another interface (D90)");
            return std::nullopt;
        }
        skipNewlines();
        if (!expect(TokenKind::LBrace, "to open the interface")) return std::nullopt;
        skipNewlines();
        while (!check(TokenKind::RBrace) && !check(TokenKind::EndOfFile)) {
            FunctionDecl method;
            method.kind = FunctionKind::Method;
            size_t afterType = skipType(0);
            bool ok = false;
            if (afterType && peek(afterType).kind == TokenKind::Identifier && peek(afterType + 1).kind != TokenKind::LParen) {
                diag_.error(peek(afterType).loc, "an interface contains only methods (D90)");
            } else if (parseSignature(method, true)) {
                if (check(TokenKind::LBrace)) {
                    diag_.error(peek().loc, "the methods of an interface have no body (D90)");
                    parseBlock();  // skipped, to go on with the next method
                }
                decl.methods.push_back(std::move(method));
                ok = true;
            }
            if (!ok) {
                skipToNextStatement();  // it also takes the newline
                skipNewlines();
                continue;
            }
            if (!match(TokenKind::Newline) && !check(TokenKind::RBrace)) {
                errorAtCurrent("a newline after the method");
                skipToNextStatement();
            }
            skipNewlines();
        }
        if (!expect(TokenKind::RBrace, "to close the interface")) return std::nullopt;
        return decl;
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
        if (check(TokenKind::Less) && !parseTypeParams(decl.typeParams)) return std::nullopt;
        if (match(TokenKind::Colon)) {  // the interfaces it implements (D49, D88)
            do {
                TypeName name;
                if (!parseType(name)) return std::nullopt;
                decl.interfaces.push_back(std::move(name));
            } while (match(TokenKind::Comma));
        }
        skipNewlines();
        if (!expect(TokenKind::LBrace, "to open the struct")) return std::nullopt;
        skipNewlines();
        while (!check(TokenKind::RBrace) && !check(TokenKind::EndOfFile)) {
            if (!parseMember(decl)) {
                skipToNextStatement();  // it also takes the newline
                skipNewlines();
                continue;
            }
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
        size_t afterType = skipType(0);
        if (afterType && peek(afterType).kind == TokenKind::Identifier &&
            (peek(afterType + 1).kind == TokenKind::LParen || peek(afterType + 1).kind == TokenKind::Less)) {
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
        if (check(TokenKind::KwConst)) return true;
        size_t afterType = check(TokenKind::Identifier) ? skipType(0) : 0;
        return afterType && peek(afterType).kind == TokenKind::Identifier;
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
        case TokenKind::KwSwitch:
            return parseSwitch();
        case TokenKind::KwTry:
            return parseTry();
        case TokenKind::KwThrow: {
            SourceLocation loc = advance().loc;
            ExprPtr value;
            if (!check(TokenKind::Newline) && !check(TokenKind::RBrace) && !check(TokenKind::EndOfFile)) {
                value = parseExpression();
                if (!value) return nullptr;
            }
            return std::make_unique<ThrowStmt>(loc, std::move(value));
        }
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

    // switch := 'switch' ['(' expression ')'] '{' { branch } '}'
    // branch := 'case' pattern {',' pattern} block | 'default' block   (D106, D108)
    StmtPtr parseSwitch() {
        auto stmt = std::make_unique<SwitchStmt>(advance().loc);
        if (match(TokenKind::LParen)) {
            stmt->value = parseExpression();
            if (!stmt->value) return nullptr;
            if (!expect(TokenKind::RParen, "to close the value of 'switch'")) return nullptr;
        }
        skipNewlines();
        if (!expect(TokenKind::LBrace, "to open the 'switch'")) return nullptr;
        skipNewlines();
        while (!check(TokenKind::RBrace) && !check(TokenKind::EndOfFile)) {
            if (check(TokenKind::KwDefault)) {
                SourceLocation loc = advance().loc;
                if (stmt->defaultBlock) {
                    diag_.error(loc, "'switch' has only one 'default'");
                    return nullptr;
                }
                stmt->defaultBlock = std::make_unique<Block>();
                if (!parseBody(*stmt->defaultBlock, "'default'")) return nullptr;
            } else if (match(TokenKind::KwCase)) {
                SwitchBranch branch;
                do {
                    CasePattern pattern;
                    if (!parsePattern(pattern)) return nullptr;
                    branch.patterns.push_back(std::move(pattern));
                } while (match(TokenKind::Comma));
                if (!parseBody(branch.body, "the case")) return nullptr;
                stmt->branches.push_back(std::move(branch));
            } else {
                errorAtCurrent("'case' or 'default'");
                return nullptr;
            }
            skipNewlines();
        }
        if (!expect(TokenKind::RBrace, "to close the 'switch'")) return nullptr;
        return stmt;
    }

    // pattern := name ['(' ['ref'] type name {',' ...} ')'] | literal | '-' literal
    bool parsePattern(CasePattern& pattern) {
        pattern.loc = peek().loc;
        if (check(TokenKind::Identifier)) {
            pattern.name = advance().text;
            if (match(TokenKind::LParen)) {
                pattern.hasBindings = true;
                if (!check(TokenKind::RParen)) {
                    do {
                        CaseBinding binding;
                        binding.isRef = match(TokenKind::KwRef);
                        if (!parseType(binding.type)) return false;
                        if (!check(TokenKind::Identifier)) {
                            errorAtCurrent("the name of the data");
                            return false;
                        }
                        binding.name = peek().text;
                        binding.loc = advance().loc;
                        pattern.bindings.push_back(std::move(binding));
                    } while (match(TokenKind::Comma));
                }
                if (!expect(TokenKind::RParen, "to close the data of the case")) return false;
            }
            return true;
        }
        bool literal = check(TokenKind::IntegerLiteral) || check(TokenKind::CharLiteral) || check(TokenKind::StringLiteral) ||
                       (check(TokenKind::Minus) && peek(1).kind == TokenKind::IntegerLiteral);
        if (!literal) {
            errorAtCurrent("a case of the enum or a literal");
            return false;
        }
        pattern.literal = parseUnary();
        return pattern.literal != nullptr;
    }

    // try := 'try' block {'catch' '(' type name ')' block} ['finally' block]   (D16)
    StmtPtr parseTry() {
        auto stmt = std::make_unique<TryStmt>(advance().loc);
        if (!parseBody(stmt->body, "'try'")) return nullptr;
        for (;;) {
            // `catch` and `finally` may also start on the next line, like `else` (D32).
            size_t ahead = 0;
            while (peek(ahead).kind == TokenKind::Newline) ++ahead;
            TokenKind next = peek(ahead).kind;
            if (next != TokenKind::KwCatch && next != TokenKind::KwFinally) break;
            pos_ += ahead;
            if (match(TokenKind::KwFinally)) {
                stmt->finallyBlock = std::make_unique<Block>();
                if (!parseBody(*stmt->finallyBlock, "'finally'")) return nullptr;
                break;
            }
            advance();  // 'catch'
            CatchClause clause;
            if (!expect(TokenKind::LParen, "after 'catch'")) return nullptr;
            if (!parseType(clause.type)) return nullptr;
            if (!check(TokenKind::Identifier)) {
                errorAtCurrent("the name of the exception");
                return nullptr;
            }
            clause.name = peek().text;
            clause.nameLoc = advance().loc;
            if (!expect(TokenKind::RParen, "to close the 'catch'")) return nullptr;
            if (!parseBody(clause.body, "the 'catch'")) return nullptr;
            stmt->catches.push_back(std::move(clause));
        }
        if (stmt->catches.empty() && !stmt->finallyBlock) {
            diag_.error(stmt->loc, "'try' needs at least one 'catch' or a 'finally'");
            return nullptr;
        }
        return stmt;
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

        if (isForEach()) {  // '(' ['ref'] type name 'in' expression ')' (D25)
            stmt->form = ForForm::Each;
            stmt->isRef = match(TokenKind::KwRef);
            if (check(TokenKind::Identifier) && peek(1).kind == TokenKind::KwIn) {
                diag_.error(peek().loc, "write the type of the element: for (T " + peek().text + " in ...) (D54)");
                return nullptr;
            }
            if (!parseType(stmt->elementType)) return nullptr;
            if (!check(TokenKind::Identifier)) {
                errorAtCurrent("the name of the element");
                return nullptr;
            }
            stmt->elementName = peek().text;
            stmt->elementLoc = advance().loc;
            if (!expect(TokenKind::KwIn, "after the name of the element")) return nullptr;
            stmt->list = parseExpression();
            if (!stmt->list) return nullptr;
            if (!expect(TokenKind::RParen, "to close the 'for' header")) return nullptr;
            if (!parseBody(stmt->body, "the 'for' header")) return nullptr;
            return stmt;
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
        size_t i = peek(0).kind == TokenKind::KwRef ? 1 : 0;
        if (peek(i).kind == TokenKind::Identifier && peek(i + 1).kind == TokenKind::KwIn) return true;  // no type: an error
        size_t afterType = skipType(i);
        return afterType && peek(afterType).kind == TokenKind::Identifier && peek(afterType + 1).kind == TokenKind::KwIn;
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
        if (isGenericType()) {  // `Result<i32, string>.Ok(5)` (D105)
            SourceLocation loc = peek().loc;
            TypeName name;
            if (!parseType(name)) return nullptr;
            expr = std::make_unique<TypeRefExpr>(loc, std::move(name));
        } else if (check(TokenKind::Identifier) && (peek(1).kind == TokenKind::LParen || isGenericCall())) {
            const Token& name = advance();
            auto call = std::make_unique<CallExpr>(name.loc, name.text);
            if (match(TokenKind::Less)) {  // `max<Money>(a, b)`, `Pair<i32>()` (D86, D92)
                do {
                    TypeName arg;
                    if (!parseType(arg)) return nullptr;
                    call->typeArgs.push_back(std::move(arg));
                } while (match(TokenKind::Comma));
                if (!expect(TokenKind::Greater, "to close the type arguments")) return nullptr;
            }
            if (!parseArgs(call->args)) return nullptr;
            expr = std::move(call);
        } else {
            expr = parsePrimary();
        }
        while (expr && (check(TokenKind::Dot) || check(TokenKind::LBracket))) {
            if (check(TokenKind::LBracket)) {  // `v[i]` (D35)
                SourceLocation loc = advance().loc;
                ExprPtr index = parseExpression();
                if (!index) return nullptr;
                if (!expect(TokenKind::RBracket, "to close the index")) return nullptr;
                expr = std::make_unique<IndexExpr>(loc, std::move(expr), std::move(index));
                continue;
            }
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
