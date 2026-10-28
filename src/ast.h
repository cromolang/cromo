#pragma once

#include "diagnostics.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace cromo {

// A type as written in the source. Its meaning is decided by sema.
struct TypeName {
    std::string name;  // "i32", "void", ...
    SourceLocation loc;
};

// ---- Expressions ----

enum class ExprKind { IntegerLiteral, Unary };

struct Expr {
    Expr(ExprKind k, SourceLocation l) : kind(k), loc(l) {}
    virtual ~Expr() = default;

    ExprKind kind;
    SourceLocation loc;
};

struct IntegerLiteralExpr : Expr {
    IntegerLiteralExpr(SourceLocation l, uint64_t v) : Expr(ExprKind::IntegerLiteral, l), value(v) {}

    uint64_t value;
};

enum class UnaryOp { Negate };

struct UnaryExpr : Expr {
    UnaryExpr(SourceLocation l, UnaryOp o, std::unique_ptr<Expr> e)
        : Expr(ExprKind::Unary, l), op(o), operand(std::move(e)) {}

    UnaryOp op;
    std::unique_ptr<Expr> operand;
};

// ---- Statements ----

enum class StmtKind { Return };

struct Stmt {
    Stmt(StmtKind k, SourceLocation l) : kind(k), loc(l) {}
    virtual ~Stmt() = default;

    StmtKind kind;
    SourceLocation loc;
};

struct ReturnStmt : Stmt {
    ReturnStmt(SourceLocation l, std::unique_ptr<Expr> v) : Stmt(StmtKind::Return, l), value(std::move(v)) {}

    std::unique_ptr<Expr> value;  // null for `ret` without a value
};

struct Block {
    SourceLocation open;   // the '{'
    SourceLocation close;  // the '}'
    std::vector<std::unique_ptr<Stmt>> statements;
};

// ---- Declarations ----

struct FunctionDecl {
    TypeName returnType;
    std::string name;
    SourceLocation loc;  // the name
    Block body;
};

struct Module {
    std::vector<FunctionDecl> functions;
};

}  // namespace cromo
