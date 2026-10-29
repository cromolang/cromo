#include "index.h"

#include <set>
#include <tuple>

namespace cromo {

namespace {

bool isIdentChar(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
}

// A type as it is written, for the declarations that sema has not checked; the
// type parameters of an instance become its type arguments.
std::string writtenType(const TypeName& name, const TypeBindings* bindings = nullptr) {
    if (bindings && name.module.empty() && name.args.empty()) {
        for (const auto& [param, type] : *bindings) {
            if (param == name.name) return type.name();
        }
    }
    std::string text = (name.module.empty() ? "" : name.module + ".") + name.name;
    if (!name.args.empty()) {
        text += "<";
        for (size_t i = 0; i < name.args.size(); ++i) text += (i ? ", " : "") + writtenType(name.args[i], bindings);
        text += ">";
    }
    return text;
}

std::string typeText(Type type, const TypeName& written, const TypeBindings* bindings = nullptr) {
    return type.isError() ? writtenType(written, bindings) : type.name();
}

std::string modePrefix(ParamMode mode, bool isConst) {
    if (mode == ParamMode::Ref) return "ref ";
    if (mode == ParamMode::Move) return isConst ? "move const " : "move ";
    return "";
}

std::string declarationOf(Type type) {
    if (type.isInterface()) return "interface " + type.interfaceDecl->name;
    if (!type.isStruct()) return type.name();
    const StructDecl& decl = *type.structDecl;
    std::string text = (decl.isEnum ? "enum " : "struct ") + decl.name;
    for (size_t i = 0; i < decl.interfaces.size(); ++i) text += (i ? ", " : " : ") + writtenType(decl.interfaces[i]);
    return text;
}

class Indexer {
public:
    explicit Indexer(const std::deque<SourceFile>& sources) : sources_(sources) {
        for (const SourceFile& source : sources) {
            std::vector<size_t> starts = {0};
            for (size_t i = 0; i < source.text.size(); ++i) {
                if (source.text[i] == '\n') starts.push_back(i + 1);
            }
            lineStarts_.push_back(std::move(starts));
        }
    }

    std::vector<Reference> run(const Module& module) {
        for (const InterfaceDecl& decl : module.interfaces) {
            add(decl.loc, decl.name, "interface " + decl.name, decl.loc);
            for (const FunctionDecl& method : decl.methods) declaration(method, false);
        }
        for (const StructDecl& decl : module.structs) structDecl(decl);
        for (const auto& decl : module.structInstances) structDecl(*decl);
        for (const FunctionDecl& function : module.functions) declaration(function, function.typeParams.empty());
        for (const auto& function : module.functionInstances) declaration(*function, true);
        return std::move(references_);
    }

private:
    // The identifier that starts at `loc`, or an empty text.
    std::string identifierAt(SourceLocation loc) const {
        if (loc.file < 0 || static_cast<size_t>(loc.file) >= sources_.size()) return {};
        const std::vector<size_t>& starts = lineStarts_[loc.file];
        if (loc.line < 1 || static_cast<size_t>(loc.line) > starts.size()) return {};
        const std::string& text = sources_[loc.file].text;
        size_t pos = starts[loc.line - 1];
        for (int column = 1; column < loc.column && pos < text.size() && text[pos] != '\n'; ++column) {
            ++pos;
            while (pos < text.size() && (static_cast<unsigned char>(text[pos]) & 0xC0) == 0x80) ++pos;
        }
        size_t end = pos;
        while (end < text.size() && isIdentChar(text[end])) ++end;
        return text.substr(pos, end - pos);
    }

    // Only where the source really has the name: sema also makes nodes that are
    // not written, as the `append` of `s += t`.
    void add(SourceLocation loc, const std::string& name, const std::string& hover, SourceLocation target,
             Type type = Type::error()) {
        if (name.empty() || identifierAt(loc) != name) return;
        if (!seen_.insert(std::make_tuple(loc.file, loc.line, loc.column)).second) return;  // the instances of a generic
        Reference reference;
        reference.loc = loc;
        reference.length = static_cast<int>(name.size());
        reference.hover = hover;
        reference.hasTarget = true;
        reference.target = target;
        reference.type = type;
        references_.push_back(std::move(reference));
    }

    void typeName(const TypeName& name) {
        Type type = baseElement(name.resolved);  // `Point[4]` leads to Point
        if (type.isStruct()) add(name.loc, name.name, declarationOf(type), type.structDecl->loc);
        if (type.isInterface()) add(name.loc, name.name, declarationOf(type), type.interfaceDecl->loc);
        for (const TypeName& arg : name.args) typeName(arg);
    }

    void variable(const Variable* variable, SourceLocation loc, const std::string& prefix = "") {
        if (!variable) return;
        std::string mode = prefix;
        if (mode.empty() && variable->kind == VariableKind::RefParam) mode = "ref ";
        if (mode.empty() && variable->kind == VariableKind::MoveParam) mode = "move ";
        std::string hover = mode + (variable->isConst ? "const " : "") + variable->type.name() + " " + variable->name;
        add(loc, variable->name, hover, variable->loc, variable->type);
    }

    void caseOf(const StructDecl& decl, int index, SourceLocation loc) {
        if (index < 0 || static_cast<size_t>(index) >= decl.cases.size()) return;
        const EnumCase& item = decl.cases[index];
        std::string hover = decl.name + "." + item.name;
        if (!item.fields.empty()) {
            hover += "(";
            for (size_t i = 0; i < item.fields.size(); ++i) {
                hover += (i ? ", " : "") + typeText(item.fields[i].resolved, item.fields[i].type) + " " + item.fields[i].name;
            }
            hover += ")";
        }
        add(loc, item.name, hover, item.loc);
    }

    void structDecl(const StructDecl& decl) {
        if (decl.typeParams.empty() || decl.generic) add(decl.loc, decl.name, declarationOf(Type::structType(&decl)), decl.loc);
        for (const TypeName& name : decl.interfaces) typeName(name);
        for (const FieldDecl& field : decl.fields) {
            typeName(field.type);
            add(field.loc, field.name, typeText(field.resolved, field.type) + " " + decl.name + "." + field.name, field.loc,
                field.resolved);
        }
        for (size_t i = 0; i < decl.cases.size(); ++i) {
            caseOf(decl, static_cast<int>(i), decl.cases[i].loc);
            for (const FieldDecl& field : decl.cases[i].fields) typeName(field.type);
        }
        for (const FunctionDecl& member : decl.members) declaration(member, member.bodyChecked);
    }

    void declaration(const FunctionDecl& function, bool checked) {
        std::string name = function.kind == FunctionKind::Constructor || function.kind == FunctionKind::Destructor
                               ? (function.owner ? function.owner->name : std::string())
                               : function.name;
        add(function.loc, name, signatureOf(function), function.loc);
        if (!checked) return;
        typeName(function.returnType);
        for (size_t i = 0; i < function.params.size(); ++i) {
            const Param& param = function.params[i];
            typeName(param.type);
            if (i < function.variables.size()) variable(function.variables[i].get(), param.loc, modePrefix(param.mode, param.isConst));
        }
        block(function.body);
    }

    void block(const Block& block) {
        for (const StmtPtr& statement : block.statements) stmt(statement.get());
    }

    void stmt(const Stmt* statement) {
        if (!statement) return;
        switch (statement->kind) {
        case StmtKind::VarDecl: {
            const auto& decl = static_cast<const VarDeclStmt&>(*statement);
            typeName(decl.typeName);
            variable(decl.variable, decl.nameLoc);
            expr(decl.init.get());
            break;
        }
        case StmtKind::Assign: {
            const auto& assign = static_cast<const AssignStmt&>(*statement);
            expr(assign.target.get());
            expr(assign.value.get());
            expr(assign.call.get());
            break;
        }
        case StmtKind::IncDec: expr(static_cast<const IncDecStmt&>(*statement).target.get()); break;
        case StmtKind::Expr: expr(static_cast<const ExprStmt&>(*statement).expr.get()); break;
        case StmtKind::If: {
            const auto& branch = static_cast<const IfStmt&>(*statement);
            expr(branch.condition.get());
            block(branch.thenBlock);
            if (branch.elseBlock) block(*branch.elseBlock);
            break;
        }
        case StmtKind::For: {
            const auto& loop = static_cast<const ForStmt&>(*statement);
            stmt(loop.init.get());
            expr(loop.condition.get());
            stmt(loop.step.get());
            if (loop.form == ForForm::Each) {
                typeName(loop.elementType);
                variable(loop.element, loop.elementLoc, loop.isRef ? "ref " : "");
                if (loop.hasValue) {  // a map (D197)
                    typeName(loop.valueType);
                    variable(loop.value, loop.valueLoc, loop.valueIsRef ? "ref " : "");
                }
                expr(loop.list.get());
            }
            block(loop.body);
            break;
        }
        case StmtKind::Switch: {
            const auto& choice = static_cast<const SwitchStmt&>(*statement);
            expr(choice.value.get());
            Type subject = choice.value ? choice.value->type : Type::error();
            for (const SwitchBranch& branch : choice.branches) {
                for (const CasePattern& pattern : branch.patterns) {
                    if (subject.isStruct() && subject.structDecl->isEnum) caseOf(*subject.structDecl, pattern.caseIndex, pattern.loc);
                    for (const CaseBinding& binding : pattern.bindings) {
                        typeName(binding.type);
                        variable(binding.variable, binding.loc, binding.isRef ? "ref " : "");
                    }
                    expr(pattern.literal.get());
                }
                block(branch.body);
            }
            if (choice.defaultBlock) block(*choice.defaultBlock);
            break;
        }
        case StmtKind::Throw: expr(static_cast<const ThrowStmt&>(*statement).value.get()); break;
        case StmtKind::Try: {
            const auto& attempt = static_cast<const TryStmt&>(*statement);
            block(attempt.body);
            for (const CatchClause& clause : attempt.catches) {
                typeName(clause.type);
                variable(clause.variable, clause.nameLoc);
                block(clause.body);
            }
            if (attempt.finallyBlock) block(*attempt.finallyBlock);
            break;
        }
        case StmtKind::Return: expr(static_cast<const ReturnStmt&>(*statement).value.get()); break;
        case StmtKind::Break:
        case StmtKind::Continue: break;
        }
    }

    void exprs(const std::vector<ExprPtr>& list) {
        for (const ExprPtr& item : list) expr(item.get());
    }

    void expr(const Expr* node) {
        if (!node) return;
        switch (node->kind) {
        case ExprKind::Name: {
            const auto& name = static_cast<const NameExpr&>(*node);
            variable(name.variable, name.loc);
            break;
        }
        case ExprKind::Unary: expr(static_cast<const UnaryExpr&>(*node).operand.get()); break;
        case ExprKind::Binary: {
            const auto& binary = static_cast<const BinaryExpr&>(*node);
            expr(binary.lhs.get());
            expr(binary.rhs.get());
            break;
        }
        case ExprKind::Call: {
            const auto& call = static_cast<const CallExpr&>(*node);
            if (call.function) add(call.loc, call.callee, signatureOf(*call.function), call.function->loc);
            for (const TypeName& arg : call.typeArgs) typeName(arg);
            exprs(call.args);
            break;
        }
        case ExprKind::Conversion: expr(static_cast<const ConversionExpr&>(*node).operand.get()); break;
        case ExprKind::Print: exprs(static_cast<const PrintExpr&>(*node).args); break;
        case ExprKind::Field: {
            const auto& field = static_cast<const FieldExpr&>(*node);
            expr(field.object.get());
            Type owner = field.object ? field.object->type : Type::error();
            if (owner.isStruct() && field.index >= 0 && static_cast<size_t>(field.index) < owner.structDecl->fields.size()) {
                const FieldDecl& decl = owner.structDecl->fields[field.index];
                add(field.loc, field.field, typeText(decl.resolved, decl.type) + " " + owner.structDecl->name + "." + decl.name,
                    decl.loc, decl.resolved);
            }
            break;
        }
        case ExprKind::MethodCall: {
            const auto& call = static_cast<const MethodCallExpr&>(*node);
            expr(call.object.get());
            if (call.function) add(call.loc, call.method, signatureOf(*call.function), call.function->loc);
            exprs(call.args);
            break;
        }
        case ExprKind::Construct: {
            const auto& construct = static_cast<const ConstructExpr&>(*node);
            if (construct.decl) {
                std::string hover = declarationOf(Type::structType(construct.decl));
                for (const FunctionDecl& member : construct.decl->members) {
                    if (member.kind == FunctionKind::Constructor) hover = signatureOf(member);
                }
                add(construct.loc, construct.decl->name, hover, construct.decl->loc);
            }
            exprs(construct.args);
            break;
        }
        case ExprKind::Move: expr(static_cast<const MoveExpr&>(*node).operand.get()); break;
        case ExprKind::Ref: expr(static_cast<const RefExpr&>(*node).operand.get()); break;
        case ExprKind::Copy: expr(static_cast<const CopyExpr&>(*node).operand.get()); break;
        case ExprKind::Index: {
            const auto& index = static_cast<const IndexExpr&>(*node);
            expr(index.object.get());
            expr(index.index.get());
            break;
        }
        case ExprKind::EnumCase: {
            const auto& value = static_cast<const EnumCaseExpr&>(*node);
            if (value.decl) caseOf(*value.decl, value.index, value.loc);
            exprs(value.args);
            break;
        }
        case ExprKind::TypeRef: typeName(static_cast<const TypeRefExpr&>(*node).name); break;
        case ExprKind::Lambda: {
            const auto& lambda = static_cast<const LambdaExpr&>(*node);
            for (const Capture& capture : lambda.captures) variable(capture.outer, capture.loc);
            const FunctionDecl& function = *lambda.function;
            if (!function.bodyChecked) break;
            typeName(function.returnType);
            for (size_t i = 0; i < function.params.size(); ++i) {
                const Param& param = function.params[i];
                typeName(param.type);
                if (i < function.variables.size()) variable(function.variables[i].get(), param.loc, modePrefix(param.mode, param.isConst));
            }
            block(function.body);
            break;
        }
        case ExprKind::CallValue: {
            const auto& call = static_cast<const CallValueExpr&>(*node);
            expr(call.callee.get());
            exprs(call.args);
            break;
        }
        case ExprKind::IntLiteral:
        case ExprKind::FloatLiteral:
        case ExprKind::BoolLiteral:
        case ExprKind::StringLiteral:
        case ExprKind::Self: break;
        }
    }

    const std::deque<SourceFile>& sources_;
    std::vector<std::vector<size_t>> lineStarts_;
    std::set<std::tuple<int, int, int>> seen_;
    std::vector<Reference> references_;
};

}  // namespace

std::string signatureOf(const FunctionDecl& function, const StructDecl* ownerDecl) {
    if (!ownerDecl) ownerDecl = function.owner;
    const TypeBindings* bindings = ownerDecl && ownerDecl->generic ? &ownerDecl->typeBindings : nullptr;
    std::string params;
    for (size_t i = 0; i < function.params.size(); ++i) {
        const Param& param = function.params[i];
        std::string type = i < function.paramTypes.size() ? typeText(function.paramTypes[i], param.type, bindings)
                                                          : writtenType(param.type, bindings);
        params += (i ? ", " : "") + modePrefix(param.mode, param.isConst) + type + " " + param.name;
    }
    std::string owner = ownerDecl ? ownerDecl->name : std::string();
    switch (function.kind) {
    case FunctionKind::Constructor: return owner + "(" + params + ")";
    case FunctionKind::Destructor: return "~" + owner + "()";
    default: break;
    }
    std::string text = typeText(function.resolvedReturn, function.returnType, bindings) + " " + (owner.empty() ? "" : owner + ".") +
                       function.name + "(" + params + ")";
    if (function.isConst) text += " const";
    if (function.isExtern) text = "extern \"" + function.library + "\" " + text;
    return text;
}

std::string fieldTypeOf(const FieldDecl& field, const StructDecl& owner) {
    return typeText(field.resolved, field.type, owner.generic ? &owner.typeBindings : nullptr);
}

std::vector<Reference> indexProgram(const Module& module, const std::deque<SourceFile>& sources) {
    return Indexer(sources).run(module);
}

}  // namespace cromo
