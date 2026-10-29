#include "sema.h"

#include <algorithm>
#include <charconv>
#include <cstring>
#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <tuple>
#include <utility>

namespace cromo {

namespace {

constexpr int kMaxWidth = 1000;
constexpr int kMaxPrecision = 500;

// How a statement ends: it falls through, it leaves the function (or loops
// forever), or it jumps with break/continue.
enum class Flow { Continues, Returns, Jumps };

bool isPrintFunction(const std::string& name) {
    return name == "print" || name == "println" || name == "eprint" || name == "eprintln" || name == "format";
}

// The functions that every program can call without declaring them (D38, D102).
bool isBuiltinFunction(const std::string& name) { return isPrintFunction(name) || name == "panic"; }

// The built-in functions of the `unsafe` dialect, only in the standard library (D98).
std::optional<Intrinsic> findIntrinsic(const std::string& name) {
    static const std::map<std::string, Intrinsic> intrinsics = {
        {"alloc", Intrinsic::Alloc},     {"resize", Intrinsic::Resize},   {"free", Intrinsic::Free},
        {"sizeOf", Intrinsic::SizeOf},   {"offset", Intrinsic::Offset},   {"take", Intrinsic::Take},
        {"put", Intrinsic::Put},         {"destroy", Intrinsic::Destroy}, {"copyAt", Intrinsic::CopyAt},
        {"copyMemory", Intrinsic::CopyMemory}, {"compareMemory", Intrinsic::CompareMemory},
        {"checkIndex", Intrinsic::CheckIndex}, {"argumentCount", Intrinsic::ArgumentCount},
        {"argumentLength", Intrinsic::ArgumentLength}, {"argumentData", Intrinsic::ArgumentData},
        {"decimalToFloat", Intrinsic::DecimalToFloat}, {"hashBytes", Intrinsic::HashBytes},
        {"systemCall", Intrinsic::SystemCall}, {"environment", Intrinsic::Environment},
        {"exitProcess", Intrinsic::ExitProcess},
    };
    auto found = intrinsics.find(name);
    if (found == intrinsics.end()) return std::nullopt;
    return found->second;
}

// An enum whose cases have no data: it can be compared and converted to numbers (D112).
bool withoutData(const StructDecl& decl) {
    for (const EnumCase& item : decl.cases) {
        if (!item.fields.empty()) return false;
    }
    return true;
}

bool isBitwise(BinaryOp op) { return op == BinaryOp::BitAnd || op == BinaryOp::BitOr || op == BinaryOp::BitXor; }

bool isArithmetic(BinaryOp op) {
    return op == BinaryOp::Add || op == BinaryOp::Sub || op == BinaryOp::Mul || op == BinaryOp::Div ||
           op == BinaryOp::Rem || isBitwise(op);
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
    case BinaryOp::BitAnd: return "&";
    case BinaryOp::BitOr: return "|";
    case BinaryOp::BitXor: return "^";
    case BinaryOp::Shl: return "<<";
    case BinaryOp::Shr: return ">>";
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
        if (unary.op == UnaryOp::Not) return unary.operand->kind != ExprKind::FloatLiteral && isUntypedLiteral(*unary.operand);
        return isUntypedLiteral(*unary.operand);
    }
    case ExprKind::Binary: {  // a shift takes the type of what it shifts (D195)
        const auto& binary = static_cast<const BinaryExpr&>(expr);
        if (binary.op == BinaryOp::Shl || binary.op == BinaryOp::Shr) return isUntypedLiteral(*binary.lhs);
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
    return expr.kind == ExprKind::Construct || expr.kind == ExprKind::Call || expr.kind == ExprKind::MethodCall ||
           expr.kind == ExprKind::StringLiteral || expr.kind == ExprKind::EnumCase || expr.kind == ExprKind::Lambda ||
           expr.kind == ExprKind::CallValue || expr.kind == ExprKind::Print;  // format gives a new string
}

// A variable, or a field or an element (D35) of a variable or of the object of a method.
bool isPlace(const Expr& expr) {
    if (expr.kind == ExprKind::Name) return true;
    const Expr* object = nullptr;
    if (expr.kind == ExprKind::Field) object = static_cast<const FieldExpr&>(expr).object.get();
    if (expr.kind == ExprKind::Index) object = static_cast<const IndexExpr&>(expr).object.get();
    return object && (object->kind == ExprKind::Self || isPlace(*object));
}

// The place as written in the source, for the messages: `s.log`, `v[i]`.
std::string placeText(const Expr& expr) {
    if (expr.kind == ExprKind::Name) return static_cast<const NameExpr&>(expr).name;
    if (expr.kind == ExprKind::Field) {
        const auto& field = static_cast<const FieldExpr&>(expr);
        if (field.object->kind == ExprKind::Self) return field.field;
        return placeText(*field.object) + "." + field.field;
    }
    if (expr.kind == ExprKind::Index) {
        const auto& element = static_cast<const IndexExpr&>(expr);
        const Expr& index = *element.index;
        std::string text = "...";
        if (index.kind == ExprKind::Name) text = static_cast<const NameExpr&>(index).name;
        if (index.kind == ExprKind::IntLiteral) text = std::to_string(static_cast<const IntLiteralExpr&>(index).magnitude);
        return placeText(*element.object) + "[" + text + "]";
    }
    return "the object";
}

// One step from a variable towards a place: a field, or an element.
struct Step {
    int field = -1;
    const Expr* index = nullptr;  // for an element
};

// The object reached by a place: a variable (null for the object of the method)
// and the steps from it.
struct Path {
    const Variable* variable = nullptr;
    std::vector<Step> steps;
};

std::optional<Path> pathOf(const Expr& expr) {
    switch (expr.kind) {
    case ExprKind::Name: return Path{static_cast<const NameExpr&>(expr).variable, {}};
    case ExprKind::Self: return Path{};
    case ExprKind::Field: {
        const auto& field = static_cast<const FieldExpr&>(expr);
        std::optional<Path> path = pathOf(*field.object);
        if (path) path->steps.push_back(Step{field.index, nullptr});
        return path;
    }
    case ExprKind::Index: {
        const auto& element = static_cast<const IndexExpr&>(expr);
        std::optional<Path> path = pathOf(*element.object);
        if (path) path->steps.push_back(Step{-1, element.index.get()});
        return path;
    }
    default: return std::nullopt;
    }
}

// The value of an index known at compile time.
std::optional<int64_t> constantIndex(const Expr& index) {
    if (index.kind != ExprKind::IntLiteral) return std::nullopt;
    const auto& literal = static_cast<const IntLiteralExpr&>(index);
    auto value = static_cast<int64_t>(literal.magnitude);
    return literal.negative ? -value : value;
}

enum class Overlap { No, Yes, Maybe };

// Two places overlap when one contains the other: `a` and `a.x`, but not `a.x`
// and `a.y`. Elements whose indices are not constants may overlap: that is
// known only at runtime (D35).
Overlap overlaps(const Path& a, const Path& b) {
    if (a.variable != b.variable) return Overlap::No;
    bool maybe = false;
    size_t common = std::min(a.steps.size(), b.steps.size());
    for (size_t i = 0; i < common; ++i) {
        const Step& x = a.steps[i];
        const Step& y = b.steps[i];
        if (!x.index && !y.index) {
            if (x.field != y.field) return Overlap::No;
            continue;
        }
        if (!x.index || !y.index) return Overlap::No;
        std::optional<int64_t> left = constantIndex(*x.index);
        std::optional<int64_t> right = constantIndex(*y.index);
        if (left && right) {
            if (*left != *right) return Overlap::No;
            continue;
        }
        maybe = true;
    }
    return maybe ? Overlap::Maybe : Overlap::Yes;
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

// The names declared in a source file, a module (D50).
struct ModuleScope {
    std::map<std::string, StructDecl*> structs;
    std::map<std::string, InterfaceDecl*> interfaces;
    std::map<std::string, FunctionDecl*> functions;
};

class Checker {
public:
    Checker(Module& module, Diagnostics& diag, bool requireMain)
        : module_(module), diag_(&diag), requireMain_(requireMain) {}

    void run() {
        declareNames();
        ModuleScope& library = modules_[kLibraryFile];
        stringDecl_ = library.structs.count("string") ? library.structs["string"] : nullptr;
        listDecl_ = library.structs.count("list") ? library.structs["list"] : nullptr;
        mapDecl_ = library.structs.count("map") ? library.structs["map"] : nullptr;
        keyInterface_ = library.interfaces.count("Key") ? library.interfaces["Key"] : nullptr;
        exceptionDecl_ = library.interfaces.count("Exception") ? library.interfaces["Exception"] : nullptr;
        declareInterfaces();
        declareTypeParams();
        // Every generic is checked once, with its type parameters as types (D52).
        for (StructDecl& decl : module_.structs) {
            if (!decl.typeParams.empty()) instantiateStruct(decl, ownParams(decl.typeParams), decl.loc);
        }
        for (FunctionDecl& function : module_.functions) {
            if (!function.typeParams.empty()) instantiateFunction(function, ownParams(function.typeParams), function.loc);
        }
        for (StructDecl& decl : module_.structs) {
            if (decl.typeParams.empty()) completeStruct(decl);
        }
        declareFunctions();
        checkPublicSignatures();
        checkBodies(definitions_);
        for (StructDecl& decl : module_.structs) {
            if (!decl.typeParams.empty()) continue;
            for (FunctionDecl& member : decl.members) checkFunction(member);
        }
        for (FunctionDecl& function : module_.functions) {
            if (function.typeParams.empty() && !function.isExtern) checkFunction(function);
        }
        checkBodies(instances_);  // it grows while the instances ask for others
        computeThrows();
    }

private:
    // A function body waiting to be checked: an instance of a generic.
    struct PendingBody {
        FunctionDecl* function;
        const void* generic;  // the generic struct or function it comes from
        bool definition;      // the check of the generic itself (D52)
        std::string context;  // added to the messages
    };

    void checkBodies(std::vector<PendingBody>& queue) {
        for (size_t i = 0; i < queue.size(); ++i) {
            PendingBody pending = queue[i];  // a copy: the queue can grow meanwhile
            if (!pending.definition && failed_.count(pending.generic)) continue;  // already reported at the definition
            int before = diag_->errorCount();
            diag_->setContext(pending.context);
            checkFunction(*pending.function);
            diag_->setContext("");
            if (pending.definition && diag_->errorCount() > before) failed_.insert(pending.generic);
        }
    }

    // Sets the type parameters visible while resolving types, until the end of the scope.
    class BindingsScope {
    public:
        BindingsScope(const TypeBindings*& slot, const TypeBindings* value) : slot_(slot), saved_(slot) { slot = value; }
        ~BindingsScope() { slot_ = saved_; }
        BindingsScope(const BindingsScope&) = delete;
        BindingsScope& operator=(const BindingsScope&) = delete;

    private:
        const TypeBindings*& slot_;
        const TypeBindings* saved_;
    };

    // ---- Declarations ----

    // The names of each module (D50). The symbols have the module in front, so
    // that two modules can use the same names; a C function keeps its own (D128).
    void declareNames() {
        modules_.resize(std::max<size_t>(module_.files.size(), kLibraryFile + 1));
        for (InterfaceDecl& decl : module_.interfaces) {
            ModuleScope& scope = modules_[scopeOf(decl.loc.file)];
            decl.symbol = symbolFor(decl.loc, decl.name);
            if (scope.interfaces.count(decl.name)) {
                diag_->error(decl.loc, "interface '" + decl.name + "' is already defined");
            } else if (findBuiltinType(decl.name) || isBuiltinFunction(decl.name) || decl.name == "void" || decl.name == "Self") {
                diag_->error(decl.loc, "'" + decl.name + "' is a built-in name");
            } else {
                scope.interfaces[decl.name] = &decl;
            }
        }
        for (StructDecl& decl : module_.structs) {
            ModuleScope& scope = modules_[scopeOf(decl.loc.file)];
            decl.symbol = symbolFor(decl.loc, decl.name);
            if (scope.structs.count(decl.name)) {
                diag_->error(decl.loc, "struct '" + decl.name + "' is already defined");
            } else if (findBuiltinType(decl.name) || isBuiltinFunction(decl.name) || decl.name == "void" || decl.name == "Self") {
                diag_->error(decl.loc, "'" + decl.name + "' is a built-in name");
            } else if (scope.interfaces.count(decl.name)) {
                diag_->error(decl.loc, "'" + decl.name + "' is already the name of an interface");
            } else {
                scope.structs[decl.name] = &decl;
            }
        }
        for (FunctionDecl& function : module_.functions) {
            ModuleScope& scope = modules_[scopeOf(function.loc.file)];
            function.symbol = function.isExtern ? function.name : symbolFor(function.loc, function.name);
            if (scope.functions.count(function.name)) {
                diag_->error(function.loc, "function '" + function.name + "' is already defined");
            } else if (function.isExtern && function.name.rfind("cromo_", 0) == 0) {
                diag_->error(function.loc, "the name '" + function.name + "' is reserved for the runtime");
            } else if (isBuiltinFunction(function.name) || findBuiltinType(function.name)) {
                diag_->error(function.loc, "'" + function.name + "' is a built-in name");
            } else if (scope.structs.count(function.name) || scope.interfaces.count(function.name)) {
                diag_->error(function.loc, "'" + function.name + "' is already the name of a type");
            } else {
                scope.functions[function.name] = &function;
            }
        }
    }

    const ModuleFile& fileOf(int file) const {
        static const ModuleFile none;
        return file >= 0 && static_cast<size_t>(file) < module_.files.size() ? module_.files[file] : none;
    }

    std::string symbolFor(SourceLocation loc, const std::string& name) const {
        const std::string& module = fileOf(loc.file).name;
        return module.empty() ? name : module + "." + name;
    }

    // The file whose names a file shares: itself, or the one without the suffix of
    // the system for `fs_windows.cro` (D202).
    int scopeOf(int file) const {
        int part = fileOf(file).part;
        return part >= 0 ? part : file;
    }

    // D127: the module wrote `import unsafe`.
    bool isUnsafe(SourceLocation loc) const { return fileOf(loc.file).isUnsafe; }

    bool importsQualifier(SourceLocation loc, const std::string& module) const {
        for (const ImportDecl& import : fileOf(loc.file).imports) {
            if (import.qualifier() == module) return true;
        }
        return false;
    }

    // What a name means in a file: its own declarations first, then the public
    // ones of the modules it imports (D126), then those of the standard library,
    // so that a name added to the library never breaks a program (D131). `module`
    // is the name written in front: `geometry.Circle`.
    template <class T>
    T* findDecl(std::map<std::string, T*> ModuleScope::*table, const std::string& name, SourceLocation at,
                const std::string& module = {}) {
        if (module.empty()) {
            auto& own = modules_[scopeOf(at.file)].*table;
            auto found = own.find(name);
            if (found != own.end()) return found->second;
        }
        T* result = nullptr;
        std::string resultModule;
        for (const ImportDecl& import : fileOf(at.file).imports) {
            if (!module.empty() && import.qualifier() != module) continue;
            for (int file : import.files) {
                auto& names = modules_[scopeOf(file)].*table;
                auto found = names.find(name);
                if (found == names.end() || !found->second->isPublic || found->second == result) continue;
                if (result) {
                    reportAmbiguous(name, at, resultModule, import.qualifier());
                    return result;
                }
                result = found->second;
                resultModule = import.qualifier();
            }
        }
        if (result || !module.empty() || at.file == kLibraryFile) return result;
        auto& library = modules_[kLibraryFile].*table;
        auto found = library.find(name);
        return found != library.end() && found->second->isPublic ? found->second : nullptr;
    }

    void reportAmbiguous(const std::string& name, SourceLocation at, const std::string& first, const std::string& second) {
        if (!ambiguous_.insert({at.file, at.line, at.column}).second) return;
        std::string where = first == second ? "twice in " + first : "in both " + first + " and " + second;
        diag_->error(at, "'" + name + "' is " + where + ": write " + first + "." + name +
                             (first == second ? "" : " or " + second + "." + name) + " (D126)");
    }

    // The imported module that has a name, but not public: for a clearer error.
    std::string privateIn(const std::string& name, SourceLocation at, const std::string& module = {}) const {
        for (const ImportDecl& import : fileOf(at.file).imports) {
            if (!module.empty() && import.qualifier() != module) continue;
            for (int file : import.files) {
                const ModuleScope& scope = modules_[scopeOf(file)];
                if (scope.structs.count(name) || scope.interfaces.count(name) || scope.functions.count(name)) {
                    return import.qualifier();
                }
            }
        }
        return "";
    }

    // D50, D132: a member is visible in its own module, or everywhere when it is
    // public. The methods that implement an interface can be called through it
    // anyway, so they need no `public`.
    bool visible(bool isPublic, const StructDecl& owner, SourceLocation at) const {
        return isPublic || scopeOf(owner.loc.file) == scopeOf(at.file);
    }

    bool memberVisible(const FunctionDecl& member, const StructDecl& owner, SourceLocation at) {
        if (visible(member.isPublic, owner, at) || member.isGenerated) return true;
        if (member.kind != FunctionKind::Method) return false;
        for (const InterfaceDecl* iface : implementsOf(owner)) {
            for (const FunctionDecl& method : iface->methods) {
                if (method.name == member.name) return true;
            }
        }
        return false;
    }

    void reportPrivate(SourceLocation loc, const std::string& what) {
        diag_->error(loc, what + " is private to its module: it is not marked public (D50)");
    }

    // The signatures of the interface methods, where `Self` is allowed (D84).
    void declareInterfaces() {
        selfAllowed_ = true;
        for (InterfaceDecl& decl : module_.interfaces) {
            std::map<std::string, const FunctionDecl*> names;
            for (FunctionDecl& method : decl.methods) {
                if (names.count(method.name)) {
                    diag_->error(method.loc, "method '" + method.name + "' is already defined in " + decl.name);
                }
                names[method.name] = &method;
                if (!method.typeParams.empty()) {
                    diag_->error(method.typeParams[0].loc, "methods with type parameters of their own will come later (D92)");
                }
                resolveSignature(method);
                bool usesSelf = method.resolvedReturn.kind == TypeKind::SelfType;
                for (Type type : method.paramTypes) usesSelf = usesSelf || type.kind == TypeKind::SelfType;
                decl.usesSelf = decl.usesSelf || usesSelf;
            }
        }
        selfAllowed_ = false;
    }

    // The constraints of the type parameters of every generic (D52).
    void declareTypeParams() {
        auto declare = [this](std::vector<TypeParam>& params) {
            std::map<std::string, bool> names;
            for (TypeParam& param : params) {
                if (names.count(param.name)) diag_->error(param.loc, "type parameter '" + param.name + "' is already defined");
                names[param.name] = true;
                if (findBuiltinType(param.name) || findDecl(&ModuleScope::structs, param.name, param.loc) ||
                    findDecl(&ModuleScope::interfaces, param.name, param.loc) || param.name == "Self") {
                    diag_->error(param.loc, "'" + param.name + "' is already the name of a type");
                }
                for (const TypeName& constraint : param.constraints) {
                    InterfaceDecl* found = findDecl(&ModuleScope::interfaces, constraint.name, constraint.loc, constraint.module);
                    if (!found || !constraint.args.empty()) {
                        diag_->error(constraint.loc, "a constraint must be an interface, found '" + constraint.name + "' (D52)");
                    } else {
                        param.interfaces.push_back(found);
                    }
                }
            }
        };
        for (StructDecl& decl : module_.structs) declare(decl.typeParams);
        for (FunctionDecl& function : module_.functions) declare(function.typeParams);
    }

    static std::vector<Type> ownParams(const std::vector<TypeParam>& params) {
        std::vector<Type> types;
        for (const TypeParam& param : params) types.push_back(Type::paramType(&param));
        return types;
    }

    static bool isDefinitionArgs(const std::vector<TypeParam>& params, const std::vector<Type>& args) {
        for (size_t i = 0; i < args.size(); ++i) {
            if (!args[i].isParam() || args[i].param != &params[i]) return false;
        }
        return true;
    }

    // `Pair<i32, Point>` for the messages; with `symbols`, the types have their
    // module in front: `geometry.Pair<i32, geometry.Point>`.
    static std::string instanceName(const std::string& name, const std::vector<Type>& args, bool symbols = false) {
        std::string result = name + "<";
        for (size_t i = 0; i < args.size(); ++i) result += (i ? ", " : "") + (symbols ? symbolOf(args[i]) : args[i].name());
        return result + ">";
    }

    static std::string symbolOf(Type type) {
        if (type.isStruct()) return type.structDecl->symbol;
        if (type.isInterface()) return type.interfaceDecl->symbol;
        return type.name();
    }

    // Identifies an instance: the generic and the identity of each type argument.
    static std::string instanceKey(const void* generic, const std::vector<Type>& args) {
        auto pointer = [](const void* p) { return std::to_string(reinterpret_cast<uintptr_t>(p)); };
        std::string key = pointer(generic);
        for (Type arg : args) {
            key += "|" + std::to_string(static_cast<int>(arg.kind)) + ":" + std::to_string(arg.bits) + ":" +
                   std::to_string(arg.isSigned) + ":" + pointer(arg.structDecl) + ":" + pointer(arg.param) + ":" +
                   pointer(arg.sig) + ":" + pointer(arg.array);
        }
        return key;
    }

    // How deep the type arguments are nested: Pair<Pair<i32>> is 2.
    static int nesting(Type type) {
        if (!type.isStruct() || !type.structDecl->generic) return 0;
        int deepest = 0;
        for (Type arg : type.structDecl->typeArgs) deepest = std::max(deepest, nesting(arg));
        return deepest + 1;
    }

    static TypeBindings bind(const std::vector<TypeParam>& params, const std::vector<Type>& args) {
        TypeBindings bindings;
        for (size_t i = 0; i < params.size(); ++i) bindings.emplace_back(params[i].name, args[i]);
        return bindings;
    }

    // Whether a type implements an interface, as a constraint requires (D52, D87).
    bool satisfies(Type type, const InterfaceDecl* iface) {
        if (iface == keyInterface_ && (type.isNumeric() || type.isBool())) return true;  // D198
        std::vector<const InterfaceDecl*> list;
        if (type.isStruct()) list = implementsOf(*type.structDecl);
        if (type.isParam()) list = type.param->interfaces;
        return std::find(list.begin(), list.end(), iface) != list.end();
    }

    bool checkConstraints(const std::vector<TypeParam>& params, const std::vector<Type>& args, const std::string& what,
                          SourceLocation loc) {
        bool ok = true;
        for (size_t i = 0; i < params.size(); ++i) {
            for (const InterfaceDecl* iface : params[i].interfaces) {
                if (satisfies(args[i], iface)) continue;
                std::string reason = args[i].isStruct() || args[i].isParam() ? " (D52)" : " (D52, D87)";
                diag_->error(loc, args[i].name() + " does not implement " + iface->name + ", required by " + params[i].name +
                                      " of " + what + reason);
                ok = false;
            }
        }
        return ok;
    }

    bool tooDeep(const std::vector<Type>& args, const std::string& name, SourceLocation loc) {
        for (Type arg : args) {
            if (nesting(arg) < kMaxNesting) continue;
            diag_->error(loc, "the instances of " + name + " are nested too deeply: is a generic using itself with a "
                                                     "bigger type?");
            return true;
        }
        return false;
    }

    // An instance of a generic struct, created and completed the first time it is used.
    StructDecl* instantiateStruct(StructDecl& generic, const std::vector<Type>& args, SourceLocation useLoc) {
        std::string key = instanceKey(&generic, args);
        auto found = structInstances_.find(key);
        if (found != structInstances_.end()) return found->second;
        bool definition = isDefinitionArgs(generic.typeParams, args);
        if (!definition && failed_.count(&generic)) return nullptr;
        if (!checkConstraints(generic.typeParams, args, generic.name, useLoc) || tooDeep(args, generic.name, useLoc)) {
            return nullptr;
        }

        auto instance = std::make_unique<StructDecl>();
        instance->name = instanceName(generic.name, args);
        instance->symbol = instanceName(generic.symbol, args, true);
        instance->loc = generic.loc;
        instance->isPublic = generic.isPublic;
        instance->interfaces = generic.interfaces;
        instance->isEnum = generic.isEnum;
        for (const EnumCase& item : generic.cases) {
            EnumCase copy;
            copy.name = item.name;
            copy.loc = item.loc;
            if (item.valueExpr) copy.valueExpr = clone(*item.valueExpr);
            for (const FieldDecl& field : item.fields) copy.fields.push_back(clone(field));
            instance->cases.push_back(std::move(copy));
        }
        for (const FieldDecl& field : generic.fields) instance->fields.push_back(clone(field));
        for (const FunctionDecl& member : generic.members) instance->members.push_back(clone(member));
        instance->generic = &generic;
        instance->typeArgs = args;
        instance->typeBindings = bind(generic.typeParams, args);
        for (Type arg : args) instance->isAbstract = instance->isAbstract || isAbstract(arg);
        StructDecl* result = instance.get();
        module_.structInstances.push_back(std::move(instance));
        structInstances_[key] = result;

        int before = diag_->errorCount();
        completeStruct(*result);
        if (definition && diag_->errorCount() > before) failed_.insert(&generic);
        if (definition) {
            for (FunctionDecl& member : result->members) schedule(member, &generic, true, result->name, useLoc);
        } else if (!result->isAbstract && result->destructor) {
            // D103: the other methods are checked when they are used.
            schedule(*result->destructor, &generic, false, result->name, useLoc);
        }
        return result;
    }

    // An instance of a generic function, created the first time it is used.
    FunctionDecl* instantiateFunction(FunctionDecl& generic, const std::vector<Type>& args, SourceLocation useLoc) {
        std::string key = instanceKey(&generic, args);
        auto found = functionInstances_.find(key);
        if (found != functionInstances_.end()) return found->second;
        bool definition = isDefinitionArgs(generic.typeParams, args);
        if (!definition && failed_.count(&generic)) return nullptr;
        std::string what = "'" + generic.name + "'";
        if (!checkConstraints(generic.typeParams, args, what, useLoc) || tooDeep(args, generic.name, useLoc)) return nullptr;

        auto instance = std::make_unique<FunctionDecl>(clone(generic));
        instance->typeParams.clear();
        instance->name = instanceName(generic.name, args);
        instance->symbol = instanceName(generic.symbol, args, true);
        instance->generic = &generic;
        instance->typeBindings = bind(generic.typeParams, args);
        for (Type arg : args) instance->isAbstract = instance->isAbstract || isAbstract(arg);
        instance->isDefinition = definition;
        FunctionDecl* result = instance.get();
        module_.functionInstances.push_back(std::move(instance));
        functionInstances_[key] = result;

        int before = diag_->errorCount();
        {
            BindingsScope scope(bindings_, &result->typeBindings);
            resolveSignature(*result);
        }
        if (definition && diag_->errorCount() > before) failed_.insert(&generic);
        if (definition || !result->isAbstract) schedule(*result, &generic, definition, result->name, useLoc);
        return result;
    }

    // D103: a method of an instance of a generic struct with real types is checked,
    // and emitted, the first time it is used.
    void use(FunctionDecl* member, SourceLocation loc) {
        if (!member || member->scheduled || member->isGenerated || !member->owner) return;
        const StructDecl& owner = *member->owner;
        if (!owner.generic || owner.isAbstract) return;
        schedule(*member, owner.generic, false, owner.name, loc);
    }

    void schedule(FunctionDecl& function, const void* generic, bool definition, const std::string& name, SourceLocation useLoc) {
        function.scheduled = true;
        if (definition) {
            function.isDefinition = true;
            definitions_.push_back({&function, generic, true, ""});
        } else {
            instances_.push_back({&function, generic, false, " (in " + name + ", used at line " + std::to_string(useLoc.line) + ")"});
        }
    }

    // A written type; the language server shows what it became (D171).
    std::optional<Type> resolveType(const TypeName& name) {
        std::optional<Type> type = resolveTypeName(name);
        if (type) name.resolved = *type;
        return type;
    }

    // `i32[3][4]` (D196): three arrays of four i32, so that `a[i][j]` has i < 3 and j < 4.
    std::optional<Type> resolveTypeName(const TypeName& name) {
        std::optional<Type> type = resolveNamedType(name);
        if (!type || name.dimensions.empty()) return type;
        if (type->isVoid() || type->kind == TypeKind::SelfType || type->isInterface()) {
            diag_->error(name.loc, "an array cannot have elements of type " + type->name() + " (D196)");
            return std::nullopt;
        }
        for (size_t i = name.dimensions.size(); i-- > 0;) {
            int64_t count = name.dimensions[i];
            int64_t element = sizeOf(*type);
            if (count < 1 || count > INT32_MAX || (element > 0 && count > INT32_MAX / element)) {
                diag_->error(name.loc, count < 1 ? "an array has at least one element (D196)"
                                                 : "this array is too large: an array takes at most 2 GB (D196)");
                return std::nullopt;
            }
            type = Type::arrayType(internArray(*type, count));
        }
        return type;
    }

    std::optional<Type> resolveNamedType(const TypeName& name) {
        if (name.isFunction) {  // D185
            std::vector<Type> params;
            std::vector<ParamMode> modes;
            for (size_t i = 0; i < name.args.size(); ++i) {
                std::optional<Type> param = resolveType(name.args[i]);
                if (!param) return std::nullopt;
                if (param->isVoid()) {
                    diag_->error(name.args[i].loc, "a parameter cannot be void");
                    return std::nullopt;
                }
                params.push_back(*param);
                modes.push_back(static_cast<ParamMode>(name.modes[i]));
            }
            Type result = Type::voidType();
            if (!name.result.empty()) {
                std::optional<Type> written = resolveType(name.result[0]);
                if (!written) return std::nullopt;
                result = *written;
            }
            return Type::functionType(internSignature(params, modes, result));
        }
        if (bindings_ && name.module.empty()) {
            for (const auto& [param, type] : *bindings_) {
                if (param != name.name) continue;
                if (!name.args.empty()) {
                    diag_->error(name.loc, "'" + name.name + "' is a type parameter: it has no type arguments");
                    return std::nullopt;
                }
                return type;
            }
        }
        if (name.name == "addr" && name.module.empty() && !findDecl(&ModuleScope::structs, name.name, name.loc)) {
            if (isUnsafe(name.loc)) return Type::addrType();
            diag_->error(name.loc, "'addr' belongs to the unsafe dialect: the module must write 'import unsafe' (D127)");
            return std::nullopt;
        }
        if (!name.module.empty() && !importsQualifier(name.loc, name.module)) {
            diag_->error(name.loc, "no imported module is called '" + name.module + "' (D126)");
            return std::nullopt;
        }
        if (name.name == "Self" && name.module.empty()) {
            if (!selfAllowed_) {
                diag_->error(name.loc, "Self can be used only in the methods of an interface (D84)");
                return std::nullopt;
            }
            return Type::selfType();
        }
        std::optional<Type> builtin = !name.module.empty() ? std::nullopt
                                      : name.name == "void"  ? Type::voidType()
                                                             : findBuiltinType(name.name);
        if (builtin) {
            if (!name.args.empty()) {
                diag_->error(name.loc, "'" + name.name + "' has no type arguments");
                return std::nullopt;
            }
            return builtin;
        }
        if (StructDecl* found = findDecl(&ModuleScope::structs, name.name, name.loc, name.module)) {
            StructDecl& decl = *found;
            if (decl.typeParams.empty()) {
                if (!name.args.empty()) {
                    diag_->error(name.loc, "'" + decl.name + "' is not generic: it has no type arguments");
                    return std::nullopt;
                }
                return Type::structType(&decl);
            }
            std::vector<Type> args;
            if (!resolveTypeArgs(name.args, decl.typeParams.size(), "'" + decl.name + "'", name.loc, args)) return std::nullopt;
            StructDecl* instance = instantiateStruct(decl, args, name.loc);
            if (!instance) return std::nullopt;
            return Type::structType(instance);
        }
        if (InterfaceDecl* iface = findDecl(&ModuleScope::interfaces, name.name, name.loc, name.module)) {
            if (!name.args.empty()) {
                diag_->error(name.loc, "an interface has no type arguments (D84)");
                return std::nullopt;
            }
            return Type::interfaceType(iface);
        }
        std::string where = privateIn(name.name, name.loc, name.module);
        if (!where.empty()) {
            reportPrivate(name.loc, "'" + name.name + "' of " + where);
        } else if (!name.module.empty()) {
            diag_->error(name.loc, "the module " + name.module + " has no type '" + name.name + "'");
        } else {
            diag_->error(name.loc, "unknown type '" + name.name + "'");
        }
        return std::nullopt;
    }

    bool resolveTypeArgs(const std::vector<TypeName>& names, size_t expected, const std::string& what, SourceLocation loc,
                         std::vector<Type>& out) {
        if (names.size() != expected) {
            diag_->error(loc, what + " takes " + std::to_string(expected) +
                                  (expected == 1 ? " type argument, but " : " type arguments, but ") +
                                  std::to_string(names.size()) + " were given");
            return false;
        }
        for (const TypeName& name : names) {
            std::optional<Type> type = resolveType(name);
            if (!type) return false;
            if (type->isVoid() || type->kind == TypeKind::SelfType) {
                diag_->error(name.loc, "'" + type->name() + "' cannot be a type argument");
                return false;
            }
            if (type->isInterface()) {
                diag_->error(name.loc, "an interface cannot be a type argument until list<" + type->name() + "> (D91)");
                return false;
            }
            out.push_back(*type);
        }
        return true;
    }

    // D49, D91: an interface is only the type of a read or `ref` parameter.
    bool checkNotInterface(Type type, SourceLocation loc, const std::string& what) {
        if (!type.isInterface()) return true;
        diag_->error(loc, what + " cannot have the interface type " + type.name() + ": use a generic, <T : " + type.name() +
                              "> (D49)");
        return false;
    }

    // Fields, members, layout and interfaces of a struct that is not generic, or of
    // an instance of a generic one. Done once, when the struct is first needed.
    void completeStruct(StructDecl& decl) {
        if (!completed_.insert(&decl).second) return;
        BindingsScope scope(bindings_, &decl.typeBindings);
        std::map<std::string, const FieldDecl*> fieldNames;
        for (FieldDecl& field : decl.fields) {
            if (fieldNames.count(field.name)) {
                diag_->error(field.loc, "field '" + field.name + "' is already defined in " + decl.name);
            }
            fieldNames[field.name] = &field;
            field.resolved = resolveType(field.type).value_or(Type::error());
            if (field.resolved.isVoid()) {
                diag_->error(field.type.loc, "a field cannot be void");
                field.resolved = Type::error();
            }
            if (!checkNotInterface(field.resolved, field.type.loc, "a field")) field.resolved = Type::error();
            if (!field.init || field.resolved.isError()) continue;
            if (field.resolved.isStruct()) {
                diag_->error(field.init->loc, "a field of type " + field.resolved.name() +
                                                  " cannot have an initial value yet (D73)");
            } else if (!isConstantLiteral(*field.init)) {
                diag_->error(field.init->loc, "for now the initial value of a field must be a literal (D73)");
            } else {
                coerce(field.init, field.resolved);
            }
        }

        // The cases of an enum, with their data (D104).
        for (EnumCase& item : decl.cases) {
            if (fieldNames.count(item.name)) diag_->error(item.loc, "case '" + item.name + "' is already defined in " + decl.name);
            fieldNames[item.name] = nullptr;
            std::map<std::string, bool> dataNames;
            for (FieldDecl& field : item.fields) {
                if (dataNames.count(field.name)) {
                    diag_->error(field.loc, "the case '" + item.name + "' already has data named '" + field.name + "'");
                }
                dataNames[field.name] = true;
                field.resolved = resolveType(field.type).value_or(Type::error());
                if (field.resolved.isVoid()) {
                    diag_->error(field.type.loc, "the data of a case cannot be void");
                    field.resolved = Type::error();
                }
                if (!checkNotInterface(field.resolved, field.type.loc, "the data of a case")) field.resolved = Type::error();
            }
        }
        if (decl.isEnum && decl.cases.empty()) diag_->error(decl.loc, "an enum needs at least one case (D104)");
        caseValues(decl);

        for (FunctionDecl& member : decl.members) {
            member.owner = &decl;
            member.typeBindings = decl.typeBindings;
            member.isAbstract = decl.isAbstract;
            if (!member.typeParams.empty()) {
                diag_->error(member.typeParams[0].loc, "methods with type parameters of their own will come later (D92)");
            }
            if (member.kind == FunctionKind::Constructor) {
                if (decl.constructor) diag_->error(member.loc, "a struct has only one constructor (D69)");
                else decl.constructor = &member;
            } else if (member.kind == FunctionKind::Destructor) {
                if (decl.destructor) diag_->error(member.loc, "a struct has only one destructor");
                else decl.destructor = &member;
                if (!member.params.empty()) diag_->error(member.params[0].loc, "a destructor has no parameters");
            } else if (fieldNames.count(member.name)) {
                diag_->error(member.loc, "'" + member.name + "' is already " + (decl.isEnum ? "a case" : "a field") + " of " +
                                             decl.name);
            } else if (decl.methods.count(member.name)) {
                diag_->error(member.loc, "method '" + member.name + "' is already defined in " + decl.name);
            } else {
                decl.methods[member.name] = &member;
            }
        }

        layout(decl);
        for (FunctionDecl& member : decl.members) resolveSignature(member);
        checkCopySignature(decl);
        checkImplements(decl);
    }

    // The values of the cases (D135): their position, or `Red = 1` in an enum
    // without data, for every case.
    void caseValues(StructDecl& decl) {
        bool written = false;
        for (const EnumCase& item : decl.cases) written = written || item.valueExpr;
        std::map<int64_t, const EnumCase*> seen;
        for (size_t i = 0; i < decl.cases.size(); ++i) {
            EnumCase& item = decl.cases[i];
            item.value = static_cast<int64_t>(i);
            if (!written) continue;
            if (!item.valueExpr) {
                diag_->error(item.loc, "give a value to every case of " + decl.name + ", or to none (D135)");
                continue;
            }
            if (!item.fields.empty()) {
                diag_->error(item.valueExpr->loc, "only the cases of an enum without data can have a value (D135)");
                continue;
            }
            const Expr* value = item.valueExpr.get();
            bool negative = false;
            if (value->kind == ExprKind::Unary && static_cast<const UnaryExpr*>(value)->op == UnaryOp::Negate) {
                negative = true;
                value = static_cast<const UnaryExpr*>(value)->operand.get();
            }
            if (value->kind != ExprKind::IntLiteral || static_cast<const IntLiteralExpr*>(value)->isChar) {
                diag_->error(item.valueExpr->loc, "the value of a case is an integer literal (D135)");
                continue;
            }
            uint64_t magnitude = static_cast<const IntLiteralExpr*>(value)->magnitude;
            if (!fits(magnitude, negative, Type::intType(32, true))) {
                diag_->error(item.valueExpr->loc, "the value of a case must fit in an i32 (D135)");
                continue;
            }
            item.value = negative ? -static_cast<int64_t>(magnitude) : static_cast<int64_t>(magnitude);
            auto [other, fresh] = seen.emplace(item.value, &item);
            if (!fresh) {
                diag_->error(item.valueExpr->loc, "the value " + std::to_string(item.value) + " is already the one of " +
                                                      other->second->name + " (D135)");
            }
        }
    }

    // Computes size, alignment and need for destruction, and rejects structs
    // that contain themselves (D11).
    void layout(StructDecl& decl) {
        int& state = layoutState_[&decl];
        if (state == 2) return;
        if (state == 1) return;  // cycle: reported by the struct that closes it
        state = 1;
        bool destruct = decl.destructor != nullptr;
        auto [size, align] = layoutFields(decl, decl.fields, destruct);
        decl.size = size;
        decl.align = align;
        if (decl.isEnum) {
            // A tag, then the data of the current case: the largest case decides
            // the size, the most aligned one where the data start.
            int largest = 0;
            align = 4;
            for (EnumCase& item : decl.cases) {
                std::tie(item.size, item.align) = layoutFields(decl, item.fields, destruct);
                largest = std::max(largest, item.size);
                align = std::max(align, item.align);
            }
            decl.payloadOffset = alignUp(4, align);
            decl.size = alignUp(decl.payloadOffset + largest, align);
            decl.align = align;
        }
        decl.needsDestruction = destruct;
        state = 2;
    }

    // Size and alignment of fields laid out as in C (D74).
    std::pair<int, int> layoutFields(const StructDecl& decl, std::vector<FieldDecl>& fields, bool& destruct) {
        int offset = 0;
        int align = 1;
        for (FieldDecl& field : fields) {
            Type type = field.resolved;
            Type base = baseElement(type);
            if (base.isStruct()) {
                auto* inner = const_cast<StructDecl*>(base.structDecl);
                if (layoutState_[inner] == 1) {
                    diag_->error(field.loc, std::string(decl.isEnum ? "enum '" : "struct '") + decl.name +
                                                "' contains itself through " + (decl.isEnum ? "the data '" : "field '") + field.name +
                                                "': use a list instead (D11)");
                    field.resolved = Type::error();
                    continue;
                }
                completeStruct(*inner);
                layout(*inner);
            }
            // A parameter may need destruction: decided by each instance. A function
            // value owns its data (D184).
            destruct = destruct || needsDestruction(type);
            if (type.isError()) continue;
            offset = alignUp(offset, alignOf(type)) + sizeOf(type);
            align = std::max(align, alignOf(type));
        }
        return {alignUp(offset, align), align};
    }

    // The interfaces a struct declares, resolved the first time they are needed.
    const std::vector<const InterfaceDecl*>& implementsOf(const StructDecl& constDecl) {
        auto& decl = const_cast<StructDecl&>(constDecl);
        if (!implementsDone_.insert(&decl).second) return decl.implements;
        for (const TypeName& name : decl.interfaces) {
            InterfaceDecl* found = findDecl(&ModuleScope::interfaces, name.name, name.loc, name.module);
            if (!found || !name.args.empty()) {
                diag_->error(name.loc, "'" + name.name + "' is not an interface: a struct can only implement interfaces (D40)");
            } else if (std::find(decl.implements.begin(), decl.implements.end(), found) != decl.implements.end()) {
                diag_->error(name.loc, decl.name + " already implements " + name.name);
            } else {
                decl.implements.push_back(found);
            }
        }
        return decl.implements;
    }

    static Type withSelf(Type type, Type self) { return type.kind == TypeKind::SelfType ? self : type; }

    // As written in the source, with `Self` replaced: `f64 area() const`.
    static std::string signatureText(const FunctionDecl& method, Type self) {
        std::string text = withSelf(method.resolvedReturn, self).name() + " " + method.name + "(";
        for (size_t i = 0; i < method.params.size(); ++i) {
            const Param& param = method.params[i];
            if (i) text += ", ";
            if (param.mode == ParamMode::Ref) text += "ref ";
            if (param.mode == ParamMode::Move) text += param.isConst ? "move const " : "move ";
            text += withSelf(method.paramTypes[i], self).name() + " " + param.name;
        }
        return text + (method.isConst ? ") const" : ")");
    }

    static bool sameSignature(const FunctionDecl& method, const FunctionDecl& required, Type self) {
        if (method.isConst != required.isConst || method.params.size() != required.params.size()) return false;
        if (method.resolvedReturn != withSelf(required.resolvedReturn, self)) return false;
        for (size_t i = 0; i < method.params.size(); ++i) {
            if (method.params[i].mode != required.params[i].mode || method.params[i].isConst != required.params[i].isConst) {
                return false;
            }
            if (method.paramTypes[i] != withSelf(required.paramTypes[i], self)) return false;
        }
        return true;
    }

    // D90: a struct has every method of the interfaces it declares, with the same signature.
    void checkImplements(const StructDecl& decl) {
        Type self = Type::structType(&decl);
        for (const InterfaceDecl* iface : implementsOf(decl)) {
            for (const FunctionDecl& required : iface->methods) {
                auto found = decl.methods.find(required.name);
                if (found == decl.methods.end()) {
                    diag_->error(decl.loc, decl.name + " does not implement '" + required.name + "' of " + iface->name +
                                               ": add '" + signatureText(required, self) + "' (D90)");
                } else if (!sameSignature(*found->second, required, self)) {
                    diag_->error(found->second->loc, "'" + required.name + "' does not match the interface " + iface->name +
                                                         ": it must be '" + signatureText(required, self) + "' (D90)");
                }
            }
        }
    }

    void resolveSignature(FunctionDecl& function) {
        function.resolvedReturn = resolveType(function.returnType).value_or(Type::error());
        if (!checkNotInterface(function.resolvedReturn, function.returnType.loc, "a return value")) {
            function.resolvedReturn = Type::error();
        }
        for (const Param& param : function.params) {
            Type type = resolveType(param.type).value_or(Type::error());
            if (type.isVoid()) {
                diag_->error(param.type.loc, "a parameter cannot be void");
                type = Type::error();
            }
            if (type.isInterface() && param.mode == ParamMode::Move) {
                diag_->error(param.type.loc, "a 'move' parameter cannot have the interface type " + type.name() +
                                                 ": the size of the object is not known (D91)");
                type = Type::error();
            } else if (type.isInterface() && type.interfaceDecl->usesSelf) {
                diag_->error(param.type.loc, type.name() + " uses Self, so it can only be a constraint: <T : " + type.name() +
                                                 "> (D84)");
                type = Type::error();
            }
            function.paramTypes.push_back(type);
        }
    }

    // The signatures of the functions that are not generic, and main.
    void declareFunctions() {
        std::map<std::string, const FunctionDecl*> externs;
        for (FunctionDecl& function : module_.functions) {
            if (function.typeParams.empty()) resolveSignature(function);
            if (!function.isExtern) continue;
            checkExtern(function);
            auto [other, fresh] = externs.emplace(function.name, &function);
            if (!fresh && !sameExtern(function, *other->second)) {
                diag_->error(function.loc, "the C function '" + function.name + "' is already declared with another signature, in " +
                                               fileOf(other->second->loc.file).name);
            }
        }

        auto main = modules_[0].functions.find("main");
        if (!requireMain_ && main == modules_[0].functions.end()) return;
        if (main == modules_[0].functions.end() || main->second->isExtern) {
            diag_->error("missing function main");
            return;
        }
        const FunctionDecl& function = *main->second;
        if (!function.typeParams.empty()) {
            diag_->error(function.loc, "main cannot be generic");
            return;
        }
        Type ret = function.resolvedReturn;
        if (!ret.isError() && !ret.isVoid() && !(ret.isInt() && ret.bits == 32 && ret.isSigned)) {
            diag_->error(function.returnType.loc, "main must return i32 or void");
        }
        // D154: no parameters, or the arguments of the program.
        bool arguments = function.params.size() == 1 && function.params[0].mode == ParamMode::Read &&
                         !function.params[0].isConst && isList(function.paramTypes[0]) &&
                         isString(elementOf(function.paramTypes[0]));
        if (!function.params.empty() && !arguments) {
            diag_->error(function.params[0].loc, "main has no parameters, or one with the arguments of the program: "
                                                 "list<string> args (D154)");
        }
    }

    // D129: the types that a C function can take and return.
    static bool isCType(Type type) { return type.isNumeric() || type.isBool() || type.kind == TypeKind::Addr; }

    // A number, or a struct of numbers: the same layout in C (D74, D129).
    static bool isCPointee(Type type) {
        if (type.isNumeric() || type.isBool()) return true;
        if (type.isArray()) return isCPointee(type.array->element);
        if (!type.isStruct() || type.structDecl->isEnum || type.structDecl->needsDestruction) return false;
        for (const FieldDecl& field : type.structDecl->fields) {
            if (!isCPointee(field.resolved)) return false;
        }
        return true;
    }

    void checkExtern(const FunctionDecl& function) {
        Type ret = function.resolvedReturn;
        if (!ret.isError() && !ret.isVoid() && !isCType(ret)) {
            diag_->error(function.returnType.loc, "a C function returns a number, bool or addr, not " + ret.name() + " (D129)");
        }
        for (size_t i = 0; i < function.params.size(); ++i) {
            const Param& param = function.params[i];
            Type type = function.paramTypes[i];
            if (type.isError()) continue;
            if (param.mode == ParamMode::Move) {
                diag_->error(param.loc, "a C function has no 'move' parameters (D129)");
            } else if (param.mode == ParamMode::Ref && !isCPointee(type)) {
                diag_->error(param.type.loc, "'ref' to a C function is a pointer to a number or to a struct of numbers, not " +
                                                 type.name() + " (D129)");
            } else if (param.mode == ParamMode::Read && !isCType(type)) {
                diag_->error(param.type.loc, "a C function takes numbers, bool, addr and 'ref' to numbers or structs of "
                                             "numbers, not " + type.name() + " (D129)");
            }
        }
    }

    static bool sameExtern(const FunctionDecl& a, const FunctionDecl& b) {
        if (a.resolvedReturn != b.resolvedReturn || a.params.size() != b.params.size()) return false;
        for (size_t i = 0; i < a.params.size(); ++i) {
            if (a.params[i].mode != b.params[i].mode || a.paramTypes[i] != b.paramTypes[i]) return false;
        }
        return true;
    }

    // D132: what a public declaration shows must be public too, or the modules that
    // use it could not name it.
    void checkExposed(Type type, SourceLocation loc, const std::string& what) {
        type = baseElement(type);
        if (type.isStruct()) {
            const StructDecl* decl = type.structDecl;
            for (Type arg : decl->typeArgs) checkExposed(arg, loc, what);
            if (decl->generic) decl = decl->generic;
            if (decl->isPublic) return;
        } else if (!type.isInterface() || type.interfaceDecl->isPublic) {
            return;
        }
        diag_->error(loc, what + " is public, but its type " + type.name() + " is private to the module (D132)");
    }

    void checkPublicSignatures() {
        auto signature = [this](const FunctionDecl& function, const std::string& what) {
            checkExposed(function.resolvedReturn, function.returnType.loc, what);
            for (size_t i = 0; i < function.params.size() && i < function.paramTypes.size(); ++i) {
                checkExposed(function.paramTypes[i], function.params[i].type.loc, what);
            }
        };
        for (const FunctionDecl& function : module_.functions) {
            if (function.isPublic && function.typeParams.empty()) signature(function, "function '" + function.name + "'");
        }
        for (const InterfaceDecl& decl : module_.interfaces) {
            if (!decl.isPublic) continue;
            for (const FunctionDecl& method : decl.methods) signature(method, "method '" + method.name + "'");
        }
        for (const StructDecl& decl : module_.structs) {
            if (!decl.isPublic || !decl.typeParams.empty()) continue;
            for (const FieldDecl& field : decl.fields) {
                if (field.isPublic) checkExposed(field.resolved, field.type.loc, "field '" + field.name + "'");
            }
            for (const EnumCase& item : decl.cases) {
                for (const FieldDecl& field : item.fields) checkExposed(field.resolved, field.type.loc, "case '" + item.name + "'");
            }
            for (const FunctionDecl& member : decl.members) {
                if (member.isPublic) signature(member, member.kind == FunctionKind::Constructor ? "the constructor" : "method '" + member.name + "'");
            }
        }
    }

    // D83: `copy()` written by hand is `T copy() const`, and only where it is not generated.
    void checkCopySignature(const StructDecl& decl) {
        auto found = decl.methods.find("copy");
        if (found == decl.methods.end()) return;
        const FunctionDecl& copy = *found->second;
        if (!decl.needsDestruction) {
            diag_->error(copy.loc, decl.name + " is copied implicitly: its copy() is generated by the compiler (D83)");
        } else if (!copy.params.empty() || !copy.isConst || copy.resolvedReturn != Type::structType(&decl)) {
            diag_->error(copy.loc, "copy() must be written '" + decl.name + " copy() const' (D83)");
        }
    }

    void checkFunction(FunctionDecl& function) {
        current_ = &function;
        bindings_ = &function.typeBindings;
        scopes_.assign(1, {});
        loops_.clear();
        tryContexts_.clear();
        catchTypes_.clear();
        finallyLoops_.clear();
        regions_.clear();
        // D113, D122: the functions that the compiler calls where nobody can catch.
        if (function.kind == FunctionKind::Destructor) {
            regions_.push_back({0, "a destructor cannot let an exception out: catch it inside (D113)"});
        } else if (function.kind == FunctionKind::Method && function.owner && !function.isGenerated) {
            if (function.name == "copy" && function.owner->needsDestruction) {
                regions_.push_back({0, "copy() cannot let an exception out: catch it inside (D122)"});
            }
            if (function.name == "message" && implementsException(*function.owner)) {
                regions_.push_back({0, "message() cannot let an exception out: catch it inside (D122)"});
            }
        }
        for (size_t i = 0; i < function.params.size(); ++i) {
            const Param& param = function.params[i];
            Variable* variable = declare(param.name, function.paramTypes[i], param.loc, param.isConst);
            if (!variable) continue;
            variable->kind = param.mode == ParamMode::Ref    ? VariableKind::RefParam
                             : param.mode == ParamMode::Move ? VariableKind::MoveParam
                                                             : VariableKind::ReadParam;
        }

        Flow flow = checkBlock(function.body);
        function.bodyChecked = true;

        // D47, D67: a non-void function must not reach its end.
        Type ret = function.resolvedReturn;
        if (!ret.isVoid() && !ret.isError() && flow != Flow::Returns) {
            diag_->error(function.body.close, "function '" + function.name + "' must end with 'ret' on every path: it returns " +
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
        if (findBuiltinType(name) || findDecl(&ModuleScope::structs, name, loc)) {
            diag_->error(loc, "'" + name + "' is a type name");
            return nullptr;
        }
        if (importsQualifier(loc, name)) {
            diag_->error(loc, "'" + name + "' is the name of an imported module (D131)");
            return nullptr;
        }
        if (ownFieldIndex(name) >= 0) {
            diag_->error(loc, "'" + name + "' is a field of " + current_->owner->name + ": choose another name (D70)");
            return nullptr;
        }
        if (Variable* existing = lookup(name)) {  // D66: no shadowing
            diag_->error(loc, "a variable named '" + name + "' is already visible here (declared at line " +
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
            if (variable->kind == VariableKind::Captured && variable->isConst) {
                return "'" + variable->name + "' is captured to be read: capture it with 'ref " + variable->name +
                       "' to modify it (D183)";
            }
            if (variable->kind == VariableKind::Element && variable->isConst) {
                return "'" + variable->name + "' is an element of a read-only 'for': write 'for (ref " + variable->type.name() +
                       " " + variable->name + " in ...)' to modify it (D25)";
            }
            if (variable->kind == VariableKind::Binding && variable->isConst) {
                return "'" + variable->name + "' is data of the case, read-only: write 'ref " + variable->type.name() + " " +
                       variable->name + "' in the case to modify it (D107)";
            }
            if (variable->kind == VariableKind::Caught) {
                return "'" + variable->name + "' is the exception of a 'catch', which is read-only (D114)";
            }
            if (variable->isConst) return "'" + variable->name + "' is const and cannot be modified (D26)";
            return "";
        }
        case ExprKind::Field:
            return readOnlyReason(*static_cast<const FieldExpr&>(expr).object);  // const is deep (D24)
        case ExprKind::Index: {
            const Expr& object = *static_cast<const IndexExpr&>(expr).object;
            if (isString(object.type)) return "the bytes of a string are read-only: build a new string (D200)";
            return readOnlyReason(object);
        }
        case ExprKind::Self:
            if (current_->isConst) return "method '" + current_->name + "' is const and cannot modify the fields (D24)";
            return "";
        default:
            return "";
        }
    }

    // Checks the target of an assignment, `++` or `--`. Returns its type, or an error.
    Type checkTarget(ExprPtr& target) {
        insertTarget_ = true;  // `m[k] = v` adds the key (D197)
        Type type = checkExpr(target, std::nullopt);
        if (type.isError()) return type;
        if (!isPlace(*target)) {
            diag_->error(target->loc, "only a variable, a field or an element can be assigned");
            return Type::error();
        }
        std::string reason = readOnlyReason(*target);
        if (!reason.empty()) {
            diag_->error(target->loc, reason);
            return Type::error();
        }
        if (!checkNotBorrowed(*target, target->loc)) return Type::error();
        return type;
    }

    // D101: while a `for` goes over the elements of a list, the list cannot be
    // modified, except its elements through the variable of the loop.
    bool checkNotBorrowed(const Expr& place, SourceLocation loc) {
        std::optional<Path> path = pathOf(place);
        if (!path) return true;
        for (const LoopBorrow& borrow : loopBorrows_) {
            if (overlaps(*path, borrow.path) == Overlap::No) continue;
            diag_->error(loc, "'" + placeText(place) + "' cannot be modified " + borrow.reason);
            return false;
        }
        return true;
    }

    // Whether `move` can take the value of this place (D23, D26, D78).
    bool canMove(const Expr& place) const {
        if (place.kind == ExprKind::Name) {
            const Variable* variable = static_cast<const NameExpr&>(place).variable;
            return variable && variable->kind != VariableKind::ReadParam &&
                   !((variable->kind == VariableKind::Element || variable->kind == VariableKind::Binding) && variable->isConst) &&
                   variable->kind != VariableKind::Caught && variable->kind != VariableKind::Captured;
        }
        return readOnlyReason(place).empty();
    }

    // A struct can be copied if it has copy(), or if the compiler can generate it (D35).
    bool canCopy(Type type) const {
        if (type.isFunction()) return false;  // it owns its captured data (D184)
        if (type.isArray()) return !needsDestruction(type);  // only arrays of simple values
        if (!type.isStruct()) return true;
        const StructDecl& decl = *type.structDecl;
        if (decl.methods.count("copy")) return true;
        if (decl.destructor) return false;
        for (const FieldDecl& field : decl.fields) {
            if (!canCopy(field.resolved)) return false;
        }
        for (const EnumCase& item : decl.cases) {
            for (const FieldDecl& field : item.fields) {
                if (!canCopy(field.resolved)) return false;
            }
        }
        return true;
    }

    // The copy() of a struct: written by hand, or generated the first time it is used (D35).
    FunctionDecl* copyFunction(const StructDecl& constDecl, SourceLocation loc) {
        auto& decl = const_cast<StructDecl&>(constDecl);
        auto found = decl.methods.find("copy");
        if (found != decl.methods.end()) {
            use(found->second, loc);
            return found->second;
        }
        if (!canCopy(Type::structType(&decl))) return nullptr;
        for (const FieldDecl& field : decl.fields) {
            if (needsDestruction(field.resolved)) copyFunction(*field.resolved.structDecl, loc);
        }
        for (const EnumCase& item : decl.cases) {
            for (const FieldDecl& field : item.fields) {
                if (needsDestruction(field.resolved)) copyFunction(*field.resolved.structDecl, loc);
            }
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
        if (type.isParam()) return true;  // D85: checked in each instance
        if (!needsDestruction(type) || isTemporary(value) || value.kind == ExprKind::Move) return true;
        std::string text = placeText(value);
        std::vector<std::string> fixes;
        if (isPlace(value) && canMove(value)) fixes.push_back("'move " + text + "'");
        if (canCopy(type)) fixes.push_back("'" + text + ".copy()'");
        std::string advice = fixes.empty()    ? "it can be neither moved nor copied here"
                             : fixes.size() == 1 ? "write " + fixes[0]
                                                 : "write " + fixes[0] + " or " + fixes[1];
        diag_->error(value.loc, "values of type " + type.name() + " are not copied implicitly (D23): " + advice);
        return false;
    }

    // `move x` (D23, D78, D80). The operand must be a variable or a field.
    Type checkMove(MoveExpr& node) {
        if (isTemporary(*node.operand)) {
            Type ignored = checkExpr(node.operand, std::nullopt);
            (void)ignored;
            diag_->error(node.loc, "'move' is not needed before a new object: it is moved anyway (D23)");
            return Type::error();
        }
        Type type = checkExpr(node.operand, std::nullopt);
        if (type.isError()) return type;
        const Expr& operand = *node.operand;
        if (!isPlace(operand)) {
            diag_->error(node.loc, "'move' is written before a variable or a field (D82)");
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
                if (!checkNotBorrowed(operand, node.loc)) return Type::error();
                node.moveKind = MoveKind::Variable;
                variable->needsDropFlag = true;
                return type;
            case VariableKind::RefParam:
                node.moveKind = MoveKind::Take;  // D78
                return type;
            case VariableKind::ReadParam:
                diag_->error(node.loc, "parameter '" + variable->name +
                                          "' is read-only: it belongs to the caller and cannot be moved (D82)");
                return Type::error();
            case VariableKind::Caught:
                diag_->error(node.loc, "cannot move '" + variable->name + "': it is the exception of a 'catch' (D114)");
                return Type::error();
            case VariableKind::Captured:
                diag_->error(node.loc, "cannot move '" + variable->name + "': it is captured, and the lambda can run again (D183)");
                return Type::error();
            case VariableKind::Binding:
            case VariableKind::Element:
                if (variable->isConst) {
                    diag_->error(node.loc, "cannot move '" + variable->name + "': " +
                                              (variable->kind == VariableKind::Element ? "it is an element of a read-only 'for' (D25)"
                                                                                        : "it is read-only data of the case (D107)"));
                    return Type::error();
                }
                node.moveKind = MoveKind::Take;  // D78
                return type;
            }
        }
        std::string reason = readOnlyReason(operand);
        if (!reason.empty()) {
            diag_->error(node.loc, "cannot move '" + placeText(operand) + "': " + reason);
            return Type::error();
        }
        if (!checkNotBorrowed(operand, node.loc)) return Type::error();
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
                diag_->warning(statement->loc, "unreachable code");
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
        case StmtKind::Expr: return checkExprStmt(static_cast<ExprStmt&>(statement));
        case StmtKind::If: return checkIf(static_cast<IfStmt&>(statement));
        case StmtKind::For: return checkFor(static_cast<ForStmt&>(statement));
        case StmtKind::Switch: return checkSwitch(static_cast<SwitchStmt&>(statement));
        case StmtKind::Throw: return checkThrow(static_cast<ThrowStmt&>(statement));
        case StmtKind::Try: return checkTry(static_cast<TryStmt&>(statement));
        case StmtKind::Break:
        case StmtKind::Continue:
            if (!switchLoops_.empty() && switchLoops_.back() == loops_.size()) {
                diag_->error(statement.loc, std::string("'") + (statement.kind == StmtKind::Break ? "break" : "continue") +
                                               "' cannot be used in a 'switch': a branch ends by itself (D116)");
            } else if (!finallyLoops_.empty() && loops_.size() <= finallyLoops_.back()) {
                diag_->error(statement.loc, std::string("'") + (statement.kind == StmtKind::Break ? "break" : "continue") +
                                               "' cannot leave a 'finally' block (D120)");
            } else if (loops_.empty()) {
                diag_->error(statement.loc, std::string("'") + (statement.kind == StmtKind::Break ? "break" : "continue") +
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
            diag_->error(decl.typeName.loc, "a variable cannot be void");
            type = Type::error();
        }
        if (!checkNotInterface(type, decl.typeName.loc, "a variable")) type = Type::error();
        if (decl.isConst && !decl.init) {
            diag_->error(decl.nameLoc, "a const variable must have a value (D51)");
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
        if (assign.op == AssignOp::Add && isString(type)) return checkAppend(assign);
        if (assign.op != AssignOp::Assign) {
            if (!type.isNumeric()) {
                diag_->error(assign.loc, "this compound assignment needs a number, found " + type.name());
                return;
            }
            if (assign.op == AssignOp::Rem && !type.isInt()) {
                diag_->error(assign.loc, "'%' only works on integers (D59)");
                return;
            }
            if (assign.op >= AssignOp::BitAnd && !type.isInt()) {
                diag_->error(assign.loc, "the bitwise operators only work on integers (D193)");
                return;
            }
            if (assign.op == AssignOp::Shl || assign.op == AssignOp::Shr) {  // the count is any integer (D195)
                Type count = checkExpr(assign.value, type);
                if (count.isUntyped() && !coerce(assign.value, type)) return;
                if (!count.isError() && !count.isInt() && !count.isUntyped()) {
                    diag_->error(assign.loc, "the count of a shift is an integer, found " + count.name() + " (D195)");
                }
                return;
            }
        }
        if (coerceValue(assign.value, type)) checkNotCopied(*assign.value, type);
    }

    // D95: `s += t` appends `t` to `s`, with the method of the library.
    void checkAppend(AssignStmt& assign) {
        if (!coerce(assign.value, assign.target->type)) return;
        FunctionDecl* append = stringDecl_->methods.at("append");
        SourceLocation loc = assign.loc;
        auto call = std::make_unique<MethodCallExpr>(loc, std::move(assign.target), "append");
        call->args.push_back(std::move(assign.value));
        call->function = append;
        call->type = Type::voidType();
        checkExclusivity(call->args, *append, call->object.get(), &call->overlaps);
        assign.call = std::move(call);
    }

    void checkIncDec(IncDecStmt& incDec) {
        Type type = checkTarget(incDec.target);
        if (!type.isError() && !type.isInt()) {
            diag_->error(incDec.loc, "'++' and '--' only work on integers, found " + type.name());
        }
    }

    Flow checkExprStmt(ExprStmt& statement) {
        ExprKind kind = statement.expr->kind;
        if (kind != ExprKind::Call && kind != ExprKind::MethodCall && kind != ExprKind::CallValue) {
            diag_->error(statement.expr->loc, "this expression does nothing: only function calls can be statements");
            return Flow::Continues;
        }
        Type ignored = checkExpr(statement.expr, std::nullopt);
        (void)ignored;
        // D102: a panic ends the path, like `ret` (D67).
        bool panics = statement.expr->kind == ExprKind::Call &&
                      static_cast<const CallExpr&>(*statement.expr).intrinsic == Intrinsic::Panic;
        return panics ? Flow::Returns : Flow::Continues;
    }

    void checkCondition(ExprPtr& condition) {
        Type type = checkExpr(condition, Type::boolType());
        if (!type.isBool() && !type.isError()) {
            diag_->error(condition->loc, "the condition must be a bool, found " + type.name() +
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

    // `switch` on an enum, an integer or a string (D34, D106-D109, D116, D117).
    Flow checkSwitch(SwitchStmt& statement) {
        if (!statement.value) {
            if (!current_->owner || !current_->owner->isEnum) {
                diag_->error(statement.loc, "'switch' needs a value, switch (x) { ... }: only in the methods of an enum it "
                                            "looks at the value of the method (D117)");
                return Flow::Continues;
            }
            statement.value = std::make_unique<SelfExpr>(statement.loc);
        }
        Type type = checkExpr(statement.value, std::nullopt);
        bool isEnum = type.isStruct() && type.structDecl->isEnum;
        bool ok = !type.isError();
        if (ok && !isEnum && !type.isInt() && !isString(type)) {
            diag_->error(statement.value->loc, "'switch' works on enums, integers and strings, found " + type.name() + " (D109)");
            ok = false;
        }
        const Expr& value = *statement.value;
        bool modifiable = (isPlace(value) || value.kind == ExprKind::Self) && readOnlyReason(value).empty();
        std::optional<Path> path = pathOf(value);
        std::vector<bool> covered(isEnum ? type.structDecl->cases.size() : 0, false);
        std::vector<std::string> seen;
        std::vector<Flow> flows;
        switchLoops_.push_back(loops_.size());
        for (SwitchBranch& branch : statement.branches) {
            scopes_.emplace_back();
            bool named = false;
            for (CasePattern& pattern : branch.patterns) {
                if (!ok) break;
                if (isEnum) {
                    named = checkCasePattern(pattern, type, covered, branch.patterns.size(), modifiable, value) || named;
                } else {
                    checkLiteralPattern(pattern, type, seen);
                }
            }
            if (named && path) loopBorrows_.push_back({*path, "while a 'switch' uses the data of its case (D125)"});
            flows.push_back(checkBlock(branch.body));
            if (named && path) loopBorrows_.pop_back();
            scopes_.pop_back();
        }
        if (statement.defaultBlock) flows.push_back(checkBlock(*statement.defaultBlock));
        switchLoops_.pop_back();

        bool all = isEnum && std::all_of(covered.begin(), covered.end(), [](bool c) { return c; });
        statement.exhaustive = statement.defaultBlock || all;
        if (ok && isEnum && !statement.exhaustive) {
            std::string missing;
            for (size_t i = 0; i < covered.size(); ++i) {
                if (!covered[i]) missing += (missing.empty() ? "" : ", ") + type.structDecl->cases[i].name;
            }
            diag_->error(statement.loc, "'switch' does not cover " + missing + " of " + type.name() +
                                            ": add the cases, or 'default' (D34)");
        }
        if (!statement.exhaustive) return Flow::Continues;
        bool returns = true;
        for (Flow flow : flows) {
            if (flow == Flow::Continues) return Flow::Continues;
            returns = returns && flow == Flow::Returns;
        }
        return returns ? Flow::Returns : Flow::Jumps;
    }

    // A case of an enum in a `switch`. Returns true if it names the data (D107).
    bool checkCasePattern(CasePattern& pattern, Type type, std::vector<bool>& covered, size_t patterns, bool modifiable,
                          const Expr& value) {
        const StructDecl& decl = *type.structDecl;
        if (pattern.literal) {
            diag_->error(pattern.loc, "the cases of " + decl.name + " are written by their name (D106)");
            return false;
        }
        int index = -1;
        for (size_t i = 0; i < decl.cases.size(); ++i) {
            if (decl.cases[i].name == pattern.name) index = static_cast<int>(i);
        }
        if (index < 0) {
            diag_->error(pattern.loc, decl.name + " has no case '" + pattern.name + "'");
            return false;
        }
        pattern.caseIndex = index;
        if (covered[index]) diag_->error(pattern.loc, "case '" + pattern.name + "' is already in this 'switch'");
        covered[index] = true;
        if (!pattern.hasBindings) return false;
        const EnumCase& item = decl.cases[index];
        if (patterns > 1) {
            diag_->error(pattern.loc, "a branch with several cases cannot name their data (D108)");
            return false;
        }
        if (pattern.bindings.size() != item.fields.size()) {
            size_t count = item.fields.size();
            diag_->error(pattern.loc, "case '" + item.name + "' has " + std::to_string(count) + " data" +
                                          ", but " + std::to_string(pattern.bindings.size()) + " names were given (D107)");
            return false;
        }
        for (size_t i = 0; i < pattern.bindings.size(); ++i) {
            CaseBinding& binding = pattern.bindings[i];
            const FieldDecl& field = item.fields[i];
            Type written = resolveType(binding.type).value_or(Type::error());
            if (!written.isError() && written != field.resolved) {
                diag_->error(binding.type.loc, "the data '" + field.name + "' of " + item.name + " is " + field.resolved.name() +
                                                   ": write " + field.resolved.name() + " " + binding.name + " (D107)");
                written = Type::error();
            }
            if (binding.isRef && !modifiable) {
                std::string reason = isPlace(value) || value.kind == ExprKind::Self
                                         ? readOnlyReason(value)
                                         : "the value of the 'switch' is not a variable or a field";
                diag_->error(binding.loc, "cannot modify the data with 'ref': " + reason);
            }
            Variable* variable = declare(binding.name, written, binding.loc, !binding.isRef);
            if (variable) variable->kind = VariableKind::Binding;
            binding.variable = variable;
        }
        return true;
    }

    // A literal in a `switch` on an integer or a string (D109).
    void checkLiteralPattern(CasePattern& pattern, Type type, std::vector<std::string>& seen) {
        if (!pattern.literal) {
            diag_->error(pattern.loc, "the cases of a switch on a " + type.name() + " are literals (D109)");
            return;
        }
        bool text = pattern.literal->kind == ExprKind::StringLiteral;
        if (text != isString(type)) {
            diag_->error(pattern.loc, "this case is not a " + type.name());
            return;
        }
        if (!coerce(pattern.literal, type)) return;
        std::string key = text ? static_cast<const StringLiteralExpr&>(*pattern.literal).value : constantText(*pattern.literal);
        if (std::find(seen.begin(), seen.end(), key) != seen.end()) {
            diag_->error(pattern.loc, "this value is already in this 'switch'");
            return;
        }
        seen.push_back(key);
    }

    // The value of an integer literal, as text, after coerce.
    static std::string constantText(const Expr& expr) {
        if (expr.kind == ExprKind::Conversion) return constantText(*static_cast<const ConversionExpr&>(expr).operand);
        if (expr.kind != ExprKind::IntLiteral) return "?";
        const auto& literal = static_cast<const IntLiteralExpr&>(expr);
        return (literal.negative && literal.magnitude ? "-" : "") + std::to_string(literal.magnitude);
    }

    bool implementsException(const StructDecl& decl) {
        if (!exceptionDecl_) return false;
        const auto& list = implementsOf(decl);
        return std::find(list.begin(), list.end(), exceptionDecl_) != list.end();
    }

    // The methods that a struct gives to an interface: used through its method table (D103).
    void useMethodsFor(Type type, const InterfaceDecl& iface, SourceLocation loc) {
        if (!type.isStruct()) return;
        for (const FunctionDecl& method : iface.methods) {
            auto found = type.structDecl->methods.find(method.name);
            if (found != type.structDecl->methods.end()) use(found->second, loc);
        }
    }

    // `throw x`, or `throw` alone in a `catch` (D36, D114, D119).
    Flow checkThrow(ThrowStmt& statement) {
        if (!statement.value) {
            if (catchTypes_.empty()) {
                diag_->error(statement.loc, "'throw' alone rethrows the exception of a 'catch': it can be used only inside one (D114)");
            } else {
                Type caught = catchTypes_.back();
                noteThrow(caught.isStruct() ? caught.structDecl : nullptr, statement.loc);
            }
            return Flow::Returns;
        }
        moveAllowed_ = statement.value->kind == ExprKind::Move;
        Type type = checkExpr(statement.value, std::nullopt);
        if (type.isError()) return Flow::Returns;
        if (!type.isStruct() || !implementsException(*type.structDecl)) {
            diag_->error(statement.value->loc, "only a struct that implements Exception can be thrown, found " + type.name() +
                                                   " (D36)");
            return Flow::Returns;
        }
        checkNotCopied(*statement.value, type);
        useMethodsFor(type, *exceptionDecl_, statement.loc);
        noteThrow(type.structDecl, statement.loc);
        return Flow::Returns;
    }

    // `try`, `catch` and `finally` (D16, D113, D114, D118, D120).
    Flow checkTry(TryStmt& statement) {
        TryContext context;
        for (CatchClause& clause : statement.catches) {
            Type type = resolveType(clause.type).value_or(Type::error());
            clause.resolved = type;
            if (type.isError()) continue;
            bool all = type.isInterface() && type.interfaceDecl == exceptionDecl_;
            if (!all && !(type.isStruct() && implementsException(*type.structDecl))) {
                diag_->error(clause.type.loc, "a 'catch' takes a struct that implements Exception, or Exception itself, found " +
                                                  type.name() + " (D118)");
                clause.resolved = Type::error();
                continue;
            }
            bool repeated = !all && std::find(context.caught.begin(), context.caught.end(), type.structDecl) != context.caught.end();
            if (context.catchAll || repeated) {
                diag_->error(clause.type.loc, "this 'catch' is never reached: a previous one already catches " +
                                                  (context.catchAll ? std::string("every exception") : type.name()) + " (D118)");
            }
            if (all) {
                context.catchAll = true;
            } else {
                context.caught.push_back(type.structDecl);
                useMethodsFor(type, *exceptionDecl_, clause.type.loc);
            }
        }

        tryContexts_.push_back(context);
        std::vector<Flow> flows{checkBlock(statement.body)};
        tryContexts_.pop_back();
        for (CatchClause& clause : statement.catches) {
            scopes_.emplace_back();
            Variable* variable = declare(clause.name, clause.resolved, clause.nameLoc, true);
            if (variable) variable->kind = VariableKind::Caught;
            clause.variable = variable;
            catchTypes_.push_back(clause.resolved);
            flows.push_back(checkBlock(clause.body));
            catchTypes_.pop_back();
            scopes_.pop_back();
        }
        if (statement.finallyBlock) {
            regions_.push_back({tryContexts_.size(), "a 'finally' block cannot let an exception out: catch it inside (D113)"});
            finallyLoops_.push_back(loops_.size());
            Flow ignored = checkBlock(*statement.finallyBlock);
            (void)ignored;
            finallyLoops_.pop_back();
            regions_.pop_back();
        }

        bool returns = true;
        for (Flow flow : flows) {
            if (flow == Flow::Continues) return Flow::Continues;
            returns = returns && flow == Flow::Returns;
        }
        return returns ? Flow::Returns : Flow::Jumps;
    }

    // D36: where an exception can come from. A call is protected by an enclosing
    // `try` that catches everything; the regions of D113 are checked at the end,
    // when it is known which functions can throw.
    void noteCall(FunctionDecl* callee, SourceLocation loc) {
        if (!callee || current_->isAbstract) return;
        if (!protectedFrom(nullptr, 0)) throwInfo_[current_].calls.push_back(callee);
        for (const Region& region : regions_) {
            if (!protectedFrom(nullptr, region.base)) escapes_.push_back({loc, callee, region.message});
        }
    }

    void noteThrow(const StructDecl* thrown, SourceLocation loc) {
        if (current_->isAbstract) return;
        if (!protectedFrom(thrown, 0)) throwInfo_[current_].throws = true;
        for (const Region& region : regions_) {
            if (!protectedFrom(thrown, region.base)) escapes_.push_back({loc, nullptr, region.message});
        }
    }

    // Whether an enclosing `try` body, from `base` on, catches the exception
    // (any exception, when it is not known).
    bool protectedFrom(const StructDecl* thrown, size_t base) const {
        for (size_t i = base; i < tryContexts_.size(); ++i) {
            const TryContext& context = tryContexts_[i];
            if (context.catchAll) return true;
            if (thrown && std::find(context.caught.begin(), context.caught.end(), thrown) != context.caught.end()) return true;
        }
        return false;
    }

    // D36: which functions can let an exception out, from the calls that are not
    // caught. An interface method can throw if one of its implementations can,
    // and then all of them take the hidden parameter of the exception.
    void computeThrows() {
        for (auto& [function, info] : throwInfo_) function->canThrow = info.throws;
        std::vector<std::pair<FunctionDecl*, FunctionDecl*>> implementations;  // (interface method, struct method)
        auto collect = [&](StructDecl& decl) {
            if (decl.isAbstract || !decl.typeParams.empty()) return;
            for (const InterfaceDecl* iface : implementsOf(decl)) {
                for (const FunctionDecl& required : iface->methods) {
                    auto found = decl.methods.find(required.name);
                    if (found != decl.methods.end()) implementations.emplace_back(const_cast<FunctionDecl*>(&required), found->second);
                }
            }
        };
        for (StructDecl& decl : module_.structs) collect(decl);
        for (auto& decl : module_.structInstances) collect(*decl);

        for (bool changed = true; changed;) {
            changed = false;
            for (auto& [function, info] : throwInfo_) {
                if (function->canThrow) continue;
                for (FunctionDecl* callee : info.calls) {
                    if (!callee->canThrow) continue;
                    function->canThrow = true;
                    changed = true;
                    break;
                }
            }
            for (auto& [required, method] : implementations) {
                if (method->canThrow != required->canThrow) {
                    required->canThrow = method->canThrow = true;
                    changed = true;
                }
            }
        }
        for (const Escape& escape : escapes_) {
            if (!escape.callee || escape.callee->canThrow) diag_->error(escape.loc, escape.message);
        }
        // A function value is called as if it could throw: every lambda takes the slot (D186).
        for (LambdaExpr* lambda : module_.lambdas) lambda->function->canThrow = true;
    }

    // `for ([ref] T x in v)` (D25, D101).
    Flow checkForEach(ForStmt& statement) {
        scopes_.emplace_back();
        Type list = checkExpr(statement.list, std::nullopt);
        Type element = resolveType(statement.elementType).value_or(Type::error());
        bool ok = !list.isError() && !element.isError();
        if (ok && isMap(list)) return checkForMap(statement, list, element);
        if (ok && statement.hasValue) {
            diag_->error(statement.valueLoc, "only a 'for' over a map has a key and a value (D197)");
            ok = false;
        }
        if (ok && !isList(list) && !list.isArray()) {
            diag_->error(statement.list->loc, "'for' goes over the elements of a list or of an array, found " + list.name() +
                                                  " (D25)");
            ok = false;
        } else if (ok && element != elementOf(list)) {
            diag_->error(statement.elementType.loc, std::string("the elements of this ") + (list.isArray() ? "array" : "list") +
                                                        " are " + elementOf(list).name() + ": write for (" +
                                                        std::string(statement.isRef ? "ref " : "") + elementOf(list).name() +
                                                        " " + statement.elementName + " in ...) (D101)");
            ok = false;
        }
        if (ok && statement.isRef) {
            std::string reason = isPlace(*statement.list) ? readOnlyReason(*statement.list)
                                                          : "only the elements of a variable or of a field can be modified";
            if (!reason.empty()) {
                diag_->error(statement.list->loc, "cannot modify the elements: " + reason);
                ok = false;
            } else if (!checkNotBorrowed(*statement.list, statement.list->loc)) {
                ok = false;
            }
        }
        Variable* variable = declare(statement.elementName, ok ? element : Type::error(), statement.elementLoc, !statement.isRef);
        if (variable) variable->kind = VariableKind::Element;
        statement.element = variable;

        std::optional<Path> path = pathOf(*statement.list);
        if (path) loopBorrows_.push_back({*path, "while a 'for' goes over the elements of '" + placeText(*statement.list) + "' (D101)"});
        loops_.push_back(false);
        checkBlock(statement.body);
        loops_.pop_back();
        if (path) loopBorrows_.pop_back();
        scopes_.pop_back();
        return Flow::Continues;
    }

    // `for (K k, [ref] V v in m)` (D197): the keys are read-only, the map cannot
    // change while the loop goes over it (D101).
    Flow checkForMap(ForStmt& statement, Type map, Type key) {
        Type keyType = map.structDecl->typeArgs[0];
        Type valueType = map.structDecl->typeArgs[1];
        bool ok = true;
        Type value = Type::error();
        if (!statement.hasValue) {
            diag_->error(statement.elementLoc, "a 'for' over a map names the key and the value: for (" + keyType.name() + " " +
                                                   statement.elementName + ", " + valueType.name() + " v in ...) (D197)");
            ok = false;
        } else {
            value = resolveType(statement.valueType).value_or(Type::error());
            ok = !value.isError();
        }
        if (ok && statement.isRef) {
            diag_->error(statement.elementLoc, "the keys of a map are read-only: only the value can be 'ref' (D197)");
            ok = false;
        }
        if (ok && (key != keyType || value != valueType)) {
            diag_->error(statement.elementType.loc, "the keys of this map are " + keyType.name() + " and the values " +
                                                        valueType.name() + ": write for (" + keyType.name() + " " +
                                                        statement.elementName + ", " + (statement.valueIsRef ? "ref " : "") +
                                                        valueType.name() + " " + statement.valueName + " in ...) (D197)");
            ok = false;
        }
        if (ok && statement.valueIsRef) {
            std::string reason = isPlace(*statement.list) ? readOnlyReason(*statement.list)
                                                          : "only the values of a variable or of a field can be modified";
            if (!reason.empty()) {
                diag_->error(statement.list->loc, "cannot modify the values: " + reason);
                ok = false;
            } else if (!checkNotBorrowed(*statement.list, statement.list->loc)) {
                ok = false;
            }
        }
        Variable* variable = declare(statement.elementName, ok ? keyType : Type::error(), statement.elementLoc, true);
        if (variable) variable->kind = VariableKind::Element;
        statement.element = variable;
        if (statement.hasValue) {
            Variable* second = declare(statement.valueName, ok ? valueType : Type::error(), statement.valueLoc,
                                       !statement.valueIsRef);
            if (second) second->kind = VariableKind::Element;
            statement.value = second;
        }

        std::optional<Path> path = pathOf(*statement.list);
        if (path) loopBorrows_.push_back({*path, "while a 'for' goes over the elements of '" + placeText(*statement.list) + "' (D101)"});
        loops_.push_back(false);
        checkBlock(statement.body);
        loops_.pop_back();
        if (path) loopBorrows_.pop_back();
        scopes_.pop_back();
        return Flow::Continues;
    }

    Flow checkFor(ForStmt& statement) {
        if (statement.form == ForForm::Each) return checkForEach(statement);
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
        if (!finallyLoops_.empty()) diag_->error(ret.loc, "'ret' cannot leave a 'finally' block (D120)");
        Type type = current_->resolvedReturn;
        if (type.isError()) return;
        if (type.isVoid()) {
            if (ret.value) {
                diag_->error(ret.value->loc, "function '" + current_->name + "' is void and does not return a value");
            }
            return;
        }
        if (!ret.value) {
            diag_->error(ret.loc, "missing value after 'ret': function '" + current_->name + "' returns " + type.name());
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
        if (target.isInterface()) {
            if (satisfies(type, target.interfaceDecl)) {  // passed with its method table (D49)
                useMethodsFor(type, *target.interfaceDecl, expr->loc);
                return true;
            }
            diag_->error(expr->loc, type.name() + " does not implement " + target.name() + " (D49)");
            return false;
        }
        if (type.isVoid()) {
            diag_->error(expr->loc, "this call does not return a value");
            return false;
        }
        if (type.isUntyped()) {
            diag_->error(expr->loc, "this " + type.name() + " cannot be used as " + target.name());
            return false;
        }
        if (convertsLosslessly(type, target)) {
            ExprPtr inner = std::move(expr);
            SourceLocation loc = inner->loc;
            expr = std::make_unique<ConversionExpr>(loc, std::move(inner), target, false);
            return true;
        }
        if (type.isStruct() || target.isStruct()) {
            diag_->error(expr->loc, "a value of type " + type.name() + " cannot be used as " + target.name());
        } else {
            diag_->error(expr->loc, "a value of type " + type.name() + " cannot be used as " + target.name() +
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
        bool lambdaArgument = std::exchange(lambdaArgument_, false);
        bool insertTarget = std::exchange(insertTarget_, false);
        switch (expr->kind) {
        case ExprKind::Lambda: return checkLambda(static_cast<LambdaExpr&>(*expr), lambdaArgument);
        case ExprKind::CallValue: return checkCallValue(expr);
        case ExprKind::IntLiteral: return checkIntLiteral(static_cast<IntLiteralExpr&>(*expr), expected);
        case ExprKind::FloatLiteral: return checkFloatLiteral(static_cast<FloatLiteralExpr&>(*expr), expected);
        case ExprKind::BoolLiteral: return Type::boolType();
        case ExprKind::StringLiteral:
            if (stringDecl_) return Type::structType(stringDecl_);  // D100
            diag_->error(expr->loc, "the standard library has no string");
            return Type::error();
        case ExprKind::Index: return checkIndex(static_cast<IndexExpr&>(*expr), insertTarget);
        case ExprKind::Name: return checkName(expr);
        case ExprKind::Unary: return checkUnary(expr, expected);
        case ExprKind::Binary: return checkBinary(expr, expected);
        case ExprKind::Call: return checkCall(expr);
        case ExprKind::Field: return checkField(expr);
        case ExprKind::MethodCall: return checkMethodCall(expr);
        case ExprKind::Self: return Type::structType(current_->owner);
        case ExprKind::Move:
            if (!moveAllowed) {
                diag_->error(expr->loc, "'move' is written only in an initialization, an assignment, the argument of a "
                                       "'move' parameter or after 'ret' (D82)");
                return Type::error();
            }
            return checkMove(static_cast<MoveExpr&>(*expr));
        case ExprKind::Ref:
            diag_->error(expr->loc, "'ref' is written only in the argument of a 'ref' parameter (D22)");
            return Type::error();
        case ExprKind::Conversion:
        case ExprKind::Print:
        case ExprKind::Construct:
        case ExprKind::EnumCase:
        case ExprKind::Copy: return expr->type;
        case ExprKind::TypeRef:
            diag_->error(expr->loc, "a type is not a value");
            return Type::error();
        }
        return Type::error();
    }

    Type checkIntLiteral(IntLiteralExpr& literal, std::optional<Type> expected) {
        if (literal.isChar) return Type::intType(8, false);  // D46
        if (expected && expected->isInt()) {
            if (!fits(literal.magnitude, literal.negative, *expected)) {
                std::string value = (literal.negative && literal.magnitude ? "-" : "") + std::to_string(literal.magnitude);
                diag_->error(literal.loc, "value " + value + " is out of range for " + expected->name() + " (" +
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
            return checkField(expr);
        }
        if (findDecl(&ModuleScope::functions, name.name, name.loc) || isPrintFunction(name.name) ||
            (current_->owner && current_->owner->methods.count(name.name))) {
            diag_->error(name.loc, "'" + name.name + "' is a function: call it with '()'");
        } else if (importsQualifier(name.loc, name.name)) {
            diag_->error(name.loc, "'" + name.name + "' is a module: write " + name.name + ".name (D126)");
        } else if (hiddenByLambda(name.name)) {
            diag_->error(name.loc, "'" + name.name + "' is not captured: write it between the [] of the lambda (D183)");
        } else {
            diag_->error(name.loc, "unknown name '" + name.name + "'");
        }
        return Type::error();
    }

    // A variable of the functions around a lambda, which its body sees only if captured.
    bool hiddenByLambda(const std::string& name) const {
        for (const auto* scopes : hiddenScopes_) {
            for (const auto& scope : *scopes) {
                if (scope.count(name)) return true;
            }
        }
        return false;
    }

    // `Shape` in `Shape.Circle(1.0)`: an enum named as a type, not a variable (D105).
    // Nothing if the expression is not that; an error type after an error.
    std::optional<Type> enumNamed(const Expr& object) {
        if (object.kind == ExprKind::TypeRef) {
            std::optional<Type> type = resolveType(static_cast<const TypeRefExpr&>(object).name);
            if (!type) return Type::error();
            if (!type->isStruct() || !type->structDecl->isEnum) {
                diag_->error(object.loc, type->name() + " is not an enum: only the values of an enum are created with '.' (D105)");
                return Type::error();
            }
            return type;
        }
        std::string name;
        std::string module;
        if (object.kind == ExprKind::Field) {  // `geometry.Shape` (D126)
            const auto& field = static_cast<const FieldExpr&>(object);
            if (field.object->kind != ExprKind::Name) return std::nullopt;
            module = static_cast<const NameExpr&>(*field.object).name;
            if (lookup(module) || ownFieldIndex(module) >= 0 || !importsQualifier(object.loc, module)) return std::nullopt;
            name = field.field;
        } else if (object.kind == ExprKind::Name) {
            name = static_cast<const NameExpr&>(object).name;
            if (lookup(name) || ownFieldIndex(name) >= 0) return std::nullopt;
        } else {
            return std::nullopt;
        }
        StructDecl* found = findDecl(&ModuleScope::structs, name, object.loc, module);
        if (!found || !found->isEnum) return std::nullopt;
        if (!found->typeParams.empty()) {
            diag_->error(object.loc, name + " is generic: write its type arguments, " + name + "<...>." + "Case (D92)");
            return Type::error();
        }
        return Type::structType(found);
    }

    // `Shape.Circle(1.0)` and `Color.Red` (D105). The data are given like `move`
    // parameters (D124).
    Type checkEnumCase(ExprPtr& expr, Type type, const std::string& name, std::vector<ExprPtr>* args, SourceLocation loc) {
        if (type.isError()) {
            if (args) checkIgnored(*args);
            return Type::error();
        }
        const StructDecl& decl = *type.structDecl;
        int index = -1;
        for (size_t i = 0; i < decl.cases.size(); ++i) {
            if (decl.cases[i].name == name) index = static_cast<int>(i);
        }
        if (index < 0) {
            diag_->error(loc, decl.name + " has no case '" + name + "'");
            if (args) checkIgnored(*args);
            return Type::error();
        }
        const EnumCase& item = decl.cases[index];
        std::string what = "'" + decl.name + "." + name + "'";
        if (!args && !item.fields.empty()) {
            diag_->error(loc, what + " has data: write " + decl.name + "." + name + "(...) (D105)");
            return Type::error();
        }
        auto node = std::make_unique<EnumCaseExpr>(loc, &decl, index);
        if (args) node->args = std::move(*args);
        expr = std::move(node);
        auto& created = static_cast<EnumCaseExpr&>(*expr);

        FunctionDecl signature;
        signature.name = name;
        signature.loc = loc;
        for (const FieldDecl& field : item.fields) {
            Param param;
            param.mode = ParamMode::Move;
            param.name = field.name;
            param.loc = field.loc;
            signature.params.push_back(param);
            signature.paramTypes.push_back(field.resolved);
        }
        if (!checkArgs(created.args, signature, what, loc, &created.overlaps)) return Type::error();
        return type;
    }

    Type checkField(ExprPtr& expr) {
        auto& field = static_cast<FieldExpr&>(*expr);
        if (std::optional<Type> named = enumNamed(*field.object)) {
            return checkEnumCase(expr, *named, field.field, nullptr, field.loc);
        }
        Type object = checkExpr(field.object, std::nullopt);
        if (object.isError()) return object;
        if (!object.isStruct()) {
            diag_->error(field.loc, "a " + object.name() + " has no fields");
            return Type::error();
        }
        const auto& fields = object.structDecl->fields;
        for (size_t i = 0; i < fields.size(); ++i) {
            if (fields[i].name == field.field) {
                if (!visible(fields[i].isPublic, *object.structDecl, field.loc)) {
                    reportPrivate(field.loc, "field '" + field.field + "' of " + object.name());
                    return Type::error();
                }
                field.index = static_cast<int>(i);
                return fields[i].resolved;
            }
        }
        diag_->error(field.loc, object.name() + " has no field named '" + field.field + "'");
        return Type::error();
    }

    // Checks one argument against the mode of its parameter (D22).
    bool checkArg(ExprPtr& arg, const Param& param, Type type) {
        std::string name = "parameter '" + param.name + "'";
        switch (param.mode) {
        case ParamMode::Read:
            if (arg->kind == ExprKind::Ref || arg->kind == ExprKind::Move) {
                diag_->error(arg->loc, name + " is read-only: remove '" + (arg->kind == ExprKind::Ref ? "ref" : "move") + "' (D22)");
                return false;
            }
            lambdaArgument_ = arg->kind == ExprKind::Lambda;  // it may capture by address (D184)
            return coerce(arg, type);
        case ParamMode::Ref: {
            if (arg->kind != ExprKind::Ref) {
                diag_->error(arg->loc, name + " is 'ref': write 'ref' before the argument (D22)");
                return false;
            }
            auto& ref = static_cast<RefExpr&>(*arg);
            Type found = checkExpr(ref.operand, std::nullopt);
            if (found.isError()) return false;
            if (!isPlace(*ref.operand)) {
                diag_->error(ref.loc, "only a variable or a field can be passed with 'ref' (D82)");
                return false;
            }
            std::string reason = readOnlyReason(*ref.operand);
            if (!reason.empty()) {
                diag_->error(ref.loc, "cannot pass '" + placeText(*ref.operand) + "' with 'ref': " + reason);
                return false;
            }
            if (found != type && !(type.isInterface() && satisfies(found, type.interfaceDecl))) {
                diag_->error(ref.loc, name + " is 'ref " + type.name() + "': the argument must have exactly that type, found " +
                                         found.name() + " (D82)");
                return false;
            }
            ref.type = found;
            return true;
        }
        case ParamMode::Move: {
            if (arg->kind == ExprKind::Ref) {
                diag_->error(arg->loc, name + " is 'move', not 'ref' (D22)");
                return false;
            }
            if (!coerceValue(arg, type)) return false;
            // A variable or a field gives its value only with `move`; new values do not need it (D22, D23).
            const Expr* value = arg.get();
            if (value->kind == ExprKind::Conversion) value = static_cast<const ConversionExpr&>(*value).operand.get();
            if (isPlace(*value) && needsDestruction(value->type)) {  // D94: not for simple types
                diag_->error(value->loc, name + " is 'move': write 'move " + placeText(*value) + "' (D22)");
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
                   OverlapChecks* overlapChecks, const Expr* object = nullptr, Type self = Type::error()) {
        size_t expected = function.params.size();
        if (args.size() != expected) {
            diag_->error(loc, what + " takes " + std::to_string(expected) + (expected == 1 ? " argument, but " : " arguments, but ") +
                                 std::to_string(args.size()) + " were given");
            return false;
        }
        bool ok = true;
        for (size_t i = 0; i < args.size(); ++i) {
            if (function.paramTypes[i].isError()) continue;
            ok = checkArg(args[i], function.params[i], withSelf(function.paramTypes[i], self)) && ok;
        }
        return ok && checkExclusivity(args, function, object, overlapChecks);
    }

    // D22, D79: what a call modifies cannot overlap anything else the call uses.
    // A read argument of a simple type is passed as a copy instead.
    bool checkExclusivity(std::vector<ExprPtr>& args, const FunctionDecl& function, const Expr* object,
                          OverlapChecks* overlapChecks) {
        struct Access {
            Path path;
            const Expr* expr;
            bool modifies;
            int arg;  // -1 for the object of the method, -2 for what a lambda captures
        };
        std::vector<Access> accesses;
        if (object) {
            if (std::optional<Path> path = pathOf(*object)) accesses.push_back({*path, object, !function.isConst, -1});
            if (!function.isConst && !checkNotBorrowed(*object, object->loc)) return false;
        }
        for (size_t i = 0; i < args.size(); ++i) {
            const Expr* arg = args[i].get();
            bool modifies = false;
            if (arg->kind == ExprKind::Lambda) {
                // What a lambda captures by address, the call uses as an argument (D184).
                for (const auto& place : static_cast<const LambdaExpr&>(*arg).capturePlaces) {
                    bool changes = capturedWithRef(static_cast<const LambdaExpr&>(*arg), *place);
                    if (std::optional<Path> path = pathOf(*place)) accesses.push_back({*path, place.get(), changes, -2});
                }
                continue;
            }
            if (arg->kind == ExprKind::Ref) {
                arg = static_cast<const RefExpr&>(*arg).operand.get();
                modifies = true;
                if (!checkNotBorrowed(*arg, arg->loc)) return false;
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
                if (!(first.modifies || second.modifies)) continue;
                Overlap overlap = overlaps(first.path, second.path);
                if (overlap == Overlap::No) continue;
                const Access& read = first.modifies ? second : first;
                if (!read.modifies && read.arg >= 0 && !needsDestruction(read.expr->type)) {
                    copied[read.arg] = true;
                    continue;
                }
                if (overlap == Overlap::Maybe && overlapChecks) {
                    overlapChecks->emplace_back(first.arg, second.arg);  // compared at runtime (D35)
                    continue;
                }
                std::string message = "'" + placeText(*second.expr) + "' overlaps '" + placeText(*first.expr) +
                                      "' in the same call, ";
                if (first.arg == -2 || second.arg == -2) {
                    message += "which a lambda of the call captures (D184)";
                } else if (read.modifies) {
                    message += "and the call can modify both (D22)";
                } else if (read.arg < 0) {
                    message += "which is the object of the method (D22)";
                } else {
                    message += "which the call can modify, and values of type " + read.expr->type.name() + " are not copied implicitly (D79)";
                }
                diag_->error(second.expr->loc, message);
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

    Type checkMethodCall(ExprPtr& expr) {
        auto& call = static_cast<MethodCallExpr&>(*expr);
        // `geometry.area(c)`: a function or a struct of an imported module (D126).
        if (call.object->kind == ExprKind::Name) {
            std::string module = static_cast<NameExpr&>(*call.object).name;
            if (!lookup(module) && ownFieldIndex(module) < 0 && importsQualifier(call.loc, module)) {
                auto qualified = std::make_unique<CallExpr>(call.loc, call.method);
                qualified->module = std::move(module);
                qualified->args = std::move(call.args);
                expr = std::move(qualified);
                return checkCall(expr);
            }
        }
        if (std::optional<Type> named = enumNamed(*call.object)) {
            return checkEnumCase(expr, *named, call.method, &call.args, call.loc);
        }
        Type object = checkExpr(call.object, std::nullopt);
        if (object.isError()) return object;
        // `x.copy()` of a number or a bool is the value itself, so that generics can copy any T (D89).
        if (call.method == "copy" && (object.isNumeric() || object.isBool())) {
            if (!call.args.empty()) {
                diag_->error(call.loc, "copy() takes no arguments");
                return Type::error();
            }
            SourceLocation loc = call.loc;
            ExprPtr operand = std::move(call.object);
            expr = std::make_unique<CopyExpr>(loc, std::move(operand));
            return object;
        }
        if (object.isParam() || object.isInterface()) return checkInterfaceCall(call, object);
        if (object.isArray()) return checkArrayMethod(expr, object);
        if ((object.isNumeric() || object.isBool()) && (call.method == "hash" || call.method == "equals")) {
            return checkKeyMethod(expr, object);
        }
        if (isString(object) && call.method == "cstring") return checkCString(expr);
        if (!object.isStruct()) {
            diag_->error(call.loc, "a " + object.name() + " has no methods");
            return Type::error();
        }
        FunctionDecl* method = nullptr;
        auto found = object.structDecl->methods.find(call.method);
        int field = found == object.structDecl->methods.end() ? fieldIndex(*object.structDecl, call.method) : -1;
        if (field >= 0 && object.structDecl->fields[field].resolved.isFunction()) {
            // `handler.run(x)`: the call of a field that holds a function (D186).
            SourceLocation loc = call.loc;
            auto value = std::make_unique<CallValueExpr>(loc, std::make_unique<FieldExpr>(loc, std::move(call.object), call.method));
            value->args = std::move(call.args);
            expr = std::move(value);
            return checkCallValue(expr);
        }
        if (found != object.structDecl->methods.end()) {
            method = found->second;
        } else if (call.method == "copy") {
            method = copyFunction(*object.structDecl, call.loc);
            if (!method) {
                diag_->error(call.loc, object.name() + " cannot be copied: it has a destructor, or a field that cannot be "
                                                      "copied, and it does not define copy() (D35)");
                return Type::error();
            }
        } else {
            diag_->error(call.loc, object.name() + " has no method named '" + call.method + "'");
            return Type::error();
        }
        if (!memberVisible(*method, *object.structDecl, call.loc)) {
            reportPrivate(call.loc, "method '" + call.method + "' of " + object.name());
            return Type::error();
        }
        if (!method->isConst) {
            std::string reason = readOnlyReason(*call.object);
            if (!reason.empty()) {
                diag_->error(call.loc, "cannot call '" + call.method + "', which is not const (D24): " +
                                          reason);
                return Type::error();
            }
        }
        call.function = method;
        use(method, call.loc);
        noteCall(method, call.loc);
        if (!checkArgs(call.args, *method, "'" + call.method + "'", call.loc, &call.overlaps, call.object.get())) {
            return Type::error();
        }
        return method->resolvedReturn;
    }

    // `s.cstring()` (D204): the address of a copy that ends with a zero, a
    // temporary of the library destroyed at the end of the statement.
    Type checkCString(ExprPtr& expr) {
        auto& call = static_cast<MethodCallExpr&>(*expr);
        if (!isUnsafe(call.loc)) {
            diag_->error(call.loc, "cstring() is for the C functions: the module must write 'import unsafe' (D204)");
            return Type::error();
        }
        if (!call.args.empty()) {
            diag_->error(call.loc, "cstring() takes no arguments");
            checkIgnored(call.args);
            return Type::error();
        }
        SourceLocation loc = call.loc;
        FunctionDecl* copy = stringDecl_->methods.at("zeroEnded");
        use(copy, loc);
        noteCall(copy, loc);
        auto made = std::make_unique<MethodCallExpr>(loc, std::move(call.object), "zeroEnded");
        made->function = copy;
        made->type = copy->resolvedReturn;
        auto data = std::make_unique<FieldExpr>(loc, std::move(made), "data");
        data->index = 0;
        data->type = Type::addrType();
        expr = std::move(data);
        return Type::addrType();
    }

    // The numbers and bool are keys by themselves (D198): `x.hash()` is computed by
    // the compiler, `x.equals(y)` is `x == y`.
    Type checkKeyMethod(ExprPtr& expr, Type object) {
        auto& call = static_cast<MethodCallExpr&>(*expr);
        size_t expected = call.method == "hash" ? 0 : 1;
        if (call.args.size() != expected) {
            diag_->error(call.loc, call.method + "() takes " + (expected ? "one argument" : "no arguments"));
            checkIgnored(call.args);
            return Type::error();
        }
        SourceLocation loc = call.loc;
        if (call.method == "hash") {
            auto hash = std::make_unique<CallExpr>(loc, "hash");
            hash->intrinsic = Intrinsic::HashNumber;
            hash->args.push_back(std::move(call.object));
            expr = std::move(hash);
            return Type::intType(64, false);
        }
        if (!coerce(call.args[0], object)) return Type::error();
        auto equal = std::make_unique<BinaryExpr>(loc, BinaryOp::Eq, std::move(call.object), std::move(call.args[0]));
        equal->operandType = object;
        expr = std::move(equal);
        return Type::boolType();
    }

    // `a.len()` is the size written in the type, `a.copy()` a copy of an array of
    // simple values (D196).
    Type checkArrayMethod(ExprPtr& expr, Type object) {
        auto& call = static_cast<MethodCallExpr&>(*expr);
        if (call.method != "len" && call.method != "copy") {
            diag_->error(call.loc, "an array has only len() and copy(), not '" + call.method + "' (D196)");
            return Type::error();
        }
        if (!call.args.empty()) {
            diag_->error(call.loc, call.method + "() takes no arguments");
            checkIgnored(call.args);
            return Type::error();
        }
        SourceLocation loc = call.loc;
        if (call.method == "len") {  // an i64, as the length of a list, not a literal without a type
            Type i64 = Type::intType(64, true);
            auto count = std::make_unique<IntLiteralExpr>(loc, static_cast<uint64_t>(object.array->count), false);
            count->type = i64;
            expr = std::make_unique<ConversionExpr>(loc, std::move(count), i64, false);
            return i64;
        }
        if (!canCopy(object)) {
            diag_->error(loc, object.name() + " cannot be copied: its elements are not simple values, move it instead (D196)");
            return Type::error();
        }
        ExprPtr operand = std::move(call.object);
        expr = std::make_unique<CopyExpr>(loc, std::move(operand));
        return object;
    }

    // A method of an interface: through a constraint of T (D52), or on a parameter
    // of interface type, with a dynamic call (D49).
    Type checkInterfaceCall(MethodCallExpr& call, Type object) {
        std::vector<const InterfaceDecl*> candidates;
        if (object.isParam()) candidates = object.param->interfaces;
        else candidates.push_back(object.interfaceDecl);
        FunctionDecl* method = nullptr;
        for (const InterfaceDecl* iface : candidates) {
            for (const FunctionDecl& candidate : iface->methods) {
                if (candidate.name == call.method && !method) method = const_cast<FunctionDecl*>(&candidate);
            }
        }
        if (!method) {
            // D85: whether T can be copied is checked in each instance.
            if (object.isParam() && call.method == "copy" && call.args.empty()) return object;
            std::string hint = object.isParam() ? ": only the methods of its constraints can be called (D52)" : "";
            diag_->error(call.loc, object.name() + " has no method named '" + call.method + "'" + hint);
            return Type::error();
        }
        if (!method->isConst) {
            std::string reason = readOnlyReason(*call.object);
            if (!reason.empty()) {
                diag_->error(call.loc, "cannot call '" + call.method + "', which is not const (D24): " + reason);
                return Type::error();
            }
        }
        call.function = method;
        noteCall(method, call.loc);
        if (!checkArgs(call.args, *method, "'" + call.method + "'", call.loc, &call.overlaps, call.object.get(), object)) {
            return Type::error();
        }
        return withSelf(method->resolvedReturn, object);
    }

    Type checkUnary(ExprPtr& expr, std::optional<Type> expected) {
        auto& unary = static_cast<UnaryExpr&>(*expr);
        if (unary.op == UnaryOp::Not) {
            // D194: on an integer, `!` inverts its bits.
            std::optional<Type> hint = expected && expected->isInt() ? expected : std::optional<Type>(Type::boolType());
            Type type = checkExpr(unary.operand, hint);
            if (type.isUntyped() && !type.isError()) {
                diag_->error(unary.loc, "the type of this expression cannot be deduced: write the type of the value, for "
                                        "example !u8(0) (D55)");
                return Type::error();
            }
            if (!type.isBool() && !type.isInt()) {
                if (!type.isError()) diag_->error(unary.loc, "'!' needs a bool or an integer, found " + type.name() + " (D194)");
                return Type::error();
            }
            return type;
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
            diag_->error(unary.loc, "cannot negate an unsigned value (" + type.name() + ") (D59)");
            return Type::error();
        }
        if (!type.isNumeric()) {
            diag_->error(unary.loc, "'-' needs a number, found " + type.name());
            return Type::error();
        }
        return type;
    }

    Type checkBinary(ExprPtr& expr, std::optional<Type> expected) {
        auto& binary = static_cast<BinaryExpr&>(*expr);
        if (binary.op == BinaryOp::And || binary.op == BinaryOp::Or) {
            for (ExprPtr* side : {&binary.lhs, &binary.rhs}) {
                Type type = checkExpr(*side, Type::boolType());
                if (!type.isBool()) {
                    if (!type.isError()) {
                        diag_->error((*side)->loc, std::string("'") + spelling(binary.op) + "' needs bool values, found " +
                                                      type.name());
                    }
                    return Type::error();
                }
            }
            binary.operandType = Type::boolType();
            return Type::boolType();
        }

        if (binary.op == BinaryOp::Shl || binary.op == BinaryOp::Shr) return checkShift(binary, expected);
        bool arithmetic = isArithmetic(binary.op);
        std::optional<Type> hint;
        if (arithmetic && expected && expected->isNumeric()) hint = expected;
        Type operand = unify(binary, hint);
        if (operand.isError()) return operand;
        binary.operandType = operand;
        if (isString(operand)) return stringOperator(expr);

        if (operand.isParam() || operand.isInterface()) {
            diag_->error(binary.loc, std::string("'") + spelling(binary.op) + "' does not work on values of type " +
                                         operand.name() + ": no interface provides it yet (D87)");
            return Type::error();
        }
        bool equality = binary.op == BinaryOp::Eq || binary.op == BinaryOp::Ne;
        if (operand.isStruct() && operand.structDecl->isEnum && equality) {
            for (const EnumCase& item : operand.structDecl->cases) {
                if (item.fields.empty()) continue;
                diag_->error(binary.loc, std::string("'") + spelling(binary.op) +
                                             "' works on the enums whose cases have no data: tell the case with 'switch' (D112)");
                return Type::error();
            }
            return Type::boolType();
        }
        if (operand.isStruct()) {
            diag_->error(binary.loc, std::string("'") + spelling(binary.op) + "' does not work on structs yet (D76)");
            return Type::error();
        }
        if (operand.isArray() || operand.isFunction()) {
            diag_->error(binary.loc, std::string("'") + spelling(binary.op) + "' does not work on values of type " + operand.name());
            return Type::error();
        }
        if (arithmetic) {
            if (!operand.isNumeric()) {
                diag_->error(binary.loc, std::string("'") + spelling(binary.op) + "' needs numbers, found " + operand.name());
                return Type::error();
            }
            if (binary.op == BinaryOp::Rem && !operand.isInt()) {
                diag_->error(binary.loc, "'%' only works on integers (D59)");
                return Type::error();
            }
            if (isBitwise(binary.op) && !operand.isInt()) {
                diag_->error(binary.loc, std::string("'") + spelling(binary.op) + "' only works on integers (D193)");
                return Type::error();
            }
            return operand;
        }
        if (!operand.isNumeric() && !(equality && (operand.isBool() || operand.kind == TypeKind::Addr))) {
            diag_->error(binary.loc, std::string("'") + spelling(binary.op) + "' cannot compare " + operand.name() + " values");
            return Type::error();
        }
        return Type::boolType();
    }

    // `x << n`, `x >> n` (D195): the type is the one of x; n is any integer.
    Type checkShift(BinaryExpr& binary, std::optional<Type> expected) {
        Type type = checkExpr(binary.lhs, expected && expected->isInt() ? expected : std::nullopt);
        if (type.isUntyped()) {
            if (!expected || !expected->isInt()) {
                diag_->error(binary.loc, "the type of this expression cannot be deduced: write the type of the value, for "
                                         "example u32(1) << n (D55)");
                return Type::error();
            }
            if (!coerce(binary.lhs, *expected)) return Type::error();
            type = *expected;
        }
        Type count = checkExpr(binary.rhs, std::nullopt);
        if (count.isUntyped() && !type.isError() && type.isInt()) {
            if (!coerce(binary.rhs, type)) return Type::error();
            count = type;
        }
        if (type.isError() || count.isError()) return Type::error();
        if (!type.isInt()) {
            diag_->error(binary.loc, std::string("'") + spelling(binary.op) + "' works on integers, found " + type.name() + " (D195)");
            return Type::error();
        }
        if (!count.isInt()) {
            diag_->error(binary.loc, "the count of a shift is an integer, found " + count.name() + " (D195)");
            return Type::error();
        }
        binary.operandType = type;
        return type;
    }

    // D95: the operators on strings are calls to the methods of the library.
    Type stringOperator(ExprPtr& expr) {
        auto& binary = static_cast<BinaryExpr&>(*expr);
        SourceLocation loc = binary.loc;
        BinaryOp op = binary.op;
        auto call = [&](const char* method, Type result) {
            auto node = std::make_unique<MethodCallExpr>(loc, std::move(binary.lhs), method);
            node->args.push_back(std::move(binary.rhs));
            node->function = stringDecl_->methods.at(method);
            node->type = result;
            return node;
        };
        Type boolean = Type::boolType();
        switch (op) {
        case BinaryOp::Add:
            expr = call("concat", stringType());
            return stringType();
        case BinaryOp::Eq:
            expr = call("equals", boolean);
            return boolean;
        case BinaryOp::Ne: {
            auto negation = std::make_unique<UnaryExpr>(loc, UnaryOp::Not, call("equals", boolean));
            expr = std::move(negation);
            return boolean;
        }
        case BinaryOp::Lt:
        case BinaryOp::Le:
        case BinaryOp::Gt:
        case BinaryOp::Ge: {
            Type i32 = Type::intType(32, true);
            auto zero = std::make_unique<IntLiteralExpr>(loc, 0, false);
            zero->type = i32;
            auto comparison = std::make_unique<BinaryExpr>(loc, op, call("compare", i32), std::move(zero));
            comparison->operandType = i32;
            expr = std::move(comparison);
            return boolean;
        }
        default:
            diag_->error(loc, std::string("'") + spelling(op) + "' does not work on strings (D95)");
            return Type::error();
        }
    }

    Type stringType() const { return Type::structType(stringDecl_); }
    bool isString(Type type) const { return stringDecl_ && type.isStruct() && type.structDecl == stringDecl_; }
    bool isList(Type type) const { return listDecl_ && type.isStruct() && type.structDecl->generic == listDecl_; }
    bool isMap(Type type) const { return mapDecl_ && type.isStruct() && type.structDecl->generic == mapDecl_; }
    static Type elementOf(Type list) { return list.isArray() ? list.array->element : list.structDecl->typeArgs[0]; }

    // `v[i]`: an element of a list, with an i64 index (D12, D35).
    Type checkIndex(IndexExpr& node, bool inserts) {
        Type object = checkExpr(node.object, std::nullopt);
        if (isMap(object)) {  // D197: through valueOf, or place for the target of an assignment
            Type key = object.structDecl->typeArgs[0];
            if (!coerce(node.index, key)) return Type::error();
            FunctionDecl* access = object.structDecl->methods.at(inserts ? "place" : "valueOf");
            node.access = access;
            use(access, node.loc);
            noteCall(access, node.loc);
            return object.structDecl->typeArgs[1];
        }
        bool indexOk = coerce(node.index, Type::intType(64, true));
        if (object.isError() || !indexOk) return Type::error();
        if (object.isArray()) {  // D196: a constant index is checked here, the others when the program runs
            std::optional<int64_t> constant = constantIndex(*node.index);
            if (constant && (*constant < 0 || *constant >= object.array->count)) {
                diag_->error(node.index->loc, "the index " + std::to_string(*constant) + " is outside the array, which has " +
                                                  std::to_string(object.array->count) + " elements (D30)");
                return Type::error();
            }
            return object.array->element;
        }
        if (isString(object)) return Type::intType(8, false);  // a byte, as the length is in bytes (D200)
        if (!isList(object)) {
            diag_->error(node.loc, "only a list, an array and a string have elements, found " + object.name() + " (D35)");
            return Type::error();
        }
        return elementOf(object);
    }

    // The built-in functions: `panic` (D102), and the `unsafe` dialect of the
    // standard library (D98).
    Type checkIntrinsic(CallExpr& call, Intrinsic kind) {
        call.intrinsic = kind;
        bool generic = kind == Intrinsic::SizeOf || kind == Intrinsic::Take || kind == Intrinsic::Put ||
                       kind == Intrinsic::Destroy || kind == Intrinsic::CopyAt;
        std::string what = "'" + call.callee + "'";
        if (generic) {
            std::vector<Type> args;
            if (!resolveTypeArgs(call.typeArgs, 1, what, call.loc, args)) {
                checkIgnored(call.args);
                return Type::error();
            }
            call.typeArg = args[0];
        } else if (!call.typeArgs.empty()) {
            diag_->error(call.loc, what + " has no type arguments");
            checkIgnored(call.args);
            return Type::error();
        }

        struct Signature {
            ParamMode mode;
            const char* name;
            Type type;
        };
        Type i64 = Type::intType(64, true);
        Type addr = Type::addrType();
        Type element = call.typeArg;
        std::vector<Signature> params;
        Type result = Type::voidType();
        switch (kind) {
        case Intrinsic::Panic: params = {{ParamMode::Read, "reason", stringType()}}; break;
        case Intrinsic::Alloc: params = {{ParamMode::Read, "bytes", i64}}; result = addr; break;
        case Intrinsic::Resize: params = {{ParamMode::Read, "p", addr}, {ParamMode::Read, "bytes", i64}}; result = addr; break;
        case Intrinsic::Free: params = {{ParamMode::Read, "p", addr}}; break;
        case Intrinsic::SizeOf: result = i64; break;
        case Intrinsic::Offset: params = {{ParamMode::Read, "p", addr}, {ParamMode::Read, "bytes", i64}}; result = addr; break;
        case Intrinsic::Take: params = {{ParamMode::Read, "p", addr}}; result = element; break;
        case Intrinsic::Put: params = {{ParamMode::Read, "p", addr}, {ParamMode::Move, "value", element}}; break;
        case Intrinsic::Destroy: params = {{ParamMode::Read, "p", addr}}; break;
        case Intrinsic::CopyAt: params = {{ParamMode::Read, "p", addr}}; result = element; break;
        case Intrinsic::CopyMemory:
            params = {{ParamMode::Read, "dest", addr}, {ParamMode::Read, "source", addr}, {ParamMode::Read, "bytes", i64}};
            break;
        case Intrinsic::CompareMemory:
            params = {{ParamMode::Read, "a", addr}, {ParamMode::Read, "b", addr}, {ParamMode::Read, "bytes", i64}};
            result = Type::intType(32, true);
            break;
        case Intrinsic::CheckIndex: params = {{ParamMode::Read, "index", i64}, {ParamMode::Read, "length", i64}}; break;
        case Intrinsic::ArgumentCount: result = i64; break;
        case Intrinsic::ArgumentLength: params = {{ParamMode::Read, "index", i64}}; result = i64; break;
        case Intrinsic::ArgumentData: params = {{ParamMode::Read, "index", i64}}; result = addr; break;
        case Intrinsic::DecimalToFloat:
            params = {{ParamMode::Read, "data", addr}, {ParamMode::Read, "length", i64}, {ParamMode::Read, "bits", i64}};
            result = Type::floatType(64);
            break;
        case Intrinsic::HashBytes:
            params = {{ParamMode::Read, "data", addr}, {ParamMode::Read, "length", i64}};
            result = Type::intType(64, false);
            break;
        case Intrinsic::HashNumber: break;
        case Intrinsic::SystemCall:  // Linux only (D202): the number, then six arguments
            params = {{ParamMode::Read, "number", i64}, {ParamMode::Read, "a", i64}, {ParamMode::Read, "b", i64},
                      {ParamMode::Read, "c", i64},      {ParamMode::Read, "d", i64}, {ParamMode::Read, "e", i64},
                      {ParamMode::Read, "f", i64}};
            result = i64;
            break;
        case Intrinsic::Environment: params = {{ParamMode::Read, "index", i64}}; result = addr; break;
        case Intrinsic::ExitProcess: params = {{ParamMode::Read, "code", Type::intType(32, true)}}; break;
        case Intrinsic::None: break;
        }

        if (kind == Intrinsic::CopyAt && !element.isParam()) {  // D85: checked for each type
            if (!canCopy(element)) {
                diag_->error(call.loc, "values of type " + element.name() + " cannot be copied: it has a destructor, or a "
                                                                            "field that cannot be copied, and no copy() (D35)");
                checkIgnored(call.args);
                return Type::error();
            }
            if (element.isStruct()) copyFunction(*element.structDecl, call.loc);
        }

        if (kind == Intrinsic::SystemCall) {  // an argument is an integer or an address (D202)
            bool ok = call.args.size() == params.size();
            if (!ok) diag_->error(call.loc, "'systemCall' takes 7 arguments: the number of the call and 6 values");
            for (ExprPtr& arg : call.args) {
                Type type = checkExpr(arg, i64);
                if (type.isError() || type.kind == TypeKind::Addr) continue;
                ok = coerce(arg, i64) && ok;
            }
            return ok ? result : Type::error();
        }
        FunctionDecl signature;
        signature.name = call.callee;
        signature.loc = call.loc;
        signature.resolvedReturn = result;
        for (const Signature& param : params) {
            Param written;
            written.mode = param.mode;
            written.name = param.name;
            written.loc = call.loc;
            signature.params.push_back(written);
            signature.paramTypes.push_back(param.type);
        }
        if (!checkArgs(call.args, signature, what, call.loc, &call.overlaps)) return Type::error();
        return result;
    }

    // Finds the common type of the two operands, giving literals the type of the
    // other side (D55) and widening without loss (D33, D56).
    Type unify(BinaryExpr& binary, std::optional<Type> hint) {
        bool leftLiteral = isUntypedLiteral(*binary.lhs);
        bool rightLiteral = isUntypedLiteral(*binary.rhs);
        if (leftLiteral && rightLiteral) {
            if (!hint) {
                diag_->error(binary.loc, "the type of this expression cannot be deduced: write the type of one of the values, "
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
                diag_->error(literal->loc, "a number cannot be combined with a value of type " + type.name());
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
            diag_->error(binary.loc, "cannot mix " + left.name() + " and " + right.name() +
                                        ": convert one of them explicitly, for example i64(x) (D56)");
        } else {
            diag_->error(binary.loc, "mismatched types " + left.name() + " and " + right.name() +
                                        ": convert one of them explicitly (D33)");
        }
        return Type::error();
    }

    static int fieldIndex(const StructDecl& decl, const std::string& name) {
        for (size_t i = 0; i < decl.fields.size(); ++i) {
            if (decl.fields[i].name == name) return static_cast<int>(i);
        }
        return -1;
    }

    static bool capturedWithRef(const LambdaExpr& lambda, const Expr& place) {
        const Variable* variable = static_cast<const NameExpr&>(place).variable;
        for (const Capture& capture : lambda.captures) {
            if (capture.outer == variable) return capture.mode == ParamMode::Ref;
        }
        return false;
    }

    // The declaration that describes the parameters of a function type, for the
    // checks of the arguments and for the code (D186). Its calls can always throw.
    const FunctionDecl* signatureOf(Type type) {
        const FunctionSig* sig = type.sig;
        for (const auto& existing : module_.signatures) {
            if (existing->resolvedReturn == sig->result && existing->paramTypes == sig->params) {
                bool same = true;
                for (size_t i = 0; i < sig->modes.size(); ++i) same = same && existing->params[i].mode == sig->modes[i];
                if (same) return existing.get();
            }
        }
        auto decl = std::make_unique<FunctionDecl>();
        decl->name = type.name();
        decl->isLambda = true;
        decl->canThrow = true;
        decl->resolvedReturn = sig->result;
        for (size_t i = 0; i < sig->params.size(); ++i) {
            Param param;
            param.mode = sig->modes[i];
            param.name = std::to_string(i + 1);
            decl->params.push_back(param);
            decl->paramTypes.push_back(sig->params[i]);
        }
        module_.signatures.push_back(std::move(decl));
        return module_.signatures.back().get();
    }

    // `less(a, b)`: the call of a function value (D186).
    Type checkCallValue(ExprPtr& expr) {
        auto& call = static_cast<CallValueExpr&>(*expr);
        Type type = checkExpr(call.callee, std::nullopt);
        if (type.isError()) {
            checkIgnored(call.args);
            return Type::error();
        }
        if (!type.isFunction()) {
            diag_->error(call.loc, "a " + type.name() + " is not a function");
            checkIgnored(call.args);
            return Type::error();
        }
        call.signature = signatureOf(type);
        noteThrow(nullptr, call.loc);  // the function is not known: it may throw (D36)
        if (!checkArgs(call.args, *call.signature, "this function", call.loc, &call.overlaps)) return Type::error();
        expr->type = type.sig->result;
        return type.sig->result;
    }

    // `[limit, ref total](i32 x) bool { ... }` (D183, D184). `asArgument`: it is
    // the argument of a read parameter, the only place for one that borrows.
    Type checkLambda(LambdaExpr& lambda, bool asArgument) {
        FunctionDecl& function = *lambda.function;
        lambda.enclosing = current_;
        std::set<std::string> seen;
        for (Capture& capture : lambda.captures) {
            if (!seen.insert(capture.name).second) {
                diag_->error(capture.loc, "'" + capture.name + "' is already captured");
                continue;
            }
            Variable* outer = lookup(capture.name);
            if (!outer) {
                if (ownFieldIndex(capture.name) >= 0) {
                    diag_->error(capture.loc, "'" + capture.name + "' is a field: copy it into a variable to capture it (D183)");
                } else {
                    diag_->error(capture.loc, "unknown name '" + capture.name + "'");
                }
                continue;
            }
            capture.outer = outer;
            NameExpr place(capture.loc, capture.name);
            place.variable = outer;
            place.type = outer->type;
            if (capture.mode == ParamMode::Ref) {
                std::string reason = readOnlyReason(place);
                if (!reason.empty()) diag_->error(capture.loc, "cannot capture '" + capture.name + "' with 'ref': " + reason);
                capture.byAddress = true;
            } else if (capture.mode == ParamMode::Move) {
                bool owned = outer->kind == VariableKind::Local || outer->kind == VariableKind::MoveParam;
                if (needsDestruction(outer->type) && !owned) {
                    diag_->error(capture.loc, "cannot capture '" + capture.name + "' with 'move': only a local variable or a "
                                              "'move' parameter can give its value (D183)");
                }
                if (needsDestruction(outer->type) && owned) outer->needsDropFlag = true;
            } else {
                capture.byAddress = needsDestruction(outer->type);  // read without a copy
            }
            lambda.borrows = lambda.borrows || capture.byAddress;
            auto shown = std::make_unique<NameExpr>(capture.loc, capture.name);
            shown->variable = outer;
            shown->type = outer->type;
            if (capture.byAddress) lambda.capturePlaces.push_back(std::move(shown));
        }

        // The body is a function of its own: its parameters, then the captures.
        FunctionDecl* outerFunction = current_;
        auto outerScopes = std::move(scopes_);
        auto outerLoops = std::move(loops_);
        auto outerTries = std::move(tryContexts_);
        auto outerCatches = std::move(catchTypes_);
        auto outerFinallyLoops = std::move(finallyLoops_);
        auto outerRegions = std::move(regions_);
        hiddenScopes_.push_back(&outerScopes);

        function.symbol = symbolFor(lambda.loc, "lambda." + std::to_string(module_.lambdas.size()));
        function.isAbstract = outerFunction->isAbstract;
        resolveSignature(function);
        current_ = &function;
        scopes_.assign(1, {});
        loops_.clear();
        tryContexts_.clear();
        catchTypes_.clear();
        finallyLoops_.clear();
        regions_.clear();
        for (size_t i = 0; i < function.params.size(); ++i) {
            const Param& param = function.params[i];
            Variable* variable = declare(param.name, function.paramTypes[i], param.loc, param.isConst);
            if (!variable) continue;
            variable->kind = param.mode == ParamMode::Ref    ? VariableKind::RefParam
                             : param.mode == ParamMode::Move ? VariableKind::MoveParam
                                                             : VariableKind::ReadParam;
        }
        int index = 0;
        for (Capture& capture : lambda.captures) {
            if (!capture.outer) continue;
            Variable* inner = declare(capture.name, capture.outer->type, capture.loc, capture.mode != ParamMode::Ref);
            if (!inner) continue;
            inner->kind = VariableKind::Captured;
            inner->captureIndex = index++;
            inner->captureByAddress = capture.byAddress;
            capture.inner = inner;
        }
        Flow flow = checkBlock(function.body);
        function.bodyChecked = true;
        Type ret = function.resolvedReturn;
        if (!ret.isVoid() && !ret.isError() && flow != Flow::Returns) {
            diag_->error(function.body.close, "the lambda must end with 'ret' on every path: it returns " + ret.name());
        }

        hiddenScopes_.pop_back();
        current_ = outerFunction;
        scopes_ = std::move(outerScopes);
        loops_ = std::move(outerLoops);
        tryContexts_ = std::move(outerTries);
        catchTypes_ = std::move(outerCatches);
        finallyLoops_ = std::move(outerFinallyLoops);
        regions_ = std::move(outerRegions);

        module_.lambdas.push_back(&lambda);
        lambda.onStack = asArgument;
        if (lambda.borrows && !asArgument) {
            diag_->error(lambda.loc, "this lambda captures by address (with 'ref', or reading a value that is not simple): "
                                     "it can only be passed as an argument; capture with 'move' to keep it (D184)");
            return Type::error();
        }
        std::vector<ParamMode> modes;
        for (const Param& param : function.params) modes.push_back(param.mode);
        return Type::functionType(internSignature(function.paramTypes, modes, function.resolvedReturn));
    }

    Type checkCall(ExprPtr& expr) {
        auto& call = static_cast<CallExpr&>(*expr);
        bool typed = !call.typeArgs.empty();
        bool qualified = !call.module.empty();  // `geometry.area(c)` (D126)

        if (!typed && !qualified) {
            if (std::optional<Type> target = findBuiltinType(call.callee)) return checkConversion(expr, *target);
            if (isPrintFunction(call.callee)) return checkPrint(expr);
            if (call.callee == "panic") return checkIntrinsic(call, Intrinsic::Panic);
        }
        if (!qualified && isUnsafe(call.loc)) {
            if (std::optional<Intrinsic> intrinsic = findIntrinsic(call.callee)) return checkIntrinsic(call, *intrinsic);
        }

        if (!typed && !qualified) {
            Variable* variable = lookup(call.callee);
            int field = ownFieldIndex(call.callee);
            bool fieldValue = !variable && field >= 0 && current_->owner->fields[field].resolved.isFunction();
            if ((variable && variable->type.isFunction()) || fieldValue) {
                SourceLocation loc = call.loc;
                ExprPtr callee = variable ? ExprPtr(std::make_unique<NameExpr>(loc, call.callee))
                                          : ExprPtr(std::make_unique<FieldExpr>(loc, std::make_unique<SelfExpr>(loc), call.callee));
                auto value = std::make_unique<CallValueExpr>(loc, std::move(callee));
                value->args = std::move(call.args);
                expr = std::move(value);
                return checkCallValue(expr);
            }
        }

        if (StructDecl* decl = findDecl(&ModuleScope::structs, call.callee, call.loc, call.module)) {
            if (!decl->typeParams.empty() && !typed) {
                diag_->error(call.loc, decl->name + " is generic: write its type arguments, " + decl->name + "<...>(...) (D92)");
                checkIgnored(call.args);
                return Type::error();
            }
            if (typed) {
                std::optional<Type> type = resolveType(TypeName{call.callee, call.loc, call.typeArgs, call.module});
                if (!type) {
                    checkIgnored(call.args);
                    return Type::error();
                }
                decl = const_cast<StructDecl*>(type->structDecl);
            }
            if (decl->isEnum && call.args.size() == 1) return checkEnumConversion(expr, decl);
            if (decl == stringDecl_ && call.args.size() == 1) return checkLibraryCall(expr, "stringFromBytes");  // D200
            if (decl == stringDecl_ && call.args.size() == 2 && isUnsafe(call.loc)) {  // the bytes of C (D204)
                return checkLibraryCall(expr, "stringFromMemory");
            }
            return checkConstruct(expr, decl);
        }

        // D72: inside a method, a call refers first to a method of the same struct.
        if (current_->owner && !typed && !qualified) {
            auto method = current_->owner->methods.find(call.callee);
            if (method != current_->owner->methods.end()) {
                SourceLocation loc = call.loc;
                auto methodCall = std::make_unique<MethodCallExpr>(loc, std::make_unique<SelfExpr>(loc), call.callee);
                methodCall->args = std::move(call.args);
                expr = std::move(methodCall);
                return checkMethodCall(expr);
            }
        }

        FunctionDecl* function = findDecl(&ModuleScope::functions, call.callee, call.loc, call.module);
        if (!function) {
            std::string where = privateIn(call.callee, call.loc, call.module);
            if (!qualified && (lookup(call.callee) || ownFieldIndex(call.callee) >= 0)) {
                diag_->error(call.loc, "'" + call.callee + "' is a variable, not a function");
            } else if (typed && !qualified && (findBuiltinType(call.callee) || isPrintFunction(call.callee))) {
                diag_->error(call.loc, "'" + call.callee + "' has no type arguments");
            } else if (!where.empty()) {
                reportPrivate(call.loc, "'" + call.callee + "' of " + where);
            } else if (!qualified && findIntrinsic(call.callee)) {
                diag_->error(call.loc, "'" + call.callee + "' belongs to the unsafe dialect: the module must write 'import unsafe' (D127)");
            } else if (qualified) {
                diag_->error(call.loc, "the module " + call.module + " has no function '" + call.callee + "'");
            } else {
                diag_->error(call.loc, "unknown function '" + call.callee + "'");
            }
            checkIgnored(call.args);
            return Type::error();
        }

        if (!function->typeParams.empty()) {
            function = instanceFor(*function, call);
        } else if (typed) {
            diag_->error(call.loc, "'" + call.callee + "' is not generic: it has no type arguments");
            function = nullptr;
        }
        if (!function) {
            checkIgnored(call.args);
            return Type::error();
        }
        call.function = function;
        noteCall(function, call.loc);
        if (!checkArgs(call.args, *function, "'" + call.callee + "'", call.loc, &call.overlaps)) return Type::error();
        return function->resolvedReturn;
    }

    // A function of the standard library that the programs do not see: the
    // compiler calls it for them, as `string(bytes)` (D200).
    FunctionDecl* libraryFunction(const std::string& name) {
        SourceLocation library;
        library.file = kLibraryFile;
        return findDecl(&ModuleScope::functions, name, library);
    }

    Type checkLibraryCall(ExprPtr& expr, const std::string& name) {
        auto& call = static_cast<CallExpr&>(*expr);
        FunctionDecl* function = libraryFunction(name);
        call.function = function;
        noteCall(function, call.loc);
        if (!checkArgs(call.args, *function, "'" + call.callee + "'", call.loc, &call.overlaps)) return Type::error();
        return function->resolvedReturn;
    }

    // `i32("42")` (D199): a function of the library reads the text, with the range
    // of the type, and throws when it is not a number of the type.
    Type numberFromText(ExprPtr& expr, ExprPtr text, Type target) {
        SourceLocation loc = expr->loc;
        Type i64 = Type::intType(64, true);
        Type u64 = Type::intType(64, false);
        auto literal = [&](uint64_t value, Type type) {
            auto number = std::make_unique<IntLiteralExpr>(loc, value, false);
            number->type = type;
            return number;
        };
        const char* name = target.isFloat() ? "parseFloat" : target.isSigned ? "parseSigned" : "parseUnsigned";
        auto call = std::make_unique<CallExpr>(loc, name);
        call->args.push_back(std::move(text));
        if (target.isFloat()) {
            call->args.push_back(literal(static_cast<uint64_t>(target.bits), i64));
        } else if (target.isSigned) {
            call->args.push_back(literal((uint64_t{1} << (target.bits - 1)) - 1, i64));
        } else {
            call->args.push_back(literal(target.bits == 64 ? ~uint64_t{0} : (uint64_t{1} << target.bits) - 1, u64));
        }
        auto typeName = std::make_unique<StringLiteralExpr>(loc, target.name());
        typeName->type = stringType();
        call->args.push_back(std::move(typeName));
        call->function = libraryFunction(name);
        call->type = call->function->resolvedReturn;
        noteCall(call->function, loc);
        if (call->type == target) {
            expr = std::move(call);
        } else {  // always in the range: the library has checked it
            expr = std::make_unique<ConversionExpr>(loc, std::move(call), target, true);
        }
        return target;
    }

    // The arguments of a call that cannot be checked against a function: only
    // their own errors are reported.
    void checkIgnored(std::vector<ExprPtr>& args) {
        for (ExprPtr& arg : args) {
            ExprPtr* value = &arg;
            if (arg->kind == ExprKind::Ref) value = &static_cast<RefExpr&>(*arg).operand;
            if (arg->kind == ExprKind::Move) value = &static_cast<MoveExpr&>(*arg).operand;
            Type ignored = checkExpr(*value, std::nullopt);
            (void)ignored;
        }
    }

    // The instance of a generic function for a call, with the type arguments
    // written or deduced from the arguments (D86).
    FunctionDecl* instanceFor(FunctionDecl& generic, CallExpr& call) {
        std::vector<Type> args;
        std::string what = "'" + generic.name + "'";
        if (!call.typeArgs.empty()) {
            if (!resolveTypeArgs(call.typeArgs, generic.typeParams.size(), what, call.loc, args)) return nullptr;
            return instantiateFunction(generic, args, call.loc);
        }
        if (failed_.count(&generic)) return nullptr;
        size_t expected = generic.params.size();
        if (call.args.size() != expected) {
            diag_->error(call.loc, what + " takes " + std::to_string(expected) + (expected == 1 ? " argument, but " : " arguments, but ") +
                                       std::to_string(call.args.size()) + " were given");
            return nullptr;
        }
        std::vector<std::optional<Type>> deduced(generic.typeParams.size());
        for (size_t i = 0; i < call.args.size(); ++i) {
            std::optional<Type> type = argumentType(*call.args[i]);
            if (type && !deduce(generic.params[i].type, *type, generic, deduced, call.args[i]->loc)) return nullptr;
        }
        for (size_t i = 0; i < deduced.size(); ++i) {
            if (!deduced[i]) {
                diag_->error(call.loc, "cannot deduce " + generic.typeParams[i].name + " in this call of " + what +
                                           ": write the type arguments, " + generic.name + "<...>(...) (D86)");
                return nullptr;
            }
            args.push_back(*deduced[i]);
        }
        return instantiateFunction(generic, args, call.loc);
    }

    // The type of an argument, checked on a copy without reporting errors: it
    // serves only to deduce the type arguments (D86). A lambda does not help, so
    // it is not checked; the lambdas inside the copy are taken back, with what
    // their bodies noted and the escapes the check of the argument will note again.
    std::optional<Type> argumentType(const Expr& arg) {
        const Expr* value = &arg;
        if (arg.kind == ExprKind::Ref) value = static_cast<const RefExpr&>(arg).operand.get();
        if (arg.kind == ExprKind::Move) value = static_cast<const MoveExpr&>(arg).operand.get();
        if (value->kind == ExprKind::Lambda) return std::nullopt;
        ExprPtr copy = clone(*value);
        size_t lambdas = module_.lambdas.size();
        size_t escapes = escapes_.size();
        bool muted = diag_->muted();
        diag_->setMuted(true);
        Type type = checkExpr(copy, std::nullopt);
        diag_->setMuted(muted);
        for (size_t i = lambdas; i < module_.lambdas.size(); ++i) throwInfo_.erase(module_.lambdas[i]->function.get());
        module_.lambdas.erase(module_.lambdas.begin() + lambdas, module_.lambdas.end());
        escapes_.erase(escapes_.begin() + escapes, escapes_.end());
        if (type.isError() || type.isUntyped() || type.isVoid()) return std::nullopt;
        return type;
    }

    // Matches the type of a parameter, as written, with the type of its argument.
    bool deduce(const TypeName& pattern, Type actual, const FunctionDecl& generic, std::vector<std::optional<Type>>& deduced,
                SourceLocation loc) {
        if (!pattern.dimensions.empty()) {  // `T[3]` takes the elements of an i32[3] (D196)
            for (int64_t count : pattern.dimensions) {
                if (!actual.isArray() || actual.array->count != count) return true;  // reported by the check of the argument
                actual = actual.array->element;
            }
            TypeName element = pattern;
            element.dimensions.clear();
            return deduce(element, actual, generic, deduced, loc);
        }
        for (size_t i = 0; i < generic.typeParams.size(); ++i) {
            if (generic.typeParams[i].name != pattern.name || !pattern.args.empty()) continue;
            if (!deduced[i]) {
                deduced[i] = actual;
                return true;
            }
            if (*deduced[i] == actual) return true;
            diag_->error(loc, generic.typeParams[i].name + " is " + deduced[i]->name() + " for a previous argument, but " +
                                  actual.name() + " here: write the type arguments, " + generic.name + "<...>(...) (D86)");
            return false;
        }
        if (!pattern.args.empty() && actual.isStruct() && actual.structDecl->generic &&
            actual.structDecl->generic->name == pattern.name) {
            const std::vector<Type>& args = actual.structDecl->typeArgs;
            for (size_t i = 0; i < pattern.args.size() && i < args.size(); ++i) {
                if (!deduce(pattern.args[i], args[i], generic, deduced, loc)) return false;
            }
        }
        return true;
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
            if (!visible(decl->constructor->isPublic, *decl, node.loc)) {
                reportPrivate(node.loc, "the constructor of " + decl->name);
                checkIgnored(node.args);
                return Type::error();
            }
            use(decl->constructor, node.loc);
            noteCall(decl->constructor, node.loc);
            if (!checkArgs(node.args, *decl->constructor, "the constructor of " + decl->name, node.loc, &node.overlaps)) {
                return Type::error();
            }
        } else if (!node.args.empty()) {
            diag_->error(node.loc, decl->name + " has no constructor: " + decl->name + "() gives its default value (D71)");
            return Type::error();
        }
        return type;
    }

    // `f64(x)`: explicit conversion (D20, D61).
    Type checkConversion(ExprPtr& expr, Type target) {
        auto& call = static_cast<CallExpr&>(*expr);
        if (call.args.size() != 1) {
            diag_->error(call.loc, "a conversion to " + target.name() + " takes exactly one value");
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
        if (type.isStruct() && type.structDecl->isEnum && withoutData(*type.structDecl) && target.isInt()) {
            expr = std::make_unique<ConversionExpr>(call.loc, std::move(operand), target, true);  // the value of the case (D135)
            return target;
        }
        if (isString(type) && !target.isBool()) return numberFromText(expr, std::move(operand), target);
        if (type.isUntyped() || type.isVoid() || type.kind == TypeKind::String || type.isStruct() ||
            type.isParam() || type.isInterface() || type.isArray() || type.isFunction()) {
            diag_->error(operand->loc, "cannot convert this value to " + target.name());
            return Type::error();
        }
        if (type.isBool() != target.isBool()) {
            diag_->error(call.loc, "cannot convert " + type.name() + " to " + target.name() + " (D61)");
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

    // `Color(n)`: the case whose value is n, or a panic if there is none (D135).
    Type checkEnumConversion(ExprPtr& expr, StructDecl* decl) {
        auto& call = static_cast<CallExpr&>(*expr);
        if (!withoutData(*decl)) {
            diag_->error(call.loc, decl->name + " has data: its values are created with " + decl->name + ".Case(...) (D105)");
            checkIgnored(call.args);
            return Type::error();
        }
        ExprPtr operand = std::move(call.args[0]);
        std::optional<Type> expected;
        if (isUntypedLiteral(*operand)) expected = Type::intType(32, true);
        Type type = checkExpr(operand, expected);
        if (type.isError()) return type;
        if (!type.isInt()) {
            diag_->error(operand->loc, "a " + decl->name + " is made from an integer, not from " + type.name() + " (D135)");
            return Type::error();
        }
        SourceLocation loc = call.loc;
        expr = std::make_unique<ConversionExpr>(loc, std::move(operand), Type::structType(decl), true);
        return expr->type;
    }

    // ---- print / println (D21, D38) ----

    Type checkPrint(ExprPtr& expr) {
        auto& call = static_cast<CallExpr&>(*expr);
        bool format = call.callee == "format";  // into a string (D200)
        int stream = format ? 3 : call.callee[0] == 'e' ? 2 : 1;
        bool newline = call.callee.size() >= 2 && call.callee.compare(call.callee.size() - 2, 2, "ln") == 0;

        if (call.args.empty() || call.args[0]->kind != ExprKind::StringLiteral) {
            diag_->error(call.loc, "the first argument of " + call.callee + " must be a string literal with the format (D38)");
            return Type::error();
        }
        auto print = std::make_unique<PrintExpr>(call.loc, stream, newline);
        std::string text = static_cast<StringLiteralExpr&>(*call.args[0]).value;
        SourceLocation formatLoc = call.args[0]->loc;

        bool ok = true;
        for (size_t i = 1; i < call.args.size(); ++i) {
            ExprPtr arg = std::move(call.args[i]);
            if (arg->kind == ExprKind::StringLiteral) {
                arg->type = Type::stringType();
            } else {
                Type type = checkExpr(arg, std::nullopt);
                if (type.isUntyped()) {
                    diag_->error(arg->loc, "the type of this literal cannot be deduced: write it explicitly, for example i32(5) (D64)");
                    ok = false;
                } else if (type.isVoid()) {
                    diag_->error(arg->loc, "this call does not return a value");
                    ok = false;
                } else if ((type.isStruct() && !isString(type)) || type.isParam() || type.isInterface() || type.isArray() ||
                           type.isFunction()) {
                    diag_->error(arg->loc, "a " + type.name() + " cannot be printed yet (D76)");
                    ok = false;
                } else if (type.isError()) {
                    ok = false;
                }
            }
            print->args.push_back(std::move(arg));
        }

        if (ok) parseFormat(text, formatLoc, *print);
        expr = std::move(print);
        if (format) return ok ? stringType() : Type::error();
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
                diag_->error(loc, "single '}' in the format string: write '}}' for a brace");
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
                diag_->error(loc, "'{' without a matching '}' in the format string: write '{{' for a brace");
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
                        diag_->error(loc, "invalid placeholder '{" + inside + "}'");
                        return;
                    }
                }
                usedManual = true;
                piece.arg = std::stoi(id);
            }
            if (usedAuto && usedManual) {
                diag_->error(loc, "cannot mix automatic '{}' and numbered '{0}' placeholders");
                return;
            }
            if (piece.arg >= static_cast<int>(print.args.size())) {
                diag_->error(loc, "the format string needs at least " + std::to_string(piece.arg + 1) + " arguments, but " +
                                     std::to_string(print.args.size()) + " were given");
                return;
            }
            if (colon != std::string::npos) {
                piece.hasSpec = true;
                std::string spec = inside.substr(colon + 1);
                if (!parseSpec(spec, piece.spec)) {
                    diag_->error(loc, "invalid format specification '" + spec + "'");
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
                diag_->error(print.args[i]->loc, "this argument is not used by the format string");
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
            diag_->error(loc, message);
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

    static constexpr int kMaxNesting = 16;

    Module& module_;
    StructDecl* mapDecl_ = nullptr;
    const InterfaceDecl* keyInterface_ = nullptr;
    bool insertTarget_ = false;  // the next Index checked is the target of an assignment
    Diagnostics* diag_;
    bool requireMain_;

    std::vector<ModuleScope> modules_;  // by SourceLocation::file
    std::set<std::tuple<int, int, int>> ambiguous_;  // where an ambiguous name was reported

    // Generics (D39): instances by key, structs already completed, the bodies to
    // check, and the generics whose definition has errors.
    std::map<std::string, StructDecl*> structInstances_;
    std::map<std::string, FunctionDecl*> functionInstances_;
    std::set<const StructDecl*> completed_;
    std::set<const StructDecl*> implementsDone_;
    std::vector<PendingBody> definitions_;
    std::vector<PendingBody> instances_;
    std::set<const void*> failed_;
    const TypeBindings* bindings_ = nullptr;  // the type parameters visible now
    bool selfAllowed_ = false;                // while resolving the methods of interfaces
    std::map<const StructDecl*, int> layoutState_;  // 0 to do, 1 in progress, 2 done
    FunctionDecl* current_ = nullptr;
    std::vector<std::map<std::string, Variable*>> scopes_;
    std::vector<const std::vector<std::map<std::string, Variable*>>*> hiddenScopes_;  // outside the lambdas being checked
    bool lambdaArgument_ = false;  // the expression being checked is the argument of a read parameter
    std::vector<bool> loops_;  // one entry per enclosing loop: true if it contains a break

    // The lists that the enclosing `for` loops go over (D101).
    struct LoopBorrow {
        Path path;
        std::string reason;
    };
    std::vector<LoopBorrow> loopBorrows_;  // also the values of `switch` whose data are named (D125)
    std::vector<size_t> switchLoops_;      // for each enclosing `switch`, how many loops enclose it (D116)

    // Exceptions (D36): the `try` bodies around the code being checked, the types
    // of the enclosing `catch`, the loops around each `finally` (D120), and the
    // regions that cannot let an exception out (D113, D122).
    struct TryContext {
        bool catchAll = false;
        std::vector<const StructDecl*> caught;
    };
    struct Region {
        size_t base;  // the `try` bodies inside the region start here
        const char* message;
    };
    struct ThrowInfo {
        bool throws = false;               // a `throw` that is not caught
        std::vector<FunctionDecl*> calls;  // calls that are not caught
    };
    struct Escape {
        SourceLocation loc;
        FunctionDecl* callee;  // null for a `throw`
        const char* message;
    };
    std::vector<TryContext> tryContexts_;
    std::vector<Type> catchTypes_;
    std::vector<size_t> finallyLoops_;
    std::vector<Region> regions_;
    std::map<FunctionDecl*, ThrowInfo> throwInfo_;
    std::vector<Escape> escapes_;
    InterfaceDecl* exceptionDecl_ = nullptr;

    // The types of the standard library (D97).
    StructDecl* stringDecl_ = nullptr;
    StructDecl* listDecl_ = nullptr;

    bool moveAllowed_ = false;  // set by coerceValue for the expression it checks
};

}  // namespace

bool check(Module& module, Diagnostics& diag, bool requireMain) {
    Checker(module, diag, requireMain).run();
    return !diag.hasErrors();
}

}  // namespace cromo
