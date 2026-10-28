#pragma once

#include "diagnostics.h"
#include "types.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace cromo {

struct FunctionDecl;

// A type as written in the source. Its meaning is decided by sema.
struct TypeName {
    std::string name;  // "i32", "void", ...
    SourceLocation loc;
};

// A local variable or a parameter, created by sema.
struct Variable {
    std::string name;
    Type type;
    SourceLocation loc;
    bool isConst = false;
    bool isParam = false;  // parameters are read-only (D22)
    int id = 0;            // unique within the function, used in the IR name
};

// ---- Expressions ----

enum class ExprKind { IntLiteral, FloatLiteral, BoolLiteral, StringLiteral, Name, Unary, Binary, Call, Conversion, Print };

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
    AssignStmt(SourceLocation l) : Stmt(StmtKind::Assign, l) {}

    std::string target;
    SourceLocation targetLoc;
    AssignOp op = AssignOp::Assign;
    ExprPtr value;
    Variable* variable = nullptr;  // set by sema
};

struct IncDecStmt : Stmt {
    IncDecStmt(SourceLocation l, std::string t, bool inc) : Stmt(StmtKind::IncDec, l), target(std::move(t)), increment(inc) {}

    std::string target;
    bool increment;
    Variable* variable = nullptr;  // set by sema
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

struct Param {
    TypeName type;
    std::string name;
    SourceLocation loc;
};

struct FunctionDecl {
    TypeName returnType;
    std::string name;
    SourceLocation loc;  // the name
    std::vector<Param> params;
    Block body;

    // Set by sema.
    Type resolvedReturn;
    std::vector<Type> paramTypes;
    std::vector<std::unique_ptr<Variable>> variables;  // parameters first
};

struct Module {
    std::vector<FunctionDecl> functions;
};

}  // namespace cromo
