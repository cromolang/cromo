#pragma once

#include "diagnostics.h"
#include "types.h"

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace cromo {

struct FunctionDecl;

// A type as written in the source. Its meaning is decided by sema.
struct TypeName {
    std::string name;  // "i32", "void", "Student", "Pair", "T", "Self", ...
    SourceLocation loc;
    std::vector<TypeName> args;  // type arguments: `Pair<i32, f64>`
    std::string module;          // written in front of the name: `geometry.Circle` (D126)
    mutable Type resolved;       // set by sema, for the language server (D171)
    // `func(i32, ref T) bool` (D185): the types of the parameters are in `args`.
    bool isFunction = false;
    std::vector<int> modes;          // ParamMode of each parameter
    std::vector<TypeName> result;    // the type returned; none: void
    std::vector<int64_t> dimensions; // `i32[3][4]` (D196): {3, 4}, the type before them is the element
};

// A type parameter of a generic, with its constraints: `T : Comparable + Named` (D52).
struct TypeParam {
    std::string name;
    SourceLocation loc;
    std::vector<TypeName> constraints;
    std::vector<const struct InterfaceDecl*> interfaces;  // set by sema
};

// The types given to the type parameters of an instance of a generic.
using TypeBindings = std::vector<std::pair<std::string, Type>>;

enum class VariableKind {
    Local,
    Captured,   // a capture inside the body of a lambda (D183)
    ReadParam,  // read-only, the caller remains the owner (D22)
    RefParam,   // the caller's object, which the function can modify (D22)
    MoveParam,  // owned by the function (D22)
    Element,    // the element of a `for` over a list: read-only, or `ref` (D25)
    Binding,    // the data of a case in a `switch`: read-only, or `ref` (D107)
    Caught,     // the exception of a `catch`: read-only (D114)
};

// A local variable or a parameter, created by sema.
struct Variable {
    std::string name;
    Type type;
    SourceLocation loc;
    VariableKind kind = VariableKind::Local;
    bool isConst = false;
    bool needsDropFlag = false;  // moved somewhere: destroyed only if still alive (D77)
    int id = 0;                  // unique within the function, used in the IR name
    int captureIndex = -1;       // Captured: its place in the data of the lambda
    bool captureByAddress = false;  // Captured: the data hold its address, not its value
    const Variable* captured = nullptr;  // Captured: the variable outside the lambda
    // A thread or a task of its block (D215, D269): the lambda it runs.
    const struct LambdaExpr* blockThread = nullptr;
    bool group = false;  // a thread.Group (D273)
};

// ---- Expressions ----

enum class ExprKind {
    IntLiteral, FloatLiteral, BoolLiteral, StringLiteral, Name, Unary, Binary, Call, Conversion, Print,
    Field, MethodCall, Construct, Self, Move, Ref, Copy, Index, EnumCase, TypeRef, Lambda, CallValue, ListLiteral,
};

struct Expr {
    Expr(ExprKind k, SourceLocation l) : kind(k), loc(l) {}
    virtual ~Expr() = default;

    ExprKind kind;
    SourceLocation loc;
    Type type;  // set by sema
};

using ExprPtr = std::unique_ptr<Expr>;

struct IntLiteralExpr : Expr {
    IntLiteralExpr(SourceLocation l, uint64_t m, bool c) : Expr(ExprKind::IntLiteral, l), magnitude(m), isChar(c) {}

    uint64_t magnitude;
    bool negative = false;  // set when sema folds a unary minus into the literal
    bool isChar;            // 'a' is a u8 (D46)
};

struct FloatLiteralExpr : Expr {
    FloatLiteralExpr(SourceLocation l, std::string t) : Expr(ExprKind::FloatLiteral, l), text(std::move(t)) {}

    std::string text;       // as written, without '_'
    bool negative = false;
    double value = 0;       // set by sema, already rounded to the type
};

struct BoolLiteralExpr : Expr {
    BoolLiteralExpr(SourceLocation l, bool v) : Expr(ExprKind::BoolLiteral, l), value(v) {}

    bool value;
};

struct StringLiteralExpr : Expr {
    StringLiteralExpr(SourceLocation l, std::string v) : Expr(ExprKind::StringLiteral, l), value(std::move(v)) {}

    std::string value;  // UTF-8, escapes already decoded
};

struct NameExpr : Expr {
    NameExpr(SourceLocation l, std::string n) : Expr(ExprKind::Name, l), name(std::move(n)) {}

    std::string name;
    Variable* variable = nullptr;  // set by sema
};

enum class UnaryOp { Negate, Not };

struct UnaryExpr : Expr {
    UnaryExpr(SourceLocation l, UnaryOp o, ExprPtr e) : Expr(ExprKind::Unary, l), op(o), operand(std::move(e)) {}

    UnaryOp op;
    ExprPtr operand;
};

enum class BinaryOp { Add, Sub, Mul, Div, Rem, Eq, Ne, Lt, Le, Gt, Ge, And, Or, BitAnd, BitOr, BitXor, Shl, Shr };

struct BinaryExpr : Expr {
    BinaryExpr(SourceLocation l, BinaryOp o, ExprPtr a, ExprPtr b)
        : Expr(ExprKind::Binary, l), op(o), lhs(std::move(a)), rhs(std::move(b)) {}

    BinaryOp op;
    ExprPtr lhs;
    ExprPtr rhs;
    Type operandType;  // set by sema: the common type of the two operands
    size_t opBegin = 0;  // the bytes of a comparison operator in the source, for assert (D351)
    size_t opEnd = 0;
};

// Functions built into the compiler: `panic` (D102) and, in the standard library
// only, the `unsafe` dialect (D37, D98).
enum class Intrinsic {
    None, Panic, Alloc, Resize, Free, SizeOf, Offset, Take, Put, Destroy, CopyAt, CopyMemory, CompareMemory, CheckIndex,
    ArgumentCount, ArgumentLength, ArgumentData, DecimalToFloat, HashBytes, SystemCall, Environment, ExitProcess,
    ThreadStart, ThreadWait, ThreadRethrow, ThreadUncaught, ThreadFree, MutexLock, MutexUnlock, SelfAddress, EqualAt,
    TaskStart, GroupStart,  // D269, D273
    Assert,                 // D351: `assert(condition)`, a built-in function as panic
    AtomicLoad, AtomicStore, AtomicAdd, AtomicExchange, AtomicCompareExchange,  // D328
    JsonWrite, JsonRead,  // std.json (D251): for a real type, a call of the function that converts it
    DebugWrite,           // the form of debug (D319): for a real type, a call of the function that writes it
    HashNumber,  // `x.hash()` of a number or a bool (D198): made by the compiler, not named
};

// Pairs of arguments that must not be the same element of a list: when the
// indices are not constants, they are compared at runtime (D35). -1 is the
// object of a method.
using OverlapChecks = std::vector<std::pair<int, int>>;

// `name(args)`: a free function, a conversion, a print, or a constructor (sema decides).
struct CallExpr : Expr {
    CallExpr(SourceLocation l, std::string c) : Expr(ExprKind::Call, l), callee(std::move(c)) {}

    std::string callee;
    std::string module;              // written in front: `geometry.area(c)` (D126); set by sema
    std::vector<TypeName> typeArgs;  // written type arguments: `max<Money>(a, b)` (D86)
    std::vector<ExprPtr> args;
    FunctionDecl* function = nullptr;  // set by sema
    Intrinsic intrinsic = Intrinsic::None;
    Type typeArg;                      // of the intrinsics with a type argument: `sizeOf<T>()`
    OverlapChecks overlaps;
    // `assert(total == 7)` (D351): the bytes between the parentheses, set by the
    // parser; the condition as written and the sides of a comparison whose values
    // are shown, set by sema.
    size_t textBegin = 0;
    size_t textEnd = 0;
    std::string assertText;
    std::string leftText;
    std::string rightText;
};

// Conversion to `type`: written as `f64(x)`, or inserted by sema when it is lossless (D33).
struct ConversionExpr : Expr {
    ConversionExpr(SourceLocation l, ExprPtr e, Type target, bool expl)
        : Expr(ExprKind::Conversion, l), operand(std::move(e)), isExplicit(expl) {
        type = target;
    }

    ExprPtr operand;
    bool isExplicit;
};

// Format specification of a placeholder, as in std::format (D21, D38).
struct FormatSpec {
    char fill = ' ';
    char align = 0;      // 0, '<', '>', '^'
    char sign = 0;       // 0, '+', '-', ' '
    bool alternate = false;
    bool zeroPad = false;
    char type = 0;       // 0 or one of "dxXbocefgs", or '?' until sema makes it a string (D319)
    int width = -1;
    int precision = -1;
};

struct FormatPiece {
    std::string text;    // literal text, when arg < 0
    int arg = -1;        // index into PrintExpr::args
    FormatSpec spec;
    bool hasSpec = false;
};

// print / println / eprint / eprintln, with the format string already parsed by sema.
struct PrintExpr : Expr {
    PrintExpr(SourceLocation l, int s, bool nl) : Expr(ExprKind::Print, l), stream(s), newline(nl) {}

    int stream;    // 1 = stdout, 2 = stderr, 3 = a string for format (D200)
    bool newline;
    std::vector<FormatPiece> pieces;
    std::vector<ExprPtr> args;
};

// `object.field`; inside a method a bare field name becomes `self.field` (D70).
struct FieldExpr : Expr {
    FieldExpr(SourceLocation l, ExprPtr o, std::string f) : Expr(ExprKind::Field, l), object(std::move(o)), field(std::move(f)) {}

    ExprPtr object;
    std::string field;
    int index = -1;  // set by sema
};

// `object.method(args)`; inside a method a bare call to a method becomes `self.method(args)`.
struct MethodCallExpr : Expr {
    MethodCallExpr(SourceLocation l, ExprPtr o, std::string m)
        : Expr(ExprKind::MethodCall, l), object(std::move(o)), method(std::move(m)) {}

    ExprPtr object;
    std::string method;
    std::vector<TypeName> typeArgs;  // `v.map<string>(f)`: of a method with type parameters (D92)
    std::vector<ExprPtr> args;
    FunctionDecl* function = nullptr;  // set by sema
    OverlapChecks overlaps;
};

// `Student("Anna")`: a new object of a struct, made by sema from a CallExpr.
// Without a constructor it is the default value (D71).
struct ConstructExpr : Expr {
    ConstructExpr(SourceLocation l, const struct StructDecl* d) : Expr(ExprKind::Construct, l), decl(d) {}

    const struct StructDecl* decl;
    std::vector<ExprPtr> args;
    OverlapChecks overlaps;
    const struct LambdaExpr* blockThread = nullptr;  // `thread.Thread(lambda)` capturing by address (D215)
};

// The object a method works on. It cannot be written in the source (D70).
struct SelfExpr : Expr {
    explicit SelfExpr(SourceLocation l) : Expr(ExprKind::Self, l) {}
};

enum class MoveKind {
    Copy,      // a simple type: the source does not change (D80)
    Variable,  // a whole local variable or `move` parameter: it can no longer be used (D23)
    Take,      // a field or a `ref` parameter: it keeps the default value (D78)
};

// `move x`: gives the value of a variable or of a field to a new owner.
struct MoveExpr : Expr {
    MoveExpr(SourceLocation l, ExprPtr o) : Expr(ExprKind::Move, l), operand(std::move(o)) {}

    ExprPtr operand;
    MoveKind moveKind = MoveKind::Copy;  // set by sema
};

// `ref x`: the argument of a `ref` parameter (D22).
struct RefExpr : Expr {
    RefExpr(SourceLocation l, ExprPtr o) : Expr(ExprKind::Ref, l), operand(std::move(o)) {}

    ExprPtr operand;
};

// A read argument of a simple type passed as a copy, because the same call
// modifies the original (D79). Inserted by sema.
struct CopyExpr : Expr {
    CopyExpr(SourceLocation l, ExprPtr o) : Expr(ExprKind::Copy, l), operand(std::move(o)) { type = operand->type; }

    ExprPtr operand;
};

// `v[i]`: an element of a list, with bounds checking (D30, D35).
struct IndexExpr : Expr {
    IndexExpr(SourceLocation l, ExprPtr o, ExprPtr i) : Expr(ExprKind::Index, l), object(std::move(o)), index(std::move(i)) {}

    ExprPtr object;
    ExprPtr index;
    // `m[k]` of a map (D197): the method of the library that gives the address of
    // the value, valueOf or place. Set by sema.
    const struct FunctionDecl* access = nullptr;
};

// `Shape.Circle(1.0)`, `Color.Red`: a value of an enum (D105), made by sema.
struct EnumCaseExpr : Expr {
    EnumCaseExpr(SourceLocation l, const struct StructDecl* d, int c) : Expr(ExprKind::EnumCase, l), decl(d), index(c) {}

    const struct StructDecl* decl;
    int index;  // of the case
    std::vector<ExprPtr> args;
    OverlapChecks overlaps;
};

// `Result<i32, string>` before a `.`: the type of a generic enum, to create its values.
struct TypeRefExpr : Expr {
    TypeRefExpr(SourceLocation l, TypeName n) : Expr(ExprKind::TypeRef, l), name(std::move(n)) {}

    TypeName name;
};

// ---- Statements ----

enum class StmtKind { VarDecl, Assign, IncDec, Expr, If, For, Break, Continue, Return, Switch, Throw, Try };

struct Stmt {
    Stmt(StmtKind k, SourceLocation l) : kind(k), loc(l) {}
    virtual ~Stmt() = default;

    StmtKind kind;
    SourceLocation loc;
};

using StmtPtr = std::unique_ptr<Stmt>;

struct Block {
    SourceLocation open;   // the '{'
    SourceLocation close;  // the '}'
    std::vector<StmtPtr> statements;
};

struct VarDeclStmt : Stmt {
    VarDeclStmt(SourceLocation l) : Stmt(StmtKind::VarDecl, l) {}

    bool isConst = false;
    TypeName typeName;
    std::string name;
    SourceLocation nameLoc;
    ExprPtr init;                  // null: default value (D31)
    Variable* variable = nullptr;  // set by sema
};

enum class AssignOp { Assign, Add, Sub, Mul, Div, Rem, BitAnd, BitOr, BitXor, Shl, Shr };

struct AssignStmt : Stmt {
    AssignStmt(SourceLocation l, ExprPtr t, AssignOp o) : Stmt(StmtKind::Assign, l), target(std::move(t)), op(o) {}

    ExprPtr target;  // a variable or a field
    AssignOp op;
    ExprPtr value;
    ExprPtr call;    // set by sema: `s += t` on strings is `s.append(t)` (D95)
};

struct IncDecStmt : Stmt {
    IncDecStmt(SourceLocation l, ExprPtr t, bool inc) : Stmt(StmtKind::IncDec, l), target(std::move(t)), increment(inc) {}

    ExprPtr target;  // a variable or a field
    bool increment;
};

struct ExprStmt : Stmt {
    ExprStmt(SourceLocation l, ExprPtr e) : Stmt(StmtKind::Expr, l), expr(std::move(e)) {}

    ExprPtr expr;
};

struct IfStmt : Stmt {
    IfStmt(SourceLocation l) : Stmt(StmtKind::If, l) {}

    ExprPtr condition;
    Block thenBlock;
    std::unique_ptr<Block> elseBlock;  // `else if` is an else block holding a single IfStmt
};

enum class ForForm { Infinite, Condition, Classic, Each };

struct ForStmt : Stmt {
    ForStmt(SourceLocation l) : Stmt(StmtKind::For, l) {}

    ForForm form = ForForm::Infinite;
    StmtPtr init;        // Classic only, may be null
    ExprPtr condition;   // Condition and Classic, may be null in Classic
    StmtPtr step;        // Classic only, may be null
    Block body;

    // Each: `for ([ref] T x in v)` (D25).
    bool isRef = false;
    TypeName elementType;
    std::string elementName;
    SourceLocation elementLoc;
    ExprPtr list;
    Variable* element = nullptr;  // set by sema

    // A map: `for (K k, [ref] V v in m)` (D197); the key is the element.
    bool hasValue = false;
    bool valueIsRef = false;
    TypeName valueType;
    std::string valueName;
    SourceLocation valueLoc;
    Variable* value = nullptr;  // set by sema
};

// The data of a case, named in a `switch` (D107).
struct CaseBinding {
    bool isRef = false;
    TypeName type;
    std::string name;
    SourceLocation loc;
    Variable* variable = nullptr;  // set by sema
};

// A pattern of a branch: a case of an enum, with or without names for its data,
// or a literal (D109).
struct CasePattern {
    SourceLocation loc;
    std::string name;             // a case of an enum
    bool hasBindings = false;     // `Circle(f64 r)`
    std::vector<CaseBinding> bindings;
    ExprPtr literal;              // a number or a string
    int caseIndex = -1;           // set by sema
};

struct SwitchBranch {
    std::vector<CasePattern> patterns;
    Block body;
};

struct SwitchStmt : Stmt {
    SwitchStmt(SourceLocation l) : Stmt(StmtKind::Switch, l) {}

    ExprPtr value;  // without one, in the methods of an enum: the value of the method (D117)
    std::vector<SwitchBranch> branches;
    std::unique_ptr<Block> defaultBlock;
    bool exhaustive = false;  // set by sema: some branch runs for every value
};

struct ThrowStmt : Stmt {
    ThrowStmt(SourceLocation l, ExprPtr v) : Stmt(StmtKind::Throw, l), value(std::move(v)) {}

    ExprPtr value;  // null: rethrows the exception of the `catch` (D114)
};

struct CatchClause {
    TypeName type;
    std::string name;
    SourceLocation nameLoc;
    Block body;
    Type resolved;                 // set by sema: a struct, or the interface Exception
    Variable* variable = nullptr;  // set by sema
};

struct TryStmt : Stmt {
    TryStmt(SourceLocation l) : Stmt(StmtKind::Try, l) {}

    Block body;
    std::vector<CatchClause> catches;
    std::unique_ptr<Block> finallyBlock;
};

struct BreakStmt : Stmt {
    explicit BreakStmt(SourceLocation l) : Stmt(StmtKind::Break, l) {}
};

struct ContinueStmt : Stmt {
    explicit ContinueStmt(SourceLocation l) : Stmt(StmtKind::Continue, l) {}
};

struct ReturnStmt : Stmt {
    ReturnStmt(SourceLocation l, ExprPtr v) : Stmt(StmtKind::Return, l), value(std::move(v)) {}

    ExprPtr value;  // null for `ret` without a value
};

// ---- Declarations ----

enum class ParamMode { Read, Ref, Move };

struct Param {
    ParamMode mode = ParamMode::Read;
    bool isConst = false;  // only with move: `move const T x` (D22)
    TypeName type;
    std::string name;
    SourceLocation loc;
};

enum class FunctionKind { Free, Method, Constructor, Destructor };

struct StructDecl;

// An argument of an attribute: a text, as "user_id", or a name, as skip (D256).
struct AttributeArg {
    bool isText = false;
    std::string text;
    SourceLocation loc;
};

// An attribute written before a field, with the syntax of C++11 (D256):
// `[[json("user_id")]]`, or before a function, `[[test]]` (D349).
struct Attribute {
    std::string name;
    SourceLocation loc;
    std::vector<AttributeArg> args;
};

struct FunctionDecl {
    FunctionKind kind = FunctionKind::Free;
    TypeName returnType;
    std::string name;
    SourceLocation loc;  // the name
    bool isPublic = false;        // visible to the modules that import this one (D50)
    bool isExtern = false;        // a C function, declared in an `extern` block (D128)
    std::string library;          // of an extern function: "kernel32"
    std::vector<Param> params;
    bool isConst = false;         // `const` method (D24)
    bool isGenerated = false;     // `copy()` generated by the compiler (D35)
    bool convertsJson = false;    // generated for JSON (D251) or for {:?} (D319): it reads and writes every field
    bool isLambda = false;        // the body of a lambda, or the signature of a function type (D183)
    std::vector<TypeParam> typeParams;  // a generic function (D39)
    Block body;
    std::vector<Attribute> attributes;  // `[[test]]` (D349)
    bool isTest = false;                // set by sema

    // Set by sema.
    std::string symbol;           // its name in the IR, with its module in front
    StructDecl* owner = nullptr;  // for methods, constructors and destructors
    Type resolvedReturn;
    std::vector<Type> paramTypes;
    std::vector<std::unique_ptr<Variable>> variables;  // parameters first

    // Instances of generics (D39), created by sema.
    const FunctionDecl* generic = nullptr;  // the generic function this is an instance of
    bool genericMethod = false;  // an instance of a method with type parameters of its own (D92)
    TypeBindings typeBindings;              // also for the members of an instance of a generic struct
    bool isAbstract = false;                // has type parameters as types: checked, never emitted
    bool isDefinition = false;              // the check of a generic with its own type parameters (D52)
    bool scheduled = false;                 // its body is waiting to be checked, or has been (D103)
    bool bodyChecked = false;
    bool canThrow = false;                  // set by sema: an exception can leave it (D36)
};

// `interface Shape { f64 area() const }`: method signatures only (D40, D49).
struct InterfaceDecl {
    std::string name;
    SourceLocation loc;
    bool isPublic = false;
    std::vector<FunctionDecl> methods;  // public with the interface
    bool usesSelf = false;  // set by sema: then it can only be a constraint (D84)
    std::string symbol;     // set by sema
};

struct FieldDecl {
    TypeName type;
    std::string name;
    SourceLocation loc;
    bool isPublic = false;
    ExprPtr init;  // initial value (D51), a literal for now (D73)
    std::vector<Attribute> attributes;
    Type resolved; // set by sema
    // Set by sema from `[[json(...)]]` (D256): its name in JSON, empty when left out.
    std::string jsonName;
};

// A case of an enum, with its data (D104).
struct EnumCase {
    std::string name;
    SourceLocation loc;
    std::vector<FieldDecl> fields;
    ExprPtr valueExpr;  // `Red = 1`, in an enum without data
    int size = 0;   // set by sema: the layout of the data, as a struct
    int align = 1;
    int64_t value = 0;  // set by sema: the tag, its position unless written
};

struct StructDecl {
    // Move only. Without this, std::vector would copy it when it grows, because
    // the move of std::map is not noexcept in MSVC.
    StructDecl() = default;
    StructDecl(StructDecl&&) = default;
    StructDecl& operator=(StructDecl&&) = default;

    std::string name;
    SourceLocation loc;
    bool isPublic = false;
    std::vector<TypeParam> typeParams;   // a generic struct (D39)
    std::vector<TypeName> interfaces;    // `struct Circle : Shape` (D40)
    std::vector<FieldDecl> fields;
    std::vector<FunctionDecl> members;  // methods, constructor and destructor
    bool isEnum = false;                // an enum: its data are in the cases (D104)
    std::vector<EnumCase> cases;

    // Set by sema.
    std::string symbol;  // its name in the IR, with its module in front
    FunctionDecl* constructor = nullptr;
    FunctionDecl* destructor = nullptr;
    std::map<std::string, FunctionDecl*> methods;
    std::unique_ptr<FunctionDecl> generatedCopy;  // created when `copy()` is used (D35)
    // D360: the initial values of the fields that are not literals, `value.field = init`,
    // computed each time the default value is made; owned by Module::functionInstances.
    FunctionDecl* defaultFunction = nullptr;
    std::vector<const InterfaceDecl*> implements;
    bool needsDestruction = false;
    bool addressed = false;  // a Mutex, a Channel, an Atomic, or holds one: other threads write it, it never moves in a call (D216)
    int size = 0;
    int align = 1;
    int payloadOffset = 0;  // of an enum: where the data of the cases start, after the tag

    // Instances of generics (D39), created by sema.
    const StructDecl* generic = nullptr;  // the generic struct this is an instance of
    std::vector<Type> typeArgs;
    TypeBindings typeBindings;
    bool isAbstract = false;              // has type parameters as types: checked, never emitted
};

// `import geometry.shapes` (D41, D50), or `import unsafe` (D127).
struct ImportDecl {
    std::vector<std::string> path;
    SourceLocation loc;
    std::vector<int> files;  // set by the driver: the files of the module, by SourceLocation::file

    // The name written in front of its names when they clash: `shapes.area` (D126).
    const std::string& qualifier() const { return path.back(); }
};

// A source file: a module (D50).
struct ModuleFile {
    std::string name;  // "geometry.shapes": the path of the module, in front of its symbols
    std::vector<ImportDecl> imports;
    bool isUnsafe = false;  // `import unsafe` (D127)
    bool isLibrary = false;  // a file of the standard library, embedded in cromo (D201)
    // `fs_windows.cro` next to `fs.cro` (D202): the file of the same module whose
    // names it shares; -1 for a file that is a module by itself.
    int part = -1;
};

// A capture of a lambda: `limit`, `ref total`, `move name` (D183).
struct Capture {
    ParamMode mode = ParamMode::Read;
    std::string name;
    SourceLocation loc;
    // Set by sema.
    Variable* outer = nullptr;   // the variable captured
    Variable* inner = nullptr;   // its name inside the body
    bool byAddress = false;      // `ref`, or the read of a value that is not simple
};

// `[limit](i32 x) bool { ret x > limit }` (D183).
struct LambdaExpr : Expr {
    explicit LambdaExpr(SourceLocation l) : Expr(ExprKind::Lambda, l) {}
    ~LambdaExpr() override;

    std::vector<Capture> captures;
    std::unique_ptr<FunctionDecl> function;  // its parameters, the type returned and the body
    // Set by sema.
    bool borrows = false;   // it captures by address: only as an argument (D184)
    bool onStack = false;   // passed as an argument: its data live in the frame of the caller
    bool forThread = false; // given to a thread (D215): its data stay in the frame, owned by the thread
    const Variable* group = nullptr;  // given to start() of a group (D273): its data on the heap
    const FunctionDecl* enclosing = nullptr;
    std::vector<std::unique_ptr<NameExpr>> capturePlaces;  // the captures by address, for exclusivity (D22)
};

// `[a, b, c]` (D308): a new list, whose elements are given to it as to add().
struct ListLiteralExpr : Expr {
    explicit ListLiteralExpr(SourceLocation l) : Expr(ExprKind::ListLiteral, l) {}

    std::vector<ExprPtr> elements;
    // Set by sema: the methods of the list that make it, and the count for reserve().
    const FunctionDecl* reserve = nullptr;
    const FunctionDecl* add = nullptr;
    std::vector<ExprPtr> count;
};

// The call of a function value: `less(a, b)`, `handler.run(x)` on a field (D186).
// Made by sema from a call.
struct CallValueExpr : Expr {
    CallValueExpr(SourceLocation l, ExprPtr c) : Expr(ExprKind::CallValue, l), callee(std::move(c)) {}

    ExprPtr callee;
    std::vector<ExprPtr> args;
    const FunctionDecl* signature = nullptr;  // the parameters of the function type
    OverlapChecks overlaps;
};

struct Module {
    std::vector<InterfaceDecl> interfaces;
    std::vector<StructDecl> structs;
    std::vector<FunctionDecl> functions;
    std::vector<ModuleFile> files;  // by SourceLocation::file

    // Instances of generic structs and functions, created by sema (D39).
    std::vector<std::unique_ptr<StructDecl>> structInstances;
    std::vector<std::unique_ptr<FunctionDecl>> functionInstances;

    // Set by sema: every lambda, and a declaration for each function type called (D183, D186).
    std::vector<LambdaExpr*> lambdas;
    std::vector<std::unique_ptr<FunctionDecl>> signatures;

    // Adds the declarations of another file, as parsed (D50, D97).
    void append(Module&& other) {
        for (InterfaceDecl& decl : other.interfaces) interfaces.push_back(std::move(decl));
        for (StructDecl& decl : other.structs) structs.push_back(std::move(decl));
        for (FunctionDecl& function : other.functions) functions.push_back(std::move(function));
        if (files.size() < other.files.size()) files.resize(other.files.size());
        for (size_t i = 0; i < other.files.size(); ++i) {
            if (!other.files[i].name.empty() || !other.files[i].imports.empty() || other.files[i].isUnsafe) {
                files[i] = std::move(other.files[i]);
            }
        }
    }
};

// Deep copies of declarations that sema has not checked yet: each instance of a
// generic is checked on its own copy.
ExprPtr clone(const Expr& expr);
StmtPtr clone(const Stmt& stmt);
Block clone(const Block& block);
FunctionDecl clone(const FunctionDecl& function);
FieldDecl clone(const FieldDecl& field);

}  // namespace cromo
