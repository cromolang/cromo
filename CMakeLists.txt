cmake_minimum_required(VERSION 3.20)
project(cromo LANGUAGES CXX)

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

add_executable(cromo
  src/main.cpp
  src/diagnostics.cpp
  src/lexer.cpp
  src/parser.cpp
  src/sema.cpp
  src/codegen.cpp
  src/backend.cpp
  src/windows_sdk.cpp
)

target_include_directories(cromo SYSTEM PRIVATE ${LLVM_INCLUDE_DIRS} ${LLD_INCLUDE_DIRS})
separate_arguments(CROMO_LLVM_DEFINITIONS NATIVE_COMMAND "${LLVM_DEFINITIONS}")
target_compile_definitions(cromo PRIVATE ${CROMO_LLVM_DEFINITIONS})

if(MSVC)
  # C4702 (unreachable code) comes from the LLVM headers during optimization.
  target_compile_options(cromo PRIVATE /utf-8 /W4 /permissive- /GR- /Zc:__cplusplus /wd4702)
endif()

llvm_map_components_to_libnames(CROMO_LLVM_LIBS
  Core IRReader Passes Support Target TargetParser MC
  X86CodeGen X86AsmParser X86Desc X86Info
)
target_link_libraries(cromo PRIVATE lldCOFF lldCommon ${CROMO_LLVM_LIBS})
