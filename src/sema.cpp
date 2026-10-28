#include "sema.h"

#include <set>

namespace cromo {

namespace {

std::string toString(const ConstInt& value) {
    std::string digits = std::to_string(value.magnitude);
    return value.negative && value.magnitude != 0 ? "-" + digits : digits;
}

bool fits(const ConstInt& value, const IntType& type) {
    if (value.negative && value.magnitude != 0) {
        if (!type.isSigned) return false;
        return value.magnitude <= (uint64_t{1} << (type.bits - 1));
    }
    uint64_t max = type.isSigned ? (uint64_t{1} << (type.bits - 1)) - 1
                   : type.bits == 64 ? UINT64_MAX
                                     : (uint64_t{1} << type.bits) - 1;
    return value.magnitude <= max;
}

std::string rangeOf(const IntType& type) {
    if (type.isSigned) {
        uint64_t limit = uint64_t{1} << (type.bits - 1);
        return "-" + std::to_string(limit) + " .. " + std::to_string(limit - 1);
    }
    uint64_t max = type.bits == 64 ? UINT64_MAX : (uint64_t{1} << type.bits) - 1;
    return "0 .. " + std::to_string(max);
}

bool isKnownButUnsupported(const std::string& name) {
    return name == "bool" || name == "f32" || name == "f64" || name == "string" || name == "list";
}

class Checker {
public:
    explicit Checker(Diagnostics& diag) : diag_(diag) {}

    void run(const Module& module) {
        std::set<std::string> names;
        const FunctionDecl* mainFunction = nullptr;
        for (const FunctionDecl& function : module.functions) {
            if (!names.insert(function.name).second) {
                diag_.error(function.loc, "function '" + function.name + "' is already defined");
            }
            if (function.name == "cromo_start" || function.name == "ExitProcess") {
                diag_.error(function.loc, "the name '" + function.name + "' is reserved for the runtime");
            }
            if (function.name == "main") mainFunction = &function;
            checkFunction(function);
        }

        if (!mainFunction) {
            diag_.error("missing function main");
        } else if (mainFunction->returnType.name != "void" && mainFunction->returnType.name != "i32") {
            diag_.error(mainFunction->returnType.loc, "main must return i32 or void");
        }
    }

private:
    void checkFunction(const FunctionDecl& function) {
        const TypeName& type = function.returnType;
        bool isVoid = type.name == "void";
        std::optional<IntType> intType;
        if (!isVoid) {
            intType = findIntType(type.name);
            if (!intType) {
                if (isKnownButUnsupported(type.name)) {
                    diag_.error(type.loc, "type '" + type.name + "' is not supported yet (milestone M1)");
                } else {
                    diag_.error(type.loc, "unknown type: '" + type.name + "'");
                }
                return;
            }
        }

        bool returned = false;
        for (const auto& statement : function.body.statements) {
            if (returned) {
                diag_.warning(statement->loc, "unreachable code after 'ret'");
                break;
            }
            if (statement->kind == StmtKind::Return) {
                const auto& ret = static_cast<const ReturnStmt&>(*statement);
                checkReturn(ret, function, intType);
                returned = true;
            }
        }

        // D47: a non-void function must end with a ret on every path.
        if (!isVoid && !returned) {
            diag_.error(function.body.close,
                        "function '" + function.name + "' must end with 'ret': it returns " + type.name);
        }
    }

    void checkReturn(const ReturnStmt& ret, const FunctionDecl& function, const std::optional<IntType>& type) {
        if (!type) {
            if (ret.value) {
                diag_.error(ret.value->loc, "function '" + function.name + "' is void and does not return a value");
            }
            return;
        }
        if (!ret.value) {
            diag_.error(ret.loc, "missing value after 'ret': function '" + function.name + "' returns " +
                                     function.returnType.name);
            return;
        }
        ConstInt value = evaluateConstant(*ret.value);
        if (!fits(value, *type)) {
            diag_.error(ret.value->loc, "value " + toString(value) + " is out of range for " +
                                            function.returnType.name + " (" + rangeOf(*type) + ")");
        }
    }

    Diagnostics& diag_;
};

}  // namespace

std::optional<IntType> findIntType(const std::string& name) {
    static const IntType types[] = {
        {"i8", 8, true},   {"i16", 16, true},  {"i32", 32, true},  {"i64", 64, true},
        {"u8", 8, false},  {"u16", 16, false}, {"u32", 32, false}, {"u64", 64, false},
    };
    std::string canonical = name == "char" ? "u8" : name;  // D46
    for (const IntType& type : types) {
        if (type.name == canonical) return type;
    }
    return std::nullopt;
}

ConstInt evaluateConstant(const Expr& expr) {
    switch (expr.kind) {
    case ExprKind::IntegerLiteral:
        return ConstInt{false, static_cast<const IntegerLiteralExpr&>(expr).value};
    case ExprKind::Unary: {
        ConstInt value = evaluateConstant(*static_cast<const UnaryExpr&>(expr).operand);
        value.negative = !value.negative;
        return value;
    }
    }
    return {};
}

bool check(const Module& module, Diagnostics& diag) {
    Checker(diag).run(module);
    return !diag.hasErrors();
}

}  // namespace cromo
