#include "parser.h"

#include <algorithm>
#include <cstdint>
#include <optional>

namespace cromo {

namespace {

class Parser {
public:
    Parser(const std::vector<Token>& tokens, Diagnostics& diag, std::set<size_t>* genericAngles)
        : tokens_(tokens), diag_(diag), genericAngles_(genericAngles) {}

    Module parseModule() {
        Module module;
        int file = tokens_.back().loc.file;
        module.files.resize(file + 1);
        ModuleFile& info = module.files[file];
        skipNewlines();
        while (check(TokenKind::KwImport)) {  // the imports come first (D131)
            parseImport(info);
            skipNewlines();
        }
        while (!check(TokenKind::EndOfFile)) {
            if (check(TokenKind::KwImport)) {
                diag_.error(peek().loc, "the imports come before the declarations of the module (D131)");
                skipToNextDeclaration();
            } else if (check(TokenKind::KwExtern)) {
                parseExtern(module, info.isUnsafe);
            } else {
                parseDeclaration(module);
            }
            skipNewlines();
        }
        return module;
    }

private:
    // declaration := ['public'] (struct | enum | interface | function)   (D50)
    void parseDeclaration(Module& module) {
        bool isPublic = false;
        if (check(TokenKind::KwPublic)) {
            SourceLocation loc = advance().loc;
            if (check(TokenKind::KwExtern) || check(TokenKind::KwImport)) {
                diag_.error(loc, check(TokenKind::KwExtern)
                                     ? "an extern block is not public: the module offers safe functions instead (D42, D133)"
                                     : "an import is not public: each module writes its own (D131)");
                advance();
                skipToNextDeclaration();
                return;
            }
            isPublic = true;
        }
        if (check(TokenKind::KwStruct) || check(TokenKind::KwEnum)) {
            auto decl = check(TokenKind::KwStruct) ? parseStruct() : parseEnum();
            if (!decl) return skipToNextDeclaration();
            decl->isPublic = isPublic;
            module.structs.push_back(std::move(*decl));
        } else if (check(TokenKind::KwInterface)) {
            auto decl = parseInterface();
            if (!decl) return skipToNextDeclaration();
            decl->isPublic = isPublic;
            module.interfaces.push_back(std::move(*decl));
        } else if (auto function = parseFunction()) {
            function->isPublic = isPublic;
            module.functions.push_back(std::move(*function));
        } else {
            skipToNextDeclaration();
        }
    }

    // import := 'import' (name {'.' name} | 'unsafe')   (D50, D127)
    void parseImport(ModuleFile& info) {
        advance();  // 'import'
        if (match(TokenKind::KwUnsafe)) {
            info.isUnsafe = true;
        } else {
            ImportDecl import;
            import.loc = peek().loc;
            do {
                if (!check(TokenKind::Identifier)) {
                    errorAtCurrent("the name of a module");
                    skipToNextDeclaration();
                    return;
                }
                import.path.push_back(advance().text);
            } while (match(TokenKind::Dot));
            info.imports.push_back(std::move(import));
        }
        if (!check(TokenKind::Newline) && !check(TokenKind::EndOfFile)) {
            errorAtCurrent("a newline after the import");
            skipToNextDeclaration();
        }
    }

    // extern := 'extern' string '{' { signature } '}'   (D128)
    void parseExtern(Module& module, bool isUnsafe) {
        SourceLocation loc = advance().loc;  // 'extern'
        if (!isUnsafe) diag_.error(loc, "an extern block needs 'import unsafe' at the top of the module (D127)");
        if (!check(TokenKind::StringLiteral)) {
            errorAtCurrent("the name of the library in quotes, as \"kernel32\" (D128)");
            return skipToNextDeclaration();
        }
        SourceLocation libraryLoc = peek().loc;
        std::string library = advance().text;
        auto endsWith = [&library](const char* suffix) {
            size_t n = std::char_traits<char>::length(suffix);
            return library.size() >= n && library.compare(library.size() - n, n, suffix) == 0;
        };
        if (library.empty() || library.find_first_of("/\\:") != std::string::npos) {
            diag_.error(libraryLoc, "the library is a name, as \"kernel32\": cromo looks for it (D133)");
        } else if (endsWith(".lib") || endsWith(".dll")) {
            diag_.error(libraryLoc, "write the library without extension, as \"kernel32\" (D133)");
        }
        skipNewlines();
        if (!expect(TokenKind::LBrace, "to open the extern block")) return skipToNextDeclaration();
        skipNewlines();
        while (!check(TokenKind::RBrace) && !check(TokenKind::EndOfFile)) {
            if (check(TokenKind::KwPublic)) {
                diag_.error(peek().loc, "the C functions are private to the module: it offers safe functions instead (D42, D133)");
                advance();
            }
            FunctionDecl function;
            function.isExtern = true;
            function.library = library;
            if (!parseSignature(function, false)) {
                skipToNextStatement();  // it also takes the newline
                skipNewlines();
                continue;
            }
            if (check(TokenKind::LBrace)) {
                diag_.error(peek().loc, "a C function has no body: it is in the library (D128)");
                parseBlock();
            }
            if (!function.typeParams.empty()) {
                diag_.error(function.typeParams[0].loc, "a C function cannot be generic (D129)");
                function.typeParams.clear();
            }
            module.functions.push_back(std::move(function));
            if (!match(TokenKind::Newline) && !check(TokenKind::RBrace)) {
                errorAtCurrent("a newline after the function");
                skipToNextStatement();
            }
            skipNewlines();
        }
        expect(TokenKind::RBrace, "to close the extern block");
    }

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

    // type := 'void' | [module '.'] name ['<' type {',' type} '>']
    // `nameFollows`: a name comes after the type, as in a declaration; it decides
    // whether `func(i32) f` returns f or names f (D185).
    bool parseType(TypeName& out, bool nameFollows = true) {
        const Token& token = peek();
        if (token.kind == TokenKind::KwFunc) return parseFunctionType(out, nameFollows);
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
        if (check(TokenKind::Dot) && peek(1).kind == TokenKind::Identifier) {  // `geometry.Circle` (D126)
            out.module = std::move(out.name);
            advance();
            out.name = peek().text;
            out.loc = advance().loc;
        }
        if (check(TokenKind::Less)) {
            markAngle();
            advance();
            do {
                TypeName arg;
                if (!parseType(arg, false)) return false;
                out.args.push_back(std::move(arg));
            } while (match(TokenKind::Comma));
            markAngle();
            if (!expect(TokenKind::Greater, "to close the type arguments")) return false;
        }
        // `i32[4]` (D196): the size is an integer literal.
        while (check(TokenKind::LBracket) && peek(1).kind == TokenKind::IntegerLiteral && peek(2).kind == TokenKind::RBracket) {
            advance();
            out.dimensions.push_back(static_cast<int64_t>(std::min<uint64_t>(advance().intValue, INT64_MAX)));
            advance();
        }
        return true;
    }

    // functionType := 'func' '(' [['ref' | 'move'] type {',' ...}] ')' [type]   (D185)
    bool parseFunctionType(TypeName& out, bool nameFollows) {
        out = TypeName{"func", advance().loc};
        out.isFunction = true;
        if (!expect(TokenKind::LParen, "after 'func'")) return false;
        if (!check(TokenKind::RParen)) {
            do {
                ParamMode mode = ParamMode::Read;
                if (match(TokenKind::KwRef)) {
                    mode = ParamMode::Ref;
                } else if (match(TokenKind::KwMove)) {
                    mode = ParamMode::Move;
                }
                TypeName param;
                if (!parseType(param, false)) return false;
                out.args.push_back(std::move(param));
                out.modes.push_back(static_cast<int>(mode));
            } while (match(TokenKind::Comma));
        }
        if (!expect(TokenKind::RParen, "to close the parameters of the function type")) return false;
        if (hasResultType(0, nameFollows)) {
            TypeName result;
            if (!parseType(result, nameFollows)) return false;
            out.result.push_back(std::move(result));
        }
        return true;
    }

    bool startsType(size_t i) const {
        TokenKind kind = peek(i).kind;
        return kind == TokenKind::Identifier || kind == TokenKind::KwVoid || kind == TokenKind::KwFunc;
    }

    // After the ')' of a function type: a type returned, unless what follows is
    // the name of a declaration (D185).
    bool hasResultType(size_t i, bool nameFollows) const {
        if (!startsType(i)) return false;
        if (!nameFollows) return true;
        size_t after = skipType(i);
        return after && peek(after).kind == TokenKind::Identifier;
    }

    // A '<' or '>' around type arguments or parameters, for cromo fmt.
    void markAngle() {
        if (genericAngles_ && (check(TokenKind::Less) || check(TokenKind::Greater))) genericAngles_->insert(peek().begin);
    }

    // Looks ahead without consuming: the position after the type that starts at
    // `i`, or 0 if there is no type there.
    size_t skipType(size_t i, bool nameFollows = true) const {
        if (peek(i).kind == TokenKind::KwFunc) {  // `func(i32, ref T) bool` (D185)
            if (peek(i + 1).kind != TokenKind::LParen) return 0;
            size_t j = i + 2;
            if (peek(j).kind != TokenKind::RParen) {
                for (;;) {
                    if (peek(j).kind == TokenKind::KwRef || peek(j).kind == TokenKind::KwMove) ++j;
                    j = skipType(j, false);
                    if (j == 0) return 0;
                    if (peek(j).kind != TokenKind::Comma) break;
                    ++j;
                }
            }
            if (peek(j).kind != TokenKind::RParen) return 0;
            ++j;
            return hasResultType(j, nameFollows) ? skipType(j, nameFollows) : j;
        }
        if (peek(i).kind == TokenKind::KwVoid) return i + 1;
        if (peek(i).kind != TokenKind::Identifier) return 0;
        if (peek(i + 1).kind == TokenKind::Dot && peek(i + 2).kind == TokenKind::Identifier) i += 2;  // `geometry.Circle`
        size_t end = peek(i + 1).kind == TokenKind::Less ? skipTypeArgs(i + 1) : i + 1;
        while (end && peek(end).kind == TokenKind::LBracket && peek(end + 1).kind == TokenKind::IntegerLiteral &&
               peek(end + 2).kind == TokenKind::RBracket) {
            end += 3;  // `i32[4]` (D196)
        }
        return end;
    }

    // The position after the type arguments that start with the '<' at `i`, or 0.
    size_t skipTypeArgs(size_t i) const {
        for (++i;;) {
            i = skipType(i, false);
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
        markAngle();
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
                    if (!parseType(constraint, false)) return false;
                    param.constraints.push_back(std::move(constraint));
                } while (match(TokenKind::Plus));
            }
            out.push_back(std::move(param));
        } while (match(TokenKind::Comma));
        markAngle();
        return expect(TokenKind::Greater, "to close the type parameters");
    }

    // params := '(' [param {',' param}] ')'
    // param  := ['ref' | 'move' ['const'] | 'const'] type name   (D22, D253)
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
                } else if (check(TokenKind::KwConst) && peek(1).kind == TokenKind::KwFunc) {
                    advance();  // `const func(...)`: a function that does not modify what it captures (D253)
                    param.isConst = true;
                }
                if (check(TokenKind::KwConst)) {
                    diag_.error(peek().loc, param.mode == ParamMode::Ref
                                                ? "'ref const' does not exist: a read parameter is already read-only (D22)"
                                                : "only a 'move' parameter can be const, written 'move const' (D22), and a "
                                                  "function, written 'const func' (D253)");
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
                if (!parseType(name, false)) return std::nullopt;
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
        std::optional<SourceLocation> publicLoc;
        if (check(TokenKind::KwPublic)) publicLoc = advance().loc;
        if (check(TokenKind::Tilde) ||
            (check(TokenKind::Identifier) && peek().text == decl.name && peek(1).kind == TokenKind::LParen)) {
            diag_.error(peek().loc, "an enum has no constructor or destructor (D110)");
            return false;
        }
        size_t afterType = skipType(0);
        if (afterType && peek(afterType).kind == TokenKind::Identifier) {  // a method
            auto method = parseFunction(true);
            if (!method) return false;
            method->kind = FunctionKind::Method;
            method->isPublic = publicLoc.has_value();
            decl.members.push_back(std::move(*method));
            return true;
        }
        if (!check(TokenKind::Identifier)) {
            errorAtCurrent("a case or a method");
            return false;
        }
        if (publicLoc) {
            diag_.error(*publicLoc, "the cases of an enum are public with it (D132)");
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
        if (match(TokenKind::Assign)) {  // `Red = 1` (D135)
            item.valueExpr = parseExpression();
            if (!item.valueExpr) return false;
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
            if (check(TokenKind::KwPublic)) {
                diag_.error(peek().loc, "the methods of an interface are public with it (D132)");
                advance();
            }
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
                if (!parseType(name, false)) return std::nullopt;
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

    // attributes := { '[' '[' name ['(' arg {',' arg} ')'] ']' ']' }   (D256)
    // arg := text | name
    bool parseAttributes(std::vector<Attribute>& out) {
        while (check(TokenKind::LBracket) && peek(1).kind == TokenKind::LBracket) {
            advance();
            advance();
            Attribute attribute;
            if (!check(TokenKind::Identifier)) {
                errorAtCurrent("the name of the attribute");
                return false;
            }
            attribute.name = peek().text;
            attribute.loc = advance().loc;
            if (match(TokenKind::LParen)) {
                do {
                    AttributeArg arg;
                    arg.loc = peek().loc;
                    if (check(TokenKind::StringLiteral) || check(TokenKind::Identifier)) {
                        arg.isText = check(TokenKind::StringLiteral);
                        arg.text = advance().text;
                    } else {
                        errorAtCurrent("a text or a name as the argument of the attribute");
                        return false;
                    }
                    attribute.args.push_back(std::move(arg));
                } while (match(TokenKind::Comma));
                if (!expect(TokenKind::RParen, "to close the arguments of the attribute")) return false;
            }
            if (!expect(TokenKind::RBracket, "to close the attribute with ']]'") ||
                !expect(TokenKind::RBracket, "to close the attribute with ']]'")) {
                return false;
            }
            out.push_back(std::move(attribute));
            skipNewlines();  // the field may follow on the next line
        }
        return true;
    }

    // member := attributes ['public'] (field | constructor | method) | destructor   (D50, D132, D256)
    bool parseMember(StructDecl& decl) {
        std::vector<Attribute> attributes;
        if (!parseAttributes(attributes)) return false;
        bool isPublic = false;
        if (check(TokenKind::KwPublic)) {
            SourceLocation loc = advance().loc;
            if (check(TokenKind::Tilde)) {
                diag_.error(loc, "a destructor has no 'public': it runs wherever its object ends (D132)");
                return false;
            }
            isPublic = true;
        }
        size_t members = decl.members.size();
        size_t fields = decl.fields.size();
        if (!parseMemberDecl(decl)) return false;
        if (decl.members.size() > members) decl.members.back().isPublic = isPublic;
        if (decl.fields.size() > fields) {
            decl.fields.back().isPublic = isPublic;
            decl.fields.back().attributes = std::move(attributes);
        } else if (!attributes.empty()) {
            diag_.error(attributes[0].loc, "an attribute is written only before a field, for now (D256)");
            return false;
        }
        return true;
    }

    bool parseMemberDecl(StructDecl& decl) {
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
        size_t afterType = check(TokenKind::Identifier) || check(TokenKind::KwFunc) ? skipType(0) : 0;
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
        case TokenKind::AmpAssign: op = AssignOp::BitAnd; break;
        case TokenKind::PipeAssign: op = AssignOp::BitOr; break;
        case TokenKind::CaretAssign: op = AssignOp::BitXor; break;
        case TokenKind::ShiftLeftAssign: op = AssignOp::Shl; break;
        default: break;
        }
        if (!op && atShiftRight(TokenKind::GreaterEqual)) {  // `>>=`
            advance();
            op = AssignOp::Shr;
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
            if (match(TokenKind::Comma)) {  // the key and the value of a map (D197)
                stmt->hasValue = true;
                stmt->valueIsRef = match(TokenKind::KwRef);
                if (!parseType(stmt->valueType)) return nullptr;
                if (!check(TokenKind::Identifier)) {
                    errorAtCurrent("the name of the value");
                    return nullptr;
                }
                stmt->valueName = peek().text;
                stmt->valueLoc = advance().loc;
            }
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
        TokenKind next = peek(afterType + 1).kind;
        return afterType && peek(afterType).kind == TokenKind::Identifier && (next == TokenKind::KwIn || next == TokenKind::Comma);
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
        ExprPtr lhs = parseBitOr();
        for (;;) {
            std::optional<BinaryOp> op;
            switch (peek().kind) {
            case TokenKind::Less: op = BinaryOp::Lt; break;
            case TokenKind::LessEqual: op = BinaryOp::Le; break;
            case TokenKind::Greater: op = BinaryOp::Gt; break;
            case TokenKind::GreaterEqual: op = BinaryOp::Ge; break;
            default: break;
            }
            if (!lhs || !op || atShiftRight(TokenKind::GreaterEqual)) return lhs;  // `>>=` is an assignment
            SourceLocation loc = advance().loc;
            lhs = binary(*op, loc, std::move(lhs), parseBitOr());
        }
    }

    // D193: the bitwise operators bind more than the comparisons, less than `+`.
    ExprPtr parseBitOr() {
        ExprPtr lhs = parseBitXor();
        while (lhs && check(TokenKind::Pipe)) {
            SourceLocation loc = advance().loc;
            lhs = binary(BinaryOp::BitOr, loc, std::move(lhs), parseBitXor());
        }
        return lhs;
    }

    ExprPtr parseBitXor() {
        ExprPtr lhs = parseBitAnd();
        while (lhs && check(TokenKind::Caret)) {
            SourceLocation loc = advance().loc;
            lhs = binary(BinaryOp::BitXor, loc, std::move(lhs), parseBitAnd());
        }
        return lhs;
    }

    ExprPtr parseBitAnd() {
        ExprPtr lhs = parseShift();
        while (lhs && check(TokenKind::Amp)) {
            SourceLocation loc = advance().loc;
            lhs = binary(BinaryOp::BitAnd, loc, std::move(lhs), parseShift());
        }
        return lhs;
    }

    // `>>` is two `>` next to each other: one alone closes the type arguments of
    // `list<list<i32>>`.
    bool atShiftRight(TokenKind second) const {
        return check(TokenKind::Greater) && peek(1).kind == second && peek().end == peek(1).begin;
    }

    ExprPtr parseShift() {
        ExprPtr lhs = parseAdditive();
        while (lhs && (check(TokenKind::ShiftLeft) || atShiftRight(TokenKind::Greater))) {
            BinaryOp op = check(TokenKind::ShiftLeft) ? BinaryOp::Shl : BinaryOp::Shr;
            SourceLocation loc = advance().loc;
            if (op == BinaryOp::Shr) advance();
            lhs = binary(op, loc, std::move(lhs), parseAdditive());
        }
        return lhs;
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
            if (!parseType(name, false)) return nullptr;
            expr = std::make_unique<TypeRefExpr>(loc, std::move(name));
        } else if (check(TokenKind::Identifier) && (peek(1).kind == TokenKind::LParen || isGenericCall())) {
            const Token& name = advance();
            auto call = std::make_unique<CallExpr>(name.loc, name.text);
            if (check(TokenKind::Less)) {  // `max<Money>(a, b)`, `Pair<i32>()` (D86, D92)
                markAngle();
                advance();
                do {
                    TypeName arg;
                    if (!parseType(arg, false)) return nullptr;
                    call->typeArgs.push_back(std::move(arg));
                } while (match(TokenKind::Comma));
                markAngle();
                if (!expect(TokenKind::Greater, "to close the type arguments")) return nullptr;
            }
            if (!parseArgs(call->args)) return nullptr;
            expr = std::move(call);
        } else {
            expr = parsePrimary();
        }
        while (expr && (check(TokenKind::Dot) || check(TokenKind::LBracket) || check(TokenKind::LParen))) {
            if (check(TokenKind::LParen)) {  // `handlers[i](x)`, `make()(x)`: a function value (D186)
                auto call = std::make_unique<CallValueExpr>(peek().loc, std::move(expr));
                if (!parseArgs(call->args)) return nullptr;
                expr = std::move(call);
                continue;
            }
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
            if (expr->kind == ExprKind::Name && check(TokenKind::Less) && skipTypeArgs(0) &&
                peek(skipTypeArgs(0)).kind == TokenKind::LParen) {
                // `json.decode<User>(text)`: a generic function of a module (D86, D126)
                auto call = std::make_unique<CallExpr>(name.loc, name.text);
                call->module = static_cast<NameExpr&>(*expr).name;
                markAngle();
                advance();
                do {
                    TypeName arg;
                    if (!parseType(arg, false)) return nullptr;
                    call->typeArgs.push_back(std::move(arg));
                } while (match(TokenKind::Comma));
                markAngle();
                if (!expect(TokenKind::Greater, "to close the type arguments")) return nullptr;
                if (!parseArgs(call->args)) return nullptr;
                expr = std::move(call);
                continue;
            }
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

    // lambda  := '[' [capture {',' capture}] ']' params [type] block   (D183)
    // capture := ['ref' | 'move'] name
    ExprPtr parseLambda() {
        auto lambda = std::make_unique<LambdaExpr>(advance().loc);
        if (!check(TokenKind::RBracket)) {
            do {
                Capture capture;
                if (match(TokenKind::KwRef)) {
                    capture.mode = ParamMode::Ref;
                } else if (match(TokenKind::KwMove)) {
                    capture.mode = ParamMode::Move;
                }
                if (!check(TokenKind::Identifier)) {
                    errorAtCurrent("the name of a variable to capture");
                    return nullptr;
                }
                capture.name = peek().text;
                capture.loc = advance().loc;
                lambda->captures.push_back(std::move(capture));
            } while (match(TokenKind::Comma));
        }
        if (!expect(TokenKind::RBracket, "to close the captures")) return nullptr;
        auto function = std::make_unique<FunctionDecl>();
        function->name = "lambda";
        function->loc = lambda->loc;
        function->isLambda = true;
        if (!check(TokenKind::LParen)) {
            errorAtCurrent("'(' with the parameters of the lambda");
            return nullptr;
        }
        if (!parseParams(*function)) return nullptr;
        if (check(TokenKind::LBrace)) {
            function->returnType = TypeName{"void", peek().loc};  // without a type it returns nothing
        } else if (!parseType(function->returnType, false)) {
            return nullptr;
        }
        if (!parseFunctionBody(*function)) return nullptr;
        lambda->function = std::move(function);
        return lambda;
    }

    ExprPtr parsePrimary() {
        const Token& token = peek();
        switch (token.kind) {
        case TokenKind::LBracket:
            return parseLambda();
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
    std::set<size_t>* genericAngles_;
    size_t pos_ = 0;
};

}  // namespace

Module parse(const std::vector<Token>& tokens, Diagnostics& diag, std::set<size_t>* genericAngles) {
    return Parser(tokens, diag, genericAngles).parseModule();
}

}  // namespace cromo
