#include "sema.h"

#include <charconv>
#include <cstring>
#include <map>
#include <optional>

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

class Checker {
public:
    Checker(Module& module, Diagnostics& diag) : module_(module), diag_(diag) {}

    void run() {
        declareFunctions();
        for (FunctionDecl& function : module_.functions) checkFunction(function);
    }

private:
    // ---- Declarations ----

    std::optional<Type> resolveType(const TypeName& name) {
        if (name.name == "void") return Type::voidType();
        if (auto type = findBuiltinType(name.name)) return type;
        if (name.name == "string" || name.name == "list") {
            diag_.error(name.loc, "the type '" + name.name + "' arrives in milestone M5");
        } else {
            diag_.error(name.loc, "unknown type '" + name.name + "'");
        }
        return std::nullopt;
    }

    void declareFunctions() {
        for (FunctionDecl& function : module_.functions) {
            if (functions_.count(function.name)) {
                diag_.error(function.loc, "function '" + function.name + "' is already defined");
            } else if (function.name.rfind("cromo_", 0) == 0 || function.name == "ExitProcess") {
                diag_.error(function.loc, "the name '" + function.name + "' is reserved for the runtime");
            } else if (isPrintFunction(function.name) || findBuiltinType(function.name)) {
                diag_.error(function.loc, "'" + function.name + "' is a built-in name");
            } else {
                functions_[function.name] = &function;
            }

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

    void checkFunction(FunctionDecl& function) {
        current_ = &function;
        scopes_.assign(1, {});
        loops_.clear();
        for (size_t i = 0; i < function.params.size(); ++i) {
            const Param& param = function.params[i];
            Variable* variable = declare(param.name, function.paramTypes[i], param.loc, false);
            if (variable) variable->isParam = true;
        }

        Flow flow = checkBlock(function.body);

        // D47, D67: a non-void function must not reach its end.
        Type ret = function.resolvedReturn;
        if (!ret.isVoid() && !ret.isError() && flow != Flow::Returns) {
            diag_.error(function.body.close, "function '" + function.name + "' must end with 'ret' on every path: it returns " +
                                                 ret.name());
        }
    }

    // ---- Variables ----

    Variable* lookup(const std::string& name) {
        for (auto scope = scopes_.rbegin(); scope != scopes_.rend(); ++scope) {
            auto found = scope->find(name);
            if (found != scope->end()) return found->second;
        }
        return nullptr;
    }

    Variable* declare(const std::string& name, Type type, SourceLocation loc, bool isConst) {
        if (findBuiltinType(name)) {
            diag_.error(loc, "'" + name + "' is a type name");
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

    // Variable that an assignment, `++` or `--` wants to modify.
    Variable* modifiable(const std::string& name, SourceLocation loc) {
        Variable* variable = lookup(name);
        if (!variable) {
            diag_.error(loc, "unknown name '" + name + "'");
        } else if (variable->isParam) {
            diag_.error(loc, "parameter '" + name + "' cannot be modified: parameters are read-only (D22)");
            return nullptr;
        } else if (variable->isConst) {
            diag_.error(loc, "'" + name + "' is const and cannot be modified (D26)");
            return nullptr;
        }
        return variable;
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
        if (decl.init && !type.isError()) coerce(decl.init, type);
        decl.variable = declare(decl.name, type, decl.nameLoc, decl.isConst);
    }

    void checkAssign(AssignStmt& assign) {
        Variable* variable = modifiable(assign.target, assign.targetLoc);
        if (!variable || variable->type.isError()) {
            Type ignored = checkExpr(assign.value, std::nullopt);
            (void)ignored;
            return;
        }
        Type type = variable->type;
        if (assign.op != AssignOp::Assign) {
            if (!type.isNumeric()) {
                diag_.error(assign.loc, "this compound assignment needs a number, but '" + assign.target + "' is of type " +
                                            type.name());
                return;
            }
            if (assign.op == AssignOp::Rem && !type.isInt()) {
                diag_.error(assign.loc, "'%' only works on integers (D59)");
                return;
            }
        }
        coerce(assign.value, type);
        assign.variable = variable;
    }

    void checkIncDec(IncDecStmt& incDec) {
        Variable* variable = modifiable(incDec.target, incDec.loc);
        if (!variable) return;
        if (!variable->type.isInt()) {
            if (!variable->type.isError()) {
                diag_.error(incDec.loc, "'++' and '--' only work on integers, but '" + incDec.target + "' is of type " +
                                            variable->type.name());
            }
            return;
        }
        incDec.variable = variable;
    }

    void checkExprStmt(ExprStmt& statement) {
        if (statement.expr->kind != ExprKind::Call) {
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
        coerce(ret.value, type);
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
        diag_.error(expr->loc, "a value of type " + type.name() + " cannot be used as " + target.name() +
                                   " without an explicit conversion, for example " + target.name() + "(x) (D33)");
        return false;
    }

    Type checkExpr(ExprPtr& expr, std::optional<Type> expected) {
        Type type = computeType(expr, expected);
        expr->type = type;
        return type;
    }

    Type computeType(ExprPtr& expr, std::optional<Type> expected) {
        switch (expr->kind) {
        case ExprKind::IntLiteral: return checkIntLiteral(static_cast<IntLiteralExpr&>(*expr), expected);
        case ExprKind::FloatLiteral: return checkFloatLiteral(static_cast<FloatLiteralExpr&>(*expr), expected);
        case ExprKind::BoolLiteral: return Type::boolType();
        case ExprKind::StringLiteral:
            diag_.error(expr->loc, "strings arrive in milestone M5: for now a string literal can only be printed (D65)");
            return Type::error();
        case ExprKind::Name: return checkName(static_cast<NameExpr&>(*expr));
        case ExprKind::Unary: return checkUnary(expr, expected);
        case ExprKind::Binary: return checkBinary(static_cast<BinaryExpr&>(*expr), expected);
        case ExprKind::Call: return checkCall(expr);
        case ExprKind::Conversion:
        case ExprKind::Print: return expr->type;
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

    Type checkName(NameExpr& name) {
        if (Variable* variable = lookup(name.name)) {
            name.variable = variable;
            return variable->type;
        }
        if (functions_.count(name.name) || isPrintFunction(name.name)) {
            diag_.error(name.loc, "'" + name.name + "' is a function: call it with '()'");
        } else {
            diag_.error(name.loc, "unknown name '" + name.name + "'");
        }
        return Type::error();
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

        auto found = functions_.find(call.callee);
        if (found == functions_.end()) {
            if (lookup(call.callee)) {
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
        if (call.args.size() != function->params.size()) {
            size_t expected = function->params.size();
            diag_.error(call.loc, "'" + call.callee + "' takes " + std::to_string(expected) +
                                      (expected == 1 ? " argument, but " : " arguments, but ") +
                                      std::to_string(call.args.size()) + " were given");
            return Type::error();
        }
        bool ok = true;
        for (size_t i = 0; i < call.args.size(); ++i) {
            if (function->paramTypes[i].isError()) continue;
            ok = coerce(call.args[i], function->paramTypes[i]) && ok;
        }
        call.function = function;
        return ok ? function->resolvedReturn : Type::error();
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
        if (type.isUntyped() || type.isVoid() || type.kind == TypeKind::String) {
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
    FunctionDecl* current_ = nullptr;
    std::vector<std::map<std::string, Variable*>> scopes_;
    std::vector<bool> loops_;  // one entry per enclosing loop: true if it contains a break
};

}  // namespace

bool check(Module& module, Diagnostics& diag) {
    Checker(module, diag).run();
    return !diag.hasErrors();
}

}  // namespace cromo
