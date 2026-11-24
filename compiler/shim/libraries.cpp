// The source of the target of CMake that is never built, cromollvm-libraries: its link
// line in build.ninja says which libraries of LLVM and of LLD cromo needs, and in which
// order, to tools/llvm-archive.ps1 and tools/llvm-archive.sh (D576).
int main() {
    return 0;
}
