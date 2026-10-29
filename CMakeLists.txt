cmake_minimum_required(VERSION 3.20)
project(cromo VERSION 0.13.0 LANGUAGES CXX)  # 0.milestone.fix (D157)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# The LLVM libraries are Release builds: no Debug build, which would use a different runtime.
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "RelWithDebInfo or Release" FORCE)
endif()

set(CROMO_LLVM_ROOT "C:/software/llvm-23.1.4" CACHE PATH "LLVM with static libraries and LLD (D53)")
list(PREPEND CMAKE_PREFIX_PATH "${CROMO_LLVM_ROOT}")

# The LLVM archive is built with static zlib, zstd and libxml2 but does not ship them:
# vcpkg provides them (vcpkg.json). zstd must be found before LLVM, because with MSVC
# LLVM's Findzstd module only recognizes a static library named zstd_static.lib.
find_package(zstd CONFIG REQUIRED)
find_package(LLVM REQUIRED CONFIG)
find_package(LLD REQUIRED CONFIG)
message(STATUS "LLVM ${LLVM_PACKAGE_VERSION}: ${LLVM_DIR}")
message(STATUS "LLD: ${LLD_DIR}")

# The prebuilt LLVM libraries use the static C runtime (/MT) and have no RTTI.
# With the static runtime cromo.exe does not need the Visual C++ Redistributable.
set(CMAKE_MSVC_RUNTIME_LIBRARY MultiThreaded)

# ---- Temporary runtime in freestanding C (D58) ----
# Compiled with the clang of the LLVM archive, without the C library, once for
# Windows and once for Linux (D142), and embedded in cromo.exe as byte arrays.
set(CROMO_CLANG "${CROMO_LLVM_ROOT}/bin/clang.exe")
set(CROMO_LLVM_LIB "${CROMO_LLVM_ROOT}/bin/llvm-lib.exe")
set(CROMO_LLVM_AR "${CROMO_LLVM_ROOT}/bin/llvm-ar.exe")
set(CROMO_RT_DIR "${CMAKE_BINARY_DIR}/runtime")
set(CROMO_RT_FLAGS --target=x86_64-pc-windows-msvc -ffreestanding -fno-builtin -nostdlib -O2
    -fno-stack-protector -mno-stack-arg-probe -Wall -Wextra)
file(MAKE_DIRECTORY "${CROMO_RT_DIR}")

add_custom_command(
  OUTPUT "${CROMO_RT_DIR}/cromo_rt.obj"
  COMMAND "${CROMO_CLANG}" ${CROMO_RT_FLAGS} -c "${CMAKE_SOURCE_DIR}/runtime/cromo_rt.c" -o "${CROMO_RT_DIR}/cromo_rt.obj"
  DEPENDS runtime/cromo_rt.c
  COMMENT "Compiling the Cromo runtime")
add_custom_command(
  OUTPUT "${CROMO_RT_DIR}/mem_x64.obj"
  COMMAND "${CROMO_CLANG}" --target=x86_64-pc-windows-msvc -c "${CMAKE_SOURCE_DIR}/runtime/mem_x64.S" -o "${CROMO_RT_DIR}/mem_x64.obj"
  DEPENDS runtime/mem_x64.S
  COMMENT "Assembling the Cromo runtime")
add_custom_command(
  OUTPUT "${CROMO_RT_DIR}/cromo_rt.lib"
  COMMAND "${CROMO_LLVM_LIB}" /nologo "/out:${CROMO_RT_DIR}/cromo_rt.lib" "${CROMO_RT_DIR}/cromo_rt.obj" "${CROMO_RT_DIR}/mem_x64.obj"
  DEPENDS "${CROMO_RT_DIR}/cromo_rt.obj" "${CROMO_RT_DIR}/mem_x64.obj"
  COMMENT "Archiving the Cromo runtime")
add_custom_command(
  OUTPUT "${CROMO_RT_DIR}/runtime_blob.inc"
  COMMAND "${CMAKE_COMMAND}" "-DINPUT=${CROMO_RT_DIR}/cromo_rt.lib" "-DOUTPUT=${CROMO_RT_DIR}/runtime_blob.inc"
          -P "${CMAKE_SOURCE_DIR}/cmake/embed.cmake"
  DEPENDS "${CROMO_RT_DIR}/cromo_rt.lib" cmake/embed.cmake
  COMMENT "Embedding the Cromo runtime")

set(CROMO_RT_LINUX_FLAGS --target=x86_64-unknown-linux-gnu -ffreestanding -fno-builtin -nostdlib -O2
    -fno-stack-protector -fno-pic -Wall -Wextra)
add_custom_command(
  OUTPUT "${CROMO_RT_DIR}/cromo_rt_linux.o"
  COMMAND "${CROMO_CLANG}" ${CROMO_RT_LINUX_FLAGS} -c "${CMAKE_SOURCE_DIR}/runtime/cromo_rt.c" -o "${CROMO_RT_DIR}/cromo_rt_linux.o"
  DEPENDS runtime/cromo_rt.c
  COMMENT "Compiling the Cromo runtime for Linux")
add_custom_command(
  OUTPUT "${CROMO_RT_DIR}/mem_x64_linux.o"
  COMMAND "${CROMO_CLANG}" --target=x86_64-unknown-linux-gnu -c "${CMAKE_SOURCE_DIR}/runtime/mem_x64.S" -o "${CROMO_RT_DIR}/mem_x64_linux.o"
  DEPENDS runtime/mem_x64.S
  COMMENT "Assembling the Cromo runtime for Linux")
add_custom_command(
  OUTPUT "${CROMO_RT_DIR}/libcromo_rt.a"
  COMMAND "${CMAKE_COMMAND}" -E rm -f "${CROMO_RT_DIR}/libcromo_rt.a"
  COMMAND "${CROMO_LLVM_AR}" rcs "${CROMO_RT_DIR}/libcromo_rt.a" "${CROMO_RT_DIR}/cromo_rt_linux.o" "${CROMO_RT_DIR}/mem_x64_linux.o"
  DEPENDS "${CROMO_RT_DIR}/cromo_rt_linux.o" "${CROMO_RT_DIR}/mem_x64_linux.o"
  COMMENT "Archiving the Cromo runtime for Linux")
add_custom_command(
  OUTPUT "${CROMO_RT_DIR}/runtime_linux_blob.inc"
  COMMAND "${CMAKE_COMMAND}" "-DINPUT=${CROMO_RT_DIR}/libcromo_rt.a" "-DOUTPUT=${CROMO_RT_DIR}/runtime_linux_blob.inc"
          -P "${CMAKE_SOURCE_DIR}/cmake/embed.cmake"
  DEPENDS "${CROMO_RT_DIR}/libcromo_rt.a" cmake/embed.cmake
  COMMENT "Embedding the Cromo runtime for Linux")

# ---- Standard library in Cromo (D97), embedded as text ----
add_custom_command(
  OUTPUT "${CROMO_RT_DIR}/library_blob.inc"
  COMMAND "${CMAKE_COMMAND}" "-DINPUT=${CMAKE_SOURCE_DIR}/library/core.cro" "-DOUTPUT=${CROMO_RT_DIR}/library_blob.inc"
          -P "${CMAKE_SOURCE_DIR}/cmake/embed.cmake"
  DEPENDS library/core.cro cmake/embed.cmake
  COMMENT "Embedding the Cromo standard library")

add_executable(cromo
  src/main.cpp
  src/ast.cpp
  src/diagnostics.cpp
  src/lexer.cpp
  src/parser.cpp
  src/types.cpp
  src/sema.cpp
  src/ownership.cpp
  src/codegen.cpp
  src/format.cpp
  src/backend.cpp
  src/libraries.cpp
  src/loader.cpp
  src/json.cpp
  src/index.cpp
  src/lsp.cpp
  src/runtime_blob.cpp
  "${CROMO_RT_DIR}/runtime_blob.inc"
  "${CROMO_RT_DIR}/runtime_linux_blob.inc"
  "${CROMO_RT_DIR}/library_blob.inc"
)

target_include_directories(cromo PRIVATE "${CROMO_RT_DIR}")
target_include_directories(cromo SYSTEM PRIVATE ${LLVM_INCLUDE_DIRS} ${LLD_INCLUDE_DIRS})
separate_arguments(CROMO_LLVM_DEFINITIONS NATIVE_COMMAND "${LLVM_DEFINITIONS}")
target_compile_definitions(cromo PRIVATE ${CROMO_LLVM_DEFINITIONS} CROMO_VERSION="${PROJECT_VERSION}")

if(MSVC)
  # C4702 (unreachable code) comes from the LLVM headers during optimization.
  target_compile_options(cromo PRIVATE /utf-8 /W4 /permissive- /GR- /Zc:__cplusplus /wd4702)
endif()

llvm_map_components_to_libnames(CROMO_LLVM_LIBS
  Core IRReader Object Passes Support Target TargetParser MC
  X86CodeGen X86AsmParser X86Desc X86Info
)
target_link_libraries(cromo PRIVATE lldCOFF lldELF lldCommon ${CROMO_LLVM_LIBS})
