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
    std::string name;  // "i32", "void", "Student", ...
    SourceLocation loc;
};

enum class VariableKind {
    Local,
    ReadParam,  // read-only, the caller remains the owner (D22)
    RefParam,   // the caller's object, which the function can modify (D22)
    MoveParam,  // owned by the function (D22)
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
};

// ---- Expressions ----

enum class ExprKind {
    IntLiteral, FloatLiteral, BoolLiteral, StringLiteral, Name, Unary, Binary, Call, Conversion, Print,
    Field, MethodCall, Construct, Self, Move, Ref, Copy,
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

enum class BinaryOp { Add, Sub, Mul, Div, Rem, Eq, Ne, Lt, Le, Gt, Ge, And, Or };

struct BinaryExpr : Expr {
    BinaryExpr(SourceLocation l, BinaryOp o, ExprPtr a, ExprPtr b)
        : Expr(ExprKind::Binary, l), op(o), lhs(std::move(a)), rhs(std::move(b)) {}

    BinaryOp op;
    ExprPtr lhs;
    ExprPtr rhs;
    Type operandType;  // set by sema: the common type of the two operands
};

// `name(args)`: a free function, a conversion, a print, or a constructor (sema decides).
struct CallExpr : Expr {
    CallExpr(SourceLocation l, std::string c) : Expr(ExprKind::Call, l), callee(std::move(c)) {}

    std::string callee;
    std::vector<ExprPtr> args;
    FunctionDecl* function = nullptr;  // set by sema
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
    char type = 0;       // 0 or one of "dxXbocefgs"
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

    int stream;    // 1 = stdout, 2 = stderr
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
    std::vector<ExprPtr> args;
    FunctionDecl* function = nullptr;  // set by sema
};

// `Student("Anna")`: a new object of a struct, made by sema from a CallExpr.
// Without a constructor it is the default value (D71).
struct ConstructExpr : Expr {
    ConstructExpr(SourceLocation l, const struct StructDecl* d) : Expr(ExprKind::Construct, l), decl(d) {}

    const struct StructDecl* decl;
    std::vector<ExprPtr> args;
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

// ---- Statements ----

enum class StmtKind { VarDecl, Assign, IncDec, Expr, If, For, Break, Continue, Return };

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

enum class AssignOp { Assign, Add, Sub, Mul, Div, Rem };

struct AssignStmt : Stmt {
    AssignStmt(SourceLocation l, ExprPtr t, AssignOp o) : Stmt(StmtKind::Assign, l), target(std::move(t)), op(o) {}

    ExprPtr target;  // a variable or a field
    AssignOp op;
    ExprPtr value;
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

enum class ForForm { Infinite, Condition, Classic };

struct ForStmt : Stmt {
    ForStmt(SourceLocation l) : Stmt(StmtKind::For, l) {}

    ForForm form = ForForm::Infinite;
    StmtPtr init;        // Classic only, may be null
    ExprPtr condition;   // Condition and Classic, may be null in Classic
    StmtPtr step;        // Classic only, may be null
    Block body;
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

struct FunctionDecl {
    FunctionKind kind = FunctionKind::Free;
    TypeName returnType;
    std::string name;
    SourceLocation loc;  // the name
    std::vector<Param> params;
    bool isConst = false;         // `const` method (D24)
    bool isGenerated = false;     // `copy()` generated by the compiler (D35)
    Block body;

    // Set by sema.
    StructDecl* owner = nullptr;  // for methods, constructors and destructors
    Type resolvedReturn;
    std::vector<Type> paramTypes;
    std::vector<std::unique_ptr<Variable>> variables;  // parameters first
};

struct FieldDecl {
    TypeName type;
    std::string name;
    SourceLocation loc;
    ExprPtr init;  // initial value (D51), a literal for now (D73)
    Type resolved; // set by sema
};

struct StructDecl {
    // Move only. Without this, std::vector would copy it when it grows, because
    // the move of std::map is not noexcept in MSVC.
    StructDecl() = default;
    StructDecl(StructDecl&&) = default;
    StructDecl& operator=(StructDecl&&) = default;

    std::string name;
    SourceLocation loc;
    std::vector<FieldDecl> fields;
    std::vector<FunctionDecl> members;  // methods, constructor and destructor

    // Set by sema.
    FunctionDecl* constructor = nullptr;
    FunctionDecl* destructor = nullptr;
    std::map<std::string, FunctionDecl*> methods;
    std::unique_ptr<FunctionDecl> generatedCopy;  // created when `copy()` is used (D35)
    bool needsDestruction = false;
    int size = 0;
    int align = 1;
};

struct Module {
    std::vector<StructDecl> structs;
    std::vector<FunctionDecl> functions;
};

}  // namespace cromo
