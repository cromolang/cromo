#include "ownership.h"

#include <cstdint>
#include <vector>

namespace cromo {

namespace {

constexpr uint8_t kLive = 1;
constexpr uint8_t kMoved = 2;

// What is known about the variables at a point of a function: for each one,
// whether it may be alive and whether it may have been moved.
struct State {
    bool reachable = true;
    std::vector<uint8_t> variables;  // by Variable::id

    bool operator==(const State&) const = default;
};

// The state where two paths meet.
State merge(const State& a, const State& b) {
    if (!a.reachable) return b;
    if (!b.reachable) return a;
    State result = a;
    for (size_t i = 0; i < result.variables.size(); ++i) result.variables[i] |= b.variables[i];
    return result;
}

class MoveChecker {
public:
    explicit MoveChecker(Diagnostics& diag) : diag_(diag) {}

    void checkFunction(const FunctionDecl& function) {
        state_ = State{};
        state_.variables.assign(function.variables.size(), 0);
        for (const auto& variable : function.variables) {
            if (variable->kind != VariableKind::Local) state_.variables[variable->id] = kLive;
        }
        loops_.clear();
        raises_.clear();
        checkBlock(function.body);
    }

private:
    struct Loop {
        std::vector<State> breaks;
        std::vector<State> continues;
    };

    State unreachable() const {
        State state;
        state.reachable = false;
        state.variables.assign(state_.variables.size(), 0);
        return state;
    }

    // A read of a variable: it must not have been moved on any path.
    void use(const Variable* variable, SourceLocation loc) {
        if (!variable || !state_.reachable) return;
        uint8_t& bits = state_.variables[variable->id];
        if (!(bits & kMoved)) return;
        if (report_) {
            if (bits == kMoved) {
                diag_.error(loc, "'" + variable->name + "' has been moved and can no longer be used (D23)");
            } else {
                diag_.error(loc, "'" + variable->name +
                                     "' may have been moved, on another path or in a previous iteration of a loop (D81)");
            }
        }
        bits = kLive;  // one error per move
    }

    void set(const Variable* variable, uint8_t bits) {
        if (variable && state_.reachable) state_.variables[variable->id] = bits;
    }

    // ---- Statements ----

    void checkBlock(const Block& block) {
        for (const StmtPtr& statement : block.statements) checkStatement(*statement);
    }

    void checkStatement(const Stmt& statement) {
        switch (statement.kind) {
        case StmtKind::VarDecl: {
            const auto& decl = static_cast<const VarDeclStmt&>(statement);
            if (decl.init) visit(*decl.init);
            set(decl.variable, kLive);
            break;
        }
        case StmtKind::Assign: {
            // The value is computed before the target is written.
            const auto& assign = static_cast<const AssignStmt&>(statement);
            if (assign.call) {  // `s += t` (D95)
                visit(*assign.call);
                break;
            }
            visit(*assign.value);
            if (assign.target->kind == ExprKind::Name && assign.op == AssignOp::Assign) {
                set(static_cast<const NameExpr&>(*assign.target).variable, kLive);  // usable again (D81)
            } else {
                visit(*assign.target);
            }
            break;
        }
        case StmtKind::IncDec: visit(*static_cast<const IncDecStmt&>(statement).target); break;
        case StmtKind::Expr: {
            const Expr& expr = *static_cast<const ExprStmt&>(statement).expr;
            visit(expr);
            // D102: nothing runs after a panic.
            if (expr.kind == ExprKind::Call && static_cast<const CallExpr&>(expr).intrinsic == Intrinsic::Panic) {
                state_ = unreachable();
            }
            break;
        }
        case StmtKind::If: {
            const auto& ifStmt = static_cast<const IfStmt&>(statement);
            visit(*ifStmt.condition);
            State before = state_;
            checkBlock(ifStmt.thenBlock);
            State afterThen = state_;
            state_ = before;
            if (ifStmt.elseBlock) checkBlock(*ifStmt.elseBlock);
            state_ = merge(afterThen, state_);
            break;
        }
        case StmtKind::For: checkFor(static_cast<const ForStmt&>(statement)); break;
        case StmtKind::Switch: {
            // Every branch starts from the same state; without a branch for every
            // value, the switch can also do nothing.
            const auto& switchStmt = static_cast<const SwitchStmt&>(statement);
            visit(*switchStmt.value);
            State before = state_;
            State after = switchStmt.exhaustive ? unreachable() : before;
            auto branch = [&](const Block& body) {
                state_ = before;
                checkBlock(body);
                after = merge(after, state_);
            };
            for (const SwitchBranch& item : switchStmt.branches) branch(item.body);
            if (switchStmt.defaultBlock) branch(*switchStmt.defaultBlock);
            state_ = after;
            break;
        }
        case StmtKind::Throw: {
            const auto& throwStmt = static_cast<const ThrowStmt&>(statement);
            if (throwStmt.value) visit(*throwStmt.value);
            noteRaise();
            state_ = unreachable();
            break;
        }
        case StmtKind::Try: checkTry(static_cast<const TryStmt&>(statement)); break;
        case StmtKind::Break:
            loops_.back().breaks.push_back(state_);
            state_ = unreachable();
            break;
        case StmtKind::Continue:
            loops_.back().continues.push_back(state_);
            state_ = unreachable();
            break;
        case StmtKind::Return: {
            const auto& ret = static_cast<const ReturnStmt&>(statement);
            if (ret.value) visit(*ret.value);
            state_ = unreachable();
            break;
        }
        }
    }

    // A `catch` starts from any point of the body where an exception can arise:
    // the states at those points are merged. `finally` runs on every exit.
    void checkTry(const TryStmt& statement) {
        raises_.push_back(state_);
        checkBlock(statement.body);
        State atCatch = raises_.back();
        raises_.pop_back();
        State after = state_;
        for (const CatchClause& clause : statement.catches) {
            state_ = atCatch;
            checkBlock(clause.body);
            after = merge(after, state_);
        }
        if (!statement.finallyBlock) {
            state_ = after;
            return;
        }
        state_ = merge(after, atCatch);
        checkBlock(*statement.finallyBlock);
        if (!after.reachable) state_ = unreachable();
    }

    // A point where an exception can arise: a call or a `throw`.
    void noteRaise() {
        for (State& state : raises_) state = merge(state, state_);
    }

    // A loop is visited until the state at its start no longer changes, without
    // reporting errors; then once more, reporting them.
    void checkFor(const ForStmt& statement) {
        if (statement.init) checkStatement(*statement.init);
        if (statement.list) visit(*statement.list);
        State head = state_;
        bool report = report_;
        report_ = false;
        for (;;) {
            State next = merge(head, iterate(statement, head, nullptr));
            if (next == head) break;
            head = next;
        }
        report_ = report;
        State exit;
        iterate(statement, head, &exit);
        state_ = exit;
    }

    // One pass over the loop, starting from `head`. Returns the state at the end of
    // an iteration; `exit`, if given, receives the state after the loop.
    State iterate(const ForStmt& statement, const State& head, State* exit) {
        state_ = head;
        State whenFalse = unreachable();
        if (statement.condition) {
            visit(*statement.condition);
            whenFalse = state_;
        }
        if (statement.form == ForForm::Each) whenFalse = state_;  // the list may have no more elements
        loops_.emplace_back();
        checkBlock(statement.body);
        Loop loop = std::move(loops_.back());
        loops_.pop_back();
        for (const State& state : loop.continues) state_ = merge(state_, state);
        if (statement.step) checkStatement(*statement.step);
        if (exit) {
            *exit = whenFalse;
            for (const State& state : loop.breaks) *exit = merge(*exit, state);
        }
        return state_;
    }

    // ---- Expressions, in the order in which they are evaluated ----

    void visit(const Expr& expr) {
        switch (expr.kind) {
        case ExprKind::IntLiteral:
        case ExprKind::FloatLiteral:
        case ExprKind::BoolLiteral:
        case ExprKind::StringLiteral:
        case ExprKind::Self:
            break;
        case ExprKind::Name: {
            const auto& name = static_cast<const NameExpr&>(expr);
            use(name.variable, name.loc);
            break;
        }
        case ExprKind::Field: visit(*static_cast<const FieldExpr&>(expr).object); break;
        case ExprKind::Unary: visit(*static_cast<const UnaryExpr&>(expr).operand); break;
        case ExprKind::Binary: {
            const auto& binary = static_cast<const BinaryExpr&>(expr);
            visit(*binary.lhs);
            if (binary.op == BinaryOp::And || binary.op == BinaryOp::Or) {
                State afterLhs = state_;  // the right side may not be evaluated
                visit(*binary.rhs);
                state_ = merge(afterLhs, state_);
            } else {
                visit(*binary.rhs);
            }
            break;
        }
        case ExprKind::Call:
            for (const ExprPtr& arg : static_cast<const CallExpr&>(expr).args) visit(*arg);
            noteRaise();
            break;
        case ExprKind::MethodCall: {
            const auto& call = static_cast<const MethodCallExpr&>(expr);
            visit(*call.object);
            for (const ExprPtr& arg : call.args) visit(*arg);
            noteRaise();
            break;
        }
        case ExprKind::Construct:
            for (const ExprPtr& arg : static_cast<const ConstructExpr&>(expr).args) visit(*arg);
            noteRaise();
            break;
        case ExprKind::EnumCase:
            for (const ExprPtr& arg : static_cast<const EnumCaseExpr&>(expr).args) visit(*arg);
            break;
        case ExprKind::TypeRef:
            break;
        case ExprKind::Conversion: visit(*static_cast<const ConversionExpr&>(expr).operand); break;
        case ExprKind::Print:
            for (const ExprPtr& arg : static_cast<const PrintExpr&>(expr).args) visit(*arg);
            break;
        case ExprKind::Move: {
            const auto& move = static_cast<const MoveExpr&>(expr);
            visit(*move.operand);
            if (move.moveKind == MoveKind::Variable) set(static_cast<const NameExpr&>(*move.operand).variable, kMoved);
            break;
        }
        case ExprKind::Ref: visit(*static_cast<const RefExpr&>(expr).operand); break;
        case ExprKind::Copy: visit(*static_cast<const CopyExpr&>(expr).operand); break;
        case ExprKind::Index: {
            const auto& element = static_cast<const IndexExpr&>(expr);
            visit(*element.object);
            visit(*element.index);
            break;
        }
        }
    }

    Diagnostics& diag_;
    State state_;
    std::vector<Loop> loops_;
    std::vector<State> raises_;  // for each enclosing `try` body: where an exception can arise
    bool report_ = true;
};

}  // namespace

bool checkMoves(const Module& module, Diagnostics& diag) {
    MoveChecker checker(diag);
    // A generic is checked in its definition, where T may need destruction: that
    // is stricter than any of its instances (D80, D85).
    auto check = [&](const FunctionDecl& function) {
        bool instance = function.generic || (function.owner && function.owner->generic);
        if (function.bodyChecked && (!instance || function.isDefinition)) checker.checkFunction(function);
    };
    for (const StructDecl& decl : module.structs) {
        for (const FunctionDecl& member : decl.members) check(member);
    }
    for (const auto& decl : module.structInstances) {
        for (const FunctionDecl& member : decl->members) check(member);
    }
    for (const FunctionDecl& function : module.functions) check(function);
    for (const auto& function : module.functionInstances) check(*function);
    return !diag.hasErrors();
}

}  // namespace cromo
