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

// Structs larger than 16 bytes are passed and returned through a pointer.
bool byPointer(Type type) { return type.isStruct() && sizeOf(type) > 16; }

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
           !function.isExtern && !implementsInterfaceMethod(function);
}

int fieldIndexOf(const StructDecl& decl, const std::string& name) {
    for (size_t i = 0; i < decl.fields.size(); ++i) {
        if (decl.fields[i].name == name) return static_cast<int>(i);
    }
    return -1;
}

std::string symbolOf(const FunctionDecl& function) {
    switch (function.kind) {
    case FunctionKind::Free: return llvmName('@', function.symbol);
    case FunctionKind::Method: return methodSymbol(*function.owner, function.name);
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

std::string defaultConstant(Type type) {
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
        out << "target triple = \"" << targetTriple(options_.target) << "\"\n\n";

        for (const InterfaceDecl& iface : module.interfaces) {
            if (iface.name == "Exception" && iface.loc.file == kLibraryFile) exceptionInterface_ = &iface;
        }
        for (const FunctionDecl& function : module.functions) {
            if (function.name == "programArguments" && function.loc.file == kLibraryFile) programArguments_ = &function;
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
        if (mainFunction) out << entryPoint(*mainFunction);

        if (freesExceptions_) out << freeExceptionFunction();
        out << globals_.str() << "\n";
        for (const FunctionDecl* function : externs) out << externDeclaration(*function);
        out << "declare void @cromo_write(i32, ptr, i64) nounwind\n";
        out << "declare void @cromo_fmt_int(i32, i64, i32, ptr) nounwind\n";
        out << "declare void @cromo_fmt_f64(i32, double, ptr) nounwind\n";
        out << "declare void @cromo_fmt_f32(i32, float, ptr) nounwind\n";
        out << "declare void @cromo_fmt_bool(i32, i32, ptr) nounwind\n";
        out << "declare void @cromo_fmt_str(i32, ptr, i64, ptr) nounwind\n";
        out << "declare void @cromo_print_end(i32, i32) nounwind\n";
        out << "declare void @cromo_exit(i32) noreturn nounwind\n";
        out << "declare void @cromo_panic(ptr, i64, ptr) noreturn nounwind cold\n";
        out << "declare void @cromo_panic_index(i64, i64, ptr) noreturn nounwind cold\n";
        out << "declare ptr @cromo_alloc(i64) nounwind\n";
        out << "declare ptr @cromo_realloc(ptr, i64) nounwind\n";
        out << "declare void @cromo_free(ptr) nounwind\n";
        if (!declared.count("memcmp")) out << "declare i32 @memcmp(ptr, ptr, i64) nounwind\n";
        out << "declare void @cromo_uncaught(ptr, i64) noreturn nounwind cold\n";
        out << "declare i64 @cromo_arg_count() nounwind\n";
        out << "declare i64 @cromo_arg_length(i64) nounwind\n";
        out << "declare ptr @cromo_arg_data(i64) nounwind\n";
        for (const std::string& declaration : intrinsics_) out << declaration << "\n";
        return out.str();
    }

private:
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
        body_ << "  " << line << "\n";
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
    }

    std::string finishFunction(const std::string& returnType, const std::string& symbol, const std::vector<std::string>& params) {
        std::ostringstream out;
        out << "define " << returnType << " " << symbol << "(";
        for (size_t i = 0; i < params.size(); ++i) out << (i ? ", " : "") << params[i];
        out << ") nounwind {\nentry:\n" << allocas_.str() << body_.str() << "}\n\n";
        return out.str();
    }

    std::string generateFunction(const FunctionDecl& function) {
        startFunction(&function);

        // The exclusivity rules (D22, D79) make every address parameter `noalias`.
        Type ret = function.resolvedReturn;
        std::vector<std::string> params;
        if (byPointer(ret)) params.push_back("ptr noalias sret(" + llvmType(ret) + ") %ret");
        if (function.owner) params.push_back(std::string("ptr noalias ") + (function.isConst ? "readonly " : "") + "%self");
        for (size_t i = 0; i < function.paramTypes.size(); ++i) {
            const Param& param = function.params[i];
            Type type = function.paramTypes[i];
            std::string arg = "%arg." + std::to_string(i);
            if (byAddress(param, type)) {
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
            Type type = Type::structType(function.owner);
            inst("store " + llvmType(type) + " " + defaultConstant(type) + ", ptr %self");
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
            } else if (field.isStruct()) {
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
        out << "define void @cromo_start() noreturn nounwind {\n";
        out << "entry:\n";
        std::vector<std::string> args;
        if (!mainFunction.params.empty()) {
            // D154: the arguments of the program, made by the standard library.
            const FunctionDecl& arguments = *programArguments_;
            std::string type = llvmType(arguments.resolvedReturn);
            out << "  %args = alloca " << type << "\n";
            out << "  call void " << symbolOf(arguments) << "(ptr %args" << (tracksCaller(arguments) ? ", ptr null" : "")
                << ")\n";
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
            out << "  call void " << symbolOf(mainFunction) << "(" << list << ")\n";
        } else {
            out << "  %code = call i32 " << symbolOf(mainFunction) << "(" << list << ")\n";
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
            out << "  call void %method(ptr %text, ptr %object)\n";
            out << "  %dataAddress = getelementptr inbounds " << structName(text) << ", ptr %text, i32 0, i32 "
                << fieldIndexOf(text, "data") << "\n";
            out << "  %data = load ptr, ptr %dataAddress\n";
            out << "  %lengthAddress = getelementptr inbounds " << structName(text) << ", ptr %text, i32 0, i32 "
                << fieldIndexOf(text, "length") << "\n";
            out << "  %length = load i64, ptr %lengthAddress\n";
            out << "  call void @cromo_uncaught(ptr %data, i64 %length)\n";
            out << "  unreachable\n";
            out << "done:\n";
        }
        out << "  call void @cromo_exit(i32 " << code << ")\n";
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
               "  call void @cromo_free(ptr %object)\n"
               "  ret void\n"
               "}\n\n";
    }

    // ---- Lifetimes (D68) ----

    void drop(const std::string& address, Type type) {
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
        inst(object + " = call ptr @cromo_alloc(i64 " + std::to_string(std::max(sizeOf(type), 1)) + ")");
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
        scopes_.emplace_back();
        for (const StmtPtr& statement : block.statements) generateStatement(*statement);
        if (!terminated_) dropObjects(scopes_.back().objects);
        scopes_.pop_back();
    }

    void generateStatement(const Stmt& statement) {
        size_t mark = temporaries_.size();
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
            if (expr.type.isStruct()) {
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
        if (type.isStruct()) {
            if (decl.init) {
                generateStructInto(*decl.init, address);
            } else {
                inst("store " + llvmType(type) + " " + defaultConstant(type) + ", ptr " + address);
            }
            if (needsDestruction(type)) scopes_.back().objects.push_back(LiveObject{address, type, &variable});
            setAlive(variable, true);
            return;
        }
        std::string value = decl.init ? generateExpr(*decl.init).ref : defaultConstant(type);
        inst("store " + llvmType(type) + " " + value + ", ptr " + address);
    }

    void generateAssign(const AssignStmt& assign) {
        if (assign.call) {  // `s += t` (D95)
            generateExpr(*assign.call);
            return;
        }
        Type type = assign.target->type;
        if (type.isStruct()) {
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
        if (assign.op != AssignOp::Assign) {
            static const BinaryOp ops[] = {BinaryOp::Add, BinaryOp::Add, BinaryOp::Sub, BinaryOp::Mul, BinaryOp::Div, BinaryOp::Rem};
            Value current = load(address, type);
            value = arithmetic(ops[static_cast<int>(assign.op)], current, value, assign.loc);
        }
        inst("store " + llvmType(type) + " " + value.ref + ", ptr " + address);
    }

    void generateReturn(const ReturnStmt& ret) {
        Type type = function_->resolvedReturn;
        const Variable* moved = nullptr;
        std::string result;
        if (ret.value && type.isStruct()) {
            // `ret local` moves the variable out (D23): it is not destroyed.
            if (ret.value->kind == ExprKind::Name) moved = static_cast<const NameExpr&>(*ret.value).variable;
            std::string dest = byPointer(type) ? "%ret" : slot(type);
            generateStructInto(*ret.value, dest);
            if (!byPointer(type)) result = dest;
        } else if (ret.value) {
            result = generateExpr(*ret.value).ref;
        }

        dropTemporaries(0);
        for (size_t i = scopes_.size(); i-- > 0;) leaveScope(i, moved);

        if (!dropFields_.empty()) {
            term("br label %" + dropFields_);
        } else if (!ret.value || byPointer(type)) {
            term("ret void");
        } else if (type.isStruct()) {
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
                    std::string literal = addressOf(*pattern.literal);
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
        const StructDecl& decl = *statement.list->type.structDecl;
        Type element = decl.typeArgs[0];
        Value length = load(listField(list, decl, "length"), i64);
        Value data = load(listField(list, decl, "data"), Type::addrType());
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
            Value index = generateExpr(*element.index);
            return elementAddress(list, *element.object->type.structDecl, index.ref, element.loc);
        }
        default: {
            std::string address = slot(expr.type);
            generateStructInto(expr, address);
            if (needsDestruction(expr.type)) temporaries_.push_back(LiveObject{address, expr.type, nullptr});
            return address;
        }
        }
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

    // The address of an element of a list, after the bounds check.
    std::string elementAddress(const std::string& list, const StructDecl& decl, const std::string& index, SourceLocation loc) {
        Value length = load(listField(list, decl, "length"), Type::intType(64, true));
        checkIndex(index, length.ref, loc);
        Value data = load(listField(list, decl, "data"), Type::addrType());
        std::string result = temp();
        inst(result + " = getelementptr inbounds " + llvmType(decl.typeArgs[0]) + ", ptr " + data.ref + ", i64 " + index);
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
        switch (expr.kind) {
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
                if (type.isStruct()) {
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
                inst("store " + structName(decl) + " " + defaultConstant(expr.type) + ", ptr " + dest);
                return;
            }
            emitCall(*decl.constructor, dest, expr.type, construct.args, "", expr.loc, &construct.overlaps);
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
            inst("store " + llvmType(move.type) + " " + defaultConstant(move.type) + ", ptr " + source);
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
        inst(object + " = extractvalue { ptr, ptr } " + pair.ref + ", 0");
        inst(table + " = extractvalue { ptr, ptr } " + pair.ref + ", 1");
        inst(entry + " = getelementptr inbounds ptr, ptr " + table + ", i64 " + std::to_string(index));
        inst(target + " = load ptr, ptr " + entry);
        return emitCall(*call.function, object, Type::error(), call.args, dest, call.loc, &call.overlaps, target);
    }

    // The method table of a struct for an interface, emitted the first time it is used.
    std::string methodTable(const StructDecl& decl, const InterfaceDecl& iface) {
        std::string name = llvmName('@', "vtable." + decl.symbol + "." + iface.symbol);
        if (!tables_.insert(name).second) return name;
        globals_ << name << " = private unnamed_addr constant [" << iface.methods.size() << " x ptr] [";
        for (size_t i = 0; i < iface.methods.size(); ++i) {
            globals_ << (i ? ", " : "") << "ptr " << methodSymbol(decl, iface.methods[i].name);
        }
        globals_ << "]\n";
        return name;
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
        if (ret.isStruct()) inst("store " + llvmType(ret) + " " + result + ", ptr " + dest);
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
        if (param.mode == ParamMode::Move && type.isStruct()) {
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
        if (type.isStruct()) return llvmType(type) + " " + load(remember(addressOf(expr)), type).ref;
        Value value = generateExpr(expr);
        return llvmType(value.type) + " " + value.ref;
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
            if (element.isStruct()) {
                copyStruct(dest, place, element);
                return {"", element};
            }
            return load(place, element);
        }
        case Intrinsic::Put: {
            std::string place = arg(0);
            const Expr& value = *call.args[1];
            if (element.isStruct()) {
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
            if (!element.isStruct()) return load(place, element);
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

    Value generateExpr(const Expr& expr) {
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
            if (expr.type.isStruct()) return load(addressOf(expr), expr.type);
            const auto& call = static_cast<const CallExpr&>(expr);
            if (call.intrinsic != Intrinsic::None) return generateIntrinsic(call, "");
            return emitCall(*call.function, "", Type::error(), call.args, "", call.loc, &call.overlaps);
        }
        case ExprKind::MethodCall: {
            if (expr.type.isStruct()) return load(addressOf(expr), expr.type);
            return emitMethodCall(static_cast<const MethodCallExpr&>(expr), "");
        }
        case ExprKind::Conversion: {
            const auto& conversion = static_cast<const ConversionExpr&>(expr);
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
            generatePrint(static_cast<const PrintExpr&>(expr));
            return {"", Type::voidType()};
        case ExprKind::Move:
        case ExprKind::Copy:
            if (expr.type.isStruct()) return load(addressOf(expr), expr.type);
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
        if (unary.op == UnaryOp::Not) {
            inst(result + " = xor i1 " + operand.ref + ", true");
            return {result, Type::boolType()};
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
            binary.op == BinaryOp::Div || binary.op == BinaryOp::Rem) {
            return arithmetic(binary.op, lhs, rhs, binary.loc);
        }

        Type type = binary.operandType;
        if (type.isStruct()) {  // enums without data: their tags (D112)
            std::string left = temp();
            std::string right = temp();
            std::string result = temp();
            inst(left + " = extractvalue " + llvmType(type) + " " + lhs.ref + ", 1");
            inst(right + " = extractvalue " + llvmType(type) + " " + rhs.ref + ", 1");
            inst(result + " = icmp " + (binary.op == BinaryOp::Eq ? "eq" : "ne") + " i32 " + left + ", " + right);
            return {result, Type::boolType()};
        }
        std::string predicate;
        if (type.isFloat()) {
            static const std::map<BinaryOp, std::string> floats = {
                {BinaryOp::Eq, "oeq"}, {BinaryOp::Ne, "une"}, {BinaryOp::Lt, "olt"},
                {BinaryOp::Le, "ole"}, {BinaryOp::Gt, "ogt"}, {BinaryOp::Ge, "oge"}};
            predicate = "fcmp " + floats.at(binary.op);
        } else {
            bool isSigned = type.isInt() && type.isSigned;
            static const std::map<BinaryOp, std::string> signedOps = {
                {BinaryOp::Eq, "eq"}, {BinaryOp::Ne, "ne"}, {BinaryOp::Lt, "slt"},
                {BinaryOp::Le, "sle"}, {BinaryOp::Gt, "sgt"}, {BinaryOp::Ge, "sge"}};
            static const std::map<BinaryOp, std::string> unsignedOps = {
                {BinaryOp::Eq, "eq"}, {BinaryOp::Ne, "ne"}, {BinaryOp::Lt, "ult"},
                {BinaryOp::Le, "ule"}, {BinaryOp::Gt, "ugt"}, {BinaryOp::Ge, "uge"}};
            predicate = "icmp " + (isSigned ? signedOps : unsignedOps).at(binary.op);
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

    // print/println: the format was parsed by sema, so here there are only calls
    // to the runtime, one per piece (D38).
    void generatePrint(const PrintExpr& print) {
        std::string stream = "i32 " + std::to_string(print.stream);
        std::vector<Value> values;
        for (const ExprPtr& arg : print.args) values.push_back(generateExpr(*arg));

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
            if (type.isInt()) {
                std::string wide = value.ref;
                if (type.bits < 64) {
                    wide = temp();
                    inst(wide + " = " + (type.isSigned ? "sext " : "zext ") + llvmType(type) + " " + value.ref + " to i64");
                }
                inst("call void @cromo_fmt_int(" + stream + ", i64 " + wide + ", i32 " + (type.isSigned ? "1" : "0") + ", " +
                     spec + ")");
            } else if (type.isFloat()) {
                const char* fn = type.bits == 32 ? "@cromo_fmt_f32" : "@cromo_fmt_f64";
                inst(std::string("call void ") + fn + "(" + stream + ", " + llvmType(type) + " " + value.ref + ", " + spec + ")");
            } else if (type.isBool()) {
                std::string wide = temp();
                inst(wide + " = zext i1 " + value.ref + " to i32");
                inst("call void @cromo_fmt_bool(" + stream + ", i32 " + wide + ", " + spec + ")");
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
        inst("call void @cromo_print_end(" + stream + ", i32 " + (print.newline ? "1" : "0") + ")");
    }

    const CodegenOptions& options_;
    std::ostringstream body_;
    std::ostringstream allocas_;
    std::ostringstream globals_;
    std::map<std::string, std::string> strings_;
    std::map<std::string, std::string> wheres_;  // panic locations already emitted
    std::set<std::string> intrinsics_;
    std::set<std::string> tables_;  // method tables already emitted
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
};

}  // namespace

std::string generateIR(const Module& module, const CodegenOptions& options) {
    return Generator(options).run(module);
}

}  // namespace cromo
