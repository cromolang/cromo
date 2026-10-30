#include "codegen.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <set>
#include <sstream>

namespace cromo {

namespace {

struct Value {
    std::string ref;  // "%t3", "42", "true", ...
    Type type;
};

// An object that must be destroyed when its scope or statement ends (D68).
struct LiveObject {
    std::string address;
    Type type;
    const Variable* variable = nullptr;  // null for temporaries
    std::string flag;                    // an i1 that says if it is still alive, when it can be given away
    bool isException = false;            // the exception of a `catch`: `address` holds { object, descriptor }
};

// The objects of a block, and what leaving it means: a `try` with `finally` runs
// it (D16), and the body of a `try` sends its exceptions to `handler` (D36).
struct Scope {
    std::vector<LiveObject> objects;
    const Block* finallyBlock = nullptr;
    std::string handler;
};

// The exception that a `catch` owns, for `throw` alone (D114).
struct Caught {
    std::string box;   // { object, descriptor }
    std::string flag;
};

struct Loop {
    std::string continueLabel;
    std::string breakLabel;
    size_t scopeDepth;  // scopes opened inside the loop are above this index
};

// Writes an integer constant as a signed value of the given width, as LLVM
// expects: for example 255 in a u8 becomes -1.
std::string intConstant(uint64_t magnitude, bool negative, int bits) {
    uint64_t pattern = negative ? ~magnitude + 1 : magnitude;
    if (bits < 64) {
        uint64_t mask = (uint64_t{1} << bits) - 1;
        pattern &= mask;
        if (pattern & (uint64_t{1} << (bits - 1))) return "-" + std::to_string((~pattern + 1) & mask);
        return std::to_string(pattern);
    }
    if (pattern & (uint64_t{1} << 63)) return "-" + std::to_string(~pattern + 1);
    return std::to_string(pattern);
}

// LLVM writes floating-point constants as the hexadecimal bits of a double; for
// a float the value must be exactly representable as a float.
std::string floatConstant(double value, int bits) {
    if (bits == 32) value = static_cast<float>(value);
    uint64_t pattern;
    std::memcpy(&pattern, &value, sizeof pattern);
    char text[32];
    std::snprintf(text, sizeof text, "0x%016llX", static_cast<unsigned long long>(pattern));
    return text;
}

// A Mutex, a Channel, an Atomic, or a value that holds one: other threads write it
// through its address (D216, D326, D328).
bool isAddressed(Type type) {
    type = baseElement(type);
    return type.isStruct() && type.structDecl->addressed;
}

// Structs and arrays larger than 16 bytes are passed and returned through a pointer,
// and so are the values whose address matters (D216).
bool byPointer(Type type) { return type.isAggregate() && (sizeOf(type) > 16 || isAddressed(type)); }

// A `ref` parameter is always an address; the others are when they are large (D22).
// A parameter of interface type is always a pair of pointers passed by value (D49).
bool byAddress(const Param& param, Type type) {
    return !type.isInterface() && (param.mode == ParamMode::Ref || byPointer(type));
}

// The symbols have the module in front (D134): `%"struct.geometry.Circle"`.
std::string structName(const StructDecl& decl) { return llvmName('%', "struct." + decl.symbol); }

// Symbols of the members of a struct. The double dot cannot clash with a method.
std::string methodSymbol(const StructDecl& decl, const std::string& method) { return llvmName('@', decl.symbol + "." + method); }
std::string constructorSymbol(const StructDecl& decl) { return llvmName('@', decl.symbol + "..new"); }
std::string dropSymbol(const StructDecl& decl) { return llvmName('@', decl.symbol + "..drop"); }

// The functions of the standard library receive where they were called from,
// and their panics report it (D102).
// A method that implements a method of an interface: it is also called through
// a method table, which knows nothing of hidden parameters.
bool implementsInterfaceMethod(const FunctionDecl& function) {
    if (!function.owner || function.kind != FunctionKind::Method) return false;
    for (const InterfaceDecl* iface : function.owner->implements) {
        for (const FunctionDecl& method : iface->methods) {
            if (method.name == function.name) return true;
        }
    }
    return false;
}

bool tracksCaller(const FunctionDecl& function) {
    return function.loc.file == kLibraryFile && function.kind != FunctionKind::Destructor && !function.isGenerated &&
           !function.isExtern && !function.isLambda && !implementsInterfaceMethod(function);
}

// The data captured by a lambda: the function that destroys them, then the captures (D183).
std::string environmentType(const FunctionDecl& lambda) { return llvmName('%', lambda.symbol + ".env"); }

int fieldIndexOf(const StructDecl& decl, const std::string& name) {
    for (size_t i = 0; i < decl.fields.size(); ++i) {
        if (decl.fields[i].name == name) return static_cast<int>(i);
    }
    return -1;
}

std::string symbolOf(const FunctionDecl& function) {
    switch (function.kind) {
    case FunctionKind::Free: return llvmName('@', function.symbol);
    case FunctionKind::Method:  // an instance with type arguments of its own has its own name (D92)
        return function.genericMethod ? llvmName('@', function.symbol) : methodSymbol(*function.owner, function.name);
    case FunctionKind::Constructor: return constructorSymbol(*function.owner);
    case FunctionKind::Destructor: return dropSymbol(*function.owner);
    }
    return "@?";
}

// The constant value of a literal of type `type` (field initial values, D73).
std::string literalConstant(const Expr& expr, Type type) {
    switch (expr.kind) {
    case ExprKind::IntLiteral: {
        const auto& literal = static_cast<const IntLiteralExpr&>(expr);
        if (type.isFloat()) {
            double value = static_cast<double>(literal.magnitude);
            return floatConstant(literal.negative ? -value : value, type.bits);
        }
        return intConstant(literal.magnitude, literal.negative, type.bits);
    }
    case ExprKind::FloatLiteral: return floatConstant(static_cast<const FloatLiteralExpr&>(expr).value, type.bits);
    case ExprKind::BoolLiteral: return static_cast<const BoolLiteralExpr&>(expr).value ? "true" : "false";
    case ExprKind::Conversion: return literalConstant(*static_cast<const ConversionExpr&>(expr).operand, type);
    default: return "undef";
    }
}

// The default value of a type (D31, D48): zero, or the initial values of the fields.
std::string defaultConstant(Type type);

// A C function (D128, D129): a `ref` is a pointer, and a bool is a byte, as in C.
std::string cType(Type type) { return type.isBool() ? "i8" : llvmType(type); }

std::string externDeclaration(const FunctionDecl& function) {
    Type ret = function.resolvedReturn;
    std::string out = "declare " + (ret.isVoid() ? std::string("void") : cType(ret)) + " " + symbolOf(function) + "(";
    for (size_t i = 0; i < function.params.size(); ++i) {
        out += (i ? ", " : "") + (function.params[i].mode == ParamMode::Ref ? std::string("ptr") : cType(function.paramTypes[i]));
    }
    return out + ") nounwind\n";
}

// An enum in memory (D104): a field of size zero that gives it the alignment of
// its most aligned case, the tag, padding up to where the data start, the data
// of the first case, and padding up to the size of the largest case:
// { [0 x i64], i32, [4 x i8], double, [8 x i8] }. The other cases are read
// through their own type, at the same offset.
std::string alignmentType(int align) { return align >= 8 ? "i64" : "i32"; }

int enumTail(const StructDecl& decl) { return decl.size - decl.payloadOffset - decl.cases[0].size; }

std::string enumTypeBody(const StructDecl& decl) {
    std::string out = "{ [0 x " + alignmentType(decl.align) + "], i32, [" + std::to_string(decl.payloadOffset - 4) + " x i8]";
    for (const FieldDecl& field : decl.cases[0].fields) out += ", " + llvmType(field.resolved);
    return out + ", [" + std::to_string(enumTail(decl)) + " x i8] }";
}

std::string caseTypeName(const StructDecl& decl, const EnumCase& item) {
    return llvmName('%', "case." + decl.symbol + "." + item.name);
}

// The default value of an enum is its first case, with the defaults of its data (D111).
std::string enumDefault(const StructDecl& decl) {
    std::string out = "{ [0 x " + alignmentType(decl.align) + "] zeroinitializer, i32 " + std::to_string(decl.cases[0].value) +
                      ", [" + std::to_string(decl.payloadOffset - 4) + " x i8] zeroinitializer";
    for (const FieldDecl& field : decl.cases[0].fields) {
        out += ", " + llvmType(field.resolved) + " " + defaultConstant(field.resolved);
    }
    return out + ", [" + std::to_string(enumTail(decl)) + " x i8] zeroinitializer }";
}

// True when the default value of the type is all zero bytes: it can be written
// with memset, and an array of it is a zeroinitializer.
bool zeroDefault(Type type) {
    if (type.isArray()) return zeroDefault(type.array->element);
    if (!type.isStruct()) return true;  // 0, false, null, a function without code
    const StructDecl& decl = *type.structDecl;
    if (decl.isEnum && decl.cases[0].value != 0) return false;
    const std::vector<FieldDecl>& fields = decl.isEnum ? decl.cases[0].fields : decl.fields;
    for (const FieldDecl& field : fields) {
        if (field.init || !zeroDefault(field.resolved)) return false;
    }
    return true;
}

std::string defaultConstant(Type type) {
    if (type.isFunction()) return "zeroinitializer";  // no code: calling it is a panic
    if (type.isInterface()) return "zeroinitializer";  // empty: calling a method is a panic (D361)
    if (type.isArray()) {  // D196: every element has its default value
        if (zeroDefault(type)) return "zeroinitializer";
        std::string element = llvmType(type.array->element) + " " + defaultConstant(type.array->element);
        std::string out = "[";
        for (int64_t i = 0; i < type.array->count; ++i) out += (i ? ", " : "") + element;
        return out + "]";
    }
    if (type.isBool()) return "false";
    if (type.isFloat()) return floatConstant(0, type.bits);
    if (type.kind == TypeKind::Addr) return "null";
    if (!type.isStruct()) return "0";
    const StructDecl& decl = *type.structDecl;
    if (decl.isEnum) return enumDefault(decl);
    if (decl.fields.empty()) return "zeroinitializer";
    std::string out = "{ ";
    for (size_t i = 0; i < decl.fields.size(); ++i) {
        const FieldDecl& field = decl.fields[i];
        if (i) out += ", ";
        // Only the literals are left in the fields: sema moves the other initial values
        // into the default function of the struct (D360).
        out += llvmType(field.resolved) + " " +
               (field.init ? literalConstant(*field.init, field.resolved) : defaultConstant(field.resolved));
    }
    return out + " }";
}

class Generator {
public:
    explicit Generator(const CodegenOptions& options) : options_(options) {}

    std::string run(const Module& module) {
        std::ostringstream out;
        out << "; Generated by cromo\n";
        out << "source_filename = " << quoted(options_.sourcePaths[0]) << "\n";
        out << "target triple = \"" << targetTriple(options_.target) << "\"\n";
        // Where the code of the program starts: the table of the stack traces
        // counts from here (D339, backend.cpp).
        out << "module asm \".text\"\nmodule asm \".globl cromo_code\"\nmodule asm \"cromo_code:\"\n\n";
        startDebug();

        for (const InterfaceDecl& iface : module.interfaces) {
            if (iface.name == "Exception" && iface.loc.file == kLibraryFile) exceptionInterface_ = &iface;
        }
        for (const FunctionDecl& function : module.functions) {
            if (function.name == "programArguments" && function.loc.file == kLibraryFile) programArguments_ = &function;
            if (function.name == "main" && function.loc.file == 0 && function.symbol.size() >= 4) {
                programModule_ = function.symbol.substr(0, function.symbol.size() - 4);  // "name.", or nothing
            }
        }

        // Generics are emitted only through their instances with real types (D39).
        std::vector<const StructDecl*> structs;
        for (const StructDecl& decl : module.structs) {
            if (decl.typeParams.empty()) structs.push_back(&decl);
        }
        for (const auto& decl : module.structInstances) {
            if (!decl->isAbstract) structs.push_back(decl.get());
        }
        std::vector<const FunctionDecl*> functions;
        std::vector<const FunctionDecl*> externs;  // the same C function can be declared by several modules
        std::set<std::string> declared;
        for (const FunctionDecl& function : module.functions) {
            if (function.isExtern) {
                if (declared.insert(function.name).second) externs.push_back(&function);
            } else if (function.typeParams.empty()) {
                functions.push_back(&function);
            }
        }
        for (const auto& function : module.functionInstances) {
            if (!function->isAbstract) functions.push_back(function.get());
        }

        for (const StructDecl* decl : structs) {
            if (decl->isEnum) {
                out << structName(*decl) << " = type " << enumTypeBody(*decl) << "\n";
                for (const EnumCase& item : decl->cases) {
                    out << caseTypeName(*decl, item) << " = type { ";
                    for (size_t i = 0; i < item.fields.size(); ++i) out << (i ? ", " : "") << llvmType(item.fields[i].resolved);
                    out << (item.fields.empty() ? "}\n" : " }\n");
                }
                continue;
            }
            out << structName(*decl) << " = type { ";
            for (size_t i = 0; i < decl->fields.size(); ++i) out << (i ? ", " : "") << llvmType(decl->fields[i].resolved);
            out << (decl->fields.empty() ? "}\n" : " }\n");
        }
        if (!structs.empty()) out << "\n";
        for (const LambdaExpr* lambda : module.lambdas) {
            if (!lambda->function->isAbstract && lambda->function->bodyChecked) out << lambdaType(*lambda);
            enclosing_[lambda->function.get()] = lambda->enclosing;
        }

        for (const StructDecl* decl : structs) {
            for (const FunctionDecl& member : decl->members) {
                if (member.bodyChecked) out << generateFunction(member);  // D103: only the methods used
            }
            // Without a destructor of its own, a struct still destroys its fields.
            if (decl->needsDestruction && !decl->destructor) out << generateFieldsDrop(*decl);
            if (decl->generatedCopy) out << generateCopy(*decl);
        }
        const FunctionDecl* mainFunction = nullptr;
        for (const FunctionDecl* function : functions) {
            out << generateFunction(*function);
            if (function->name == "main" && function->loc.file == 0 && !function->generic) mainFunction = function;
        }
        if (options_.tests) {
            out << testEntryPoint(testFunctions(module));
        } else if (mainFunction) {
            out << entryPoint(*mainFunction);
        }
        for (const LambdaExpr* lambda : module.lambdas) {
            if (lambda->function->isAbstract || !lambda->function->bodyChecked) continue;
            out << lambdaData(*lambda);
            out << generateFunction(*lambda->function);
        }

        if (freesExceptions_) out << freeExceptionFunction();
        out << releaseBodies_;
        out << globals_.str() << "\n";
        for (const FunctionDecl* function : externs) out << externDeclaration(*function);
        out << "declare void @cromo_write(i64, ptr, i64) nounwind\n";
        out << "declare void @cromo_fmt_int(i64, i64, i32, ptr) nounwind\n";
        out << "declare void @cromo_fmt_f64(i64, double, ptr) nounwind\n";
        out << "declare void @cromo_fmt_f32(i64, float, ptr) nounwind\n";
        out << "declare void @cromo_fmt_bool(i64, i32, ptr) nounwind\n";
        out << "declare void @cromo_fmt_str(i64, ptr, i64, ptr) nounwind\n";
        out << "declare void @cromo_print_begin() nounwind\n";
        out << "declare void @cromo_print_end(i64, i32) nounwind\n";
        out << "declare double @cromo_parse_float(ptr, i64, i64) nounwind\n";
        out << "declare i64 @cromo_hash_bytes(ptr, i64) nounwind readonly\n";
        out << "declare i64 @cromo_hash_bits(i64) nounwind readnone\n";
        out << "declare void @cromo_exit(i32) noreturn nounwind\n";
        out << "declare void @cromo_panic(ptr, i64, ptr) noreturn nounwind cold\n";
        out << "declare void @cromo_panic_index(i64, i64, ptr) noreturn nounwind cold\n";
        out << "declare ptr @cromo_alloc(i64) nounwind\n";
        out << "declare ptr @cromo_realloc(ptr, i64) nounwind\n";
        out << "declare void @cromo_free(ptr) nounwind\n";
        if (!declared.count("memcmp")) out << "declare i32 @memcmp(ptr, ptr, i64) nounwind\n";
        out << "declare void @cromo_uncaught(ptr, i64, ptr) noreturn nounwind cold\n";
        out << "declare ptr @cromo_exception_new(i64) nounwind\n";
        out << "declare void @cromo_runtime_start() nounwind\n";
        if (options_.tests) out << "declare i64 @cromo_test_index() nounwind\n";
        out << "declare void @cromo_exception_free(ptr) nounwind\n";
        out << "declare i64 @cromo_arg_count() nounwind\n";
        out << "declare i64 @cromo_arg_length(i64) nounwind\n";
        out << "declare ptr @cromo_arg_data(i64) nounwind\n";
        for (const std::string& declaration : intrinsics_) out << declaration << "\n";
        out << finishDebug();
        return out.str();
    }

private:
    // ---- Debug information (D338, D339) ----
    //
    // Always the lines, from which backend.cpp makes the table of the stack traces;
    // without --release also the variables and their types, for a debugger. The
    // nodes are written after the code, numbered as they are made.

    std::string metadata(const std::string& node) {
        std::string id = "!" + std::to_string(nextMeta_++);
        meta_ << id << " = " << node << "\n";
        return id;
    }

    std::string reserveMetadata() { return "!" + std::to_string(nextMeta_++); }
    void defineMetadata(const std::string& id, const std::string& node) { meta_ << id << " = " << node << "\n"; }

    void startDebug() {
        compileUnit_ = reserveMetadata();
        subroutineType_ = metadata("!DISubroutineType(types: !{null})");
    }

    std::string finishDebug() {
        bool windows = options_.target == Target::Windows;
        defineMetadata(compileUnit_, "distinct !DICompileUnit(language: DW_LANG_C_plus_plus, file: " + fileNode(0) +
                                         ", producer: \"cromo\", isOptimized: " + (options_.optimized ? "true" : "false") +
                                         ", runtimeVersion: 0, emissionKind: " +
                                         // With only the lines LLVM leaves out the functions that inline nothing,
                                         // unless the information is for profiling: the stack traces need them.
                                         (options_.release ? "LineTablesOnly, debugInfoForProfiling: true" : "FullDebug") +
                                         ", nameTableKind: None)");
        // DWARF on both systems, for the table of the stack traces; on Windows also
        // CodeView, for the PDB of a debugger.
        std::string flags = metadata("!{i32 2, !\"Debug Info Version\", i32 3}");
        flags += ", " + metadata(std::string("!{i32 2, !\"Dwarf Version\", i32 ") + (windows ? "4" : "5") + "}");
        if (windows && !options_.release) flags += ", " + metadata("!{i32 2, !\"CodeView\", i32 1}");
        return "\n!llvm.dbg.cu = !{" + compileUnit_ + "}\n!llvm.module.flags = !{" + flags + "}\n" + meta_.str();
    }

    std::string fileNode(int file) {
        if (files_.size() <= static_cast<size_t>(file)) files_.resize(file + 1);
        if (files_[file].empty()) {
            const std::string& path = static_cast<size_t>(file) < options_.debugPaths.size() ? options_.debugPaths[file]
                                                                                           : options_.sourcePaths[file];
            files_[file] = metadata("!DIFile(filename: " + quoted(path) + ", directory: \"\")");
        }
        return files_[file];
    }

    std::string subprogramNode(const std::string& name, SourceLocation loc) {
        std::string file = fileNode(loc.file);
        std::string line = std::to_string(loc.line);
        return metadata("distinct !DISubprogram(name: " + quoted(name) + ", scope: " + file + ", file: " + file +
                        ", line: " + line + ", type: " + subroutineType_ + ", scopeLine: " + line +
                        ", flags: DIFlagPrototyped, spFlags: DISPFlagLocalToUnit | DISPFlagDefinition" +
                        (options_.optimized ? " | DISPFlagOptimized" : "") + ", unit: " + compileUnit_ + ")");
    }

    // The name of a function in a debugger and in a stack trace: `main`,
    // `Point.area`, `list<i64>.add`, `main.lambda`.
    std::string debugName(const FunctionDecl& function) const {
        auto lambda = enclosing_.find(&function);
        if (lambda != enclosing_.end()) return (lambda->second ? debugName(*lambda->second) : "?") + ".lambda";
        if (!function.owner) {  // `deep`, `fs.copy`: the module of the program is not written
            bool program = !programModule_.empty() && function.symbol.compare(0, programModule_.size(), programModule_) == 0;
            return program ? function.symbol.substr(programModule_.size()) : function.symbol;
        }
        const std::string& base = function.owner->generic ? function.owner->generic->name : function.owner->name;
        switch (function.kind) {
        case FunctionKind::Constructor: return function.owner->name + "." + base;
        case FunctionKind::Destructor: return function.owner->name + ".~" + base;
        default: return function.owner->name + "." + function.name;
        }
    }

    std::string debugScope() const { return debugScopes_.empty() ? subprogram_ : debugScopes_.back(); }

    std::string locationNode() {
        std::string scope = debugScope();
        std::string key = std::to_string(location_.line) + ":" + std::to_string(location_.column) + ":" + scope;
        auto found = locations_.find(key);
        if (found != locations_.end()) return found->second;
        std::string id = metadata("!DILocation(line: " + std::to_string(location_.line) + ", column: " +
                                  std::to_string(location_.column) + ", scope: " + scope + ")");
        locations_[key] = id;
        return id;
    }

    // A variable for a debugger, at its address (D338). `arg` numbers a parameter.
    void declareVariable(const Variable& variable, const std::string& address, int arg = 0) {
        if (options_.release || subprogram_.empty() || address.empty()) return;
        std::string node = metadata("!DILocalVariable(name: " + quoted(variable.name) +
                                    (arg ? ", arg: " + std::to_string(arg) : "") + ", scope: " + debugScope() +
                                    ", file: " + fileNode(variable.loc.file) + ", line: " +
                                    std::to_string(variable.loc.line) + ", type: " + debugType(variable.type) + ")");
        ensureLive();
        body_ << "    #dbg_declare(ptr " << address << ", " << node << ", !DIExpression(), " << locationNode() << ")\n";
    }

    // The object of a method, as `this` of C++ for a debugger.
    void declareSelf(const StructDecl& owner) {
        if (options_.release || subprogram_.empty()) return;
        std::string pointer = metadata("!DIDerivedType(tag: DW_TAG_pointer_type, baseType: " +
                                       debugType(Type::structType(&owner)) + ", size: 64)");
        std::string node = metadata("!DILocalVariable(name: \"this\", arg: 1, scope: " + subprogram_ + ", file: " +
                                    fileNode(function_->loc.file) + ", line: " + std::to_string(function_->loc.line) +
                                    ", type: " + pointer + ", flags: DIFlagArtificial | DIFlagObjectPointer)");
        ensureLive();
        body_ << "    #dbg_value(ptr %self, " << node << ", !DIExpression(), " << locationNode() << ")\n";
    }

    // The types of Cromo for a debugger: numbers and bools as in C, a struct with
    // its fields, an enum with its case and the data of its cases.
    std::string debugType(Type type) {
        std::string key = type.isStruct() ? "struct." + std::to_string(reinterpret_cast<uintptr_t>(type.structDecl))
                                          : type.name();
        auto found = debugTypes_.find(key);
        if (found != debugTypes_.end()) return found->second;
        std::string id = reserveMetadata();  // first: a field can name it again
        debugTypes_[key] = id;
        std::string bits = std::to_string(sizeOf(type) * 8);
        switch (type.kind) {
        case TypeKind::Bool: defineMetadata(id, "!DIBasicType(name: \"bool\", size: 8, encoding: DW_ATE_boolean)"); break;
        case TypeKind::Int:
            defineMetadata(id, "!DIBasicType(name: " + quoted(type.name()) + ", size: " + bits +
                                   ", encoding: " + (type.isSigned ? "DW_ATE_signed" : "DW_ATE_unsigned") + ")");
            break;
        case TypeKind::Float:
            defineMetadata(id, "!DIBasicType(name: " + quoted(type.name()) + ", size: " + bits + ", encoding: DW_ATE_float)");
            break;
        case TypeKind::Array:
            defineMetadata(id, "!DICompositeType(tag: DW_TAG_array_type, baseType: " + debugType(type.array->element) +
                                   ", size: " + bits + ", elements: !{" +
                                   metadata("!DISubrange(count: " + std::to_string(type.array->count) + ")") + "})");
            break;
        case TypeKind::Struct: debugStruct(id, *type.structDecl); break;
        case TypeKind::Function:
        case TypeKind::Interface: {  // two addresses: the code and its data, the object and its table
            bool function = type.isFunction();
            std::string members = debugMember(id, function ? "code" : "object", Type::addrType(), 0, {}) + ", " +
                                  debugMember(id, function ? "data" : "table", Type::addrType(), 8, {});
            defineMetadata(id, "!DICompositeType(tag: DW_TAG_structure_type, name: " + quoted(type.name()) +
                                   ", size: 128, elements: !{" + members + "})");
            break;
        }
        default:  // an address
            defineMetadata(id, "!DIDerivedType(tag: DW_TAG_pointer_type, name: \"addr\", baseType: null, size: 64)");
            break;
        }
        return id;
    }

    std::string debugMember(const std::string& scope, const std::string& name, Type type, int offset, SourceLocation loc) {
        return metadata("!DIDerivedType(tag: DW_TAG_member, name: " + quoted(name) + ", scope: " + scope + ", file: " +
                        fileNode(loc.file) + ", line: " + std::to_string(loc.line) + ", baseType: " + debugType(type) +
                        ", size: " + std::to_string(sizeOf(type) * 8) + ", offset: " + std::to_string(offset * 8) + ")");
    }

    void debugStruct(const std::string& id, const StructDecl& decl) {
        std::vector<std::string> members;
        // The fields one after the other, each where its alignment puts it.
        auto addFields = [&](const std::vector<FieldDecl>& fields, int start, const std::string& prefix) {
            int offset = start;
            for (const FieldDecl& field : fields) {
                offset = (offset + alignOf(field.resolved) - 1) / alignOf(field.resolved) * alignOf(field.resolved);
                members.push_back(debugMember(id, prefix + field.name, field.resolved, offset, field.loc));
                offset += sizeOf(field.resolved);
            }
        };
        if (decl.isEnum) {
            std::string enumerators;
            bool data = false;
            for (const EnumCase& item : decl.cases) {
                enumerators += (enumerators.empty() ? "" : ", ") +
                               metadata("!DIEnumerator(name: " + quoted(item.name) + ", value: " + std::to_string(item.value) + ")");
                data = data || !item.fields.empty();
            }
            std::string tagType = "!DICompositeType(tag: DW_TAG_enumeration_type, name: " + quoted(decl.name) +
                                  ", file: " + fileNode(decl.loc.file) + ", line: " + std::to_string(decl.loc.line) +
                                  ", baseType: " + debugType(Type::intType(32, true)) + ", size: 32, elements: !{" +
                                  enumerators + "})";
            if (!data && decl.size == 4) {
                defineMetadata(id, tagType);
                return;
            }
            members.push_back(metadata("!DIDerivedType(tag: DW_TAG_member, name: \"case\", scope: " + id +
                                       ", baseType: " + metadata(tagType) + ", size: 32, offset: 0)"));
            for (const EnumCase& item : decl.cases) addFields(item.fields, decl.payloadOffset, item.name + ".");
        } else {
            addFields(decl.fields, 0, "");
        }
        std::string list;
        for (size_t i = 0; i < members.size(); ++i) list += (i ? ", " : "") + members[i];
        defineMetadata(id, "distinct !DICompositeType(tag: DW_TAG_structure_type, name: " + quoted(decl.name) +
                               ", file: " + fileNode(decl.loc.file) + ", line: " + std::to_string(decl.loc.line) +
                               ", size: " + std::to_string(decl.size * 8) + ", align: " + std::to_string(decl.align * 8) +
                               ", elements: !{" + list + "}, identifier: " + quoted(decl.symbol) + ")");
    }

    // ---- Output helpers ----

    static std::string quoted(const std::string& text) {
        std::string out = "\"";
        for (unsigned char c : text) {
            if (c == '"' || c == '\\' || c < 0x20 || c >= 0x7F) {
                const char* hex = "0123456789ABCDEF";
                out += '\\';
                out += hex[c >> 4];
                out += hex[c & 0xF];
            } else {
                out += static_cast<char>(c);
            }
        }
        return out + "\"";
    }

    std::string temp() { return "%t" + std::to_string(nextTemp_++); }
    std::string label(const char* base) { return std::string(base) + "." + std::to_string(nextLabel_++); }

    // A stack slot in the entry block, where mem2reg and SROA can find it.
    std::string slot(Type type) {
        std::string name = "%slot." + std::to_string(nextSlot_++);
        allocas_ << "  " << name << " = alloca " << llvmType(type) << "\n";
        return name;
    }

    // Code after a terminator (ret, br) is unreachable: give it its own block.
    void ensureLive() {
        if (!terminated_) return;
        std::string dead = label("dead");
        body_ << dead << ":\n";
        current_ = dead;
        terminated_ = false;
    }

    void inst(const std::string& line) {
        ensureLive();
        body_ << "  " << line;
        if (!subprogram_.empty()) body_ << ", !dbg " << locationNode();
        body_ << "\n";
    }

    void term(const std::string& line) {
        inst(line);
        terminated_ = true;
    }

    // Starts a new block, falling through from the current one if it is still open.
    void block(const std::string& name) {
        if (!terminated_) body_ << "  br label %" << name << "\n";
        body_ << name << ":\n";
        current_ = name;
        terminated_ = false;
    }

    // A global constant with the given bytes, shared when the same text repeats.
    std::string stringConstant(const std::string& bytes) {
        auto found = strings_.find(bytes);
        if (found != strings_.end()) return found->second;
        std::string name = "@.str." + std::to_string(strings_.size());
        globals_ << name << " = private unnamed_addr constant [" << bytes.size() << " x i8] c" << quoted(bytes)
                 << ", align 1\n";
        strings_[bytes] = name;
        return name;
    }

    // Constant FmtSpec for the runtime (runtime/cromo_rt.c).
    std::string specConstant(const FormatSpec& spec) {
        std::string name = "@.spec." + std::to_string(nextSpec_++);
        auto byte = [](int value) { return "i8 " + std::to_string(static_cast<unsigned char>(value)); };
        globals_ << name << " = private unnamed_addr constant { i8, i8, i8, i8, i8, i8, [2 x i8], i32, i32 } { "
                 << byte(spec.fill) << ", " << byte(spec.align) << ", " << byte(spec.sign) << ", "
                 << byte(spec.alternate ? 1 : 0) << ", " << byte(spec.zeroPad ? 1 : 0) << ", " << byte(spec.type)
                 << ", [2 x i8] zeroinitializer, i32 " << spec.width << ", i32 " << spec.precision << " }, align 4\n";
        return name;
    }

    std::string intrinsic(const std::string& declaration, const std::string& name) {
        intrinsics_.insert(declaration);
        return name;
    }

    // Branches to a panic (D62) when `condition` is true.
    // Where a panic happens (D62): the line of the code, or, inside the standard
    // library, the line of the user's code that called it (D102).
    std::string where(SourceLocation loc) {
        if (tracksCaller_) return "%caller";
        std::string text = options_.sourcePaths[loc.file] + ":" + std::to_string(loc.line) + ":" + std::to_string(loc.column);
        auto found = wheres_.find(text);
        if (found != wheres_.end()) return found->second;
        std::string name = "@.where." + std::to_string(wheres_.size());
        std::string bytes = stringConstant(text);  // first: it writes a global of its own
        globals_ << name << " = private unnamed_addr constant { ptr, i64 } { ptr " << bytes << ", i64 " << text.size()
                 << " }\n";
        wheres_[text] = name;
        return name;
    }

    void panic(const std::string& reason, SourceLocation loc) {
        inst("call void @cromo_panic(ptr " + stringConstant(reason) + ", i64 " + std::to_string(reason.size()) + ", ptr " +
             where(loc) + ")");
        term("unreachable");
    }

    void panicIf(const std::string& condition, const std::string& reason, SourceLocation loc) {
        std::string fail = label("panic");
        std::string ok = label("ok");
        term("br i1 " + condition + ", label %" + fail + ", label %" + ok);
        block(fail);
        panic(reason, loc);
        block(ok);
    }

    // Writes the default value of a type: its constant, then the initial values of
    // the fields that are computed (D360).
    void storeDefault(const std::string& address, Type type) {
        storeConstant(address, type);
        if (computedDefault(type)) fillDefault(address, type);
    }

    // A large value is not written as one constant, which LLVM would split into a
    // store for each element.
    void storeConstant(const std::string& address, Type type) {
        if (sizeOf(type) > 64 && zeroDefault(type)) {
            std::string fn = intrinsic("declare void @llvm.memset.p0.i64(ptr, i8, i64, i1)", "@llvm.memset.p0.i64");
            inst("call void " + fn + "(ptr " + address + ", i8 0, i64 " + std::to_string(sizeOf(type)) + ", i1 false)");
        } else if (type.isArray() && sizeOf(type) > 64) {
            forEachElement(address, type, false, [&](const std::string& element) { storeConstant(element, type.array->element); });
        } else {
            inst("store " + llvmType(type) + " " + defaultConstant(type) + ", ptr " + address);
        }
    }

    // A struct with initial values that are not literals, or that holds one (D360).
    bool computedDefault(Type type) {
        if (type.isArray()) return computedDefault(type.array->element);
        if (!type.isStruct()) return false;
        const StructDecl& decl = *type.structDecl;
        auto found = computedDefaults_.find(&decl);
        if (found != computedDefaults_.end()) return found->second;
        computedDefaults_[&decl] = false;  // a struct does not hold itself (D11)
        bool computed = decl.defaultFunction != nullptr;
        const std::vector<FieldDecl>& fields = decl.isEnum ? decl.cases[0].fields : decl.fields;
        for (const FieldDecl& field : fields) computed = computedDefault(field.resolved) || computed;
        computedDefaults_[&decl] = computed;
        return computed;
    }

    // The structs inside first, then the initial values of the struct, in the order
    // of its fields.
    void fillDefault(const std::string& address, Type type) {
        if (type.isArray()) {
            forEachElement(address, type, false, [&](const std::string& element) { fillDefault(element, type.array->element); });
            return;
        }
        const StructDecl& decl = *type.structDecl;
        if (decl.isEnum) {  // the first case, with the defaults of its data (D111)
            const EnumCase& item = decl.cases[0];
            for (size_t i = 0; i < item.fields.size(); ++i) {
                if (computedDefault(item.fields[i].resolved)) {
                    fillDefault(caseField(enumPayload(address, decl), decl, item, i), item.fields[i].resolved);
                }
            }
            return;
        }
        for (size_t i = 0; i < decl.fields.size(); ++i) {
            if (computedDefault(decl.fields[i].resolved)) {
                fillDefault(fieldAddress(address, decl, static_cast<int>(i)), decl.fields[i].resolved);
            }
        }
        if (decl.defaultFunction) {
            const FunctionDecl& function = *decl.defaultFunction;
            inst("call void " + symbolOf(function) + "(ptr " + address + (tracksCaller(function) ? ", ptr null" : "") + ")");
        }
    }

    // Runs `body` with the address of each element of the array at `array`, in a loop.
    template <typename Body>
    void forEachElement(const std::string& array, Type type, bool reverse, Body body) {
        Type i64 = Type::intType(64, true);
        int64_t count = type.array->count;
        std::string counter = slot(i64);
        inst("store i64 " + std::to_string(reverse ? count - 1 : 0) + ", ptr " + counter);
        std::string loop = label("elements");
        std::string done = label("elements.end");
        block(loop);
        Value index = load(counter, i64);
        std::string address = temp();
        inst(address + " = getelementptr inbounds " + llvmType(type) + ", ptr " + array + ", i64 0, i64 " + index.ref);
        body(address);
        std::string next = temp();
        std::string more = temp();
        inst(next + " = " + (reverse ? "sub" : "add") + " i64 " + index.ref + ", 1");
        inst("store i64 " + next + ", ptr " + counter);
        inst(more + " = icmp " + (reverse ? "sge i64 " + next + ", 0" : "slt i64 " + next + ", " + std::to_string(count)));
        term("br i1 " + more + ", label %" + loop + ", label %" + done);
        block(done);
    }

    void copyStruct(const std::string& dest, const std::string& source, Type type) {
        std::string fn = intrinsic("declare void @llvm.memcpy.p0.p0.i64(ptr, ptr, i64, i1)", "@llvm.memcpy.p0.p0.i64");
        inst("call void " + fn + "(ptr " + dest + ", ptr " + source + ", i64 " + std::to_string(sizeOf(type)) + ", i1 false)");
    }

    std::string fieldAddress(const std::string& object, const StructDecl& decl, int index) {
        std::string result = temp();
        inst(result + " = getelementptr inbounds " + structName(decl) + ", ptr " + object + ", i32 0, i32 " +
             std::to_string(index));
        return result;
    }

    // ---- Functions ----

    void startFunction(const FunctionDecl* function) {
        body_.str("");
        body_.clear();
        allocas_.str("");
        allocas_.clear();
        nextTemp_ = 0;
        nextLabel_ = 0;
        nextSlot_ = 0;
        terminated_ = false;
        current_ = "entry";
        function_ = function;
        scopes_.clear();
        temporaries_.clear();
        loops_.clear();
        addresses_.assign(function ? function->variables.size() : 0, "");
        flags_.assign(addresses_.size(), "");
        dropFields_.clear();
        tracksCaller_ = false;
        localRaised_.clear();
        catches_.clear();
        subprogram_.clear();
        debugScopes_.clear();
        if (function) {
            location_ = function->loc;
            subprogram_ = subprogramNode(debugName(*function), function->loc);
        }
    }

    std::string finishFunction(const std::string& returnType, const std::string& symbol, const std::vector<std::string>& params) {
        std::ostringstream out;
        // Internal: a function that nothing calls is removed (backend.cpp), with
        // the C code that only it uses, as mbedTLS for connectTls (D241).
        out << "define internal " << returnType << " " << symbol << "(";
        for (size_t i = 0; i < params.size(); ++i) out << (i ? ", " : "") << params[i];
        out << ") " << functionAttributes();
        if (!subprogram_.empty()) out << " !dbg " << subprogram_;
        out << " {\nentry:\n" << allocas_.str() << body_.str() << "}\n\n";
        return out.str();
    }

    // D341: the tables of unwinding for Windows, the frame pointers for Linux.
    std::string functionAttributes() const {
        return options_.target == Target::Linux ? "nounwind uwtable \"frame-pointer\"=\"all\"" : "nounwind uwtable";
    }

    std::string generateFunction(const FunctionDecl& function) {
        startFunction(&function);

        // The exclusivity rules (D22, D79) make every address parameter `noalias`.
        Type ret = function.resolvedReturn;
        std::vector<std::string> params;
        if (byPointer(ret)) params.push_back("ptr noalias sret(" + llvmType(ret) + ") %ret");
        if (function.owner && function.owner->addressed) {  // other threads write it too (D216)
            params.push_back("ptr %self");
        } else if (function.owner) {
            params.push_back(std::string("ptr noalias ") + (function.isConst ? "readonly " : "") + "%self");
        }
        if (function.isLambda) params.push_back("ptr %env");  // the captured data (D183)
        for (size_t i = 0; i < function.paramTypes.size(); ++i) {
            const Param& param = function.params[i];
            Type type = function.paramTypes[i];
            std::string arg = "%arg." + std::to_string(i);
            if (byAddress(param, type) && isAddressed(type)) {  // a mutex is written by other threads (D216)
                params.push_back("ptr " + arg);
            } else if (byAddress(param, type)) {
                params.push_back(std::string("ptr noalias ") + (param.mode == ParamMode::Read ? "readonly " : "") + arg);
            } else {
                params.push_back(llvmType(type) + " " + arg);
            }
        }
        tracksCaller_ = tracksCaller(function);
        if (tracksCaller_) params.push_back("ptr %caller");
        if (function.canThrow) params.push_back("ptr %raised");  // where an exception goes (D36)

        // Every variable gets a stack slot; mem2reg turns them into registers.
        // A parameter passed by address is used through the pointer it arrives with.
        for (size_t i = 0; i < function.variables.size(); ++i) {
            const Variable& variable = *function.variables[i];
            // The element of a `for` and the data of a case are where they are in memory.
            if (variable.kind == VariableKind::Element || variable.kind == VariableKind::Binding) continue;
            if (variable.kind == VariableKind::Caught && !variable.type.isInterface()) continue;  // the thrown object
            if (variable.kind == VariableKind::Captured) {
                // In the data of the lambda: the value, or the address of the variable outside.
                std::string field = "%capture." + std::to_string(variable.captureIndex);
                inst(field + " = getelementptr inbounds " + environmentType(function) + ", ptr %env, i32 0, i32 " +
                     std::to_string(variable.captureIndex + 1));
                if (variable.captureByAddress) {
                    addresses_[variable.id] = temp();
                    inst(addresses_[variable.id] + " = load ptr, ptr " + field);
                } else {
                    addresses_[variable.id] = field;
                }
                continue;
            }
            std::string name = "%v" + std::to_string(variable.id) + "." + variable.name;
            if (variable.needsDropFlag) {
                flags_[variable.id] = name + ".alive";
                allocas_ << "  " << flags_[variable.id] << " = alloca i1\n";
                inst("store i1 " + std::string(variable.kind == VariableKind::MoveParam ? "true" : "false") + ", ptr " +
                     flags_[variable.id]);
            }
            if (i < function.paramTypes.size() && byAddress(function.params[i], variable.type)) {
                addresses_[variable.id] = "%arg." + std::to_string(i);
                continue;
            }
            addresses_[variable.id] = name;
            allocas_ << "  " << name << " = alloca " << llvmType(variable.type) << "\n";
        }
        for (size_t i = 0; i < function.paramTypes.size(); ++i) {
            if (byAddress(function.params[i], function.paramTypes[i])) continue;
            inst("store " + llvmType(function.paramTypes[i]) + " %arg." + std::to_string(i) + ", ptr " +
                 addresses_[function.variables[i]->id]);
        }
        if (function.owner) declareSelf(*function.owner);
        for (size_t i = 0; i < function.variables.size(); ++i) {
            const Variable& variable = *function.variables[i];
            if (i < function.paramTypes.size()) {
                declareVariable(variable, addresses_[variable.id], static_cast<int>(i) + (function.owner ? 2 : 1));
            } else if (variable.kind == VariableKind::Captured) {
                declareVariable(variable, addresses_[variable.id]);
            }
        }

        // The function owns its `move` parameters and destroys them (D22).
        scopes_.emplace_back();
        for (size_t i = 0; i < function.paramTypes.size(); ++i) {
            const Variable& variable = *function.variables[i];
            if (variable.kind == VariableKind::MoveParam && needsDestruction(variable.type)) {
                scopes_.back().objects.push_back(LiveObject{addresses_[variable.id], variable.type, &variable});
            }
        }

        if (function.kind == FunctionKind::Constructor) {
            // The constructor starts from the default value (D48).
            storeDefault("%self", Type::structType(function.owner));
        }
        if (function.kind == FunctionKind::Destructor) dropFields_ = label("drop.fields");

        generateBlock(function.body);
        if (!terminated_) dropObjects(scopes_.back().objects);

        if (!dropFields_.empty()) {
            // D68: after the body of the destructor, the fields are destroyed.
            block(dropFields_);
            dropFieldsOf(*function.owner);
            term("ret void");
        } else if (!terminated_) {
            term(ret.isVoid() || byPointer(ret) ? "ret void" : "unreachable");
        }
        return finishFunction(byPointer(ret) ? "void" : llvmType(ret), symbolOf(function), params);
    }

    void dropFieldsOf(const StructDecl& decl) {
        for (size_t i = decl.fields.size(); i-- > 0;) {
            Type type = decl.fields[i].resolved;
            if (needsDestruction(type)) drop(fieldAddress("%self", decl, static_cast<int>(i)), type);
        }
    }

    // `T..drop` of a struct with no destructor whose fields need destruction.
    std::string generateFieldsDrop(const StructDecl& decl) {
        startFunction(nullptr);
        if (decl.isEnum) {
            // The data of the current case (D111).
            forEachCase(decl, "%self", [&](const EnumCase& item, const std::string& payload) {
                for (size_t i = item.fields.size(); i-- > 0;) {
                    Type type = item.fields[i].resolved;
                    if (needsDestruction(type)) drop(caseField(payload, decl, item, i), type);
                }
            });
        } else {
            dropFieldsOf(decl);
        }
        term("ret void");
        return finishFunction("void", dropSymbol(decl), {"ptr noalias %self"});
    }

    std::string enumTag(const std::string& address, const StructDecl& decl) {
        std::string result = temp();
        inst(result + " = getelementptr inbounds " + structName(decl) + ", ptr " + address + ", i32 0, i32 1");
        return result;
    }

    std::string enumPayload(const std::string& address, const StructDecl& decl) {
        std::string result = temp();
        inst(result + " = getelementptr inbounds i8, ptr " + address + ", i64 " + std::to_string(decl.payloadOffset));
        return result;
    }

    std::string caseField(const std::string& payload, const StructDecl& decl, const EnumCase& item, size_t index) {
        std::string result = temp();
        inst(result + " = getelementptr inbounds " + caseTypeName(decl, item) + ", ptr " + payload + ", i32 0, i32 " +
             std::to_string(index));
        return result;
    }

    // Runs `body` for the current case of the enum at `address`, with the address
    // of its data; only for the cases that have data.
    template <typename Body>
    void forEachCase(const StructDecl& decl, const std::string& address, Body body) {
        Value tag = load(enumTag(address, decl), Type::intType(32, true));
        std::string done = label("case.done");
        std::vector<std::string> labels(decl.cases.size());
        std::string table;
        for (size_t i = 0; i < decl.cases.size(); ++i) {
            if (decl.cases[i].fields.empty()) continue;
            labels[i] = label("case");
            table += " i32 " + std::to_string(decl.cases[i].value) + ", label %" + labels[i];
        }
        term("switch i32 " + tag.ref + ", label %" + done + " [" + table + " ]");
        for (size_t i = 0; i < decl.cases.size(); ++i) {
            if (labels[i].empty()) continue;
            block(labels[i]);
            body(decl.cases[i], enumPayload(address, decl));
            term("br label %" + done);
        }
        block(done);
    }

    // `copy()` generated by the compiler (D35): the simple fields are copied, the
    // others with their own copy().
    std::string generateCopy(const StructDecl& decl) {
        startFunction(nullptr);
        Type type = Type::structType(&decl);
        std::string dest = byPointer(type) ? "%ret" : slot(type);
        if (decl.isEnum) {
            // The tag, then the data of the current case (D111).
            copyStruct(dest, "%self", type);
            if (decl.needsDestruction) {
                forEachCase(decl, "%self", [&](const EnumCase& item, const std::string& payload) {
                    std::string target = enumPayload(dest, decl);
                    for (size_t i = 0; i < item.fields.size(); ++i) {
                        Type field = item.fields[i].resolved;
                        if (!needsDestruction(field)) continue;
                        emitCall(*field.structDecl->methods.at("copy"), caseField(payload, decl, item, i), field, {},
                                 caseField(target, decl, item, i), decl.loc);
                    }
                });
            }
        }
        for (size_t i = 0; i < decl.fields.size(); ++i) {
            Type field = decl.fields[i].resolved;
            std::string source = fieldAddress("%self", decl, static_cast<int>(i));
            std::string target = fieldAddress(dest, decl, static_cast<int>(i));
            if (needsDestruction(field)) {
                emitCall(*field.structDecl->methods.at("copy"), source, field, {}, target, decl.loc);
            } else if (field.isAggregate()) {
                copyStruct(target, source, field);
            } else {
                inst("store " + llvmType(field) + " " + load(source, field).ref + ", ptr " + target);
            }
        }
        std::vector<std::string> params = {"ptr noalias readonly %self"};
        if (byPointer(type)) {
            params.insert(params.begin(), "ptr noalias sret(" + llvmType(type) + ") %ret");
            term("ret void");
            return finishFunction("void", methodSymbol(decl, "copy"), params);
        }
        term("ret " + llvmType(type) + " " + load(dest, type).ref);
        return finishFunction(llvmType(type), methodSymbol(decl, "copy"), params);
    }

    std::string entryPoint(const FunctionDecl& mainFunction) {
        std::ostringstream out;
        // A subprogram of its own, so that main keeps its lines when it is inlined
        // here; the stack traces leave it out (backend.cpp).
        std::string start = subprogramNode("cromo_start", mainFunction.loc);
        std::string dbg = ", !dbg " + metadata("!DILocation(line: " + std::to_string(mainFunction.loc.line) +
                                               ", scope: " + start + ")");
        out << "define void @cromo_start() noreturn " << functionAttributes() << " !dbg " << start << " {\n";
        out << "entry:\n";
        out << "  call void @cromo_runtime_start()" << dbg << "\n";
        std::vector<std::string> args;
        if (!mainFunction.params.empty()) {
            // D154: the arguments of the program, made by the standard library.
            const FunctionDecl& arguments = *programArguments_;
            std::string type = llvmType(arguments.resolvedReturn);
            out << "  %args = alloca " << type << "\n";
            out << "  call void " << symbolOf(arguments) << "(ptr %args" << (tracksCaller(arguments) ? ", ptr null" : "")
                << ")" << dbg << "\n";
            args.push_back("ptr %args");
        }
        if (mainFunction.canThrow) {
            out << "  %raised = alloca { ptr, ptr }\n";
            out << "  store { ptr, ptr } zeroinitializer, ptr %raised\n";
            args.push_back("ptr %raised");
        }
        std::string list;
        for (size_t i = 0; i < args.size(); ++i) list += (i ? ", " : "") + args[i];
        if (mainFunction.resolvedReturn.isVoid()) {
            out << "  call void " << symbolOf(mainFunction) << "(" << list << ")" << dbg << "\n";
        } else {
            out << "  %code = call i32 " << symbolOf(mainFunction) << "(" << list << ")" << dbg << "\n";
        }
        std::string code = mainFunction.resolvedReturn.isVoid() ? "0" : "%code";
        if (mainFunction.canThrow && exceptionInterface_) {
            // D121: an exception that leaves main stops the program with its message.
            const FunctionDecl& message = exceptionInterface_->methods[0];
            const StructDecl& text = *message.resolvedReturn.structDecl;
            out << "  %object = load ptr, ptr %raised\n";
            out << "  %threw = icmp ne ptr %object, null\n";
            out << "  br i1 %threw, label %uncaught, label %done\n";
            out << "uncaught:\n";
            out << "  %descriptorAddress = getelementptr inbounds { ptr, ptr }, ptr %raised, i32 0, i32 1\n";
            out << "  %descriptor = load ptr, ptr %descriptorAddress\n";
            out << "  %tableAddress = getelementptr inbounds { ptr, ptr }, ptr %descriptor, i32 0, i32 1\n";
            out << "  %table = load ptr, ptr %tableAddress\n";
            out << "  %method = load ptr, ptr %table\n";
            out << "  %text = alloca " << structName(text) << "\n";
            out << "  call void %method(ptr %text, ptr %object)" << dbg << "\n";
            out << "  %dataAddress = getelementptr inbounds " << structName(text) << ", ptr %text, i32 0, i32 "
                << fieldIndexOf(text, "data") << "\n";
            out << "  %data = load ptr, ptr %dataAddress\n";
            out << "  %lengthAddress = getelementptr inbounds " << structName(text) << ", ptr %text, i32 0, i32 "
                << fieldIndexOf(text, "length") << "\n";
            out << "  %length = load i64, ptr %lengthAddress\n";
            out << "  call void @cromo_uncaught(ptr %data, i64 %length, ptr %object)" << dbg << "\n";
            out << "  unreachable\n";
            out << "done:\n";
        }
        out << "  call void @cromo_exit(i32 " << code << ")\n";
        out << "  unreachable\n";
        out << "}\n\n";
        return out.str();
    }

    // The tests of the program, in the order of cromo test (main.cpp): those of its
    // files, not of the library (D349, D350).
    static std::vector<const FunctionDecl*> testFunctions(const Module& module) {
        std::vector<const FunctionDecl*> tests;
        for (const FunctionDecl& function : module.functions) {
            if (function.isTest && !module.files[function.loc.file].isLibrary) tests.push_back(&function);
        }
        return tests;
    }

    // D352: the executable of cromo test runs one test, by its number in the first
    // argument; an exception that leaves it stops the program as for main (D121).
    std::string testEntryPoint(const std::vector<const FunctionDecl*>& tests) {
        std::ostringstream out;
        SourceLocation loc = tests.empty() ? SourceLocation{} : tests[0]->loc;
        std::string start = subprogramNode("cromo_start", loc);
        std::string dbg = ", !dbg " + metadata("!DILocation(line: " + std::to_string(loc.line) + ", scope: " + start + ")");
        out << "define void @cromo_start() noreturn " << functionAttributes() << " !dbg " << start << " {\n";
        out << "entry:\n";
        out << "  %raised = alloca { ptr, ptr }\n";
        out << "  store { ptr, ptr } zeroinitializer, ptr %raised\n";
        out << "  call void @cromo_runtime_start()" << dbg << "\n";
        out << "  %index = call i64 @cromo_test_index()" << dbg << "\n";
        out << "  switch i64 %index, label %none [";
        for (size_t i = 0; i < tests.size(); ++i) out << " i64 " << i << ", label %test." << i;
        out << " ]\n";
        for (size_t i = 0; i < tests.size(); ++i) {
            const FunctionDecl& test = *tests[i];
            out << "test." << i << ":\n";
            out << "  call void " << symbolOf(test) << "(" << (test.canThrow ? "ptr %raised" : "") << ")" << dbg << "\n";
            out << "  br label %done\n";
        }
        out << "done:\n";
        if (exceptionInterface_) {
            const FunctionDecl& message = exceptionInterface_->methods[0];
            const StructDecl& text = *message.resolvedReturn.structDecl;
            out << "  %object = load ptr, ptr %raised\n";
            out << "  %threw = icmp ne ptr %object, null\n";
            out << "  br i1 %threw, label %uncaught, label %passed\n";
            out << "uncaught:\n";
            out << "  %descriptorAddress = getelementptr inbounds { ptr, ptr }, ptr %raised, i32 0, i32 1\n";
            out << "  %descriptor = load ptr, ptr %descriptorAddress\n";
            out << "  %tableAddress = getelementptr inbounds { ptr, ptr }, ptr %descriptor, i32 0, i32 1\n";
            out << "  %table = load ptr, ptr %tableAddress\n";
            out << "  %method = load ptr, ptr %table\n";
            out << "  %text = alloca " << structName(text) << "\n";
            out << "  call void %method(ptr %text, ptr %object)" << dbg << "\n";
            out << "  %dataAddress = getelementptr inbounds " << structName(text) << ", ptr %text, i32 0, i32 "
                << fieldIndexOf(text, "data") << "\n";
            out << "  %data = load ptr, ptr %dataAddress\n";
            out << "  %lengthAddress = getelementptr inbounds " << structName(text) << ", ptr %text, i32 0, i32 "
                << fieldIndexOf(text, "length") << "\n";
            out << "  %length = load i64, ptr %lengthAddress\n";
            out << "  call void @cromo_uncaught(ptr %data, i64 %length, ptr %object)" << dbg << "\n";
            out << "  unreachable\n";
            out << "passed:\n";
        }
        out << "  call void @cromo_exit(i32 0)\n";
        out << "  unreachable\n";
        out << "none:\n";  // no test with that number: cromo test asks only for those it knows
        out << "  call void @cromo_exit(i32 2)\n";
        out << "  unreachable\n";
        out << "}\n\n";
        return out.str();
    }

    // Destroys the object of an exception and frees its memory, through the
    // descriptor that travels with it.
    static std::string freeExceptionFunction() {
        return "define private void @cromo.exception.free(ptr %box) nounwind {\n"
               "entry:\n"
               "  %object = load ptr, ptr %box\n"
               "  %descriptorAddress = getelementptr inbounds { ptr, ptr }, ptr %box, i32 0, i32 1\n"
               "  %descriptor = load ptr, ptr %descriptorAddress\n"
               "  %drop = load ptr, ptr %descriptor\n"
               "  %destroy = icmp ne ptr %drop, null\n"
               "  br i1 %destroy, label %drop.object, label %free\n"
               "drop.object:\n"
               "  call void %drop(ptr %object)\n"
               "  br label %free\n"
               "free:\n"
               "  call void @cromo_exception_free(ptr %object)\n"
               "  ret void\n"
               "}\n\n";
    }

    // ---- Lifetimes (D68) ----

    void drop(const std::string& address, Type type) {
        if (type.isArray()) {  // the elements, the last first (D68, D196)
            forEachElement(address, type, true, [&](const std::string& element) { drop(element, type.array->element); });
            return;
        }
        if (type.isFunction()) {
            // The data of a lambda, if it has any, go with the function that the lambda
            // wrote in them (D184).
            std::string value = temp();
            std::string env = temp();
            std::string owned = temp();
            std::string release = label("release");
            std::string done = label("released");
            inst(value + " = load { ptr, ptr }, ptr " + address);
            inst(env + " = extractvalue { ptr, ptr } " + value + ", 1");
            inst(owned + " = icmp ne ptr " + env + ", null");
            term("br i1 " + owned + ", label %" + release + ", label %" + done);
            block(release);
            std::string destroy = temp();
            std::string kept = temp();
            std::string call = label("release.call");
            inst(destroy + " = load ptr, ptr " + env);
            inst(kept + " = icmp eq ptr " + destroy + ", null");  // data in a frame that own nothing (D215)
            term("br i1 " + kept + ", label %" + done + ", label %" + call);
            block(call);
            inst("call void " + destroy + "(ptr " + env + ")");
            term("br label %" + done);
            block(done);
            return;
        }
        if (type.isInterface()) {
            // D357: the object, if there is one, through the release at the end of its table.
            std::string value = temp();
            std::string object = temp();
            std::string owned = temp();
            std::string release = label("release");
            std::string done = label("released");
            inst(value + " = load { ptr, ptr }, ptr " + address);
            inst(object + " = extractvalue { ptr, ptr } " + value + ", 0");
            inst(owned + " = icmp ne ptr " + object + ", null");
            term("br i1 " + owned + ", label %" + release + ", label %" + done);
            block(release);
            std::string table = temp();
            std::string entry = temp();
            std::string function = temp();
            inst(table + " = extractvalue { ptr, ptr } " + value + ", 1");
            inst(entry + " = getelementptr inbounds ptr, ptr " + table + ", i64 " +
                 std::to_string(type.interfaceDecl->methods.size()));
            inst(function + " = load ptr, ptr " + entry);
            inst("call void " + function + "(ptr " + object + ")");
            term("br label %" + done);
            block(done);
            return;
        }
        inst("call void " + dropSymbol(*type.structDecl) + "(ptr " + address + ")");
    }

    // Destroys a variable, checking its drop flag if it may have been moved (D77).
    void dropVariable(const std::string& address, Type type, const Variable* variable) {
        dropObject(LiveObject{address, type, variable});
    }

    void dropObject(const LiveObject& object) {
        std::string flag = object.flag;
        if (flag.empty() && object.variable && object.variable->needsDropFlag) flag = flags_[object.variable->id];
        std::string done;
        if (!flag.empty()) {
            std::string alive = temp();
            std::string yes = label("drop");
            done = label("dropped");
            inst(alive + " = load i1, ptr " + flag);
            term("br i1 " + alive + ", label %" + yes + ", label %" + done);
            block(yes);
        }
        if (object.isException) {
            freesExceptions_ = true;
            inst("call void @cromo.exception.free(ptr " + object.address + ")");
        } else {
            drop(object.address, object.type);
        }
        if (!done.empty()) block(done);
    }

    void setAlive(const Variable& variable, bool alive) {
        if (variable.needsDropFlag) inst(std::string("store i1 ") + (alive ? "true" : "false") + ", ptr " + flags_[variable.id]);
    }

    // Destroys objects in reverse order, except `kept` (moved by `ret`).
    void dropObjects(const std::vector<LiveObject>& objects, const Variable* kept = nullptr) {
        for (auto object = objects.rbegin(); object != objects.rend(); ++object) {
            if (object->variable && object->variable == kept) continue;
            dropObject(*object);
        }
    }

    // Leaves the scope at `index`: destroys its objects and runs its `finally` (D16).
    void leaveScope(size_t index, const Variable* kept) {
        Scope scope = scopes_[index];  // a copy: running finally opens scopes
        dropObjects(scope.objects, kept);
        if (scope.finallyBlock) generateFinally(*scope.finallyBlock, index);
    }

    // Runs a `finally` block where the code leaves its `try`. It cannot jump out or
    // let an exception out (D113, D120), so it runs as if the scopes from `index` on
    // were already closed.
    void generateFinally(const Block& finallyBlock, size_t index) {
        std::vector<Scope> inner(scopes_.begin() + static_cast<std::ptrdiff_t>(index), scopes_.end());
        std::vector<LiveObject> temporaries = std::move(temporaries_);
        scopes_.resize(index);
        temporaries_.clear();
        generateBlock(finallyBlock);
        scopes_.insert(scopes_.end(), inner.begin(), inner.end());
        temporaries_ = std::move(temporaries);
    }

    // Destroys the temporaries created since `mark`.
    void dropTemporaries(size_t mark) {
        if (temporaries_.size() <= mark) return;
        if (!terminated_) {
            for (size_t i = temporaries_.size(); i-- > mark;) dropObject(temporaries_[i]);
        }
        temporaries_.resize(mark);
    }

    // ---- Exceptions (D36) ----

    // Where the exceptions of this function go: the slot of the caller, or one of
    // its own when nothing can leave it.
    std::string raisedSlot() {
        if (function_ && function_->canThrow) return "%raised";
        if (localRaised_.empty()) {
            localRaised_ = "%raised.local";
            allocas_ << "  %raised.local = alloca { ptr, ptr }\n";
            allocas_ << "  store { ptr, ptr } zeroinitializer, ptr %raised.local\n";
        }
        return localRaised_;
    }

    // After a call that can throw: an object in the slot is an exception.
    void checkRaised() {
        std::string object = temp();
        std::string raised = temp();
        inst(object + " = load ptr, ptr " + raisedSlot());
        inst(raised + " = icmp ne ptr " + object + ", null");
        std::string unwind = label("unwind");
        std::string ok = label("ok");
        term("br i1 " + raised + ", label %" + unwind + ", label %" + ok);
        block(unwind);
        emitPropagation();
        block(ok);
    }

    // An exception is in the slot: leave the scopes up to the nearest `try` body,
    // or out of the function. Nothing is popped: the code goes on after it.
    void emitPropagation() {
        for (size_t i = temporaries_.size(); i-- > 0;) dropObject(temporaries_[i]);
        for (size_t i = scopes_.size(); i-- > 0;) {
            if (!scopes_[i].handler.empty()) {
                term("br label %" + scopes_[i].handler);
                return;
            }
            leaveScope(i, nullptr);
        }
        // Out of the function, with the exception still in the slot of the caller.
        // A constructor destroys the fields of its object (D16, D123).
        if (function_ && function_->kind == FunctionKind::Constructor) dropFieldsOf(*function_->owner);
        Type ret = function_ ? function_->resolvedReturn : Type::voidType();
        if (ret.isVoid() || byPointer(ret)) {
            term("ret void");
        } else {
            term("ret " + llvmType(ret) + " " + defaultConstant(ret));
        }
    }

    // What a thrown struct carries with it: its destructor, to free it, and its
    // methods of Exception, for `catch (Exception e)`. Its address also tells the
    // `catch` clauses its type.
    std::string exceptionDescriptor(const StructDecl& decl) {
        std::string name = llvmName('@', "exception." + decl.symbol);
        if (!tables_.insert(name).second) return name;
        std::string table = methodTable(decl, *exceptionInterface_);  // first: it writes a global of its own
        std::string destroy = decl.needsDestruction ? dropSymbol(decl) : "null";
        globals_ << name << " = private unnamed_addr constant { ptr, ptr } { ptr " << destroy << ", ptr " << table << " }\n";
        return name;
    }

    // `throw x`: the object goes to the heap, with its descriptor, into the slot.
    // `throw` alone gives the exception of the `catch` back (D114).
    void generateThrow(const ThrowStmt& statement) {
        std::string target = raisedSlot();
        if (!statement.value) {
            const Caught& caught = catches_.back();
            std::string pair = temp();
            inst(pair + " = load { ptr, ptr }, ptr " + caught.box);
            inst("store { ptr, ptr } " + pair + ", ptr " + target);
            inst("store i1 false, ptr " + caught.flag);
            emitPropagation();
            return;
        }
        Type type = statement.value->type;
        std::string object = temp();
        inst(object + " = call ptr @cromo_exception_new(i64 " + std::to_string(std::max(sizeOf(type), 1)) + ")");
        generateStructInto(*statement.value, object);
        std::string descriptor = exceptionDescriptor(*type.structDecl);
        std::string first = temp();
        std::string pair = temp();
        inst(first + " = insertvalue { ptr, ptr } undef, ptr " + object + ", 0");
        inst(pair + " = insertvalue { ptr, ptr } " + first + ", ptr " + descriptor + ", 1");
        inst("store { ptr, ptr } " + pair + ", ptr " + target);
        emitPropagation();
    }

    // `try`, `catch` and `finally` (D16): the exceptions of the body go to a
    // dispatch that compares their descriptor with the type of each `catch`.
    void generateTry(const TryStmt& statement) {
        const Block* finallyBlock = statement.finallyBlock.get();
        std::string end = label("try.end");
        std::string dispatch = statement.catches.empty() ? "" : label("try.dispatch");

        scopes_.push_back(Scope{{}, finallyBlock, dispatch});
        generateBlock(statement.body);
        scopes_.pop_back();
        if (!terminated_) {
            if (finallyBlock) generateFinally(*finallyBlock, scopes_.size());
            term("br label %" + end);
        }
        if (dispatch.empty()) {
            block(end);
            return;
        }

        block(dispatch);
        size_t frame = scopes_.size();
        scopes_.push_back(Scope{{}, finallyBlock, ""});  // leaving a catch runs finally
        std::string slotAddress = raisedSlot();
        std::string descriptorAddress = temp();
        std::string descriptor = temp();
        inst(descriptorAddress + " = getelementptr inbounds { ptr, ptr }, ptr " + slotAddress + ", i32 0, i32 1");
        inst(descriptor + " = load ptr, ptr " + descriptorAddress);
        std::vector<std::string> labels;
        bool catchesAll = false;
        for (const CatchClause& clause : statement.catches) {
            labels.push_back(label("catch"));
            if (clause.resolved.isInterface()) {
                term("br label %" + labels.back());
                catchesAll = true;
                break;
            }
            std::string match = temp();
            std::string next = label("catch.next");
            inst(match + " = icmp eq ptr " + descriptor + ", " + exceptionDescriptor(*clause.resolved.structDecl));
            term("br i1 " + match + ", label %" + labels.back() + ", label %" + next);
            block(next);
        }
        if (!catchesAll) emitPropagation();  // no catch matches: on, through finally

        for (size_t i = 0; i < labels.size(); ++i) {
            const CatchClause& clause = statement.catches[i];
            block(labels[i]);
            // The catch takes the exception out of the slot, and owns it (D114).
            std::string box = "%caught." + std::to_string(nextSlot_++);
            std::string flag = box + ".alive";
            allocas_ << "  " << box << " = alloca { ptr, ptr }\n";
            allocas_ << "  " << flag << " = alloca i1\n";
            std::string pair = temp();
            inst(pair + " = load { ptr, ptr }, ptr " + slotAddress);
            inst("store { ptr, ptr } " + pair + ", ptr " + box);
            inst("store { ptr, ptr } zeroinitializer, ptr " + slotAddress);
            inst("store i1 true, ptr " + flag);
            scopes_.emplace_back();
            scopes_.back().objects.push_back(LiveObject{box, Type::error(), nullptr, flag, true});
            if (clause.variable) {
                std::string object = temp();
                inst(object + " = extractvalue { ptr, ptr } " + pair + ", 0");
                if (clause.resolved.isInterface()) {
                    // An interface value: the object with its methods of Exception (D49).
                    std::string described = temp();
                    std::string tableAddress = temp();
                    std::string table = temp();
                    std::string first = temp();
                    std::string value = temp();
                    inst(described + " = extractvalue { ptr, ptr } " + pair + ", 1");
                    inst(tableAddress + " = getelementptr inbounds { ptr, ptr }, ptr " + described + ", i32 0, i32 1");
                    inst(table + " = load ptr, ptr " + tableAddress);
                    inst(first + " = insertvalue { ptr, ptr } undef, ptr " + object + ", 0");
                    inst(value + " = insertvalue { ptr, ptr } " + first + ", ptr " + table + ", 1");
                    inst("store { ptr, ptr } " + value + ", ptr " + addresses_[clause.variable->id]);
                } else {
                    addresses_[clause.variable->id] = object;
                }
            }
            if (clause.variable) declareVariable(*clause.variable, addresses_[clause.variable->id]);
            catches_.push_back(Caught{box, flag});
            generateBlock(clause.body);
            catches_.pop_back();
            if (!terminated_) dropObjects(scopes_.back().objects);
            scopes_.pop_back();
            if (!terminated_) {
                if (finallyBlock) generateFinally(*finallyBlock, frame);
                term("br label %" + end);
            }
        }
        scopes_.pop_back();
        block(end);
    }

    // ---- Statements ----

    void generateBlock(const Block& block) {
        bool scoped = !subprogram_.empty() && !options_.release;
        if (scoped) {  // the variables of a block, for a debugger (D338)
            debugScopes_.push_back(metadata("distinct !DILexicalBlock(scope: " + debugScope() + ", file: " +
                                            fileNode(block.open.file) + ", line: " + std::to_string(block.open.line) +
                                            ", column: " + std::to_string(block.open.column) + ")"));
        }
        scopes_.emplace_back();
        for (const StmtPtr& statement : block.statements) generateStatement(*statement);
        if (!terminated_) dropObjects(scopes_.back().objects);
        scopes_.pop_back();
        if (scoped) debugScopes_.pop_back();
    }

    void generateStatement(const Stmt& statement) {
        size_t mark = temporaries_.size();
        location_ = statement.loc;
        switch (statement.kind) {
        case StmtKind::VarDecl: generateVarDecl(static_cast<const VarDeclStmt&>(statement)); break;
        case StmtKind::Assign: generateAssign(static_cast<const AssignStmt&>(statement)); break;
        case StmtKind::IncDec: {
            const auto& incDec = static_cast<const IncDecStmt&>(statement);
            std::string address = addressOf(*incDec.target);
            Type type = incDec.target->type;
            Value current = load(address, type);
            Value value = arithmetic(incDec.increment ? BinaryOp::Add : BinaryOp::Sub, current, Value{"1", type}, incDec.loc);
            inst("store " + llvmType(type) + " " + value.ref + ", ptr " + address);
            break;
        }
        case StmtKind::Expr: {
            const Expr& expr = *static_cast<const ExprStmt&>(statement).expr;
            if (expr.type.isAggregate() || expr.type.isFunction() || expr.type.isInterface()) {
                addressOf(expr);  // a temporary, destroyed below
            } else {
                generateExpr(expr);
            }
            break;
        }
        case StmtKind::If: generateIf(static_cast<const IfStmt&>(statement)); break;
        case StmtKind::For: generateFor(static_cast<const ForStmt&>(statement)); break;
        case StmtKind::Break:
        case StmtKind::Continue: {
            const Loop& loop = loops_.back();
            for (size_t i = scopes_.size(); i-- > loop.scopeDepth;) leaveScope(i, nullptr);
            term("br label %" + (statement.kind == StmtKind::Break ? loop.breakLabel : loop.continueLabel));
            break;
        }
        case StmtKind::Return: generateReturn(static_cast<const ReturnStmt&>(statement)); break;
        case StmtKind::Switch: generateSwitch(static_cast<const SwitchStmt&>(statement)); break;
        case StmtKind::Throw: generateThrow(static_cast<const ThrowStmt&>(statement)); break;
        case StmtKind::Try: generateTry(static_cast<const TryStmt&>(statement)); break;
        }
        dropTemporaries(mark);
    }

    void generateVarDecl(const VarDeclStmt& decl) {
        const Variable& variable = *decl.variable;
        const std::string& address = addresses_[variable.id];
        Type type = variable.type;
        declareVariable(variable, address);
        if (type.isAggregate()) {
            if (decl.init) {
                generateStructInto(*decl.init, address);
            } else {
                storeDefault(address, type);
            }
            if (needsDestruction(type)) scopes_.back().objects.push_back(LiveObject{address, type, &variable});
            setAlive(variable, true);
            return;
        }
        std::string value = decl.init ? generateExpr(*decl.init).ref : defaultConstant(type);
        inst("store " + llvmType(type) + " " + value + ", ptr " + address);
        if (needsDestruction(type)) {  // a function value owns its data (D184)
            scopes_.back().objects.push_back(LiveObject{address, type, &variable});
            setAlive(variable, true);
        }
    }

    void generateAssign(const AssignStmt& assign) {
        if (assign.call) {  // `s += t` (D95)
            generateExpr(*assign.call);
            return;
        }
        Type type = assign.target->type;
        if (type.isAggregate()) {
            // The new value is built apart: it may read the old one (D75). A moved
            // variable has no old value to destroy (D81).
            std::string fresh = slot(type);
            generateStructInto(*assign.value, fresh);
            std::string address = addressOf(*assign.target);
            const Variable* variable =
                assign.target->kind == ExprKind::Name ? static_cast<const NameExpr&>(*assign.target).variable : nullptr;
            if (needsDestruction(type)) dropVariable(address, type, variable);
            copyStruct(address, fresh, type);
            if (variable) setAlive(*variable, true);
            return;
        }
        Value value = generateExpr(*assign.value);
        std::string address = addressOf(*assign.target);
        if (type.isFunction() || type.isInterface()) {  // the old value gives back what it owns (D184, D357)
            const Variable* variable =
                assign.target->kind == ExprKind::Name ? static_cast<const NameExpr&>(*assign.target).variable : nullptr;
            dropVariable(address, type, variable);
            inst("store " + llvmType(type) + " " + value.ref + ", ptr " + address);
            if (variable) setAlive(*variable, true);
            return;
        }
        if (assign.op != AssignOp::Assign) {
            static const BinaryOp ops[] = {BinaryOp::Add,    BinaryOp::Add,   BinaryOp::Sub,    BinaryOp::Mul,
                                           BinaryOp::Div,    BinaryOp::Rem,   BinaryOp::BitAnd, BinaryOp::BitOr,
                                           BinaryOp::BitXor, BinaryOp::Shl,   BinaryOp::Shr};
            Value current = load(address, type);
            value = arithmetic(ops[static_cast<int>(assign.op)], current, value, assign.loc);
        }
        inst("store " + llvmType(type) + " " + value.ref + ", ptr " + address);
    }

    void generateReturn(const ReturnStmt& ret) {
        Type type = function_->resolvedReturn;
        const Variable* moved = nullptr;
        std::string result;
        if (ret.value && type.isAggregate()) {
            // `ret local` moves the variable out (D23): it is not destroyed.
            if (ret.value->kind == ExprKind::Name) moved = static_cast<const NameExpr&>(*ret.value).variable;
            std::string dest = byPointer(type) ? "%ret" : slot(type);
            generateStructInto(*ret.value, dest);
            if (!byPointer(type)) result = dest;
        } else if (ret.value) {
            bool owned = type.isFunction() || type.isInterface();
            if (owned && ret.value->kind == ExprKind::Name) moved = static_cast<const NameExpr&>(*ret.value).variable;
            result = generateExpr(*ret.value).ref;
        }

        dropTemporaries(0);
        for (size_t i = scopes_.size(); i-- > 0;) leaveScope(i, moved);

        if (!dropFields_.empty()) {
            term("br label %" + dropFields_);
        } else if (!ret.value || byPointer(type)) {
            term("ret void");
        } else if (type.isAggregate()) {
            Value value = load(result, type);
            term("ret " + llvmType(type) + " " + value.ref);
        } else {
            term("ret " + llvmType(type) + " " + result);
        }
    }

    // Evaluates a condition, destroying its temporaries before the branch (D68).
    Value generateCondition(const Expr& condition) {
        size_t mark = temporaries_.size();
        Value value = generateExpr(condition);
        dropTemporaries(mark);
        return value;
    }

    void generateIf(const IfStmt& statement) {
        Value condition = generateCondition(*statement.condition);
        std::string thenLabel = label("if.then");
        std::string elseLabel = statement.elseBlock ? label("if.else") : "";
        std::string endLabel = label("if.end");
        term("br i1 " + condition.ref + ", label %" + thenLabel + ", label %" +
             (statement.elseBlock ? elseLabel : endLabel));
        block(thenLabel);
        generateBlock(statement.thenBlock);
        if (statement.elseBlock) {
            if (!terminated_) term("br label %" + endLabel);
            block(elseLabel);
            generateBlock(*statement.elseBlock);
        }
        block(endLabel);
    }

    // `switch` (D106): on an enum it jumps on the tag, on an integer it is a
    // `switch` of LLVM, on a string it compares the literals in turn (D109).
    void generateSwitch(const SwitchStmt& statement) {
        Type type = statement.value->type;
        std::string end = label("switch.end");
        std::string otherwise = label("switch.default");
        std::vector<std::string> labels;
        for (size_t i = 0; i < statement.branches.size(); ++i) labels.push_back(label("switch.case"));

        std::string address;
        if (type.isStruct() && type.structDecl->isEnum) {
            address = addressOf(*statement.value);
            const StructDecl& decl = *type.structDecl;
            Value tag = load(enumTag(address, decl), Type::intType(32, true));
            std::string table;
            for (size_t b = 0; b < statement.branches.size(); ++b) {
                for (const CasePattern& pattern : statement.branches[b].patterns) {
                    table += " i32 " + std::to_string(decl.cases[pattern.caseIndex].value) + ", label %" + labels[b];
                }
            }
            term("switch i32 " + tag.ref + ", label %" + otherwise + " [" + table + " ]");
        } else if (type.isInt()) {
            Value value = generateExpr(*statement.value);
            std::string table;
            for (size_t b = 0; b < statement.branches.size(); ++b) {
                for (const CasePattern& pattern : statement.branches[b].patterns) {
                    table += " " + llvmType(type) + " " + literalConstant(*pattern.literal, type) + ", label %" + labels[b];
                }
            }
            term("switch " + llvmType(type) + " " + value.ref + ", label %" + otherwise + " [" + table + " ]");
        } else {
            std::string text = addressOf(*statement.value);
            const FunctionDecl& equals = *type.structDecl->methods.at("equals");
            for (size_t b = 0; b < statement.branches.size(); ++b) {
                for (const CasePattern& pattern : statement.branches[b].patterns) {
                    // A literal owns no memory: it is not a temporary to destroy, which
                    // a `ret` in an earlier case would destroy before it exists.
                    std::string literal = slot(pattern.literal->type);
                    generateStructInto(*pattern.literal, literal);
                    std::string same = temp();
                    std::string call = same + " = call i1 " + symbolOf(equals) + "(ptr " + text + ", ptr " + literal;
                    if (tracksCaller(equals)) call += ", ptr " + where(pattern.loc);
                    inst(call + ")");
                    std::string next = label("switch.next");
                    term("br i1 " + same + ", label %" + labels[b] + ", label %" + next);
                    block(next);
                }
            }
            term("br label %" + otherwise);
        }

        for (size_t b = 0; b < statement.branches.size(); ++b) {
            const SwitchBranch& branch = statement.branches[b];
            block(labels[b]);
            for (const CasePattern& pattern : branch.patterns) {
                if (!pattern.hasBindings) continue;  // D107: the data of the case, where they are
                const StructDecl& decl = *type.structDecl;
                const EnumCase& item = decl.cases[pattern.caseIndex];
                std::string payload = enumPayload(address, decl);
                for (size_t i = 0; i < pattern.bindings.size(); ++i) {
                    if (pattern.bindings[i].variable) {
                        addresses_[pattern.bindings[i].variable->id] = caseField(payload, decl, item, i);
                        declareVariable(*pattern.bindings[i].variable, addresses_[pattern.bindings[i].variable->id]);
                    }
                }
            }
            generateBlock(branch.body);
            if (!terminated_) term("br label %" + end);
        }
        block(otherwise);
        if (statement.defaultBlock) {
            generateBlock(*statement.defaultBlock);
        } else if (statement.exhaustive) {
            term("unreachable");
        }
        block(end);
    }

    // `for ([ref] T x in v)` (D25): `x` is the address of each element in turn.
    // The list cannot change while the loop runs (D101), so its length and its
    // buffer are read once.
    void generateForEach(const ForStmt& statement) {
        Type i64 = Type::intType(64, true);
        std::string list = addressOf(*statement.list);
        Type type = statement.list->type;
        Type element;
        Value length;
        Value data;
        if (type.isArray()) {  // D196: the elements start where the array is
            element = type.array->element;
            length = {std::to_string(type.array->count), i64};
            data = {list, Type::addrType()};
        } else {
            const StructDecl& decl = *type.structDecl;
            element = decl.typeArgs[0];
            length = load(listField(list, decl, "length"), i64);
            data = load(listField(list, decl, "data"), Type::addrType());
        }
        std::string counter = slot(i64);
        inst("store i64 0, ptr " + counter);
        std::string condLabel = label("each.cond");
        std::string bodyLabel = label("each.body");
        std::string stepLabel = label("each.step");
        std::string endLabel = label("each.end");

        block(condLabel);
        Value index = load(counter, i64);
        std::string more = temp();
        inst(more + " = icmp slt i64 " + index.ref + ", " + length.ref);
        term("br i1 " + more + ", label %" + bodyLabel + ", label %" + endLabel);
        block(bodyLabel);
        Value current = load(counter, i64);
        std::string address = temp();
        inst(address + " = getelementptr inbounds " + llvmType(element) + ", ptr " + data.ref + ", i64 " + current.ref);
        addresses_[statement.element->id] = address;
        declareVariable(*statement.element, address);
        loops_.push_back(Loop{stepLabel, endLabel, scopes_.size()});
        generateBlock(statement.body);
        loops_.pop_back();
        block(stepLabel);
        Value last = load(counter, i64);
        std::string next = temp();
        inst(next + " = add i64 " + last.ref + ", 1");
        inst("store i64 " + next + ", ptr " + counter);
        term("br label %" + condLabel);
        block(endLabel);
    }

    // `for (K k, V v in m)` (D197): the slots that are full, in their order. A slot
    // of the map of the library is its hash, the key at 8 and the value after it,
    // each at a multiple of 8 bytes.
    void generateForMap(const ForStmt& statement) {
        Type i64 = Type::intType(64, true);
        std::string map = addressOf(*statement.list);
        const StructDecl& decl = *statement.list->type.structDecl;
        Type keyType = decl.typeArgs[0];
        Type valueType = decl.typeArgs[1];
        int valueOffset = 8 + (sizeOf(keyType) + 7) / 8 * 8;
        int slotSize = valueOffset + (sizeOf(valueType) + 7) / 8 * 8;
        Value capacity = load(listField(map, decl, "capacity"), i64);
        Value slots = load(listField(map, decl, "slots"), Type::addrType());
        std::string counter = slot(i64);
        inst("store i64 0, ptr " + counter);
        std::string condLabel = label("map.cond");
        std::string testLabel = label("map.test");
        std::string bodyLabel = label("map.body");
        std::string stepLabel = label("map.step");
        std::string endLabel = label("map.end");

        block(condLabel);
        Value index = load(counter, i64);
        std::string more = temp();
        inst(more + " = icmp slt i64 " + index.ref + ", " + capacity.ref);
        term("br i1 " + more + ", label %" + testLabel + ", label %" + endLabel);
        block(testLabel);
        std::string start = temp();
        std::string slotAddress = temp();
        std::string hash = temp();
        std::string full = temp();
        inst(start + " = mul i64 " + index.ref + ", " + std::to_string(slotSize));
        inst(slotAddress + " = getelementptr inbounds i8, ptr " + slots.ref + ", i64 " + start);
        inst(hash + " = load i64, ptr " + slotAddress);
        inst(full + " = icmp slt i64 " + hash + ", 0");  // a full slot has the highest bit
        term("br i1 " + full + ", label %" + bodyLabel + ", label %" + stepLabel);
        block(bodyLabel);
        std::string key = temp();
        std::string value = temp();
        inst(key + " = getelementptr inbounds i8, ptr " + slotAddress + ", i64 8");
        inst(value + " = getelementptr inbounds i8, ptr " + slotAddress + ", i64 " + std::to_string(valueOffset));
        addresses_[statement.element->id] = key;
        addresses_[statement.value->id] = value;
        declareVariable(*statement.element, key);
        declareVariable(*statement.value, value);
        loops_.push_back(Loop{stepLabel, endLabel, scopes_.size()});
        generateBlock(statement.body);
        loops_.pop_back();
        block(stepLabel);
        Value last = load(counter, i64);
        std::string next = temp();
        inst(next + " = add i64 " + last.ref + ", 1");
        inst("store i64 " + next + ", ptr " + counter);
        term("br label %" + condLabel);
        block(endLabel);
    }

    void generateFor(const ForStmt& statement) {
        if (statement.form == ForForm::Each && statement.hasValue) {
            generateForMap(statement);
            return;
        }
        if (statement.form == ForForm::Each) {
            generateForEach(statement);
            return;
        }
        scopes_.emplace_back();  // the variable of the classic form
        if (statement.init) generateStatement(*statement.init);
        std::string condLabel = label("for.cond");
        std::string bodyLabel = label("for.body");
        std::string stepLabel = label("for.step");
        std::string endLabel = label("for.end");

        block(condLabel);
        if (statement.condition) {
            Value condition = generateCondition(*statement.condition);
            term("br i1 " + condition.ref + ", label %" + bodyLabel + ", label %" + endLabel);
        }
        block(bodyLabel);
        loops_.push_back(Loop{stepLabel, endLabel, scopes_.size()});
        generateBlock(statement.body);
        loops_.pop_back();
        block(stepLabel);
        if (statement.step) generateStatement(*statement.step);
        term("br label %" + condLabel);
        block(endLabel);
        dropObjects(scopes_.back().objects);
        scopes_.pop_back();
    }

    // ---- Structs ----

    // The address of a struct or of a place. A new object gets a slot and
    // lives until the end of the statement (D68).
    std::string addressOf(const Expr& expr) {
        switch (expr.kind) {
        case ExprKind::Name: return addresses_[static_cast<const NameExpr&>(expr).variable->id];
        case ExprKind::Self: return "%self";
        case ExprKind::Field: {
            const auto& field = static_cast<const FieldExpr&>(expr);
            std::string object = addressOf(*field.object);
            return fieldAddress(object, *field.object->type.structDecl, field.index);
        }
        case ExprKind::Index: {
            const auto& element = static_cast<const IndexExpr&>(expr);
            std::string list = addressOf(*element.object);
            Value index = element.access ? Value{} : generateExpr(*element.index);
            Type type = element.object->type;
            if (element.access) return mapValue(list, element);
            if (type.isArray()) {  // D196: the size is in the type
                if (element.index->kind != ExprKind::IntLiteral) checkIndex(index.ref, std::to_string(type.array->count), element.loc);
                std::string result = temp();
                inst(result + " = getelementptr inbounds " + llvmType(type) + ", ptr " + list + ", i64 0, i64 " + index.ref);
                return result;
            }
            return elementAddress(list, *type.structDecl, index.ref, element.loc);
        }
        default: {
            std::string address = slot(expr.type);
            generateStructInto(expr, address);
            if (needsDestruction(expr.type)) temporaries_.push_back(LiveObject{address, expr.type, nullptr});
            return address;
        }
        }
    }

    // `m[k]` (D197): the address of the value, from the method of the library.
    std::string mapValue(const std::string& map, const IndexExpr& element) {
        const FunctionDecl& access = *element.access;
        std::string args = "ptr " + map + ", " + argument(*element.index, access.params[0], access.paramTypes[0]);
        if (tracksCaller(access)) args += ", ptr " + where(element.loc);
        if (access.canThrow) args += ", ptr " + raisedSlot();
        std::string result = temp();
        inst(result + " = call ptr " + symbolOf(access) + "(" + args + ")");
        if (access.canThrow) checkRaised();
        return result;
    }

    std::string listField(const std::string& list, const StructDecl& decl, const char* name) {
        return fieldAddress(list, decl, fieldIndexOf(decl, name));
    }

    // The bounds check (D30). An index below zero is a huge unsigned number, so
    // one comparison covers both ends.
    void checkIndex(const std::string& index, const std::string& length, SourceLocation loc) {
        std::string outside = temp();
        inst(outside + " = icmp uge i64 " + index + ", " + length);
        std::string fail = label("bounds");
        std::string ok = label("ok");
        term("br i1 " + outside + ", label %" + fail + ", label %" + ok);
        block(fail);
        inst("call void @cromo_panic_index(i64 " + index + ", i64 " + length + ", ptr " + where(loc) + ")");
        term("unreachable");
        block(ok);
    }

    // The address of an element of a list, or of a byte of a string (D200), after
    // the bounds check.
    std::string elementAddress(const std::string& list, const StructDecl& decl, const std::string& index, SourceLocation loc) {
        Value length = load(listField(list, decl, "length"), Type::intType(64, true));
        checkIndex(index, length.ref, loc);
        Value data = load(listField(list, decl, "data"), Type::addrType());
        std::string result = temp();
        std::string element = decl.typeArgs.empty() ? "i8" : llvmType(decl.typeArgs[0]);
        inst(result + " = getelementptr inbounds " + element + ", ptr " + data.ref + ", i64 " + index);
        return result;
    }

    // A string literal as a `string` that does not own its bytes: its capacity is 0 (D100).
    std::string stringValue(const StringLiteralExpr& literal) {
        const StructDecl& decl = *literal.type.structDecl;
        std::vector<std::string> values(decl.fields.size(), "0");
        values[fieldIndexOf(decl, "data")] = stringConstant(literal.value);
        values[fieldIndexOf(decl, "length")] = std::to_string(literal.value.size());
        std::string out = "{ ";
        for (size_t i = 0; i < values.size(); ++i) out += (i ? ", " : "") + llvmType(decl.fields[i].resolved) + " " + values[i];
        return out + " }";
    }

    // Builds the value of a struct expression directly in `dest`, which then owns it.
    void generateStructInto(const Expr& expr, const std::string& dest) {
        if (expr.type.isFunction() || expr.type.isInterface()) {  // a pair of values, not in memory
            Value value = generateExpr(expr);
            inst("store { ptr, ptr } " + value.ref + ", ptr " + dest);
            return;
        }
        switch (expr.kind) {
        case ExprKind::CallValue:
            callValue(static_cast<const CallValueExpr&>(expr), dest);
            return;
        case ExprKind::Print:  // format (D200)
            generatePrint(static_cast<const PrintExpr&>(expr), dest);
            return;
        case ExprKind::StringLiteral:
            inst("store " + llvmType(expr.type) + " " + stringValue(static_cast<const StringLiteralExpr&>(expr)) + ", ptr " + dest);
            return;
        case ExprKind::EnumCase: {
            // The data are moved in (D124), then the tag says which case it is.
            const auto& node = static_cast<const EnumCaseExpr&>(expr);
            const StructDecl& decl = *node.decl;
            const EnumCase& item = decl.cases[node.index];
            std::string payload = enumPayload(dest, decl);
            for (size_t i = 0; i < node.args.size(); ++i) {
                Type type = item.fields[i].resolved;
                std::string field = caseField(payload, decl, item, i);
                if (type.isAggregate()) {
                    generateStructInto(*node.args[i], field);
                } else {
                    inst("store " + llvmType(type) + " " + generateExpr(*node.args[i]).ref + ", ptr " + field);
                }
            }
            inst("store i32 " + std::to_string(item.value) + ", ptr " + enumTag(dest, decl));
            return;
        }
        case ExprKind::Conversion: {
            // `Color(n)`: the case whose value is n, or a panic (D135).
            const StructDecl& decl = *expr.type.structDecl;
            Value number = generateExpr(*static_cast<const ConversionExpr&>(expr).operand);
            Type from = number.type;
            std::string ok = label("case.ok");
            std::string bad = label("case.bad");
            std::string table;
            for (const EnumCase& item : decl.cases) {
                bool negative = item.value < 0;
                uint64_t magnitude = negative ? uint64_t(0) - static_cast<uint64_t>(item.value) : static_cast<uint64_t>(item.value);
                if (negative && !from.isSigned) continue;
                if (from.bits < 64 && magnitude > (uint64_t{1} << (from.isSigned ? from.bits - 1 : from.bits)) - (negative ? 0 : 1)) continue;
                table += " " + llvmType(from) + " " + intConstant(magnitude, negative, from.bits) + ", label %" + ok;
            }
            term("switch " + llvmType(from) + " " + number.ref + ", label %" + bad + " [" + table + " ]");
            block(bad);
            panic("the value is not a case of " + decl.name + " (D135)", expr.loc);
            block(ok);
            Value tag = convert(number, Type::intType(32, true), expr.loc);
            inst("store " + structName(decl) + " " + defaultConstant(expr.type) + ", ptr " + dest);
            inst("store i32 " + tag.ref + ", ptr " + enumTag(dest, decl));
            return;
        }
        case ExprKind::Construct: {
            const auto& construct = static_cast<const ConstructExpr&>(expr);
            const StructDecl& decl = *construct.decl;
            if (!decl.constructor) {
                storeDefault(dest, expr.type);
                return;
            }
            emitCall(*decl.constructor, dest, expr.type, construct.args, "", expr.loc, &construct.overlaps);
            return;
        }
        case ExprKind::ListLiteral: {
            // `[a, b, c]` (D308): reserve(), then add() of each element. If an element
            // throws, the list made so far is destroyed with the temporaries.
            const auto& list = static_cast<const ListLiteralExpr&>(expr);
            storeDefault(dest, expr.type);
            size_t made = temporaries_.size();
            temporaries_.push_back(LiveObject{dest, expr.type, nullptr});
            emitCall(*list.reserve, dest, expr.type, list.count, "", expr.loc);
            // emitCall takes the arguments of a call: each element, one at a time.
            auto& elements = const_cast<std::vector<ExprPtr>&>(list.elements);
            for (ExprPtr& element : elements) {
                std::vector<ExprPtr> args;
                args.push_back(std::move(element));
                emitCall(*list.add, dest, expr.type, args, "", expr.loc);
                element = std::move(args[0]);
            }
            temporaries_.erase(temporaries_.begin() + static_cast<std::ptrdiff_t>(made));
            return;
        }
        case ExprKind::Call: {
            const auto& call = static_cast<const CallExpr&>(expr);
            if (call.intrinsic != Intrinsic::None) {
                generateIntrinsic(call, dest);
                return;
            }
            emitCall(*call.function, "", Type::error(), call.args, dest, call.loc, &call.overlaps);
            return;
        }
        case ExprKind::MethodCall: {
            emitMethodCall(static_cast<const MethodCallExpr&>(expr), dest);
            return;
        }
        case ExprKind::Move: {
            const auto& move = static_cast<const MoveExpr&>(expr);
            std::string source = addressOf(*move.operand);
            copyStruct(dest, source, expr.type);
            finishMove(move, source);
            return;
        }
        case ExprKind::Copy:
            copyStruct(dest, addressOf(*static_cast<const CopyExpr&>(expr).operand), expr.type);
            return;
        default:
            // A copy of a place: sema allows it only for simple structs (D23).
            copyStruct(dest, addressOf(expr), expr.type);
            return;
        }
    }

    // After its value has been taken, a moved variable is no longer alive (D23),
    // and a field or a `ref` parameter gets the default value (D78).
    void finishMove(const MoveExpr& move, const std::string& source) {
        if (move.moveKind == MoveKind::Variable) {
            setAlive(*static_cast<const NameExpr&>(*move.operand).variable, false);
        } else if (move.moveKind == MoveKind::Take) {
            storeDefault(source, move.type);
        }
    }

    // A method call: direct, or through the method table of a parameter of
    // interface type (D49).
    Value emitMethodCall(const MethodCallExpr& call, const std::string& dest) {
        if (!call.object->type.isInterface()) {
            return emitCall(*call.function, addressOf(*call.object), call.object->type, call.args, dest, call.loc, &call.overlaps);
        }
        const InterfaceDecl& iface = *call.object->type.interfaceDecl;
        auto index = static_cast<size_t>(call.function - iface.methods.data());
        Value pair = load(addressOf(*call.object), call.object->type);
        std::string object = temp();
        std::string table = temp();
        std::string entry = temp();
        std::string target = temp();
        std::string empty = temp();
        inst(object + " = extractvalue { ptr, ptr } " + pair.ref + ", 0");
        inst(empty + " = icmp eq ptr " + object + ", null");
        panicIf(empty, "the value of " + iface.name + " is empty: it was moved, or never given (D361)", call.loc);
        inst(table + " = extractvalue { ptr, ptr } " + pair.ref + ", 1");
        inst(entry + " = getelementptr inbounds ptr, ptr " + table + ", i64 " + std::to_string(index));
        inst(target + " = load ptr, ptr " + entry);
        return emitCall(*call.function, object, Type::error(), call.args, dest, call.loc, &call.overlaps, target);
    }

    // The method table of a struct for an interface, emitted the first time it is
    // used: the methods, then the release of an object on the heap (D357).
    std::string methodTable(const StructDecl& decl, const InterfaceDecl& iface) {
        std::string name = llvmName('@', "vtable." + decl.symbol + "." + iface.symbol);
        if (!tables_.insert(name).second) return name;
        globals_ << name << " = private unnamed_addr constant [" << iface.methods.size() + 1 << " x ptr] [";
        for (size_t i = 0; i < iface.methods.size(); ++i) {
            globals_ << "ptr " << methodSymbol(decl, iface.methods[i].name) << ", ";
        }
        globals_ << "ptr " << releaseFunction(decl) << "]\n";
        return name;
    }

    // Destroys an object on the heap, the value of an interface, and frees it (D357).
    std::string releaseFunction(const StructDecl& decl) {
        std::string name = llvmName('@', decl.symbol + "..release");
        if (!releases_.insert(name).second) return name;
        Type type = Type::structType(&decl);
        std::string body = "define private void " + name + "(ptr %object) " + functionAttributes() + " {\nentry:\n";
        if (needsDestruction(type)) body += "  call void " + dropSymbol(decl) + "(ptr %object)\n";
        body += "  call void @cromo_free(ptr %object)\n  ret void\n}\n\n";
        releaseBodies_ += body;
        return name;
    }

    // `Shape s = Circle(2.0)` (D357): the struct moves to the heap, with its table.
    Value boxValue(const ConversionExpr& conversion) {
        Type type = conversion.operand->type;
        std::string object = temp();
        inst(object + " = call ptr @cromo_alloc(i64 " + std::to_string(std::max(sizeOf(type), 1)) + ")");
        generateStructInto(*conversion.operand, object);
        std::string table = methodTable(*type.structDecl, *conversion.type.interfaceDecl);
        std::string first = temp();
        std::string pair = temp();
        inst(first + " = insertvalue { ptr, ptr } undef, ptr " + object + ", 0");
        inst(pair + " = insertvalue { ptr, ptr } " + first + ", ptr " + table + ", 1");
        return {pair, conversion.type};
    }

    // Calls a function, a method (with `self`) or a constructor (with `self` set
    // to the new object). A struct result is written to `dest`.
    Value emitCall(const FunctionDecl& function, const std::string& self, Type selfType, const std::vector<ExprPtr>& args,
                   const std::string& dest, SourceLocation loc, const OverlapChecks* overlaps = nullptr,
                   const std::string& callee = "") {
        Type ret = function.resolvedReturn;
        std::vector<std::string> list;
        if (byPointer(ret)) list.push_back("ptr " + dest);
        if (!self.empty()) list.push_back("ptr " + self);
        bool checks = overlaps && !overlaps->empty();
        std::vector<std::string> addresses(args.size());
        Handover handover;
        for (size_t i = 0; i < args.size(); ++i) {
            std::string value =
                argument(*args[i], function.params[i], function.paramTypes[i], checks ? &addresses[i] : nullptr, &handover);
            if (function.isExtern && function.params[i].mode == ParamMode::Read && function.paramTypes[i].isBool()) {
                std::string byte = temp();  // a bool of C is a byte (D129)
                inst(byte + " = zext " + value + " to i8");
                value = "i8 " + byte;
            }
            list.push_back(value);
        }
        if (tracksCaller(function)) list.push_back("ptr " + where(loc));
        if (function.canThrow) list.push_back("ptr " + raisedSlot());
        if (checks) checkOverlaps(*overlaps, self, selfType, args, addresses, loc);
        // From here the callee owns its `move` arguments (D22): until now, an
        // exception in another argument destroyed them here.
        for (auto owned = handover.owned.rbegin(); owned != handover.owned.rend(); ++owned) {
            temporaries_.erase(temporaries_.begin() + static_cast<std::ptrdiff_t>(*owned));
        }
        for (auto& [move, address] : handover.variables) finishMove(*move, address);
        std::string joined;
        for (size_t i = 0; i < list.size(); ++i) joined += (i ? ", " : "") + list[i];

        if (ret.isVoid() || byPointer(ret)) {
            inst("call void " + (callee.empty() ? symbolOf(function) : callee) + "(" + joined + ")");
            if (function.canThrow) checkRaised();
            return {"", ret};
        }
        std::string result = temp();
        if (function.isExtern && ret.isBool()) {
            std::string byte = temp();
            inst(byte + " = call i8 " + symbolOf(function) + "(" + joined + ")");
            inst(result + " = icmp ne i8 " + byte + ", 0");
            return {result, ret};
        }
        inst(result + " = call " + llvmType(ret) + " " + (callee.empty() ? symbolOf(function) : callee) + "(" + joined + ")");
        if (function.canThrow) checkRaised();
        if (ret.isAggregate()) inst("store " + llvmType(ret) + " " + result + ", ptr " + dest);
        return {result, ret};
    }

    // The `move` arguments of a call, given to the callee only when it is called.
    struct Handover {
        std::vector<size_t> owned;  // temporaries that the callee will own
        std::vector<std::pair<const MoveExpr*, std::string>> variables;  // whole variables handed over in place
    };

    // D35: two arguments that may be the same element of a list are compared
    // at runtime, before the call.
    void checkOverlaps(const OverlapChecks& pairs, const std::string& self, Type selfType, const std::vector<ExprPtr>& args,
                       const std::vector<std::string>& addresses, SourceLocation loc) {
        auto range = [&](int arg) -> std::pair<std::string, int> {
            if (arg < 0) return {self, sizeOf(selfType)};
            const Expr& expr = *args[arg];
            Type type = expr.kind == ExprKind::Ref ? static_cast<const RefExpr&>(expr).operand->type : expr.type;
            return {addresses[arg], sizeOf(type)};
        };
        for (auto [a, b] : pairs) {
            auto [first, firstSize] = range(a);
            auto [second, secondSize] = range(b);
            if (first.empty() || second.empty()) continue;
            std::string firstEnd = temp();
            std::string secondEnd = temp();
            std::string before = temp();
            std::string after = temp();
            std::string overlap = temp();
            inst(firstEnd + " = getelementptr i8, ptr " + first + ", i64 " + std::to_string(firstSize));
            inst(secondEnd + " = getelementptr i8, ptr " + second + ", i64 " + std::to_string(secondSize));
            inst(before + " = icmp ult ptr " + first + ", " + secondEnd);
            inst(after + " = icmp ult ptr " + second + ", " + firstEnd);
            inst(overlap + " = and i1 " + before + ", " + after);
            panicIf(overlap, "the same element is passed twice to a call that modifies it (D35)", loc);
        }
    }

    // An argument as the callee expects it (D22): `ref` and large structs by
    // address, the rest by value. `address`, if given, receives the address of
    // the place passed, for the checks of D35.
    std::string argument(const Expr& expr, const Param& param, Type type, std::string* address = nullptr,
                         Handover* handover = nullptr) {
        auto remember = [address](const std::string& place) {
            if (address) *address = place;
            return place;
        };
        if (type.isInterface() && param.mode == ParamMode::Move) {
            return "{ ptr, ptr } " + generateExpr(expr).ref;  // the callee owns it (D357)
        }
        if (type.isInterface()) {
            // The object by address, with the method table of its struct (D49).
            const Expr& value = param.mode == ParamMode::Ref ? *static_cast<const RefExpr&>(expr).operand : expr;
            if (value.type.isInterface()) return "{ ptr, ptr } " + load(addressOf(value), type).ref;
            std::string object = remember(addressOf(value));
            std::string table = methodTable(*value.type.structDecl, *type.interfaceDecl);
            std::string first = temp();
            std::string pair = temp();
            inst(first + " = insertvalue { ptr, ptr } undef, ptr " + object + ", 0");
            inst(pair + " = insertvalue { ptr, ptr } " + first + ", ptr " + table + ", 1");
            return "{ ptr, ptr } " + pair;
        }
        if (param.mode == ParamMode::Ref) return "ptr " + remember(addressOf(*static_cast<const RefExpr&>(expr).operand));
        if (param.mode == ParamMode::Move && type.isAggregate()) {
            // The callee becomes the owner. A whole variable is handed over where it
            // is; anything else is built in a slot that nobody else destroys.
            if (byPointer(type) && expr.kind == ExprKind::Move &&
                static_cast<const MoveExpr&>(expr).moveKind == MoveKind::Variable) {
                const auto& move = static_cast<const MoveExpr&>(expr);
                std::string variable = addressOf(*move.operand);
                if (handover) {
                    handover->variables.emplace_back(&move, variable);
                } else {
                    finishMove(move, variable);
                }
                return "ptr " + variable;
            }
            std::string owned = slot(type);
            generateStructInto(expr, owned);
            if (handover && needsDestruction(type)) {
                handover->owned.push_back(temporaries_.size());
                temporaries_.push_back(LiveObject{owned, type, nullptr});
            }
            if (byPointer(type)) return "ptr " + owned;
            return llvmType(type) + " " + load(owned, type).ref;
        }
        if (byPointer(type)) return "ptr " + remember(addressOf(expr));
        if (type.isAggregate()) return llvmType(type) + " " + load(remember(addressOf(expr)), type).ref;
        Value value = generateExpr(expr);
        return llvmType(value.type) + " " + value.ref;
    }

    // The atomic instructions take a bool as the byte it is in memory (D328).
    static std::string atomicType(Type type) { return type.isBool() ? "i8" : llvmType(type); }

    std::string atomicValue(const Expr& expr, Type type) {
        std::string value = generateExpr(expr).ref;
        if (!type.isBool()) return value;
        std::string wide = temp();
        inst(wide + " = zext i1 " + value + " to i8");
        return wide;
    }

    Value atomicResult(const std::string& value, Type type) {
        if (!type.isBool()) return {value, type};
        std::string bit = temp();
        inst(bit + " = trunc i8 " + value + " to i1");
        return {bit, type};
    }

    // The built-in functions: `panic` (D102) and the `unsafe` dialect (D98). A
    // struct result is written to `dest`.
    Value generateIntrinsic(const CallExpr& call, const std::string& dest) {
        Type i64 = Type::intType(64, true);
        Type addr = Type::addrType();
        Type element = call.typeArg;
        auto arg = [&](size_t i) { return generateExpr(*call.args[i]).ref; };
        std::string result = temp();
        switch (call.intrinsic) {
        case Intrinsic::Assert:
            generateAssert(call);
            return {"", Type::voidType()};
        case Intrinsic::Panic: {
            std::string message = addressOf(*call.args[0]);
            const StructDecl& decl = *call.args[0]->type.structDecl;
            Value data = load(listField(message, decl, "data"), addr);
            Value length = load(listField(message, decl, "length"), i64);
            inst("call void @cromo_panic(ptr " + data.ref + ", i64 " + length.ref + ", ptr " + where(call.loc) + ")");
            term("unreachable");
            return {"", Type::voidType()};
        }
        case Intrinsic::Alloc:
            inst(result + " = call ptr @cromo_alloc(i64 " + arg(0) + ")");
            return {result, addr};
        case Intrinsic::Resize: {
            std::string memory = arg(0);
            inst(result + " = call ptr @cromo_realloc(ptr " + memory + ", i64 " + arg(1) + ")");
            return {result, addr};
        }
        case Intrinsic::Free:
            inst("call void @cromo_free(ptr " + arg(0) + ")");
            return {"", Type::voidType()};
        case Intrinsic::SizeOf:
            return {std::to_string(sizeOf(element)), i64};
        case Intrinsic::Offset: {
            std::string base = arg(0);
            inst(result + " = getelementptr i8, ptr " + base + ", i64 " + arg(1));
            return {result, addr};
        }
        case Intrinsic::Take: {
            std::string place = arg(0);
            if (element.isAggregate()) {
                copyStruct(dest, place, element);
                return {"", element};
            }
            return load(place, element);
        }
        case Intrinsic::Put: {
            std::string place = arg(0);
            const Expr& value = *call.args[1];
            if (element.isAggregate()) {
                generateStructInto(value, place);
            } else {
                inst("store " + llvmType(element) + " " + generateExpr(value).ref + ", ptr " + place);
            }
            return {"", Type::voidType()};
        }
        case Intrinsic::Destroy: {
            std::string place = arg(0);
            if (needsDestruction(element)) drop(place, element);
            return {"", Type::voidType()};
        }
        case Intrinsic::CopyAt: {
            std::string place = arg(0);
            if (!element.isAggregate()) return load(place, element);
            if (needsDestruction(element)) {
                emitCall(*element.structDecl->methods.at("copy"), place, element, {}, dest, call.loc);
            } else {
                copyStruct(dest, place, element);
            }
            return {"", element};
        }
        case Intrinsic::CopyMemory: {
            std::string fn = intrinsic("declare void @llvm.memmove.p0.p0.i64(ptr, ptr, i64, i1)", "@llvm.memmove.p0.p0.i64");
            std::string target = arg(0);
            std::string source = arg(1);
            inst("call void " + fn + "(ptr " + target + ", ptr " + source + ", i64 " + arg(2) + ", i1 false)");
            return {"", Type::voidType()};
        }
        case Intrinsic::CompareMemory: {
            std::string first = arg(0);
            std::string second = arg(1);
            inst(result + " = call i32 @memcmp(ptr " + first + ", ptr " + second + ", i64 " + arg(2) + ")");
            return {result, Type::intType(32, true)};
        }
        case Intrinsic::CheckIndex: {
            std::string index = arg(0);
            checkIndex(index, arg(1), call.loc);
            return {"", Type::voidType()};
        }
        case Intrinsic::ArgumentCount:
            inst(result + " = call i64 @cromo_arg_count()");
            return {result, i64};
        case Intrinsic::ArgumentLength:
            inst(result + " = call i64 @cromo_arg_length(i64 " + arg(0) + ")");
            return {result, i64};
        case Intrinsic::ArgumentData:
            inst(result + " = call ptr @cromo_arg_data(i64 " + arg(0) + ")");
            return {result, addr};
        case Intrinsic::ThreadStart:  // D214
        case Intrinsic::TaskStart:    // D269
        case Intrinsic::GroupStart: {  // D273: the runtime owns the data of the lambda from here on
            bool group = call.intrinsic == Intrinsic::GroupStart;
            std::string record = group ? arg(0) : "";
            Value body = generateExpr(*call.args[group ? 1 : 0]);
            std::string code = temp();
            std::string env = temp();
            std::string empty = temp();
            inst(code + " = extractvalue { ptr, ptr } " + body.ref + ", 0");
            inst(env + " = extractvalue { ptr, ptr } " + body.ref + ", 1");
            inst(empty + " = icmp eq ptr " + code + ", null");
            panicIf(empty, "the function is empty: no lambda was given to it (D189)", call.loc);
            if (group) {
                std::string fn = intrinsic("declare void @cromo_group_start(ptr, ptr, ptr) nounwind", "@cromo_group_start");
                inst("call void " + fn + "(ptr " + record + ", ptr " + code + ", ptr " + env + ")");
                return {"", Type::voidType()};
            }
            const char* name = call.intrinsic == Intrinsic::TaskStart ? "@cromo_task_start" : "@cromo_thread_start";
            std::string fn = intrinsic(std::string("declare ptr ") + name + "(ptr, ptr) nounwind", name);
            inst(result + " = call ptr " + fn + "(ptr " + code + ", ptr " + env + ")");
            return {result, addr};
        }
        case Intrinsic::ThreadWait: {
            std::string fn = intrinsic("declare i32 @cromo_thread_wait(ptr) nounwind", "@cromo_thread_wait");
            std::string ended = temp();
            inst(ended + " = call i32 " + fn + "(ptr " + arg(0) + ")");
            inst(result + " = icmp ne i32 " + ended + ", 0");
            return {result, Type::boolType()};
        }
        case Intrinsic::ThreadRethrow: {  // D217: the exception goes on in this thread
            std::string fn = intrinsic("declare void @cromo_thread_take(ptr, ptr) nounwind", "@cromo_thread_take");
            inst("call void " + fn + "(ptr " + arg(0) + ", ptr " + raisedSlot() + ")");
            emitPropagation();
            return {"", Type::voidType()};
        }
        case Intrinsic::ThreadUncaught: {
            std::string fn = intrinsic("declare void @cromo_thread_uncaught(ptr) noreturn nounwind", "@cromo_thread_uncaught");
            inst("call void " + fn + "(ptr " + arg(0) + ")");
            term("unreachable");
            return {"", Type::voidType()};
        }
        case Intrinsic::ThreadFree:
        case Intrinsic::MutexLock:
        case Intrinsic::MutexUnlock: {
            const char* name = call.intrinsic == Intrinsic::ThreadFree ? "@cromo_thread_free"
                               : call.intrinsic == Intrinsic::MutexLock ? "@cromo_mutex_lock"
                                                                         : "@cromo_mutex_unlock";
            std::string fn = intrinsic(std::string("declare void ") + name + "(ptr) nounwind", name);
            inst("call void " + fn + "(ptr " + arg(0) + ")");
            return {"", Type::voidType()};
        }
        case Intrinsic::SelfAddress:
            return {"%self", addr};
        case Intrinsic::EqualAt: {  // D198: equals() with the key where it is, without moving it
            std::string place = arg(0);
            if (!element.isStruct()) {
                Value stored = load(place, element);
                Value other = generateExpr(*call.args[1]);
                std::string compare = element.isFloat() ? "fcmp oeq " : "icmp eq ";
                inst(result + " = " + compare + llvmType(element) + " " + stored.ref + ", " + other.ref);
                return {result, Type::boolType()};
            }
            const FunctionDecl& equals = *element.structDecl->methods.at("equals");
            std::string args = "ptr " + place + ", " + argument(*call.args[1], equals.params[0], equals.paramTypes[0]);
            if (tracksCaller(equals)) args += ", ptr " + where(call.loc);
            if (equals.canThrow) args += ", ptr " + raisedSlot();
            inst(result + " = call i1 " + symbolOf(equals) + "(" + args + ")");
            if (equals.canThrow) checkRaised();
            return {result, Type::boolType()};
        }
        case Intrinsic::SystemCall: {  // Linux (D202): an address is passed as an integer
            std::string args;
            for (size_t i = 0; i < 7; ++i) {
                Value value = generateExpr(*call.args[i]);
                std::string number = value.ref;
                if (value.type.kind == TypeKind::Addr) {
                    number = temp();
                    inst(number + " = ptrtoint ptr " + value.ref + " to i64");
                }
                args += (i ? ", i64 " : "i64 ") + number;
            }
            std::string fn = intrinsic("declare i64 @cromo_system_call(i64, i64, i64, i64, i64, i64, i64) nounwind",
                                       "@cromo_system_call");
            inst(result + " = call i64 " + fn + "(" + args + ")");
            return {result, i64};
        }
        case Intrinsic::Environment: {
            std::string fn = intrinsic("declare ptr @cromo_environment(i64) nounwind", "@cromo_environment");
            inst(result + " = call ptr " + fn + "(i64 " + arg(0) + ")");
            return {result, addr};
        }
        case Intrinsic::ExitProcess:
            inst("call void @cromo_exit(i32 " + arg(0) + ")");
            term("unreachable");
            return {"", Type::voidType()};
        case Intrinsic::AtomicLoad: {  // D328: all in the order of the sequence
            std::string place = arg(0);
            inst(result + " = load atomic " + atomicType(element) + ", ptr " + place + " seq_cst, align " +
                 std::to_string(sizeOf(element)));
            return atomicResult(result, element);
        }
        case Intrinsic::AtomicStore: {
            std::string place = arg(0);
            std::string value = atomicValue(*call.args[1], element);
            inst("store atomic " + atomicType(element) + " " + value + ", ptr " + place + " seq_cst, align " +
                 std::to_string(sizeOf(element)));
            return {"", Type::voidType()};
        }
        case Intrinsic::AtomicAdd:
        case Intrinsic::AtomicExchange: {
            std::string place = arg(0);
            std::string value = atomicValue(*call.args[1], element);
            const char* operation = call.intrinsic == Intrinsic::AtomicAdd ? "add" : "xchg";
            inst(result + " = atomicrmw " + operation + " ptr " + place + ", " + atomicType(element) + " " + value + " seq_cst");
            return atomicResult(result, element);
        }
        case Intrinsic::AtomicCompareExchange: {
            std::string place = arg(0);
            std::string type = atomicType(element);
            std::string expected = atomicValue(*call.args[1], element);
            std::string desired = atomicValue(*call.args[2], element);
            std::string pair = temp();
            inst(pair + " = cmpxchg ptr " + place + ", " + type + " " + expected + ", " + type + " " + desired +
                 " seq_cst seq_cst");
            inst(result + " = extractvalue { " + type + ", i1 } " + pair + ", 1");
            return {result, Type::boolType()};
        }
        case Intrinsic::HashBytes: {  // D198
            std::string data = arg(0);
            inst(result + " = call i64 @cromo_hash_bytes(ptr " + data + ", i64 " + arg(1) + ")");
            return {result, Type::intType(64, false)};
        }
        case Intrinsic::HashNumber: {  // the bits of the value; -0.0 is 0.0, as they are equal (D198)
            Value value = generateExpr(*call.args[0]);
            Type type = value.type;
            std::string bits = value.ref;
            if (type.isFloat()) {
                std::string positive = temp();
                inst(positive + " = fadd " + llvmType(type) + " " + value.ref + ", " + floatConstant(0, type.bits));
                bits = temp();
                inst(bits + " = bitcast " + llvmType(type) + " " + positive + " to i" + std::to_string(type.bits));
            }
            int width = type.isBool() ? 1 : type.bits;
            if (width < 64) {
                std::string wide = temp();
                inst(wide + " = zext i" + std::to_string(width) + " " + bits + " to i64");
                bits = wide;
            }
            inst(result + " = call i64 @cromo_hash_bits(i64 " + bits + ")");
            return {result, Type::intType(64, false)};
        }
        case Intrinsic::DecimalToFloat: {  // D199
            std::string data = arg(0);
            std::string length = arg(1);
            inst(result + " = call double @cromo_parse_float(ptr " + data + ", i64 " + length + ", i64 " + arg(2) + ")");
            return {result, Type::floatType(64)};
        }
        case Intrinsic::None: break;
        }
        return {"", Type::error()};
    }

    // ---- Expressions ----

    Value load(const std::string& address, Type type) {
        std::string result = temp();
        inst(result + " = load " + llvmType(type) + ", ptr " + address);
        return {result, type};
    }

    // The value of a lambda: its code and its data (D183, D186). As an argument the
    // data are in the frame of the caller, and what they own goes at the end of the
    // statement; otherwise they are on the heap, owned by the value.
    Value createLambda(const LambdaExpr& lambda) {
        const FunctionDecl& function = *lambda.function;
        std::string code = llvmName('@', function.symbol);
        std::vector<const Capture*> captures = capturesOf(lambda);
        if (captures.empty()) return {"{ ptr " + code + ", ptr null }", lambda.type};
        std::string type = environmentType(function);
        std::string env = temp();
        if (lambda.onStack) {
            allocas_ << "  " << env << " = alloca " << type << "\n";
        } else {
            std::string size = temp();
            inst(size + " = ptrtoint ptr getelementptr (" + type + ", ptr null, i32 1) to i64");
            inst(env + " = call ptr @cromo_alloc(i64 " + size + ")");
        }
        bool owns = !lambda.onStack || ownsCaptures(lambda);
        std::string destroy = owns ? llvmName('@', function.symbol + (lambda.onStack ? ".release" : ".drop")) : "null";
        inst("store ptr " + destroy + ", ptr " + env);
        for (const Capture* capture : captures) {
            const Variable& outer = *capture->outer;
            std::string field = temp();
            inst(field + " = getelementptr inbounds " + type + ", ptr " + env + ", i32 0, i32 " +
                 std::to_string(capture->inner->captureIndex + 1));
            std::string source = addresses_[outer.id];
            if (capture->byAddress) {
                inst("store ptr " + source + ", ptr " + field);
                continue;
            }
            Value value = load(source, outer.type);
            inst("store " + llvmType(outer.type) + " " + value.ref + ", ptr " + field);
            if (capture->mode == ParamMode::Move && needsDestruction(outer.type)) setAlive(outer, false);
        }
        std::string half = temp();
        std::string pair = temp();
        inst(half + " = insertvalue { ptr, ptr } undef, ptr " + code + ", 0");
        inst(pair + " = insertvalue { ptr, ptr } " + half + ", ptr " + env + ", 1");
        if (lambda.onStack && owns && !lambda.forThread) {  // the thread owns them (D215)
            std::string place = slot(lambda.type);
            inst("store { ptr, ptr } " + pair + ", ptr " + place);
            temporaries_.push_back(LiveObject{place, lambda.type, nullptr});
        }
        return {pair, lambda.type};
    }

    static std::vector<const Capture*> capturesOf(const LambdaExpr& lambda) {
        std::vector<const Capture*> captures;
        for (const Capture& capture : lambda.captures) {
            if (capture.inner) captures.push_back(&capture);
        }
        return captures;
    }

    // The values taken with `move` that need to be destroyed.
    static bool ownsCaptures(const LambdaExpr& lambda) {
        for (const Capture* capture : capturesOf(lambda)) {
            if (!capture->byAddress && needsDestruction(capture->outer->type)) return true;
        }
        return false;
    }

    // The type of the data of a lambda: before the code, which takes its size.
    static std::string lambdaType(const LambdaExpr& lambda) {
        std::vector<const Capture*> captures = capturesOf(lambda);
        if (captures.empty()) return "";
        std::string out = environmentType(*lambda.function) + " = type { ptr";
        for (const Capture* capture : captures) out += ", " + (capture->byAddress ? std::string("ptr") : llvmType(capture->outer->type));
        return out + " }\n";
    }

    // The functions that destroy the data of a lambda: .drop also frees them from
    // the heap, .release leaves them in the frame (D184).
    std::string lambdaData(const LambdaExpr& lambda) {
        const FunctionDecl& function = *lambda.function;
        std::vector<const Capture*> captures = capturesOf(lambda);
        if (captures.empty()) return "";
        std::string type = environmentType(function);
        std::string out;
        for (bool frees : {true, false}) {
            startFunction(nullptr);
            for (const Capture* capture : captures) {
                if (capture->byAddress || !needsDestruction(capture->outer->type)) continue;
                std::string field = temp();
                inst(field + " = getelementptr inbounds " + type + ", ptr %env, i32 0, i32 " +
                     std::to_string(capture->inner->captureIndex + 1));
                drop(field, capture->outer->type);
            }
            if (frees) inst("call void @cromo_free(ptr %env)");
            term("ret void");
            out += finishFunction("void", llvmName('@', function.symbol + (frees ? ".drop" : ".release")), {"ptr %env"});
        }
        return out;
    }

    // `less(a, b)`: through the address of the code, with the data of the lambda
    // where a method has its object (D186).
    Value callValue(const CallValueExpr& call, const std::string& dest) {
        Value callee = generateExpr(*call.callee);
        std::string code = temp();
        std::string env = temp();
        std::string empty = temp();
        inst(code + " = extractvalue { ptr, ptr } " + callee.ref + ", 0");
        inst(env + " = extractvalue { ptr, ptr } " + callee.ref + ", 1");
        inst(empty + " = icmp eq ptr " + code + ", null");
        panicIf(empty, "the function is empty: no lambda was given to it (D189)", call.loc);
        return emitCall(*call.signature, env, Type::error(), call.args, dest, call.loc, &call.overlaps, code);
    }

    Value generateExpr(const Expr& expr) {
        if (expr.kind == ExprKind::Call || expr.kind == ExprKind::MethodCall || expr.kind == ExprKind::CallValue) {
            location_ = expr.loc;  // a call on a line of its own, for the stack traces (D339)
        }
        switch (expr.kind) {
        case ExprKind::IntLiteral:
        case ExprKind::FloatLiteral:
        case ExprKind::BoolLiteral:
            return {literalConstant(expr, expr.type), expr.type};
        case ExprKind::StringLiteral:
            if (expr.type.isStruct()) return {stringValue(static_cast<const StringLiteralExpr&>(expr)), expr.type};
            return {stringConstant(static_cast<const StringLiteralExpr&>(expr).value), expr.type};
        case ExprKind::Name:
        case ExprKind::Field:
        case ExprKind::Index:
        case ExprKind::Self:
        case ExprKind::Construct:
        case ExprKind::EnumCase:
            return load(addressOf(expr), expr.type);
        case ExprKind::Unary:
            return generateUnary(static_cast<const UnaryExpr&>(expr));
        case ExprKind::Binary:
            return generateBinary(static_cast<const BinaryExpr&>(expr));
        case ExprKind::Call: {
            if (expr.type.isAggregate()) return load(addressOf(expr), expr.type);
            const auto& call = static_cast<const CallExpr&>(expr);
            if (call.intrinsic != Intrinsic::None) return generateIntrinsic(call, "");
            return emitCall(*call.function, "", Type::error(), call.args, "", call.loc, &call.overlaps);
        }
        case ExprKind::MethodCall: {
            if (expr.type.isAggregate()) return load(addressOf(expr), expr.type);
            return emitMethodCall(static_cast<const MethodCallExpr&>(expr), "");
        }
        case ExprKind::Conversion: {
            const auto& conversion = static_cast<const ConversionExpr&>(expr);
            if (expr.type.isInterface()) return boxValue(conversion);  // D357
            if (expr.type.isStruct()) return load(addressOf(expr), expr.type);  // `Color(n)` (D135)
            if (conversion.operand->type.isStruct()) {
                // `i32(color)`: the value of the case (D135).
                const StructDecl& decl = *conversion.operand->type.structDecl;
                Value tag = load(enumTag(addressOf(*conversion.operand), decl), Type::intType(32, true));
                return convert(tag, expr.type, expr.loc);
            }
            return convert(generateExpr(*conversion.operand), expr.type, expr.loc);
        }
        case ExprKind::Print:
            if (expr.type.isAggregate()) return load(addressOf(expr), expr.type);  // format (D200)
            generatePrint(static_cast<const PrintExpr&>(expr), "");
            return {"", Type::voidType()};
        case ExprKind::Lambda:
            return createLambda(static_cast<const LambdaExpr&>(expr));
        case ExprKind::ListLiteral:
            return load(addressOf(expr), expr.type);
        case ExprKind::CallValue:
            if (expr.type.isAggregate()) return load(addressOf(expr), expr.type);
            return callValue(static_cast<const CallValueExpr&>(expr), "");
        case ExprKind::Move:
        case ExprKind::Copy:
            if (expr.type.isAggregate()) return load(addressOf(expr), expr.type);
            if (expr.kind == ExprKind::Move && (expr.type.isFunction() || expr.type.isInterface())) {
                // The value changes owner (D184, D357).
                const auto& move = static_cast<const MoveExpr&>(expr);
                std::string address = addressOf(*move.operand);
                Value value = load(address, expr.type);
                finishMove(move, address);
                return value;
            }
            // A simple value: moving or copying it is reading it (D80).
            return generateExpr(expr.kind == ExprKind::Move ? *static_cast<const MoveExpr&>(expr).operand
                                                            : *static_cast<const CopyExpr&>(expr).operand);
        case ExprKind::Ref:
            return generateExpr(*static_cast<const RefExpr&>(expr).operand);
        case ExprKind::TypeRef:
            break;
        }
        return {"", Type::error()};
    }

    Value generateUnary(const UnaryExpr& unary) {
        Value operand = generateExpr(*unary.operand);
        std::string result = temp();
        if (unary.op == UnaryOp::Not) {  // on an integer it inverts the bits (D194)
            inst(result + " = xor " + llvmType(operand.type) + " " + operand.ref + ", " + (operand.type.isBool() ? "true" : "-1"));
            return {result, operand.type};
        }
        if (operand.type.isFloat()) {
            inst(result + " = fneg " + llvmType(operand.type) + " " + operand.ref);
            return {result, operand.type};
        }
        return arithmetic(BinaryOp::Sub, Value{"0", operand.type}, operand, unary.loc);
    }

    Value generateBinary(const BinaryExpr& binary) {
        if (binary.op == BinaryOp::And || binary.op == BinaryOp::Or) return shortCircuit(binary);

        Value lhs = generateExpr(*binary.lhs);
        Value rhs = generateExpr(*binary.rhs);
        if (binary.op == BinaryOp::Add || binary.op == BinaryOp::Sub || binary.op == BinaryOp::Mul ||
            binary.op == BinaryOp::Div || binary.op == BinaryOp::Rem || binary.op >= BinaryOp::BitAnd) {
            return arithmetic(binary.op, lhs, rhs, binary.loc);
        }

        return compare(binary.op, binary.operandType, lhs, rhs);
    }

    // A comparison of two values already computed.
    Value compare(BinaryOp op, Type type, const Value& lhs, const Value& rhs) {
        if (type.isStruct()) {  // enums without data: their tags (D112)
            std::string left = temp();
            std::string right = temp();
            std::string result = temp();
            inst(left + " = extractvalue " + llvmType(type) + " " + lhs.ref + ", 1");
            inst(right + " = extractvalue " + llvmType(type) + " " + rhs.ref + ", 1");
            inst(result + " = icmp " + (op == BinaryOp::Eq ? "eq" : "ne") + " i32 " + left + ", " + right);
            return {result, Type::boolType()};
        }
        std::string predicate;
        if (type.isFloat()) {
            static const std::map<BinaryOp, std::string> floats = {
                {BinaryOp::Eq, "oeq"}, {BinaryOp::Ne, "une"}, {BinaryOp::Lt, "olt"},
                {BinaryOp::Le, "ole"}, {BinaryOp::Gt, "ogt"}, {BinaryOp::Ge, "oge"}};
            predicate = "fcmp " + floats.at(op);
        } else {
            bool isSigned = type.isInt() && type.isSigned;
            static const std::map<BinaryOp, std::string> signedOps = {
                {BinaryOp::Eq, "eq"}, {BinaryOp::Ne, "ne"}, {BinaryOp::Lt, "slt"},
                {BinaryOp::Le, "sle"}, {BinaryOp::Gt, "sgt"}, {BinaryOp::Ge, "sge"}};
            static const std::map<BinaryOp, std::string> unsignedOps = {
                {BinaryOp::Eq, "eq"}, {BinaryOp::Ne, "ne"}, {BinaryOp::Lt, "ult"},
                {BinaryOp::Le, "ule"}, {BinaryOp::Gt, "ugt"}, {BinaryOp::Ge, "uge"}};
            predicate = "icmp " + (isSigned ? signedOps : unsignedOps).at(op);
        }
        std::string result = temp();
        inst(result + " = " + predicate + " " + llvmType(type) + " " + lhs.ref + ", " + rhs.ref);
        return {result, Type::boolType()};
    }

    // && and || evaluate the right side only when needed (D59). The temporaries
    // of the right side exist only on that path: they are destroyed there.
    Value shortCircuit(const BinaryExpr& binary) {
        bool isAnd = binary.op == BinaryOp::And;
        Value lhs = generateExpr(*binary.lhs);
        std::string lhsBlock = current_;
        std::string rhsLabel = label(isAnd ? "and.rhs" : "or.rhs");
        std::string endLabel = label(isAnd ? "and.end" : "or.end");
        if (isAnd) {
            term("br i1 " + lhs.ref + ", label %" + rhsLabel + ", label %" + endLabel);
        } else {
            term("br i1 " + lhs.ref + ", label %" + endLabel + ", label %" + rhsLabel);
        }
        block(rhsLabel);
        Value rhs = generateCondition(*binary.rhs);
        std::string rhsBlock = current_;
        term("br label %" + endLabel);
        block(endLabel);
        std::string result = temp();
        inst(result + " = phi i1 [ " + std::string(isAnd ? "false" : "true") + ", %" + lhsBlock + " ], [ " + rhs.ref +
             ", %" + rhsBlock + " ]");
        return {result, Type::boolType()};
    }

    // + - * / % with the checks of D30, D36 and D60.
    // D195: a count out of the width is a panic in debug, and taken modulo the
    // width in release. `>>` keeps the sign of a signed integer.
    Value shift(BinaryOp op, Value lhs, Value rhs, SourceLocation loc) {
        Type type = lhs.type;
        std::string t = llvmType(type);
        std::string count = rhs.ref;
        if (!options_.release) {
            std::string wide = count;
            if (rhs.type.bits < 64) {
                wide = temp();
                inst(wide + " = " + (rhs.type.isSigned ? "sext " : "zext ") + llvmType(rhs.type) + " " + count + " to i64");
            } else if (rhs.type.bits > 64) {
                wide = temp();
                inst(wide + " = trunc " + llvmType(rhs.type) + " " + count + " to i64");
            }
            std::string outside = temp();
            inst(outside + " = icmp uge i64 " + wide + ", " + std::to_string(type.bits));  // a negative count is huge
            panicIf(outside, "shift count out of the width of " + type.name() + " (D195)", loc);
        }
        if (rhs.type.bits != type.bits) {
            std::string fitted = temp();
            inst(fitted + " = " + (rhs.type.bits < type.bits ? (rhs.type.isSigned ? "sext " : "zext ") : "trunc ") +
                 llvmType(rhs.type) + " " + count + " to " + t);
            count = fitted;
        }
        std::string masked = temp();
        inst(masked + " = and " + t + " " + count + ", " + std::to_string(type.bits - 1));
        std::string result = temp();
        const char* name = op == BinaryOp::Shl ? "shl" : type.isSigned ? "ashr" : "lshr";
        inst(result + " = " + name + " " + t + " " + lhs.ref + ", " + masked);
        return {result, type};
    }

    Value arithmetic(BinaryOp op, Value lhs, Value rhs, SourceLocation loc) {
        Type type = lhs.type;
        std::string t = llvmType(type);
        std::string result = temp();

        if (type.isFloat()) {
            static const std::map<BinaryOp, std::string> ops = {
                {BinaryOp::Add, "fadd"}, {BinaryOp::Sub, "fsub"}, {BinaryOp::Mul, "fmul"}, {BinaryOp::Div, "fdiv"}};
            inst(result + " = " + ops.at(op) + " " + t + " " + lhs.ref + ", " + rhs.ref);
            return {result, type};
        }

        if (op == BinaryOp::BitAnd || op == BinaryOp::BitOr || op == BinaryOp::BitXor) {  // D193
            const char* name = op == BinaryOp::BitAnd ? "and" : op == BinaryOp::BitOr ? "or" : "xor";
            inst(result + " = " + name + " " + t + " " + lhs.ref + ", " + rhs.ref);
            return {result, type};
        }
        if (op == BinaryOp::Shl || op == BinaryOp::Shr) return shift(op, lhs, rhs, loc);

        if (op == BinaryOp::Add || op == BinaryOp::Sub || op == BinaryOp::Mul) {
            const char* name = op == BinaryOp::Add ? "add" : op == BinaryOp::Sub ? "sub" : "mul";
            if (options_.release) {  // D57: integers wrap around
                inst(result + " = " + name + " " + t + " " + lhs.ref + ", " + rhs.ref);
                return {result, type};
            }
            std::string fn = std::string("@llvm.") + (type.isSigned ? "s" : "u") + name + ".with.overflow." + t;
            intrinsic("declare { " + t + ", i1 } " + fn + "(" + t + ", " + t + ")", fn);
            std::string pair = temp();
            std::string overflow = temp();
            inst(pair + " = call { " + t + ", i1 } " + fn + "(" + t + " " + lhs.ref + ", " + t + " " + rhs.ref + ")");
            inst(result + " = extractvalue { " + t + ", i1 } " + pair + ", 0");
            inst(overflow + " = extractvalue { " + t + ", i1 } " + pair + ", 1");
            panicIf(overflow, "integer overflow", loc);
            return {result, type};
        }

        // Division and remainder: dividing by zero always panics (D36).
        std::string isZero = temp();
        inst(isZero + " = icmp eq " + t + " " + rhs.ref + ", 0");
        panicIf(isZero, "division by zero", loc);
        if (!type.isSigned) {
            inst(result + " = " + (op == BinaryOp::Div ? "udiv " : "urem ") + t + " " + lhs.ref + ", " + rhs.ref);
            return {result, type};
        }

        // MIN / -1 overflows, and in LLVM it is undefined: handle -1 apart (D60).
        std::string isMinusOne = temp();
        inst(isMinusOne + " = icmp eq " + t + " " + rhs.ref + ", -1");
        if (op == BinaryOp::Div && !options_.release) {
            std::string isMin = temp();
            std::string overflow = temp();
            inst(isMin + " = icmp eq " + t + " " + lhs.ref + ", " + intConstant(uint64_t{1} << (type.bits - 1), true, type.bits));
            inst(overflow + " = and i1 " + isMinusOne + ", " + isMin);
            panicIf(overflow, "integer overflow", loc);
        }
        std::string safeDivisor = temp();
        std::string quotient = temp();
        inst(safeDivisor + " = select i1 " + isMinusOne + ", " + t + " 1, " + t + " " + rhs.ref);
        inst(quotient + " = " + (op == BinaryOp::Div ? "sdiv " : "srem ") + t + " " + lhs.ref + ", " + safeDivisor);
        if (op == BinaryOp::Div) {
            std::string negated = temp();
            inst(negated + " = sub " + t + " 0, " + lhs.ref);
            inst(result + " = select i1 " + isMinusOne + ", " + t + " " + negated + ", " + t + " " + quotient);
        } else {
            inst(result + " = select i1 " + isMinusOne + ", " + t + " 0, " + t + " " + quotient);
        }
        return {result, type};
    }

    // Conversions between numeric types (D33, D61).
    Value convert(Value value, Type to, SourceLocation loc) {
        Type from = value.type;
        if (from == to) return value;
        std::string result = temp();
        std::string source = llvmType(from) + " " + value.ref;
        std::string target = llvmType(to);

        if (from.isInt() && to.isInt()) {
            if (from.bits == to.bits) return {value.ref, to};
            const char* op = to.bits < from.bits ? "trunc" : (from.isSigned ? "sext" : "zext");
            inst(result + " = " + op + " " + source + " to " + target);
        } else if (from.isInt() && to.isFloat()) {
            inst(result + " = " + (from.isSigned ? "sitofp " : "uitofp ") + source + " to " + target);
        } else if (from.isFloat() && to.isFloat()) {
            inst(result + " = " + (to.bits > from.bits ? "fpext " : "fptrunc ") + source + " to " + target);
        } else if (from.isFloat() && to.isInt()) {
            return floatToInt(value, to, loc);
        } else {
            return value;
        }
        return {result, to};
    }

    // Truncates toward zero. Out of range or nan: panic in debug, the nearest
    // value in release (D61).
    Value floatToInt(Value value, Type to, SourceLocation loc) {
        Type from = value.type;
        std::string target = llvmType(to);
        std::string floatName = from.bits == 32 ? "f32" : "f64";
        std::string result = temp();

        if (options_.release) {
            std::string fn = std::string("@llvm.") + (to.isSigned ? "fptosi" : "fptoui") + ".sat." + target + "." + floatName;
            intrinsic("declare " + target + " " + fn + "(" + llvmType(from) + ")", fn);
            inst(result + " = call " + target + " " + fn + "(" + llvmType(from) + " " + value.ref + ")");
            return {result, to};
        }

        // The check is done in double precision, where all the bounds are exact.
        std::string wide = value.ref;
        if (from.bits == 32) {
            wide = temp();
            inst(wide + " = fpext float " + value.ref + " to double");
        }
        double lower, upper;
        bool lowerInclusive;
        if (to.isSigned) {
            upper = std::ldexp(1.0, to.bits - 1);
            lower = to.bits < 64 ? -upper - 1 : -upper;
            lowerInclusive = to.bits == 64;
        } else {
            upper = std::ldexp(1.0, to.bits);
            lower = -1;
            lowerInclusive = false;
        }
        std::string aboveLower = temp();
        std::string belowUpper = temp();
        std::string inRange = temp();
        std::string outOfRange = temp();
        inst(aboveLower + " = fcmp " + (lowerInclusive ? "oge" : "ogt") + " double " + wide + ", " + floatConstant(lower, 64));
        inst(belowUpper + " = fcmp olt double " + wide + ", " + floatConstant(upper, 64));
        inst(inRange + " = and i1 " + aboveLower + ", " + belowUpper);
        inst(outOfRange + " = xor i1 " + inRange + ", true");
        panicIf(outOfRange, "floating-point value out of range for " + to.name(), loc);
        inst(result + " = " + (to.isSigned ? "fptosi " : "fptoui ") + llvmType(from) + " " + value.ref + " to " + target);
        return {result, to};
    }

    // A number or a bool to a stream, or to a string being formatted (D38).
    void writeNumber(const std::string& stream, const Value& value, const std::string& spec) {
        Type type = value.type;
        if (type.isInt()) {
            std::string wide = value.ref;
            if (type.bits < 64) {
                wide = temp();
                inst(wide + " = " + (type.isSigned ? "sext " : "zext ") + llvmType(type) + " " + value.ref + " to i64");
            }
            inst("call void @cromo_fmt_int(" + stream + ", i64 " + wide + ", i32 " + (type.isSigned ? "1" : "0") + ", " + spec +
                 ")");
        } else if (type.isFloat()) {
            const char* fn = type.bits == 32 ? "@cromo_fmt_f32" : "@cromo_fmt_f64";
            inst(std::string("call void ") + fn + "(" + stream + ", " + llvmType(type) + " " + value.ref + ", " + spec + ")");
        } else {
            std::string wide = temp();
            inst(wide + " = zext i1 " + value.ref + " to i32");
            inst("call void @cromo_fmt_bool(" + stream + ", i32 " + wide + ", " + spec + ")");
        }
    }

    void writeText(const std::string& stream, const std::string& text) {
        if (text.empty()) return;
        inst("call void @cromo_write(" + stream + ", ptr " + stringConstant(text) + ", i64 " + std::to_string(text.size()) + ")");
    }

    // `assert(condition)` (D351): on false a panic, "assertion failed: total == 7
    // (total is 6)", made as a format. The sides of a comparison are computed once.
    void generateAssert(const CallExpr& call) {
        const Expr& condition = *call.args[0];
        const BinaryExpr* comparison = nullptr;
        Value holds;
        Value left;
        Value right;
        if (!call.leftText.empty() || !call.rightText.empty()) {
            comparison = static_cast<const BinaryExpr*>(&condition);
            left = generateExpr(*comparison->lhs);
            right = generateExpr(*comparison->rhs);
            holds = compare(comparison->op, comparison->operandType, left, right);
        } else {
            holds = generateCondition(condition);
        }
        std::string fail = label("assert.fail");
        std::string ok = label("assert.ok");
        term("br i1 " + holds.ref + ", label %" + ok + ", label %" + fail);
        block(fail);
        Type text = call.typeArg;
        std::string message = slot(text);
        storeDefault(message, text);
        std::string address = temp();
        inst(address + " = ptrtoint ptr " + message + " to i64");
        std::string stream = "i64 " + address;
        writeText(stream, "assertion failed: " + call.assertText);
        if (comparison) {
            writeText(stream, " (");
            if (!call.leftText.empty()) {
                writeText(stream, call.leftText + " is ");
                writeNumber(stream, left, "ptr null");
            }
            if (!call.leftText.empty() && !call.rightText.empty()) writeText(stream, ", ");
            if (!call.rightText.empty()) {
                writeText(stream, call.rightText + " is ");
                writeNumber(stream, right, "ptr null");
            }
            writeText(stream, ")");
        }
        const StructDecl& decl = *text.structDecl;
        Value data = load(listField(message, decl, "data"), Type::addrType());
        Value length = load(listField(message, decl, "length"), Type::intType(64, true));
        inst("call void @cromo_panic(ptr " + data.ref + ", i64 " + length.ref + ", ptr " + where(call.loc) + ")");
        term("unreachable");
        block(ok);
    }

    // print and println, or format into `dest` (D200): the format was parsed by
    // sema, so here there are only calls to the runtime, one per piece (D38). The
    // arguments are computed first: a format inside them is finished before this
    // one starts writing.
    void generatePrint(const PrintExpr& print, const std::string& dest) {
        std::string stream = "i64 " + std::to_string(print.stream);
        std::vector<Value> values;
        for (const ExprPtr& arg : print.args) values.push_back(generateExpr(*arg));
        if (print.stream == 3) {  // the runtime writes into the string itself, which starts empty
            storeDefault(dest, print.type);
            std::string address = temp();
            inst(address + " = ptrtoint ptr " + dest + " to i64");
            stream = "i64 " + address;
        } else {
            inst("call void @cromo_print_begin()");  // one print at a time among the threads (D214)
        }

        for (const FormatPiece& piece : print.pieces) {
            if (piece.arg < 0) {
                if (piece.text.empty()) continue;
                inst("call void @cromo_write(" + stream + ", ptr " + stringConstant(piece.text) + ", i64 " +
                     std::to_string(piece.text.size()) + ")");
                continue;
            }
            std::string spec = "ptr " + (piece.hasSpec ? specConstant(piece.spec) : std::string("null"));
            const Value& value = values[piece.arg];
            Type type = value.type;
            if (type.isNumeric() || type.isBool()) {
                writeNumber(stream, value, spec);
            } else if (type.isStruct()) {  // a string (D100)
                const StructDecl& decl = *type.structDecl;
                std::string data = temp();
                std::string length = temp();
                inst(data + " = extractvalue " + llvmType(type) + " " + value.ref + ", " +
                     std::to_string(fieldIndexOf(decl, "data")));
                inst(length + " = extractvalue " + llvmType(type) + " " + value.ref + ", " +
                     std::to_string(fieldIndexOf(decl, "length")));
                inst("call void @cromo_fmt_str(" + stream + ", ptr " + data + ", i64 " + length + ", " + spec + ")");
            } else {
                const auto& literal = static_cast<const StringLiteralExpr&>(*print.args[piece.arg]);
                inst("call void @cromo_fmt_str(" + stream + ", ptr " + value.ref + ", i64 " +
                     std::to_string(literal.value.size()) + ", " + spec + ")");
            }
        }
        if (print.stream != 3) inst("call void @cromo_print_end(" + stream + ", i32 " + (print.newline ? "1" : "0") + ")");
    }

    const CodegenOptions& options_;
    std::ostringstream body_;
    std::ostringstream allocas_;
    std::ostringstream globals_;
    std::map<std::string, std::string> strings_;
    std::map<std::string, std::string> wheres_;  // panic locations already emitted
    std::set<std::string> intrinsics_;
    std::set<std::string> tables_;  // method tables already emitted
    std::set<std::string> releases_;  // the releases of the objects of interfaces (D357)
    std::string releaseBodies_;
    std::map<const StructDecl*, bool> computedDefaults_;  // D360
    const FunctionDecl* function_ = nullptr;
    std::vector<std::string> addresses_;            // by Variable::id
    std::vector<std::string> flags_;                // drop flags (D77), by Variable::id
    std::vector<Scope> scopes_;                     // variables to destroy, one list per open block
    std::vector<LiveObject> temporaries_;           // to destroy at the end of the statement
    std::vector<Loop> loops_;
    std::string dropFields_;                        // in a destructor: the block that destroys the fields
    std::string current_;
    int nextTemp_ = 0;
    int nextLabel_ = 0;
    int nextSlot_ = 0;
    int nextSpec_ = 0;
    bool terminated_ = false;
    bool tracksCaller_ = false;  // in the standard library: panics report the caller (D102)
    const FunctionDecl* programArguments_ = nullptr;  // of the standard library, for main (D154)
    std::string localRaised_;    // the slot for exceptions of a function that cannot throw
    std::vector<Caught> catches_;
    bool freesExceptions_ = false;
    const InterfaceDecl* exceptionInterface_ = nullptr;
    // Debug information (D338)
    std::ostringstream meta_;
    int nextMeta_ = 0;
    std::string compileUnit_;
    std::string subroutineType_;
    std::vector<std::string> files_;                  // the DIFile of each source file, by SourceLocation::file
    std::string subprogram_;                          // of the function being generated, or none
    std::vector<std::string> debugScopes_;            // its open blocks
    SourceLocation location_;                         // of the code being generated
    std::map<std::string, std::string> locations_;    // the DILocation already made
    std::map<std::string, std::string> debugTypes_;
    std::map<const FunctionDecl*, const FunctionDecl*> enclosing_;  // of each lambda
    std::string programModule_;  // the name of the module of the program and a dot, in front of its functions
};

}  // namespace

std::string generateIR(const Module& module, const CodegenOptions& options) {
    return Generator(options).run(module);
}

}  // namespace cromo
