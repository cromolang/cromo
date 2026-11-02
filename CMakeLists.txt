cmake_minimum_required(VERSION 3.16...3.20)  # 3.16 in WSL for cromo for Linux, without the runtime
project(cromo VERSION 0.29.0 LANGUAGES CXX)  # 0.milestone.fix (D157)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# The LLVM libraries are Release builds: no Debug build, which would use a different runtime.
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "RelWithDebInfo or Release" FORCE)
endif()

if(WIN32)
  set(CROMO_LLVM_DEFAULT "C:/software/llvm-23.1.4")
else()
  set(CROMO_LLVM_DEFAULT "$ENV{HOME}/llvm-23.1.4")
endif()
set(CROMO_LLVM_ROOT "${CROMO_LLVM_DEFAULT}" CACHE PATH "LLVM with static libraries and LLD (D53, D292)")
list(PREPEND CMAKE_PREFIX_PATH "${CROMO_LLVM_ROOT}")

# The runtime and lib/ are built on Windows, with the clang of the LLVM archive, for
# both systems; cromo for Linux is only the compiler, and its archive takes lib/
# from the build of Windows (D292).
option(CROMO_BUILD_LIBRARY "Build the runtime and place the library in lib/" ${WIN32})

if(WIN32)
  # The LLVM archive is built with static zlib, zstd and libxml2 but does not ship them:
  # vcpkg provides them (vcpkg.json). zstd must be found before LLVM, because with MSVC
  # LLVM's Findzstd module only recognizes a static library named zstd_static.lib.
  find_package(zstd CONFIG REQUIRED)
else()
  # cromo for Linux depends only on glibc (D292): the libraries that LLVM uses are
  # linked statically, defined before LLVM looks for them. libxml2, for the
  # manifests of LLD, would bring ICU with it: src/llvm_trims.cpp takes its place.
  set(CROMO_SYSTEM_LIBS "/usr/lib/x86_64-linux-gnu" CACHE PATH "The static libraries of the system")
  set(ZLIB_LIBRARY "${CROMO_SYSTEM_LIBS}/libz.a" CACHE FILEPATH "" FORCE)
  set(LIBXML2_LIBRARY "${CROMO_SYSTEM_LIBS}/libz.a" CACHE FILEPATH "" FORCE)
  foreach(target LibXml2::LibXml2 LibXml2::LibXml2Static)  # FindLibXml2 of LLVM defines both
    add_library(${target} INTERFACE IMPORTED)
    set_target_properties(${target} PROPERTIES
      INTERFACE_LINK_LIBRARIES "${CROMO_SYSTEM_LIBS}/libz.a;m;dl;pthread")
  endforeach()
endif()
find_package(LLVM REQUIRED CONFIG)
find_package(LLD REQUIRED CONFIG)
message(STATUS "LLVM ${LLVM_PACKAGE_VERSION}: ${LLVM_DIR}")
message(STATUS "LLD: ${LLD_DIR}")

# The prebuilt LLVM libraries use the static C runtime (/MT) and have no RTTI.
# With the static runtime cromo.exe does not need the Visual C++ Redistributable.
set(CMAKE_MSVC_RUNTIME_LIBRARY MultiThreaded)

if(CROMO_BUILD_LIBRARY)

# ---- Temporary runtime in freestanding C (D58) ----
# Compiled with the clang of the LLVM archive, without the C library, once for
# Windows, once for Linux and once for Linux on ARM64 (D142, D405), and placed in
# lib/ (D237).
set(CROMO_CLANG "${CROMO_LLVM_ROOT}/bin/clang.exe")
set(CROMO_LLVM_LIB "${CROMO_LLVM_ROOT}/bin/llvm-lib.exe")
set(CROMO_LLVM_AR "${CROMO_LLVM_ROOT}/bin/llvm-ar.exe")
set(CROMO_RT_DIR "${CMAKE_BINARY_DIR}/runtime")
set(CROMO_RT_BASE_FLAGS --target=x86_64-pc-windows-msvc -ffreestanding -fno-builtin -nostdlib -O2
    -fno-stack-protector -mno-stack-arg-probe)
set(CROMO_RT_LINUX_BASE_FLAGS --target=x86_64-unknown-linux-gnu -ffreestanding -fno-builtin -nostdlib -O2
    -fno-stack-protector -fno-pic)
# ARMv8.0, whose atomics call the functions of mem_arm64.S (D407).
set(CROMO_RT_ARM64_BASE_FLAGS --target=aarch64-unknown-linux-gnu -ffreestanding -fno-builtin -nostdlib -O2
    -fno-stack-protector -fno-pic -moutline-atomics)
# The unwind tables lead the stack traces of Windows through the runtime (D341).
set(CROMO_RT_FLAGS ${CROMO_RT_BASE_FLAGS} -funwind-tables -Wall -Wextra)
# The unwind tables lead the stack traces through the runtime too (D375).
set(CROMO_RT_LINUX_FLAGS ${CROMO_RT_LINUX_BASE_FLAGS} -fasynchronous-unwind-tables -Wall -Wextra)
set(CROMO_RT_ARM64_FLAGS ${CROMO_RT_ARM64_BASE_FLAGS} -fasynchronous-unwind-tables -Wall -Wextra)
file(MAKE_DIRECTORY "${CROMO_RT_DIR}")

# ---- TLS and cryptography (D233, D236) ----
# mbedTLS and Monocypher (third_party), with the pieces of the C library that they
# use and the glue of std.net and std.crypto (runtime/tls), compiled like the
# runtime and added to its archives: a program links only the objects it uses.
# Only runtime/libc is searched for the headers of C, not the Windows SDK.
file(GLOB CROMO_TLS_THIRD_PARTY CONFIGURE_DEPENDS
     "${CMAKE_SOURCE_DIR}/third_party/mbedtls/library/*.c" "${CMAKE_SOURCE_DIR}/third_party/monocypher/*.c")
file(GLOB CROMO_TLS_GLUE CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/runtime/tls/*.c")
file(GLOB CROMO_TLS_HEADERS CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/runtime/tls/*.h" "${CMAKE_SOURCE_DIR}/runtime/libc/*.h")
# Without the compatibility with MSVC, clang defines __GNUC__ for Windows too, and
# mbedTLS multiplies its big integers with the assembly of x64, as on Linux: RSA
# takes half the time. The headers are those of runtime/libc, not of the SDK.
set(CROMO_TLS_WINDOWS_FLAGS ${CROMO_RT_BASE_FLAGS} -fno-ms-compatibility)
set(CROMO_TLS_FLAGS -nostdlibinc -isystem "${CMAKE_SOURCE_DIR}/runtime/libc" "-I${CMAKE_SOURCE_DIR}/runtime/tls"
    "-I${CMAKE_SOURCE_DIR}/third_party/mbedtls/include" "-I${CMAKE_SOURCE_DIR}/third_party/monocypher"
    "-DMBEDTLS_USER_CONFIG_FILE=\"mbedtls_user_config.h\"")

# The objects for one system, in <folder>, and a response file that lists them
# for the archiver: their command line would be too long for Windows. The archive
# depends on the response file too, so that a source removed leaves it.
function(cromo_tls_objects folder flags objects)
  set(result)
  set(listing "")
  foreach(source ${CROMO_TLS_THIRD_PARTY} ${CROMO_TLS_GLUE})
    get_filename_component(name "${source}" NAME_WE)
    set(object "${CROMO_RT_DIR}/${folder}/${name}.o")
    set(warnings)
    if(source IN_LIST CROMO_TLS_GLUE)
      set(warnings -Wall -Wextra)
    endif()
    add_custom_command(
      OUTPUT "${object}"
      COMMAND "${CROMO_CLANG}" ${flags} ${warnings} ${CROMO_TLS_FLAGS} -c "${source}" -o "${object}"
      DEPENDS "${source}" ${CROMO_TLS_HEADERS}
      VERBATIM)
    list(APPEND result "${object}")
    string(APPEND listing "${object}\n")
  endforeach()
  file(MAKE_DIRECTORY "${CROMO_RT_DIR}/${folder}")
  file(CONFIGURE OUTPUT "${CROMO_RT_DIR}/${folder}.rsp" CONTENT "${listing}")
  set(${objects} ${result} PARENT_SCOPE)
endfunction()
cromo_tls_objects(tls_windows "${CROMO_TLS_WINDOWS_FLAGS}" CROMO_TLS_WINDOWS_OBJECTS)
cromo_tls_objects(tls_linux "${CROMO_RT_LINUX_BASE_FLAGS}" CROMO_TLS_LINUX_OBJECTS)
cromo_tls_objects(tls_linux_arm64 "${CROMO_RT_ARM64_BASE_FLAGS}" CROMO_TLS_ARM64_OBJECTS)

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
  COMMAND "${CMAKE_COMMAND}" -E rm -f "${CROMO_RT_DIR}/cromo_rt.lib"
  COMMAND "${CROMO_LLVM_LIB}" /nologo "/out:${CROMO_RT_DIR}/cromo_rt.lib" "${CROMO_RT_DIR}/cromo_rt.obj" "${CROMO_RT_DIR}/mem_x64.obj"
          "${CROMO_RT_DIR}/runtime_cromo.obj" "@${CROMO_RT_DIR}/tls_windows.rsp"
  DEPENDS "${CROMO_RT_DIR}/cromo_rt.obj" "${CROMO_RT_DIR}/mem_x64.obj" "${CROMO_RT_DIR}/runtime_cromo.obj" ${CROMO_TLS_WINDOWS_OBJECTS}
          "${CROMO_RT_DIR}/tls_windows.rsp"
  COMMENT "Archiving the Cromo runtime")

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
          "${CROMO_RT_DIR}/runtime_cromo_linux.o" "@${CROMO_RT_DIR}/tls_linux.rsp"
  DEPENDS "${CROMO_RT_DIR}/cromo_rt_linux.o" "${CROMO_RT_DIR}/mem_x64_linux.o" "${CROMO_RT_DIR}/runtime_cromo_linux.o"
          ${CROMO_TLS_LINUX_OBJECTS} "${CROMO_RT_DIR}/tls_linux.rsp"
  COMMENT "Archiving the Cromo runtime for Linux")

add_custom_command(
  OUTPUT "${CROMO_RT_DIR}/cromo_rt_linux_arm64.o"
  COMMAND "${CROMO_CLANG}" ${CROMO_RT_ARM64_FLAGS} -c "${CMAKE_SOURCE_DIR}/runtime/cromo_rt.c" -o "${CROMO_RT_DIR}/cromo_rt_linux_arm64.o"
  DEPENDS runtime/cromo_rt.c
  COMMENT "Compiling the Cromo runtime for Linux on ARM64")
add_custom_command(
  OUTPUT "${CROMO_RT_DIR}/mem_arm64_linux.o"
  COMMAND "${CROMO_CLANG}" --target=aarch64-unknown-linux-gnu -c "${CMAKE_SOURCE_DIR}/runtime/mem_arm64.S" -o "${CROMO_RT_DIR}/mem_arm64_linux.o"
  DEPENDS runtime/mem_arm64.S
  COMMENT "Assembling the Cromo runtime for Linux on ARM64")
add_custom_command(
  OUTPUT "${CROMO_RT_DIR}/libcromo_rt_arm64.a"
  COMMAND "${CMAKE_COMMAND}" -E rm -f "${CROMO_RT_DIR}/libcromo_rt_arm64.a"
  COMMAND "${CROMO_LLVM_AR}" rcs "${CROMO_RT_DIR}/libcromo_rt_arm64.a" "${CROMO_RT_DIR}/cromo_rt_linux_arm64.o"
          "${CROMO_RT_DIR}/mem_arm64_linux.o" "${CROMO_RT_DIR}/runtime_cromo_linux_arm64.o" "@${CROMO_RT_DIR}/tls_linux_arm64.rsp"
  DEPENDS "${CROMO_RT_DIR}/cromo_rt_linux_arm64.o" "${CROMO_RT_DIR}/mem_arm64_linux.o"
          "${CROMO_RT_DIR}/runtime_cromo_linux_arm64.o" ${CROMO_TLS_ARM64_OBJECTS} "${CROMO_RT_DIR}/tls_linux_arm64.rsp"
  COMMENT "Archiving the Cromo runtime for Linux on ARM64")
# ---- lib/ of the build, as in the archive (D237) ----
# cromo reads the library and the runtime from lib/: next to its folder bin/ in
# an archive, inside its folder in a build. Here they change at the next build.
set(CROMO_LIB_DIR "${CMAKE_BINARY_DIR}/lib")
file(GLOB CROMO_STD_MODULES CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/library/std/*.cro")
# The programs of cromo written in Cromo, as cromo update (D403).
file(GLOB CROMO_TOOLS CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/library/tools/*.cro")
add_custom_command(
  OUTPUT "${CROMO_RT_DIR}/library.stamp"
  COMMAND "${CMAKE_COMMAND}" -E rm -rf "${CROMO_LIB_DIR}/std" "${CROMO_LIB_DIR}/tools"
  COMMAND "${CMAKE_COMMAND}" -E make_directory "${CROMO_LIB_DIR}/std" "${CROMO_LIB_DIR}/tools" "${CROMO_LIB_DIR}/windows"
          "${CROMO_LIB_DIR}/linux" "${CROMO_LIB_DIR}/linux_arm64"
  COMMAND "${CMAKE_COMMAND}" -E copy "${CMAKE_SOURCE_DIR}/library/core.cro" "${CROMO_LIB_DIR}"
  COMMAND "${CMAKE_COMMAND}" -E copy ${CROMO_STD_MODULES} "${CROMO_LIB_DIR}/std"
  COMMAND "${CMAKE_COMMAND}" -E copy ${CROMO_TOOLS} "${CROMO_LIB_DIR}/tools"
  COMMAND "${CMAKE_COMMAND}" -E touch "${CROMO_RT_DIR}/library.stamp"
  DEPENDS library/core.cro ${CROMO_STD_MODULES} ${CROMO_TOOLS}
  COMMENT "Placing the library in lib/")
add_custom_target(cromo_library ALL DEPENDS "${CROMO_RT_DIR}/library.stamp")

# The runtime written in Cromo, std.runtime (D393): compiled once by the cromo just
# built, as in --release, into an object of the archive of the runtime of each
# system, next to the C, so that the programs only link it.
add_custom_command(
  OUTPUT "${CROMO_RT_DIR}/runtime_cromo.obj"
  COMMAND cromo build --runtime --target windows -o "${CROMO_RT_DIR}/runtime_cromo.obj"
  DEPENDS cromo "${CROMO_RT_DIR}/library.stamp"
  COMMENT "Compiling the runtime written in Cromo")
add_custom_command(
  OUTPUT "${CROMO_RT_DIR}/runtime_cromo_linux.o"
  COMMAND cromo build --runtime --target linux -o "${CROMO_RT_DIR}/runtime_cromo_linux.o"
  DEPENDS cromo "${CROMO_RT_DIR}/library.stamp"
  COMMENT "Compiling the runtime written in Cromo for Linux")
add_custom_command(
  OUTPUT "${CROMO_RT_DIR}/runtime_cromo_linux_arm64.o"
  COMMAND cromo build --runtime --target linux-arm64 -o "${CROMO_RT_DIR}/runtime_cromo_linux_arm64.o"
  DEPENDS cromo "${CROMO_RT_DIR}/library.stamp"
  COMMENT "Compiling the runtime written in Cromo for Linux on ARM64")
add_custom_command(
  OUTPUT "${CROMO_RT_DIR}/runtime.stamp"
  COMMAND "${CMAKE_COMMAND}" -E copy "${CROMO_RT_DIR}/cromo_rt.lib" "${CROMO_LIB_DIR}/windows"
  COMMAND "${CMAKE_COMMAND}" -E copy "${CROMO_RT_DIR}/libcromo_rt.a" "${CROMO_LIB_DIR}/linux"
  COMMAND "${CMAKE_COMMAND}" -E copy "${CROMO_RT_DIR}/libcromo_rt_arm64.a" "${CROMO_LIB_DIR}/linux_arm64/libcromo_rt.a"
  COMMAND "${CMAKE_COMMAND}" -E touch "${CROMO_RT_DIR}/runtime.stamp"
  DEPENDS "${CROMO_RT_DIR}/cromo_rt.lib" "${CROMO_RT_DIR}/libcromo_rt.a" "${CROMO_RT_DIR}/libcromo_rt_arm64.a"
          "${CROMO_RT_DIR}/library.stamp"
  COMMENT "Placing the runtime in lib/")
add_custom_target(cromo_runtime ALL DEPENDS "${CROMO_RT_DIR}/runtime.stamp")

endif()

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
  src/installation.cpp
  src/llvm_trims.cpp
)
if(CROMO_BUILD_LIBRARY)
  add_dependencies(cromo cromo_library)
endif()

target_include_directories(cromo SYSTEM PRIVATE ${LLVM_INCLUDE_DIRS} ${LLD_INCLUDE_DIRS})
separate_arguments(CROMO_LLVM_DEFINITIONS NATIVE_COMMAND "${LLVM_DEFINITIONS}")
target_compile_definitions(cromo PRIVATE ${CROMO_LLVM_DEFINITIONS} CROMO_VERSION="${PROJECT_VERSION}")

if(MSVC)
  # C4702 (unreachable code) comes from the LLVM headers during optimization.
  target_compile_options(cromo PRIVATE /utf-8 /W4 /permissive- /GR- /Zc:__cplusplus /wd4702)
else()
  # As LLVM, without RTTI; the runtime of C++ inside cromo, so that cromo for
  # Linux depends only on glibc (D292).
  target_compile_options(cromo PRIVATE -Wall -Wextra -fno-rtti)
  # Without the symbols and the sections that nothing uses: a smaller archive (D286).
  target_link_options(cromo PRIVATE -static-libstdc++ -static-libgcc -Wl,--gc-sections -s)
  target_link_libraries(cromo PRIVATE pthread)
  target_sources(cromo PRIVATE src/glibc_compat.cpp)
endif()

llvm_map_components_to_libnames(CROMO_LLVM_LIBS
  Core DebugInfoDWARF IRReader Object Passes Support Target TargetParser MC
  X86CodeGen X86AsmParser X86Desc X86Info
  AArch64CodeGen AArch64AsmParser AArch64Desc AArch64Info
)
target_link_libraries(cromo PRIVATE lldCOFF lldELF lldCommon ${CROMO_LLVM_LIBS})
