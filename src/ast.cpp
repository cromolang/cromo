#include "ast.h"

namespace cromo {

namespace {

ExprPtr cloneOrNull(const ExprPtr& expr) { return expr ? clone(*expr) : nullptr; }

std::vector<ExprPtr> cloneAll(const std::vector<ExprPtr>& exprs) {
    std::vector<ExprPtr> result;
    for (const ExprPtr& expr : exprs) result.push_back(clone(*expr));
    return result;
}

ExprPtr cloneExpr(const Expr& expr) {
    switch (expr.kind) {
    case ExprKind::IntLiteral: {
        const auto& from = static_cast<const IntLiteralExpr&>(expr);
        auto to = std::make_unique<IntLiteralExpr>(from.loc, from.magnitude, from.isChar);
        to->negative = from.negative;
        return to;
    }
    case ExprKind::FloatLiteral: {
        const auto& from = static_cast<const FloatLiteralExpr&>(expr);
        auto to = std::make_unique<FloatLiteralExpr>(from.loc, from.text);
        to->negative = from.negative;
        to->value = from.value;
        return to;
    }
    case ExprKind::BoolLiteral:
        return std::make_unique<BoolLiteralExpr>(expr.loc, static_cast<const BoolLiteralExpr&>(expr).value);
    case ExprKind::StringLiteral:
        return std::make_unique<StringLiteralExpr>(expr.loc, static_cast<const StringLiteralExpr&>(expr).value);
    case ExprKind::Name:
        return std::make_unique<NameExpr>(expr.loc, static_cast<const NameExpr&>(expr).name);
    case ExprKind::Unary: {
        const auto& from = static_cast<const UnaryExpr&>(expr);
        return std::make_unique<UnaryExpr>(from.loc, from.op, clone(*from.operand));
    }
    case ExprKind::Binary: {
        const auto& from = static_cast<const BinaryExpr&>(expr);
        return std::make_unique<BinaryExpr>(from.loc, from.op, clone(*from.lhs), clone(*from.rhs));
    }
    case ExprKind::Call: {
        const auto& from = static_cast<const CallExpr&>(expr);
        auto to = std::make_unique<CallExpr>(from.loc, from.callee);
        to->typeArgs = from.typeArgs;
        to->args = cloneAll(from.args);
        return to;
    }
    case ExprKind::Conversion: {
        const auto& from = static_cast<const ConversionExpr&>(expr);
        return std::make_unique<ConversionExpr>(from.loc, clone(*from.operand), from.type, from.isExplicit);
    }
    case ExprKind::Print: {
        const auto& from = static_cast<const PrintExpr&>(expr);
        auto to = std::make_unique<PrintExpr>(from.loc, from.stream, from.newline);
        to->pieces = from.pieces;
        to->args = cloneAll(from.args);
        return to;
    }
    case ExprKind::Field: {
        const auto& from = static_cast<const FieldExpr&>(expr);
        return std::make_unique<FieldExpr>(from.loc, clone(*from.object), from.field);
    }
    case ExprKind::MethodCall: {
        const auto& from = static_cast<const MethodCallExpr&>(expr);
        auto to = std::make_unique<MethodCallExpr>(from.loc, clone(*from.object), from.method);
        to->args = cloneAll(from.args);
        return to;
    }
    case ExprKind::Construct: {
        const auto& from = static_cast<const ConstructExpr&>(expr);
        auto to = std::make_unique<ConstructExpr>(from.loc, from.decl);
        to->args = cloneAll(from.args);
        return to;
    }
    case ExprKind::Self: return std::make_unique<SelfExpr>(expr.loc);
    case ExprKind::Move:
        return std::make_unique<MoveExpr>(expr.loc, clone(*static_cast<const MoveExpr&>(expr).operand));
    case ExprKind::Ref:
        return std::make_unique<RefExpr>(expr.loc, clone(*static_cast<const RefExpr&>(expr).operand));
    case ExprKind::Copy:
        return std::make_unique<CopyExpr>(expr.loc, clone(*static_cast<const CopyExpr&>(expr).operand));
    case ExprKind::Index: {
        const auto& from = static_cast<const IndexExpr&>(expr);
        return std::make_unique<IndexExpr>(from.loc, clone(*from.object), clone(*from.index));
    }
    }
    return nullptr;
}

}  // namespace

ExprPtr clone(const Expr& expr) {
    ExprPtr result = cloneExpr(expr);
    result->type = expr.type;
    return result;
}

StmtPtr clone(const Stmt& stmt) {
    switch (stmt.kind) {
    case StmtKind::VarDecl: {
        const auto& from = static_cast<const VarDeclStmt&>(stmt);
        auto to = std::make_unique<VarDeclStmt>(from.loc);
        to->isConst = from.isConst;
        to->typeName = from.typeName;
        to->name = from.name;
        to->nameLoc = from.nameLoc;
        to->init = cloneOrNull(from.init);
        return to;
    }
    case StmtKind::Assign: {
        const auto& from = static_cast<const AssignStmt&>(stmt);
        auto to = std::make_unique<AssignStmt>(from.loc, clone(*from.target), from.op);
        to->value = clone(*from.value);
        return to;
    }
    case StmtKind::IncDec: {
        const auto& from = static_cast<const IncDecStmt&>(stmt);
        return std::make_unique<IncDecStmt>(from.loc, clone(*from.target), from.increment);
    }
    case StmtKind::Expr:
        return std::make_unique<ExprStmt>(stmt.loc, clone(*static_cast<const ExprStmt&>(stmt).expr));
    case StmtKind::If: {
        const auto& from = static_cast<const IfStmt&>(stmt);
        auto to = std::make_unique<IfStmt>(from.loc);
        to->condition = clone(*from.condition);
        to->thenBlock = clone(from.thenBlock);
        if (from.elseBlock) to->elseBlock = std::make_unique<Block>(clone(*from.elseBlock));
        return to;
    }
    case StmtKind::For: {
        const auto& from = static_cast<const ForStmt&>(stmt);
        auto to = std::make_unique<ForStmt>(from.loc);
        to->form = from.form;
        if (from.init) to->init = clone(*from.init);
        to->condition = cloneOrNull(from.condition);
        if (from.step) to->step = clone(*from.step);
        to->body = clone(from.body);
        to->isRef = from.isRef;
        to->elementType = from.elementType;
        to->elementName = from.elementName;
        to->elementLoc = from.elementLoc;
        to->list = cloneOrNull(from.list);
        return to;
    }
    case StmtKind::Break: return std::make_unique<BreakStmt>(stmt.loc);
    case StmtKind::Continue: return std::make_unique<ContinueStmt>(stmt.loc);
    case StmtKind::Return: {
        const auto& from = static_cast<const ReturnStmt&>(stmt);
        return std::make_unique<ReturnStmt>(from.loc, cloneOrNull(from.value));
    }
    }
    return nullptr;
}

Block clone(const Block& block) {
    Block result;
    result.open = block.open;
    result.close = block.close;
    for (const StmtPtr& statement : block.statements) result.statements.push_back(clone(*statement));
    return result;
}

FunctionDecl clone(const FunctionDecl& function) {
    FunctionDecl result;
    result.kind = function.kind;
    result.returnType = function.returnType;
    result.name = function.name;
    result.loc = function.loc;
    result.params = function.params;
    result.isConst = function.isConst;
    result.isGenerated = function.isGenerated;
    result.typeParams = function.typeParams;
    result.body = clone(function.body);
    return result;
}

FieldDecl clone(const FieldDecl& field) {
    FieldDecl result;
    result.type = field.type;
    result.name = field.name;
    result.loc = field.loc;
    result.init = cloneOrNull(field.init);
    return result;
}

}  // namespace cromo
