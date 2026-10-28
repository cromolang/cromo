#include "sema.h"

#include <algorithm>
#include <charconv>
#include <cstring>
#include <map>
#include <optional>
#include <utility>

namespace cromo {

namespace {

constexpr int kMaxWidth = 1000;
constexpr int kMaxPrecision = 500;

// How a statement ends: it falls through, it leaves the function (or loops
// forever), or it jumps with break/continue.
enum class Flow { Continues, Returns, Jumps };

bool isPrintFunction(const std::string& name) {
    return name == "print" || name == "println" || name == "eprint" || name == "eprintln";
}

bool isArithmetic(BinaryOp op) {
    return op == BinaryOp::Add || op == BinaryOp::Sub || op == BinaryOp::Mul || op == BinaryOp::Div ||
           op == BinaryOp::Rem;
}

const char* spelling(BinaryOp op) {
    switch (op) {
    case BinaryOp::Add: return "+";
    case BinaryOp::Sub: return "-";
    case BinaryOp::Mul: return "*";
    case BinaryOp::Div: return "/";
    case BinaryOp::Rem: return "%";
    case BinaryOp::Eq: return "==";
    case BinaryOp::Ne: return "!=";
    case BinaryOp::Lt: return "<";
    case BinaryOp::Le: return "<=";
    case BinaryOp::Gt: return ">";
    case BinaryOp::Ge: return ">=";
    case BinaryOp::And: return "&&";
    case BinaryOp::Or: return "||";
    }
    return "?";
}

// True for an expression made only of literals without a type yet (D55),
// for example `5`, `-1.5` or `2 * 3`. A character literal already is a u8.
bool isUntypedLiteral(const Expr& expr) {
    switch (expr.kind) {
    case ExprKind::IntLiteral: return !static_cast<const IntLiteralExpr&>(expr).isChar;
    case ExprKind::FloatLiteral: return true;
    case ExprKind::Unary: {
        const auto& unary = static_cast<const UnaryExpr&>(expr);
        return unary.op == UnaryOp::Negate && isUntypedLiteral(*unary.operand);
    }
    case ExprKind::Binary: {
        const auto& binary = static_cast<const BinaryExpr&>(expr);
        return isArithmetic(binary.op) && isUntypedLiteral(*binary.lhs) && isUntypedLiteral(*binary.rhs);
    }
    default: return false;
    }
}

// A literal, possibly negative: the only initial value a field can have for now (D73).
bool isConstantLiteral(const Expr& expr) {
    switch (expr.kind) {
    case ExprKind::IntLiteral:
    case ExprKind::FloatLiteral:
    case ExprKind::BoolLiteral: return true;
    case ExprKind::Unary: {
        const auto& unary = static_cast<const UnaryExpr&>(expr);
        return unary.op == UnaryOp::Negate &&
               (unary.operand->kind == ExprKind::IntLiteral || unary.operand->kind == ExprKind::FloatLiteral);
    }
    default: return false;
    }
}

// A new object that nobody owns yet: it is moved without writing `move` (D23).
bool isTemporary(const Expr& expr) {
    return expr.kind == ExprKind::Construct || expr.kind == ExprKind::Call || expr.kind == ExprKind::MethodCall;
}

// A variable, or a field of a variable or of the object of a method.
bool isPlace(const Expr& expr) {
    if (expr.kind == ExprKind::Name) return true;
    if (expr.kind == ExprKind::Field) {
        const Expr& object = *static_cast<const FieldExpr&>(expr).object;
        return object.kind == ExprKind::Self || isPlace(object);
    }
    return false;
}

// The place as written in the source, for the messages: `s.log`.
std::string placeText(const Expr& expr) {
    if (expr.kind == ExprKind::Name) return static_cast<const NameExpr&>(expr).name;
    if (expr.kind == ExprKind::Field) {
        const auto& field = static_cast<const FieldExpr&>(expr);
        if (field.object->kind == ExprKind::Self) return field.field;
        return placeText(*field.object) + "." + field.field;
    }
    return "the object";
}

// The object reached by a place: a variable (null for the object of the method)
// and the chain of field indices.
struct Path {
    const Variable* variable = nullptr;
    std::vector<int> fields;
};

std::optional<Path> pathOf(const Expr& expr) {
    switch (expr.kind) {
    case ExprKind::Name: return Path{static_cast<const NameExpr&>(expr).variable, {}};
    case ExprKind::Self: return Path{};
    case ExprKind::Field: {
        const auto& field = static_cast<const FieldExpr&>(expr);
        std::optional<Path> path = pathOf(*field.object);
        if (path) path->fields.push_back(field.index);
        return path;
    }
    default: return std::nullopt;
    }
}

// Two places overlap when one contains the other: `a` and `a.x`, but not `a.x` and `a.y`.
bool overlaps(const Path& a, const Path& b) {
    if (a.variable != b.variable) return false;
    size_t common = std::min(a.fields.size(), b.fields.size());
    return std::equal(a.fields.begin(), a.fields.begin() + common, b.fields.begin());
}

bool fits(uint64_t magnitude, bool negative, Type type) {
    if (negative && magnitude != 0) {
        if (!type.isSigned) return false;
        return magnitude <= (uint64_t{1} << (type.bits - 1));
    }
    uint64_t max = type.isSigned ? (uint64_t{1} << (type.bits - 1)) - 1
                   : type.bits == 64 ? UINT64_MAX
                                     : (uint64_t{1} << type.bits) - 1;
    return magnitude <= max;
}

std::string rangeOf(Type type) {
    if (type.isSigned) {
        uint64_t limit = uint64_t{1} << (type.bits - 1);
        return "-" + std::to_string(limit) + " .. " + std::to_string(limit - 1);
    }
    uint64_t max = type.bits == 64 ? UINT64_MAX : (uint64_t{1} << type.bits) - 1;
    return "0 .. " + std::to_string(max);
}

int alignUp(int value, int align) { return (value + align - 1) / align * align; }

class Checker {
public:
    Checker(Module& module, Diagnostics& diag) : module_(module), diag_(diag) {}

    void run() {
        declareStructs();
        declareFunctions();
        for (StructDecl& decl : module_.structs) {
            for (FunctionDecl& member : decl.members) checkFunction(member);
        }
        for (FunctionDecl& function : module_.functions) checkFunction(function);
    }

private:
    // ---- Declarations ----

    std::optional<Type> resolveType(const TypeName& name) {
        if (name.name == "void") return Type::voidType();
        if (auto type = findBuiltinType(name.name)) return type;
        auto found = structs_.find(name.name);
        if (found != structs_.end()) return Type::structType(found->second);
        if (name.name == "string" || name.name == "list") {
            diag_.error(name.loc, "the type '" + name.name + "' arrives in milestone M5");
        } else {
            diag_.error(name.loc, "unknown type '" + name.name + "'");
        }
        return std::nullopt;
    }

    void declareStructs() {
        for (StructDecl& decl : module_.structs) {
            if (structs_.count(decl.name)) {
                diag_.error(decl.loc, "struct '" + decl.name + "' is already defined");
            } else if (findBuiltinType(decl.name) || isPrintFunction(decl.name) || decl.name == "void") {
                diag_.error(decl.loc, "'" + decl.name + "' is a built-in name");
            } else {
                structs_[decl.name] = &decl;
            }
        }

        for (StructDecl& decl : module_.structs) {
            std::map<std::string, const FieldDecl*> fieldNames;
            for (FieldDecl& field : decl.fields) {
                if (fieldNames.count(field.name)) {
                    diag_.error(field.loc, "field '" + field.name + "' is already defined in " + decl.name);
                }
                fieldNames[field.name] = &field;
                field.resolved = resolveType(field.type).value_or(Type::error());
                if (field.resolved.isVoid()) {
                    diag_.error(field.type.loc, "a field cannot be void");
                    field.resolved = Type::error();
                }
                if (!field.init || field.resolved.isError()) continue;
                if (field.resolved.isStruct()) {
                    diag_.error(field.init->loc, "a field of type " + field.resolved.name() +
                                                     " cannot have an initial value yet (D73)");
                } else if (!isConstantLiteral(*field.init)) {
                    diag_.error(field.init->loc, "for now the initial value of a field must be a literal (D73)");
                } else {
                    coerce(field.init, field.resolved);
                }
            }

            for (FunctionDecl& member : decl.members) {
                member.owner = &decl;
                if (member.kind == FunctionKind::Constructor) {
                    if (decl.constructor) diag_.error(member.loc, "a struct has only one constructor (D69)");
                    else decl.constructor = &member;
                } else if (member.kind == FunctionKind::Destructor) {
                    if (decl.destructor) diag_.error(member.loc, "a struct has only one destructor");
                    else decl.destructor = &member;
                    if (!member.params.empty()) diag_.error(member.params[0].loc, "a destructor has no parameters");
                } else if (fieldNames.count(member.name)) {
                    diag_.error(member.loc, "'" + member.name + "' is already a field of " + decl.name);
                } else if (decl.methods.count(member.name)) {
                    diag_.error(member.loc, "method '" + member.name + "' is already defined in " + decl.name);
                } else {
                    decl.methods[member.name] = &member;
                }
            }
        }

        for (StructDecl& decl : module_.structs) layout(decl);
    }

    // Computes size, alignment and need for destruction, and rejects structs
    // that contain themselves (D11).
    void layout(StructDecl& decl) {
        int& state = layoutState_[&decl];
        if (state == 2) return;
        if (state == 1) return;  // cycle: reported by the struct that closes it
        state = 1;
        int offset = 0;
        int align = 1;
        bool destruct = decl.destructor != nullptr;
        for (FieldDecl& field : decl.fields) {
            Type type = field.resolved;
            if (type.isStruct()) {
                auto* inner = const_cast<StructDecl*>(type.structDecl);
                if (layoutState_[inner] == 1) {
                    diag_.error(field.loc, "struct '" + decl.name + "' contains itself through field '" + field.name +
                                               "': use a list instead (D11)");
                    field.resolved = Type::error();
                    continue;
                }
                layout(*inner);
                destruct = destruct || inner->needsDestruction;
            }
            if (type.isError()) continue;
            offset = alignUp(offset, alignOf(type)) + sizeOf(type);
            align = std::max(align, alignOf(type));
        }
        decl.size = alignUp(offset, align);
        decl.align = align;
        decl.needsDestruction = destruct;
        state = 2;
    }

    void resolveSignature(FunctionDecl& function) {
        function.resolvedReturn = resolveType(function.returnType).value_or(Type::error());
        for (const Param& param : function.params) {
            Type type = resolveType(param.type).value_or(Type::error());
            if (type.isVoid()) {
                diag_.error(param.type.loc, "a parameter cannot be void");
                type = Type::error();
            }
            function.paramTypes.push_back(type);
        }
    }

    void declareFunctions() {
        for (StructDecl& decl : module_.structs) {
            for (FunctionDecl& member : decl.members) resolveSignature(member);
            checkCopySignature(decl);
        }
        for (FunctionDecl& function : module_.functions) {
            if (functions_.count(function.name)) {
                diag_.error(function.loc, "function '" + function.name + "' is already defined");
            } else if (function.name.rfind("cromo_", 0) == 0 || function.name == "ExitProcess") {
                diag_.error(function.loc, "the name '" + function.name + "' is reserved for the runtime");
            } else if (isPrintFunction(function.name) || findBuiltinType(function.name)) {
                diag_.error(function.loc, "'" + function.name + "' is a built-in name");
            } else if (structs_.count(function.name)) {
                diag_.error(function.loc, "'" + function.name + "' is already the name of a struct");
            } else {
                functions_[function.name] = &function;
            }
            resolveSignature(function);
        }

        auto main = functions_.find("main");
        if (main == functions_.end()) {
            diag_.error("missing function main");
            return;
        }
        const FunctionDecl& function = *main->second;
        Type ret = function.resolvedReturn;
        if (!ret.isError() && !ret.isVoid() && !(ret.isInt() && ret.bits == 32 && ret.isSigned)) {
            diag_.error(function.returnType.loc, "main must return i32 or void");
        }
        if (!function.params.empty()) {
            diag_.error(function.params[0].loc, "main has no parameters");
        }
    }

    // D83: `copy()` written by hand is `T copy() const`, and only where it is not generated.
    void checkCopySignature(const StructDecl& decl) {
        auto found = decl.methods.find("copy");
        if (found == decl.methods.end()) return;
        const FunctionDecl& copy = *found->second;
        if (!decl.needsDestruction) {
            diag_.error(copy.loc, decl.name + " is copied implicitly: its copy() is generated by the compiler (D83)");
        } else if (!copy.params.empty() || !copy.isConst || copy.resolvedReturn != Type::structType(&decl)) {
            diag_.error(copy.loc, "copy() must be written '" + decl.name + " copy() const' (D83)");
        }
    }

    void checkFunction(FunctionDecl& function) {
        current_ = &function;
        scopes_.assign(1, {});
        loops_.clear();
        for (size_t i = 0; i < function.params.size(); ++i) {
            const Param& param = function.params[i];
            Variable* variable = declare(param.name, function.paramTypes[i], param.loc, param.isConst);
            if (!variable) continue;
            variable->kind = param.mode == ParamMode::Ref    ? VariableKind::RefParam
                             : param.mode == ParamMode::Move ? VariableKind::MoveParam
                                                             : VariableKind::ReadParam;
        }

        Flow flow = checkBlock(function.body);

        // D47, D67: a non-void function must not reach its end.
        Type ret = function.resolvedReturn;
        if (!ret.isVoid() && !ret.isError() && flow != Flow::Returns) {
            diag_.error(function.body.close, "function '" + function.name + "' must end with 'ret' on every path: it returns " +
                                                 ret.name());
        }
    }

    // ---- Variables and fields ----

    Variable* lookup(const std::string& name) {
        for (auto scope = scopes_.rbegin(); scope != scopes_.rend(); ++scope) {
            auto found = scope->find(name);
            if (found != scope->end()) return found->second;
        }
        return nullptr;
    }

    // Field of the struct whose method is being checked (D70).
    int ownFieldIndex(const std::string& name) const {
        if (!current_ || !current_->owner) return -1;
        const auto& fields = current_->owner->fields;
        for (size_t i = 0; i < fields.size(); ++i) {
            if (fields[i].name == name) return static_cast<int>(i);
        }
        return -1;
    }

    Variable* declare(const std::string& name, Type type, SourceLocation loc, bool isConst) {
        if (findBuiltinType(name) || structs_.count(name)) {
            diag_.error(loc, "'" + name + "' is a type name");
            return nullptr;
        }
        if (ownFieldIndex(name) >= 0) {
            diag_.error(loc, "'" + name + "' is a field of " + current_->owner->name + ": choose another name (D70)");
            return nullptr;
        }
        if (Variable* existing = lookup(name)) {  // D66: no shadowing
            diag_.error(loc, "a variable named '" + name + "' is already visible here (declared at line " +
                                 std::to_string(existing->loc.line) + ")");
            return nullptr;
        }
        auto variable = std::make_unique<Variable>();
        variable->name = name;
        variable->type = type;
        variable->loc = loc;
        variable->isConst = isConst;
        variable->id = static_cast<int>(current_->variables.size());
        Variable* result = variable.get();
        current_->variables.push_back(std::move(variable));
        scopes_.back()[name] = result;
        return result;
    }

    // Why a place cannot be modified, or an empty string if it can (D22, D24, D26).
    std::string readOnlyReason(const Expr& expr) const {
        switch (expr.kind) {
        case ExprKind::Name: {
            const Variable* variable = static_cast<const NameExpr&>(expr).variable;
            if (!variable) return "";
            if (variable->kind == VariableKind::ReadParam) {
                return "parameter '" + variable->name + "' cannot be modified: parameters are read-only (D22)";
            }
            if (variable->isConst) return "'" + variable->name + "' is const and cannot be modified (D26)";
            return "";
        }
        case ExprKind::Field:
            return readOnlyReason(*static_cast<const FieldExpr&>(expr).object);  // const is deep (D24)
        case ExprKind::Self:
            if (current_->isConst) return "method '" + current_->name + "' is const and cannot modify the fields (D24)";
            return "";
        default:
            return "";
        }
    }

    // Checks the target of an assignment, `++` or `--`. Returns its type, or an error.
    Type checkTarget(ExprPtr& target) {
        Type type = checkExpr(target, std::nullopt);
        if (type.isError()) return type;
        if (!isPlace(*target)) {
            diag_.error(target->loc, "only a variable or a field can be assigned");
            return Type::error();
        }
        std::string reason = readOnlyReason(*target);
        if (!reason.empty()) {
            diag_.error(target->loc, reason);
            return Type::error();
        }
        return type;
    }

    // Whether `move` can take the value of this place (D23, D26, D78).
    bool canMove(const Expr& place) const {
        if (place.kind == ExprKind::Name) {
            const Variable* variable = static_cast<const NameExpr&>(place).variable;
            return variable && variable->kind != VariableKind::ReadParam;
        }
        return readOnlyReason(place).empty();
    }

    // A struct can be copied if it has copy(), or if the compiler can generate it (D35).
    bool canCopy(Type type) const {
        if (!type.isStruct()) return true;
        const StructDecl& decl = *type.structDecl;
        if (decl.methods.count("copy")) return true;
        if (decl.destructor) return false;
        for (const FieldDecl& field : decl.fields) {
            if (!canCopy(field.resolved)) return false;
        }
        return true;
    }

    // The copy() of a struct: written by hand, or generated the first time it is used (D35).
    FunctionDecl* copyFunction(const StructDecl& constDecl) {
        StructDecl& decl = *structs_.at(constDecl.name);
        auto found = decl.methods.find("copy");
        if (found != decl.methods.end()) return found->second;
        if (!canCopy(Type::structType(&decl))) return nullptr;
        for (const FieldDecl& field : decl.fields) {
            if (needsDestruction(field.resolved)) copyFunction(*field.resolved.structDecl);
        }
        auto copy = std::make_unique<FunctionDecl>();
        copy->kind = FunctionKind::Method;
        copy->name = "copy";
        copy->loc = decl.loc;
        copy->returnType = TypeName{decl.name, decl.loc};
        copy->isConst = true;
        copy->isGenerated = true;
        copy->owner = &decl;
        copy->resolvedReturn = Type::structType(&decl);
        decl.methods["copy"] = copy.get();
        decl.generatedCopy = std::move(copy);
        return decl.methods["copy"];
    }

    // D23: a type that needs destruction is not copied implicitly. Returns false
    // after reporting the error.
    bool checkNotCopied(const Expr& value, Type type) {
        if (!needsDestruction(type) || isTemporary(value) || value.kind == ExprKind::Move) return true;
        std::string text = placeText(value);
        std::vector<std::string> fixes;
        if (isPlace(value) && canMove(value)) fixes.push_back("'move " + text + "'");
        if (canCopy(type)) fixes.push_back("'" + text + ".copy()'");
        std::string advice = fixes.empty()    ? "it can be neither moved nor copied here"
                             : fixes.size() == 1 ? "write " + fixes[0]
                                                 : "write " + fixes[0] + " or " + fixes[1];
        diag_.error(value.loc, "values of type " + type.name() + " are not copied implicitly (D23): " + advice);
        return false;
    }

    // `move x` (D23, D78, D80). The operand must be a variable or a field.
    Type checkMove(MoveExpr& node) {
        if (isTemporary(*node.operand)) {
            Type ignored = checkExpr(node.operand, std::nullopt);
            (void)ignored;
            diag_.error(node.loc, "'move' is not needed before a new object: it is moved anyway (D23)");
            return Type::error();
        }
        Type type = checkExpr(node.operand, std::nullopt);
        if (type.isError()) return type;
        const Expr& operand = *node.operand;
        if (!isPlace(operand)) {
            diag_.error(node.loc, "'move' is written before a variable or a field (D82)");
            return Type::error();
        }
        if (!needsDestruction(type)) {
            node.moveKind = MoveKind::Copy;  // D80
            return type;
        }
        if (operand.kind == ExprKind::Name) {
            Variable* variable = static_cast<const NameExpr&>(operand).variable;
            switch (variable->kind) {
            case VariableKind::Local:
            case VariableKind::MoveParam:
                node.moveKind = MoveKind::Variable;
                variable->needsDropFlag = true;
                return type;
            case VariableKind::RefParam:
                node.moveKind = MoveKind::Take;  // D78
                return type;
            case VariableKind::ReadParam:
                diag_.error(node.loc, "parameter '" + variable->name +
                                          "' is read-only: it belongs to the caller and cannot be moved (D82)");
                return Type::error();
            }
        }
        std::string reason = readOnlyReason(operand);
        if (!reason.empty()) {
            diag_.error(node.loc, "cannot move '" + placeText(operand) + "': " + reason);
            return Type::error();
        }
        node.moveKind = MoveKind::Take;  // D78
        return type;
    }

    // A value that can be written with `move`: an initialization, an assignment,
    // the argument of a `move` parameter, or after `ret` (D82).
    bool coerceValue(ExprPtr& expr, Type target) {
        moveAllowed_ = expr->kind == ExprKind::Move;
        return coerce(expr, target);
    }

    // ---- Statements ----

    Flow checkBlock(Block& block) {
        scopes_.emplace_back();
        Flow flow = Flow::Continues;
        bool warned = false;
        for (StmtPtr& statement : block.statements) {
            if (flow != Flow::Continues && !warned) {
                diag_.warning(statement->loc, "unreachable code");
                warned = true;
            }
            Flow next = checkStatement(*statement);
            if (flow == Flow::Continues) flow = next;
        }
        scopes_.pop_back();
        return flow;
    }

    Flow checkStatement(Stmt& statement) {
        switch (statement.kind) {
        case StmtKind::VarDecl: checkVarDecl(static_cast<VarDeclStmt&>(statement)); return Flow::Continues;
        case StmtKind::Assign: checkAssign(static_cast<AssignStmt&>(statement)); return Flow::Continues;
        case StmtKind::IncDec: checkIncDec(static_cast<IncDecStmt&>(statement)); return Flow::Continues;
        case StmtKind::Expr: checkExprStmt(static_cast<ExprStmt&>(statement)); return Flow::Continues;
        case StmtKind::If: return checkIf(static_cast<IfStmt&>(statement));
        case StmtKind::For: return checkFor(static_cast<ForStmt&>(statement));
        case StmtKind::Break:
        case StmtKind::Continue:
            if (loops_.empty()) {
                diag_.error(statement.loc, std::string("'") + (statement.kind == StmtKind::Break ? "break" : "continue") +
                                               "' can only be used inside a 'for'");
            } else if (statement.kind == StmtKind::Break) {
                loops_.back() = true;
            }
            return Flow::Jumps;
        case StmtKind::Return: checkReturn(static_cast<ReturnStmt&>(statement)); return Flow::Returns;
        }
        return Flow::Continues;
    }

    void checkVarDecl(VarDeclStmt& decl) {
        Type type = resolveType(decl.typeName).value_or(Type::error());
        if (type.isVoid()) {
            diag_.error(decl.typeName.loc, "a variable cannot be void");
            type = Type::error();
        }
        if (decl.isConst && !decl.init) {
            diag_.error(decl.nameLoc, "a const variable must have a value (D51)");
        }
        if (decl.init && !type.isError()) {
            if (coerceValue(decl.init, type)) checkNotCopied(*decl.init, type);
        }
        decl.variable = declare(decl.name, type, decl.nameLoc, decl.isConst);
    }

    void checkAssign(AssignStmt& assign) {
        Type type = checkTarget(assign.target);
        if (type.isError()) {
            moveAllowed_ = assign.value->kind == ExprKind::Move;
            Type ignored = checkExpr(assign.value, std::nullopt);
            (void)ignored;
            return;
        }
        if (assign.op != AssignOp::Assign) {
            if (!type.isNumeric()) {
                diag_.error(assign.loc, "this compound assignment needs a number, found " + type.name());
                return;
            }
            if (assign.op == AssignOp::Rem && !type.isInt()) {
                diag_.error(assign.loc, "'%' only works on integers (D59)");
                return;
            }
        }
        if (coerceValue(assign.value, type)) checkNotCopied(*assign.value, type);
    }

    void checkIncDec(IncDecStmt& incDec) {
        Type type = checkTarget(incDec.target);
        if (!type.isError() && !type.isInt()) {
            diag_.error(incDec.loc, "'++' and '--' only work on integers, found " + type.name());
        }
    }

    void checkExprStmt(ExprStmt& statement) {
        ExprKind kind = statement.expr->kind;
        if (kind != ExprKind::Call && kind != ExprKind::MethodCall) {
            diag_.error(statement.expr->loc, "this expression does nothing: only function calls can be statements");
            return;
        }
        Type ignored = checkExpr(statement.expr, std::nullopt);
        (void)ignored;
    }

    void checkCondition(ExprPtr& condition) {
        Type type = checkExpr(condition, Type::boolType());
        if (!type.isBool() && !type.isError()) {
            diag_.error(condition->loc, "the condition must be a bool, found " + type.name() +
                                            " (D32): compare it explicitly, for example x != 0");
        }
    }

    Flow checkIf(IfStmt& statement) {
        checkCondition(statement.condition);
        Flow thenFlow = checkBlock(statement.thenBlock);
        if (!statement.elseBlock) return Flow::Continues;
        Flow elseFlow = checkBlock(*statement.elseBlock);
        if (thenFlow == Flow::Continues || elseFlow == Flow::Continues) return Flow::Continues;
        return thenFlow == Flow::Returns && elseFlow == Flow::Returns ? Flow::Returns : Flow::Jumps;
    }

    Flow checkFor(ForStmt& statement) {
        scopes_.emplace_back();  // the variable of the classic form lives only in the loop
        if (statement.init) checkStatement(*statement.init);
        if (statement.condition) checkCondition(statement.condition);
        if (statement.step) checkStatement(*statement.step);
        loops_.push_back(false);
        checkBlock(statement.body);
        bool hasBreak = loops_.back();
        loops_.pop_back();
        scopes_.pop_back();

        // An endless loop without break never falls through (D67).
        bool endless = statement.form == ForForm::Infinite || (statement.form == ForForm::Classic && !statement.condition);
        return endless && !hasBreak ? Flow::Returns : Flow::Continues;
    }

    void checkReturn(ReturnStmt& ret) {
        Type type = current_->resolvedReturn;
        if (type.isError()) return;
        if (type.isVoid()) {
            if (ret.value) {
                diag_.error(ret.value->loc, "function '" + current_->name + "' is void and does not return a value");
            }
            return;
        }
        if (!ret.value) {
            diag_.error(ret.loc, "missing value after 'ret': function '" + current_->name + "' returns " + type.name());
            return;
        }
        if (!coerceValue(ret.value, type)) return;

        // D23: `ret` of a local variable moves it without writing `move`.
        const Expr& value = *ret.value;
        const Variable* variable = value.kind == ExprKind::Name ? static_cast<const NameExpr&>(value).variable : nullptr;
        if (variable && (variable->kind == VariableKind::Local || variable->kind == VariableKind::MoveParam)) return;
        checkNotCopied(value, type);
    }

    // ---- Expressions ----

    // Checks `expr` and makes it of type `target`, adding a lossless conversion if needed (D33).
    bool coerce(ExprPtr& expr, Type target) {
        Type type = checkExpr(expr, target);
        if (type.isError() || target.isError()) return false;
        if (type == target) return true;
        if (type.isVoid()) {
            diag_.error(expr->loc, "this call does not return a value");
            return false;
        }
        if (type.isUntyped()) {
            diag_.error(expr->loc, "this " + type.name() + " cannot be used as " + target.name());
            return false;
        }
        if (convertsLosslessly(type, target)) {
            ExprPtr inner = std::move(expr);
            SourceLocation loc = inner->loc;
            expr = std::make_unique<ConversionExpr>(loc, std::move(inner), target, false);
            return true;
        }
        if (type.isStruct() || target.isStruct()) {
            diag_.error(expr->loc, "a value of type " + type.name() + " cannot be used as " + target.name());
        } else {
            diag_.error(expr->loc, "a value of type " + type.name() + " cannot be used as " + target.name() +
                                       " without an explicit conversion, for example " + target.name() + "(x) (D33)");
        }
        return false;
    }

    Type checkExpr(ExprPtr& expr, std::optional<Type> expected) {
        Type type = computeType(expr, expected);
        expr->type = type;
        return type;
    }

    Type computeType(ExprPtr& expr, std::optional<Type> expected) {
        // Only the expression that coerceValue is checking may be a `move`.
        bool moveAllowed = std::exchange(moveAllowed_, false);
        switch (expr->kind) {
        case ExprKind::IntLiteral: return checkIntLiteral(static_cast<IntLiteralExpr&>(*expr), expected);
        case ExprKind::FloatLiteral: return checkFloatLiteral(static_cast<FloatLiteralExpr&>(*expr), expected);
        case ExprKind::BoolLiteral: return Type::boolType();
        case ExprKind::StringLiteral:
            diag_.error(expr->loc, "strings arrive in milestone M5: for now a string literal can only be printed (D65)");
            return Type::error();
        case ExprKind::Name: return checkName(expr);
        case ExprKind::Unary: return checkUnary(expr, expected);
        case ExprKind::Binary: return checkBinary(static_cast<BinaryExpr&>(*expr), expected);
        case ExprKind::Call: return checkCall(expr);
        case ExprKind::Field: return checkField(static_cast<FieldExpr&>(*expr));
        case ExprKind::MethodCall: return checkMethodCall(static_cast<MethodCallExpr&>(*expr));
        case ExprKind::Self: return Type::structType(current_->owner);
        case ExprKind::Move:
            if (!moveAllowed) {
                diag_.error(expr->loc, "'move' is written only in an initialization, an assignment, the argument of a "
                                       "'move' parameter or after 'ret' (D82)");
                return Type::error();
            }
            return checkMove(static_cast<MoveExpr&>(*expr));
        case ExprKind::Ref:
            diag_.error(expr->loc, "'ref' is written only in the argument of a 'ref' parameter (D22)");
            return Type::error();
        case ExprKind::Conversion:
        case ExprKind::Print:
        case ExprKind::Construct:
        case ExprKind::Copy: return expr->type;
        }
        return Type::error();
    }

    Type checkIntLiteral(IntLiteralExpr& literal, std::optional<Type> expected) {
        if (literal.isChar) return Type::intType(8, false);  // D46
        if (expected && expected->isInt()) {
            if (!fits(literal.magnitude, literal.negative, *expected)) {
                std::string value = (literal.negative && literal.magnitude ? "-" : "") + std::to_string(literal.magnitude);
                diag_.error(literal.loc, "value " + value + " is out of range for " + expected->name() + " (" +
                                             rangeOf(*expected) + ")");
                return Type::error();
            }
            return *expected;
        }
        if (expected && expected->isFloat()) return *expected;  // D55: an integer literal fits a float type too
        return Type::untypedInt();
    }

    Type checkFloatLiteral(FloatLiteralExpr& literal, std::optional<Type> expected) {
        if (!expected || !expected->isFloat()) return Type::untypedFloat();
        const char* begin = literal.text.data();
        const char* end = begin + literal.text.size();
        if (expected->bits == 32) {
            float value = 0;
            std::from_chars(begin, end, value);
            literal.value = value;
        } else {
            double value = 0;
            std::from_chars(begin, end, value);
            literal.value = value;
        }
        if (literal.negative) literal.value = -literal.value;
        return *expected;
    }

    // A local variable or parameter; inside a method also a field (D70).
    Type checkName(ExprPtr& expr) {
        auto& name = static_cast<NameExpr&>(*expr);
        if (Variable* variable = lookup(name.name)) {
            name.variable = variable;
            return variable->type;
        }
        if (ownFieldIndex(name.name) >= 0) {
            SourceLocation loc = name.loc;
            std::string field = name.name;
            expr = std::make_unique<FieldExpr>(loc, std::make_unique<SelfExpr>(loc), field);
            return checkField(static_cast<FieldExpr&>(*expr));
        }
        if (functions_.count(name.name) || isPrintFunction(name.name) ||
            (current_->owner && current_->owner->methods.count(name.name))) {
            diag_.error(name.loc, "'" + name.name + "' is a function: call it with '()'");
        } else {
            diag_.error(name.loc, "unknown name '" + name.name + "'");
        }
        return Type::error();
    }

    Type checkField(FieldExpr& field) {
        Type object = checkExpr(field.object, std::nullopt);
        if (object.isError()) return object;
        if (!object.isStruct()) {
            diag_.error(field.loc, "a " + object.name() + " has no fields");
            return Type::error();
        }
        const auto& fields = object.structDecl->fields;
        for (size_t i = 0; i < fields.size(); ++i) {
            if (fields[i].name == field.field) {
                field.index = static_cast<int>(i);
                return fields[i].resolved;
            }
        }
        diag_.error(field.loc, object.name() + " has no field named '" + field.field + "'");
        return Type::error();
    }

    // Checks one argument against the mode of its parameter (D22).
    bool checkArg(ExprPtr& arg, const Param& param, Type type) {
        std::string name = "parameter '" + param.name + "'";
        switch (param.mode) {
        case ParamMode::Read:
            if (arg->kind == ExprKind::Ref || arg->kind == ExprKind::Move) {
                diag_.error(arg->loc, name + " is read-only: remove '" + (arg->kind == ExprKind::Ref ? "ref" : "move") + "' (D22)");
                return false;
            }
            return coerce(arg, type);
        case ParamMode::Ref: {
            if (arg->kind != ExprKind::Ref) {
                diag_.error(arg->loc, name + " is 'ref': write 'ref' before the argument (D22)");
                return false;
            }
            auto& ref = static_cast<RefExpr&>(*arg);
            Type found = checkExpr(ref.operand, std::nullopt);
            if (found.isError()) return false;
            if (!isPlace(*ref.operand)) {
                diag_.error(ref.loc, "only a variable or a field can be passed with 'ref' (D82)");
                return false;
            }
            std::string reason = readOnlyReason(*ref.operand);
            if (!reason.empty()) {
                diag_.error(ref.loc, "cannot pass '" + placeText(*ref.operand) + "' with 'ref': " + reason);
                return false;
            }
            if (found != type) {
                diag_.error(ref.loc, name + " is 'ref " + type.name() + "': the argument must have exactly that type, found " +
                                         found.name() + " (D82)");
                return false;
            }
            ref.type = found;
            return true;
        }
        case ParamMode::Move: {
            if (arg->kind == ExprKind::Ref) {
                diag_.error(arg->loc, name + " is 'move', not 'ref' (D22)");
                return false;
            }
            if (!coerceValue(arg, type)) return false;
            // A variable or a field gives its value only with `move`; new values do not need it (D22, D23).
            const Expr* value = arg.get();
            if (value->kind == ExprKind::Conversion) value = static_cast<const ConversionExpr&>(*value).operand.get();
            if (isPlace(*value)) {
                diag_.error(value->loc, name + " is 'move': write 'move " + placeText(*value) + "' (D22)");
                return false;
            }
            return true;
        }
        }
        return false;
    }

    // Checks the arguments of a call against the parameters of `function`, then
    // exclusivity (D22, D79). `object` is the object of a method call, if any.
    bool checkArgs(std::vector<ExprPtr>& args, const FunctionDecl& function, const std::string& what, SourceLocation loc,
                   const Expr* object = nullptr) {
        size_t expected = function.params.size();
        if (args.size() != expected) {
            diag_.error(loc, what + " takes " + std::to_string(expected) + (expected == 1 ? " argument, but " : " arguments, but ") +
                                 std::to_string(args.size()) + " were given");
            return false;
        }
        bool ok = true;
        for (size_t i = 0; i < args.size(); ++i) {
            if (function.paramTypes[i].isError()) continue;
            ok = checkArg(args[i], function.params[i], function.paramTypes[i]) && ok;
        }
        return ok && checkExclusivity(args, function, object);
    }

    // D22, D79: what a call modifies cannot overlap anything else the call uses.
    // A read argument of a simple type is passed as a copy instead.
    bool checkExclusivity(std::vector<ExprPtr>& args, const FunctionDecl& function, const Expr* object) {
        struct Access {
            Path path;
            const Expr* expr;
            bool modifies;
            int arg;  // -1 for the object of the method
        };
        std::vector<Access> accesses;
        if (object) {
            if (std::optional<Path> path = pathOf(*object)) accesses.push_back({*path, object, !function.isConst, -1});
        }
        for (size_t i = 0; i < args.size(); ++i) {
            const Expr* arg = args[i].get();
            bool modifies = false;
            if (arg->kind == ExprKind::Ref) {
                arg = static_cast<const RefExpr&>(*arg).operand.get();
                modifies = true;
            } else if (arg->kind == ExprKind::Move) {
                // Only a whole variable is handed over in place; the other moves make a new value.
                const auto& move = static_cast<const MoveExpr&>(*arg);
                if (move.moveKind != MoveKind::Variable) continue;
                arg = move.operand.get();
                modifies = true;
            }
            if (std::optional<Path> path = pathOf(*arg)) accesses.push_back({*path, arg, modifies, static_cast<int>(i)});
        }

        bool ok = true;
        std::vector<bool> copied(args.size(), false);
        for (size_t a = 0; a < accesses.size(); ++a) {
            for (size_t b = a + 1; b < accesses.size(); ++b) {
                const Access& first = accesses[a];
                const Access& second = accesses[b];
                if (!(first.modifies || second.modifies) || !overlaps(first.path, second.path)) continue;
                const Access& read = first.modifies ? second : first;
                if (!read.modifies && read.arg >= 0 && !needsDestruction(read.expr->type)) {
                    copied[read.arg] = true;
                    continue;
                }
                std::string message = "'" + placeText(*second.expr) + "' overlaps '" + placeText(*first.expr) +
                                      "' in the same call, ";
                if (read.modifies) {
                    message += "and the call can modify both (D22)";
                } else if (read.arg < 0) {
                    message += "which is the object of the method (D22)";
                } else {
                    message += "which the call can modify, and values of type " + read.expr->type.name() + " are not copied implicitly (D79)";
                }
                diag_.error(second.expr->loc, message);
                ok = false;
            }
        }
        for (size_t i = 0; i < args.size(); ++i) {
            if (!copied[i]) continue;
            SourceLocation argLoc = args[i]->loc;
            args[i] = std::make_unique<CopyExpr>(argLoc, std::move(args[i]));
        }
        return ok;
    }

    Type checkMethodCall(MethodCallExpr& call) {
        Type object = checkExpr(call.object, std::nullopt);
        if (object.isError()) return object;
        if (!object.isStruct()) {
            diag_.error(call.loc, "a " + object.name() + " has no methods");
            return Type::error();
        }
        FunctionDecl* method = nullptr;
        auto found = object.structDecl->methods.find(call.method);
        if (found != object.structDecl->methods.end()) {
            method = found->second;
        } else if (call.method == "copy") {
            method = copyFunction(*object.structDecl);
            if (!method) {
                diag_.error(call.loc, object.name() + " cannot be copied: it has a destructor, or a field that cannot be "
                                                      "copied, and it does not define copy() (D35)");
                return Type::error();
            }
        } else {
            diag_.error(call.loc, object.name() + " has no method named '" + call.method + "'");
            return Type::error();
        }
        if (!method->isConst) {
            std::string reason = readOnlyReason(*call.object);
            if (!reason.empty()) {
                diag_.error(call.loc, "cannot call '" + call.method + "', which is not const (D24): " +
                                          reason);
                return Type::error();
            }
        }
        call.function = method;
        if (!checkArgs(call.args, *method, "'" + call.method + "'", call.loc, call.object.get())) return Type::error();
        return method->resolvedReturn;
    }

    Type checkUnary(ExprPtr& expr, std::optional<Type> expected) {
        auto& unary = static_cast<UnaryExpr&>(*expr);
        if (unary.op == UnaryOp::Not) {
            Type type = checkExpr(unary.operand, Type::boolType());
            if (!type.isBool()) {
                if (!type.isError()) diag_.error(unary.loc, "'!' needs a bool, found " + type.name());
                return Type::error();
            }
            return Type::boolType();
        }

        // A minus in front of a literal becomes part of the literal, so that
        // -128 fits in an i8.
        Expr& operand = *unary.operand;
        if ((operand.kind == ExprKind::IntLiteral && !static_cast<IntLiteralExpr&>(operand).isChar) ||
            operand.kind == ExprKind::FloatLiteral) {
            if (operand.kind == ExprKind::IntLiteral) {
                auto& literal = static_cast<IntLiteralExpr&>(operand);
                literal.negative = !literal.negative;
            } else {
                auto& literal = static_cast<FloatLiteralExpr&>(operand);
                literal.negative = !literal.negative;
            }
            SourceLocation loc = unary.loc;
            ExprPtr inner = std::move(unary.operand);
            expr = std::move(inner);
            expr->loc = loc;
            return computeType(expr, expected);
        }

        Type type = checkExpr(unary.operand, expected && expected->isNumeric() ? expected : std::nullopt);
        if (type.isError()) return type;
        if (type.isInt() && !type.isSigned) {
            diag_.error(unary.loc, "cannot negate an unsigned value (" + type.name() + ") (D59)");
            return Type::error();
        }
        if (!type.isNumeric()) {
            diag_.error(unary.loc, "'-' needs a number, found " + type.name());
            return Type::error();
        }
        return type;
    }

    Type checkBinary(BinaryExpr& binary, std::optional<Type> expected) {
        if (binary.op == BinaryOp::And || binary.op == BinaryOp::Or) {
            for (ExprPtr* side : {&binary.lhs, &binary.rhs}) {
                Type type = checkExpr(*side, Type::boolType());
                if (!type.isBool()) {
                    if (!type.isError()) {
                        diag_.error((*side)->loc, std::string("'") + spelling(binary.op) + "' needs bool values, found " +
                                                      type.name());
                    }
                    return Type::error();
                }
            }
            binary.operandType = Type::boolType();
            return Type::boolType();
        }

        bool arithmetic = isArithmetic(binary.op);
        std::optional<Type> hint;
        if (arithmetic && expected && expected->isNumeric()) hint = expected;
        Type operand = unify(binary, hint);
        if (operand.isError()) return operand;
        binary.operandType = operand;

        if (operand.isStruct()) {
            diag_.error(binary.loc, std::string("'") + spelling(binary.op) + "' does not work on structs yet (D76)");
            return Type::error();
        }
        if (arithmetic) {
            if (!operand.isNumeric()) {
                diag_.error(binary.loc, std::string("'") + spelling(binary.op) + "' needs numbers, found " + operand.name());
                return Type::error();
            }
            if (binary.op == BinaryOp::Rem && !operand.isInt()) {
                diag_.error(binary.loc, "'%' only works on integers (D59)");
                return Type::error();
            }
            return operand;
        }
        bool equality = binary.op == BinaryOp::Eq || binary.op == BinaryOp::Ne;
        if (!operand.isNumeric() && !(equality && operand.isBool())) {
            diag_.error(binary.loc, std::string("'") + spelling(binary.op) + "' cannot compare " + operand.name() + " values");
            return Type::error();
        }
        return Type::boolType();
    }

    // Finds the common type of the two operands, giving literals the type of the
    // other side (D55) and widening without loss (D33, D56).
    Type unify(BinaryExpr& binary, std::optional<Type> hint) {
        bool leftLiteral = isUntypedLiteral(*binary.lhs);
        bool rightLiteral = isUntypedLiteral(*binary.rhs);
        if (leftLiteral && rightLiteral) {
            if (!hint) {
                diag_.error(binary.loc, "the type of this expression cannot be deduced: write the type of one of the values, "
                                        "for example i32(5) (D55)");
                return Type::error();
            }
            bool ok = coerce(binary.lhs, *hint);
            ok = coerce(binary.rhs, *hint) && ok;
            return ok ? *hint : Type::error();
        }
        if (leftLiteral || rightLiteral) {
            ExprPtr& typed = leftLiteral ? binary.rhs : binary.lhs;
            ExprPtr& literal = leftLiteral ? binary.lhs : binary.rhs;
            Type type = checkExpr(typed, hint);
            if (type.isError()) return type;
            if (!type.isNumeric()) {
                diag_.error(literal->loc, "a number cannot be combined with a value of type " + type.name());
                return Type::error();
            }
            return coerce(literal, type) ? type : Type::error();
        }

        Type left = checkExpr(binary.lhs, hint);
        Type right = checkExpr(binary.rhs, hint);
        if (left.isError() || right.isError()) return Type::error();
        if (left == right) return left;
        if (convertsLosslessly(left, right)) {
            coerce(binary.lhs, right);
            return right;
        }
        if (convertsLosslessly(right, left)) {
            coerce(binary.rhs, left);
            return left;
        }
        if (left.isInt() && right.isInt() && left.isSigned != right.isSigned) {
            diag_.error(binary.loc, "cannot mix " + left.name() + " and " + right.name() +
                                        ": convert one of them explicitly, for example i64(x) (D56)");
        } else {
            diag_.error(binary.loc, "mismatched types " + left.name() + " and " + right.name() +
                                        ": convert one of them explicitly (D33)");
        }
        return Type::error();
    }

    Type checkCall(ExprPtr& expr) {
        auto& call = static_cast<CallExpr&>(*expr);

        if (std::optional<Type> target = findBuiltinType(call.callee)) return checkConversion(expr, *target);
        if (isPrintFunction(call.callee)) return checkPrint(expr);

        auto structFound = structs_.find(call.callee);
        if (structFound != structs_.end()) return checkConstruct(expr, structFound->second);

        // D72: inside a method, a call refers first to a method of the same struct.
        if (current_->owner) {
            auto method = current_->owner->methods.find(call.callee);
            if (method != current_->owner->methods.end()) {
                SourceLocation loc = call.loc;
                auto methodCall = std::make_unique<MethodCallExpr>(loc, std::make_unique<SelfExpr>(loc), call.callee);
                methodCall->args = std::move(call.args);
                expr = std::move(methodCall);
                return checkMethodCall(static_cast<MethodCallExpr&>(*expr));
            }
        }

        auto found = functions_.find(call.callee);
        if (found == functions_.end()) {
            if (lookup(call.callee) || ownFieldIndex(call.callee) >= 0) {
                diag_.error(call.loc, "'" + call.callee + "' is a variable, not a function");
            } else {
                diag_.error(call.loc, "unknown function '" + call.callee + "'");
            }
            for (ExprPtr& arg : call.args) {
                Type ignored = checkExpr(arg, std::nullopt);
                (void)ignored;
            }
            return Type::error();
        }

        FunctionDecl* function = found->second;
        call.function = function;
        if (!checkArgs(call.args, *function, "'" + call.callee + "'", call.loc)) return Type::error();
        return function->resolvedReturn;
    }

    // `Student("Anna")`: calls the constructor; without one, `Point()` is the default value (D71).
    Type checkConstruct(ExprPtr& expr, StructDecl* decl) {
        auto& call = static_cast<CallExpr&>(*expr);
        auto construct = std::make_unique<ConstructExpr>(call.loc, decl);
        construct->args = std::move(call.args);
        expr = std::move(construct);
        auto& node = static_cast<ConstructExpr&>(*expr);
        Type type = Type::structType(decl);

        if (decl->constructor) {
            if (!checkArgs(node.args, *decl->constructor, "the constructor of " + decl->name, node.loc)) return Type::error();
        } else if (!node.args.empty()) {
            diag_.error(node.loc, decl->name + " has no constructor: " + decl->name + "() gives its default value (D71)");
            return Type::error();
        }
        return type;
    }

    // `f64(x)`: explicit conversion (D20, D61).
    Type checkConversion(ExprPtr& expr, Type target) {
        auto& call = static_cast<CallExpr&>(*expr);
        if (call.args.size() != 1) {
            diag_.error(call.loc, "a conversion to " + target.name() + " takes exactly one value");
            return Type::error();
        }
        ExprPtr operand = std::move(call.args[0]);
        Type type;
        if (isUntypedLiteral(*operand)) {
            // A literal takes the target type directly (D55); a decimal literal
            // converted to an integer first becomes an f64.
            type = checkExpr(operand, target);
            if (type.kind == TypeKind::UntypedFloat) type = checkExpr(operand, Type::floatType(64));
        } else {
            type = checkExpr(operand, std::nullopt);
        }
        if (type.isError()) return type;
        if (type.isUntyped() || type.isVoid() || type.kind == TypeKind::String || type.isStruct()) {
            diag_.error(operand->loc, "cannot convert this value to " + target.name());
            return Type::error();
        }
        if (type.isBool() != target.isBool()) {
            diag_.error(call.loc, "cannot convert " + type.name() + " to " + target.name() + " (D61)");
            return Type::error();
        }
        SourceLocation loc = call.loc;
        if (type == target) {
            expr = std::move(operand);
        } else {
            expr = std::make_unique<ConversionExpr>(loc, std::move(operand), target, true);
        }
        return target;
    }

    // ---- print / println (D21, D38) ----

    Type checkPrint(ExprPtr& expr) {
        auto& call = static_cast<CallExpr&>(*expr);
        int stream = call.callee[0] == 'e' ? 2 : 1;
        bool newline = call.callee.size() >= 2 && call.callee.compare(call.callee.size() - 2, 2, "ln") == 0;

        if (call.args.empty() || call.args[0]->kind != ExprKind::StringLiteral) {
            diag_.error(call.loc, "the first argument of " + call.callee + " must be a string literal with the format (D38)");
            return Type::error();
        }
        auto print = std::make_unique<PrintExpr>(call.loc, stream, newline);
        std::string format = static_cast<StringLiteralExpr&>(*call.args[0]).value;
        SourceLocation formatLoc = call.args[0]->loc;

        bool ok = true;
        for (size_t i = 1; i < call.args.size(); ++i) {
            ExprPtr arg = std::move(call.args[i]);
            if (arg->kind == ExprKind::StringLiteral) {
                arg->type = Type::stringType();
            } else {
                Type type = checkExpr(arg, std::nullopt);
                if (type.isUntyped()) {
                    diag_.error(arg->loc, "the type of this literal cannot be deduced: write it explicitly, for example i32(5) (D64)");
                    ok = false;
                } else if (type.isVoid()) {
                    diag_.error(arg->loc, "this call does not return a value");
                    ok = false;
                } else if (type.isStruct()) {
                    diag_.error(arg->loc, "a " + type.name() + " cannot be printed yet (D76)");
                    ok = false;
                } else if (type.isError()) {
                    ok = false;
                }
            }
            print->args.push_back(std::move(arg));
        }

        if (ok) parseFormat(format, formatLoc, *print);
        expr = std::move(print);
        return Type::voidType();
    }

    void parseFormat(const std::string& format, SourceLocation loc, PrintExpr& print) {
        std::string text;
        int nextAuto = 0;
        bool usedAuto = false;
        bool usedManual = false;
        std::vector<bool> used(print.args.size(), false);

        for (size_t i = 0; i < format.size(); ++i) {
            char c = format[i];
            if (c == '}') {
                if (i + 1 < format.size() && format[i + 1] == '}') {
                    text += '}';
                    ++i;
                    continue;
                }
                diag_.error(loc, "single '}' in the format string: write '}}' for a brace");
                return;
            }
            if (c != '{') {
                text += c;
                continue;
            }
            if (i + 1 < format.size() && format[i + 1] == '{') {
                text += '{';
                ++i;
                continue;
            }
            size_t close = format.find('}', i);
            if (close == std::string::npos) {
                diag_.error(loc, "'{' without a matching '}' in the format string: write '{{' for a brace");
                return;
            }
            std::string inside = format.substr(i + 1, close - i - 1);
            i = close;

            if (!text.empty()) {
                print.pieces.push_back(FormatPiece{text});
                text.clear();
            }

            FormatPiece piece;
            size_t colon = inside.find(':');
            std::string id = inside.substr(0, colon);
            if (id.empty()) {
                usedAuto = true;
                piece.arg = nextAuto++;
            } else {
                for (char d : id) {
                    if (d < '0' || d > '9') {
                        diag_.error(loc, "invalid placeholder '{" + inside + "}'");
                        return;
                    }
                }
                usedManual = true;
                piece.arg = std::stoi(id);
            }
            if (usedAuto && usedManual) {
                diag_.error(loc, "cannot mix automatic '{}' and numbered '{0}' placeholders");
                return;
            }
            if (piece.arg >= static_cast<int>(print.args.size())) {
                diag_.error(loc, "the format string needs at least " + std::to_string(piece.arg + 1) + " arguments, but " +
                                     std::to_string(print.args.size()) + " were given");
                return;
            }
            if (colon != std::string::npos) {
                piece.hasSpec = true;
                std::string spec = inside.substr(colon + 1);
                if (!parseSpec(spec, piece.spec)) {
                    diag_.error(loc, "invalid format specification '" + spec + "'");
                    return;
                }
                if (!validateSpec(piece.spec, print.args[piece.arg]->type, loc)) return;
            }
            used[piece.arg] = true;
            print.pieces.push_back(piece);
        }
        if (!text.empty()) print.pieces.push_back(FormatPiece{text});

        for (size_t i = 0; i < used.size(); ++i) {
            if (!used[i]) {
                diag_.error(print.args[i]->loc, "this argument is not used by the format string");
            }
        }
    }

    // [[fill]align][sign]['#']['0'][width]['.' precision][type]
    static bool parseSpec(const std::string& text, FormatSpec& spec) {
        auto isAlign = [](char c) { return c == '<' || c == '>' || c == '^'; };
        auto isDigit = [](char c) { return c >= '0' && c <= '9'; };
        size_t p = 0;
        if (text.size() >= 2 && isAlign(text[1])) {
            if (static_cast<unsigned char>(text[0]) >= 0x80 || text[0] == '{' || text[0] == '}') return false;
            spec.fill = text[0];
            spec.align = text[1];
            p = 2;
        } else if (!text.empty() && isAlign(text[0])) {
            spec.align = text[0];
            p = 1;
        }
        if (p < text.size() && (text[p] == '+' || text[p] == '-' || text[p] == ' ')) spec.sign = text[p++];
        if (p < text.size() && text[p] == '#') {
            spec.alternate = true;
            ++p;
        }
        if (p < text.size() && text[p] == '0') {
            spec.zeroPad = true;
            ++p;
        }
        if (p < text.size() && isDigit(text[p])) {
            int width = 0;
            while (p < text.size() && isDigit(text[p]) && width <= kMaxWidth) width = width * 10 + (text[p++] - '0');
            if (width > kMaxWidth) return false;
            spec.width = width;
        }
        if (p < text.size() && text[p] == '.') {
            ++p;
            if (p >= text.size() || !isDigit(text[p])) return false;
            int precision = 0;
            while (p < text.size() && isDigit(text[p]) && precision <= kMaxPrecision) precision = precision * 10 + (text[p++] - '0');
            if (precision > kMaxPrecision) return false;
            spec.precision = precision;
        }
        if (p < text.size() && std::strchr("dxXbocefgs", text[p])) spec.type = text[p++];
        return p == text.size();
    }

    bool validateSpec(const FormatSpec& spec, Type type, SourceLocation loc) {
        auto fail = [&](const std::string& message) {
            diag_.error(loc, message);
            return false;
        };
        bool numberFlags = spec.sign || spec.alternate || spec.zeroPad;
        if (type.isInt()) {
            if (spec.type && !std::strchr("dxXboc", spec.type)) return fail(std::string("'") + spec.type + "' is not a format for integers");
            if (spec.precision >= 0) return fail("a precision is not allowed for integers");
            if (spec.type == 'c') {
                if (type.bits != 8 || type.isSigned) return fail("'c' prints a character: the value must be a char (u8)");
                if (numberFlags) return fail("'c' only allows fill, alignment and width");
            }
            return true;
        }
        if (type.isFloat()) {
            if (spec.type && !std::strchr("efg", spec.type)) return fail(std::string("'") + spec.type + "' is not a format for floating-point numbers");
            return true;
        }
        // bool and string literals
        if (spec.type && spec.type != 's') return fail(std::string("'") + spec.type + "' is not a format for a " + type.name());
        if (numberFlags) return fail("a " + type.name() + " only allows fill, alignment, width" +
                                     (type.isBool() ? std::string() : std::string(" and precision")));
        if (type.isBool() && spec.precision >= 0) return fail("a precision is not allowed for a bool");
        return true;
    }

    Module& module_;
    Diagnostics& diag_;
    std::map<std::string, FunctionDecl*> functions_;
    std::map<std::string, StructDecl*> structs_;
    std::map<const StructDecl*, int> layoutState_;  // 0 to do, 1 in progress, 2 done
    FunctionDecl* current_ = nullptr;
    std::vector<std::map<std::string, Variable*>> scopes_;
    std::vector<bool> loops_;  // one entry per enclosing loop: true if it contains a break
    bool moveAllowed_ = false;  // set by coerceValue for the expression it checks
};

}  // namespace

bool check(Module& module, Diagnostics& diag) {
    Checker(module, diag).run();
    return !diag.hasErrors();
}

}  // namespace cromo
