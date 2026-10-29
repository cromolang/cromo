// A DLL for the tests of M8 (D136, D137). run-tests.ps1 builds it into
// modules/adder.dll, and for 32 bits into modules/old32.dll.
__declspec(dllexport) int add(int a, int b) { return a + b; }
__declspec(dllexport) int twice(int x) { return 2 * x; }
