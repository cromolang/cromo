# Turns a binary file into a list of C++ byte values, to embed it in cromo.exe.
# Usage: cmake -DINPUT=<file> -DOUTPUT=<file.inc> -P embed.cmake
file(READ "${INPUT}" content HEX)
string(REGEX REPLACE "(([0-9a-f][0-9a-f]){32})" "\\1\n" content "${content}")
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," content "${content}")
file(WRITE "${OUTPUT}" "${content}\n")
