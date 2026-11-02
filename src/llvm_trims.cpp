// What cromo leaves out of the libraries of LLVM, to be smaller.
//
// lld initializes every target of LLVM, for the LTO that cromo does not use, so it
// would bring them all into cromo, which makes code only for x86-64 and AArch64
// (D142, D405). These empty initializers take the place of those of the other
// targets: the linker finds them first, and the code of the targets stays in their
// archives.

#define CROMO_NO_TARGET(name)                                 \
    extern "C" void LLVMInitialize##name##TargetInfo() {}     \
    extern "C" void LLVMInitialize##name##Target() {}         \
    extern "C" void LLVMInitialize##name##TargetMC() {}       \
    extern "C" void LLVMInitialize##name##AsmPrinter() {}     \
    extern "C" void LLVMInitialize##name##AsmParser() {}      \
    extern "C" void LLVMInitialize##name##Disassembler() {}

CROMO_NO_TARGET(AMDGPU)
CROMO_NO_TARGET(ARM)
CROMO_NO_TARGET(AVR)
CROMO_NO_TARGET(BPF)
CROMO_NO_TARGET(Hexagon)
CROMO_NO_TARGET(Lanai)
CROMO_NO_TARGET(LoongArch)
CROMO_NO_TARGET(Mips)
CROMO_NO_TARGET(MSP430)
CROMO_NO_TARGET(NVPTX)
CROMO_NO_TARGET(PowerPC)
CROMO_NO_TARGET(RISCV)
CROMO_NO_TARGET(Sparc)
CROMO_NO_TARGET(SPIRV)
CROMO_NO_TARGET(SystemZ)
CROMO_NO_TARGET(VE)
CROMO_NO_TARGET(WebAssembly)
CROMO_NO_TARGET(XCore)

#ifndef _WIN32
// libxml2 serves only the manifests of Windows that lld-link merges with /manifest,
// which cromo never asks for, and it brings 28 MB of ICU with it on Linux. These
// take its place: reading a manifest fails, and nothing else is ever called.
extern "C" {
void* xmlReadMemory(const char*, int, const char*, const char*, int) { return nullptr; }
void* xmlDocGetRootElement(void*) { return nullptr; }
void* xmlDocSetRootElement(void*, void*) { return nullptr; }
void* xmlNewDoc(const unsigned char*) { return nullptr; }
void* xmlNewNs(void*, const unsigned char*, const unsigned char*) { return nullptr; }
void* xmlNewProp(void*, const unsigned char*, const unsigned char*) { return nullptr; }
void* xmlAddChild(void*, void*) { return nullptr; }
void* xmlCopyNamespace(void*) { return nullptr; }
unsigned char* xmlStrdup(const unsigned char*) { return nullptr; }
void xmlDocDumpFormatMemoryEnc(void*, unsigned char** text, int* size, const char*, int) {
    *text = nullptr;
    *size = 0;
}
void xmlFreeDoc(void*) {}
void xmlFreeNode(void*) {}
void xmlFreeNs(void*) {}
void xmlUnlinkNode(void*) {}
void xmlSetGenericErrorFunc(void*, void*) {}
static void freeNothing(void*) {}
void (*xmlFree)(void*) = freeNothing;  // a variable in libxml2
}
#endif
