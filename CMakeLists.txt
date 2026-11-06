cmake_minimum_required(VERSION 3.16...3.20)  # 3.16 in WSL for cromo for Linux, without the runtime
project(cromo VERSION 0.49.0 LANGUAGES CXX)  # 0.milestone.fix (D157)

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

# The part of std.gui in C (D441), with stb_truetype, the fonts of Inter (D433,
# D440) and the icons of Lucide (D448), in the archives of the runtime as the
# glue of TLS: only the programs with windows link it. The images, with stb_image
# (D446), are an object of their own, with the headers of runtime/libc. Not for
# ARM64, which has no SDL yet.
set(CROMO_GUI_SOURCES runtime/gui/gui.c runtime/gui/fonts.S third_party/stb/stb_truetype.h
    third_party/inter/Inter-Regular.ttf third_party/inter/Inter-SemiBold.ttf third_party/lucide/lucide-icons.ttf)
set(CROMO_GUI_IMAGE_SOURCES runtime/gui/image.c third_party/stb/stb_image.h)
set(CROMO_GUI_IMAGE_FLAGS -nostdlibinc -isystem "${CMAKE_SOURCE_DIR}/runtime/libc" "-I${CMAKE_SOURCE_DIR}/third_party/stb")
add_custom_command(
  OUTPUT "${CROMO_RT_DIR}/gui.obj" "${CROMO_RT_DIR}/gui_fonts.obj"
  COMMAND "${CROMO_CLANG}" ${CROMO_RT_FLAGS} "-I${CMAKE_SOURCE_DIR}/third_party/stb" -c "${CMAKE_SOURCE_DIR}/runtime/gui/gui.c" -o "${CROMO_RT_DIR}/gui.obj"
  COMMAND "${CROMO_CLANG}" --target=x86_64-pc-windows-msvc "-I${CMAKE_SOURCE_DIR}/third_party/inter"
          "-I${CMAKE_SOURCE_DIR}/third_party/lucide" -c "${CMAKE_SOURCE_DIR}/runtime/gui/fonts.S" -o "${CROMO_RT_DIR}/gui_fonts.obj"
  DEPENDS ${CROMO_GUI_SOURCES}
  COMMENT "Compiling the part of std.gui in C")
add_custom_command(
  OUTPUT "${CROMO_RT_DIR}/gui_image.obj"
  COMMAND "${CROMO_CLANG}" ${CROMO_RT_FLAGS} -fno-ms-compatibility ${CROMO_GUI_IMAGE_FLAGS} -c "${CMAKE_SOURCE_DIR}/runtime/gui/image.c" -o "${CROMO_RT_DIR}/gui_image.obj"
  DEPENDS ${CROMO_GUI_IMAGE_SOURCES}
  COMMENT "Compiling the images of std.gui")
add_custom_command(
  OUTPUT "${CROMO_RT_DIR}/gui_linux.o" "${CROMO_RT_DIR}/gui_fonts_linux.o"
  COMMAND "${CROMO_CLANG}" ${CROMO_RT_LINUX_FLAGS} "-I${CMAKE_SOURCE_DIR}/third_party/stb" -c "${CMAKE_SOURCE_DIR}/runtime/gui/gui.c" -o "${CROMO_RT_DIR}/gui_linux.o"
  COMMAND "${CROMO_CLANG}" --target=x86_64-unknown-linux-gnu "-I${CMAKE_SOURCE_DIR}/third_party/inter"
          "-I${CMAKE_SOURCE_DIR}/third_party/lucide" -c "${CMAKE_SOURCE_DIR}/runtime/gui/fonts.S" -o "${CROMO_RT_DIR}/gui_fonts_linux.o"
  DEPENDS ${CROMO_GUI_SOURCES}
  COMMENT "Compiling the part of std.gui in C for Linux")
add_custom_command(
  OUTPUT "${CROMO_RT_DIR}/gui_image_linux.o"
  COMMAND "${CROMO_CLANG}" ${CROMO_RT_LINUX_FLAGS} ${CROMO_GUI_IMAGE_FLAGS} -c "${CMAKE_SOURCE_DIR}/runtime/gui/image.c" -o "${CROMO_RT_DIR}/gui_image_linux.o"
  DEPENDS ${CROMO_GUI_IMAGE_SOURCES}
  COMMENT "Compiling the images of std.gui for Linux")
set(CROMO_GUI_WINDOWS_OBJECTS "${CROMO_RT_DIR}/gui.obj" "${CROMO_RT_DIR}/gui_fonts.obj" "${CROMO_RT_DIR}/gui_image.obj")
set(CROMO_GUI_LINUX_OBJECTS "${CROMO_RT_DIR}/gui_linux.o" "${CROMO_RT_DIR}/gui_fonts_linux.o"
    "${CROMO_RT_DIR}/gui_image_linux.o")

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
          ${CROMO_GUI_WINDOWS_OBJECTS}
          "${CROMO_RT_DIR}/runtime_cromo.obj" "@${CROMO_RT_DIR}/tls_windows.rsp"
  DEPENDS "${CROMO_RT_DIR}/cromo_rt.obj" "${CROMO_RT_DIR}/mem_x64.obj" "${CROMO_RT_DIR}/runtime_cromo.obj" ${CROMO_TLS_WINDOWS_OBJECTS}
          ${CROMO_GUI_WINDOWS_OBJECTS}
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
          "${CROMO_RT_DIR}/runtime_cromo_linux.o" ${CROMO_GUI_LINUX_OBJECTS} "@${CROMO_RT_DIR}/tls_linux.rsp"
  DEPENDS "${CROMO_RT_DIR}/cromo_rt_linux.o" "${CROMO_RT_DIR}/mem_x64_linux.o" "${CROMO_RT_DIR}/runtime_cromo_linux.o"
          ${CROMO_GUI_LINUX_OBJECTS} ${CROMO_TLS_LINUX_OBJECTS} "${CROMO_RT_DIR}/tls_linux.rsp"
  COMMENT "Archiving the Cromo runtime for Linux")

# The runtime for the executables linked with glibc (D428): the same, with
# CROMO_GLIBC and runtime/glibc.c, and the stubs of the libraries of glibc, written
# from runtime/glibc/symbols.txt, for the linker.
add_custom_command(
  OUTPUT "${CROMO_RT_DIR}/cromo_rt_glibc.o"
  COMMAND "${CROMO_CLANG}" ${CROMO_RT_LINUX_FLAGS} -DCROMO_GLIBC -c "${CMAKE_SOURCE_DIR}/runtime/cromo_rt.c" -o "${CROMO_RT_DIR}/cromo_rt_glibc.o"
  DEPENDS runtime/cromo_rt.c
  COMMENT "Compiling the Cromo runtime for glibc")
add_custom_command(
  OUTPUT "${CROMO_RT_DIR}/glibc.o"
  COMMAND "${CROMO_CLANG}" ${CROMO_RT_LINUX_FLAGS} -c "${CMAKE_SOURCE_DIR}/runtime/glibc.c" -o "${CROMO_RT_DIR}/glibc.o"
  DEPENDS runtime/glibc.c
  COMMENT "Compiling the start for glibc")
add_custom_command(
  OUTPUT "${CROMO_RT_DIR}/mem_x64_glibc.o"
  COMMAND "${CROMO_CLANG}" --target=x86_64-unknown-linux-gnu -DCROMO_GLIBC -c "${CMAKE_SOURCE_DIR}/runtime/mem_x64.S" -o "${CROMO_RT_DIR}/mem_x64_glibc.o"
  DEPENDS runtime/mem_x64.S
  COMMENT "Assembling the Cromo runtime for glibc")
add_custom_command(
  OUTPUT "${CROMO_RT_DIR}/libcromo_rt_glibc.a"
  COMMAND "${CMAKE_COMMAND}" -E rm -f "${CROMO_RT_DIR}/libcromo_rt_glibc.a"
  COMMAND "${CROMO_LLVM_AR}" rcs "${CROMO_RT_DIR}/libcromo_rt_glibc.a" "${CROMO_RT_DIR}/cromo_rt_glibc.o" "${CROMO_RT_DIR}/glibc.o"
          "${CROMO_RT_DIR}/mem_x64_glibc.o" "${CROMO_RT_DIR}/runtime_cromo_linux.o" ${CROMO_GUI_LINUX_OBJECTS}
          "@${CROMO_RT_DIR}/tls_linux.rsp"
  DEPENDS "${CROMO_RT_DIR}/cromo_rt_glibc.o" "${CROMO_RT_DIR}/glibc.o" "${CROMO_RT_DIR}/mem_x64_glibc.o"
          "${CROMO_RT_DIR}/runtime_cromo_linux.o" ${CROMO_GUI_LINUX_OBJECTS} ${CROMO_TLS_LINUX_OBJECTS}
          "${CROMO_RT_DIR}/tls_linux.rsp"
  COMMENT "Archiving the Cromo runtime for glibc")
set(CROMO_GLIBC_STUBS libc.so.6 libm.so.6 libpthread.so.0 libdl.so.2 librt.so.1 ld-linux-x86-64.so.2)
list(TRANSFORM CROMO_GLIBC_STUBS PREPEND "${CROMO_RT_DIR}/glibc/" OUTPUT_VARIABLE CROMO_GLIBC_STUB_FILES)
add_custom_command(
  OUTPUT ${CROMO_GLIBC_STUB_FILES}
  COMMAND "${CMAKE_COMMAND}" "-DSYMBOLS=${CMAKE_SOURCE_DIR}/runtime/glibc/symbols.txt" "-DOUT=${CROMO_RT_DIR}/glibc"
          "-DCLANG=${CROMO_CLANG}" "-DLLD=${CROMO_LLVM_ROOT}/bin/ld.lld.exe" -P "${CMAKE_SOURCE_DIR}/runtime/glibc/stubs.cmake"
  DEPENDS runtime/glibc/symbols.txt runtime/glibc/stubs.cmake
  COMMENT "Writing the stubs of glibc")

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

# ---- Android (D452, P43) ----
# The runtime for Bionic, of API 26 (D454), on ARM64 and on x86-64: position
# independent, as the programs of Android, with CROMO_BIONIC and runtime/bionic.c,
# and the stubs of the libraries of Bionic, written from
# runtime/bionic/symbols_<architecture>.txt.
set(CROMO_ANDROID_ARCHIVES)
foreach(arch arm64 x86_64)
  if(arch STREQUAL "arm64")
    set(triple aarch64-linux-android26)
    set(mem mem_arm64)
    set(extra -moutline-atomics)  # D407
  else()
    set(triple x86_64-linux-android26)
    set(mem mem_x64)
    set(extra)
  endif()
  set(base --target=${triple} -ffreestanding -fno-builtin -nostdlib -O2 -fno-stack-protector -fPIC -DCROMO_BIONIC ${extra})
  cromo_tls_objects(tls_android_${arch} "${base}" tls_objects)
  set(dir "${CROMO_RT_DIR}/android_${arch}")
  file(MAKE_DIRECTORY "${dir}")
  add_custom_command(
    OUTPUT "${dir}/cromo_rt.o"
    COMMAND "${CROMO_CLANG}" ${base} -fasynchronous-unwind-tables -Wall -Wextra -c "${CMAKE_SOURCE_DIR}/runtime/cromo_rt.c"
            -o "${dir}/cromo_rt.o"
    DEPENDS runtime/cromo_rt.c
    COMMENT "Compiling the Cromo runtime for Android on ${arch}")
  add_custom_command(
    OUTPUT "${dir}/bionic.o"
    COMMAND "${CROMO_CLANG}" ${base} -Wall -Wextra -c "${CMAKE_SOURCE_DIR}/runtime/bionic.c" -o "${dir}/bionic.o"
    DEPENDS runtime/bionic.c
    COMMENT "Compiling the start for Android on ${arch}")
  add_custom_command(
    OUTPUT "${dir}/${mem}.o"
    COMMAND "${CROMO_CLANG}" --target=${triple} -fPIC -DCROMO_BIONIC -c "${CMAKE_SOURCE_DIR}/runtime/${mem}.S" -o "${dir}/${mem}.o"
    DEPENDS runtime/${mem}.S
    COMMENT "Assembling the Cromo runtime for Android on ${arch}")
  # The part of std.gui in C (D441), as for Linux (M44).
  add_custom_command(
    OUTPUT "${dir}/gui.o" "${dir}/gui_fonts.o" "${dir}/gui_image.o"
    COMMAND "${CROMO_CLANG}" ${base} -Wall -Wextra "-I${CMAKE_SOURCE_DIR}/third_party/stb" -c
            "${CMAKE_SOURCE_DIR}/runtime/gui/gui.c" -o "${dir}/gui.o"
    COMMAND "${CROMO_CLANG}" --target=${triple} "-I${CMAKE_SOURCE_DIR}/third_party/inter" "-I${CMAKE_SOURCE_DIR}/third_party/lucide"
            -c "${CMAKE_SOURCE_DIR}/runtime/gui/fonts.S" -o "${dir}/gui_fonts.o"
    COMMAND "${CROMO_CLANG}" ${base} ${CROMO_GUI_IMAGE_FLAGS} -c "${CMAKE_SOURCE_DIR}/runtime/gui/image.c" -o "${dir}/gui_image.o"
    DEPENDS ${CROMO_GUI_SOURCES} ${CROMO_GUI_IMAGE_SOURCES}
    COMMENT "Compiling the part of std.gui in C for Android on ${arch}")
  add_custom_command(
    OUTPUT "${dir}/libcromo_rt.a"
    COMMAND "${CMAKE_COMMAND}" -E rm -f "${dir}/libcromo_rt.a"
    COMMAND "${CROMO_LLVM_AR}" rcs "${dir}/libcromo_rt.a" "${dir}/cromo_rt.o" "${dir}/bionic.o" "${dir}/${mem}.o"
            "${dir}/runtime_cromo.o" "${dir}/gui.o" "${dir}/gui_fonts.o" "${dir}/gui_image.o"
            "@${CROMO_RT_DIR}/tls_android_${arch}.rsp"
    DEPENDS "${dir}/cromo_rt.o" "${dir}/bionic.o" "${dir}/${mem}.o" "${dir}/runtime_cromo.o" "${dir}/gui.o" "${dir}/gui_fonts.o"
            "${dir}/gui_image.o" ${tls_objects} "${CROMO_RT_DIR}/tls_android_${arch}.rsp"
    COMMENT "Archiving the Cromo runtime for Android on ${arch}")
  set(stubs libc.so libm.so libdl.so liblog.so)
  list(TRANSFORM stubs PREPEND "${dir}/bionic/" OUTPUT_VARIABLE stub_files)
  add_custom_command(
    OUTPUT ${stub_files}
    COMMAND "${CMAKE_COMMAND}" "-DSYMBOLS=${CMAKE_SOURCE_DIR}/runtime/bionic/symbols_${arch}.txt" "-DOUT=${dir}/bionic"
            "-DCLANG=${CROMO_CLANG}" "-DLLD=${CROMO_LLVM_ROOT}/bin/ld.lld.exe" "-DTRIPLE=${triple}"
            -P "${CMAKE_SOURCE_DIR}/runtime/glibc/stubs.cmake"
    DEPENDS runtime/bionic/symbols_${arch}.txt runtime/glibc/stubs.cmake
    COMMENT "Writing the stubs of Bionic for ${arch}")
  list(APPEND CROMO_ANDROID_ARCHIVES "${dir}/libcromo_rt.a" ${stub_files})
  set("CROMO_ANDROID_STUBS_${arch}" ${stub_files})
endforeach()

# ---- lib/ of the build, as in the archive (D237) ----
# cromo reads the library and the runtime from lib/: next to its folder bin/ in
# an archive, inside its folder in a build. Here they change at the next build.
set(CROMO_LIB_DIR "${CMAKE_BINARY_DIR}/lib")
set(CROMO_ANDROID_PLACE)
foreach(arch arm64 x86_64)
  list(APPEND CROMO_ANDROID_PLACE
       COMMAND "${CMAKE_COMMAND}" -E make_directory "${CROMO_LIB_DIR}/android_${arch}/bionic"
       COMMAND "${CMAKE_COMMAND}" -E copy "${CROMO_RT_DIR}/android_${arch}/libcromo_rt.a" "${CROMO_LIB_DIR}/android_${arch}"
       COMMAND "${CMAKE_COMMAND}" -E copy ${CROMO_ANDROID_STUBS_${arch}} "${CROMO_LIB_DIR}/android_${arch}/bionic")
endforeach()

# ---- SDL3 (D429) ----
# For Windows, compiled from third_party/SDL with the clang of LLVM, without the C
# runtime (SDL_LIBC=OFF): /GS- without the cookies of the stack, and without the
# idioms of strlen and wcslen, which clang would turn into calls of the C library.
# runtime/sdl/windows.ps1 makes it SDL3.lib. For Linux, runtime/sdl/linux.sh in
# WSL, on Ubuntu 20.04 (D292), writes libSDL3.a; without WSL there is none, and
# std.gui is only for Windows.
include(ExternalProject)
set(CROMO_SDL_DIR "${CMAKE_BINARY_DIR}/sdl")
ExternalProject_Add(sdl_windows
  SOURCE_DIR "${CMAKE_SOURCE_DIR}/third_party/SDL"
  BINARY_DIR "${CROMO_SDL_DIR}/windows"
  CMAKE_GENERATOR Ninja
  CMAKE_ARGS
    -DCMAKE_BUILD_TYPE=MinSizeRel
    "-DCMAKE_C_FLAGS=/DWIN32 /D_WINDOWS /GS- /clang:-fno-builtin-strlen /clang:-fno-builtin-wcslen"
    "-DCMAKE_CXX_FLAGS=/DWIN32 /D_WINDOWS /GS- /EHsc"
    "-DCMAKE_C_COMPILER=${CROMO_LLVM_ROOT}/bin/clang-cl.exe"
    "-DCMAKE_CXX_COMPILER=${CROMO_LLVM_ROOT}/bin/clang-cl.exe"
    "-DCMAKE_AR=${CROMO_LLVM_LIB}"
    "-DCMAKE_LINKER=${CROMO_LLVM_ROOT}/bin/lld-link.exe"
    -DSDL_SHARED=OFF -DSDL_STATIC=ON -DSDL_LIBC=OFF -DSDL_TEST_LIBRARY=OFF -DSDL_TESTS=OFF -DSDL_EXAMPLES=OFF
    -DSDL_INSTALL=OFF
  BUILD_ALWAYS ON
  INSTALL_COMMAND ""
  BUILD_BYPRODUCTS "${CROMO_SDL_DIR}/windows/SDL3-static.lib")
find_program(CROMO_POWERSHELL NAMES powershell pwsh PATHS "$ENV{SystemRoot}/System32/WindowsPowerShell/v1.0" REQUIRED)
add_custom_command(
  OUTPUT "${CROMO_SDL_DIR}/SDL3.lib"
  COMMAND "${CROMO_POWERSHELL}" -NoProfile -ExecutionPolicy Bypass -File "${CMAKE_SOURCE_DIR}/runtime/sdl/windows.ps1"
          -Static "${CROMO_SDL_DIR}/windows/SDL3-static.lib" -Out "${CROMO_SDL_DIR}/SDL3.lib"
          -Work "${CROMO_SDL_DIR}/windows-parts" -Llvm "${CROMO_LLVM_ROOT}"
  DEPENDS sdl_windows "${CROMO_SDL_DIR}/windows/SDL3-static.lib" runtime/sdl/windows.ps1 runtime/sdl/guids.c
  COMMENT "Making SDL3.lib")
set(CROMO_SDL_FILES "${CROMO_SDL_DIR}/SDL3.lib")
set(CROMO_SDL_PLACE COMMAND "${CMAKE_COMMAND}" -E copy "${CROMO_SDL_DIR}/SDL3.lib" "${CROMO_LIB_DIR}/windows")
find_program(CROMO_WSL wsl)
if(CROMO_WSL)
  file(GLOB_RECURSE CROMO_SDL_SOURCES CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/third_party/SDL/*")
  # The paths as WSL sees them: /mnt/c/...
  string(REGEX REPLACE "^([A-Za-z]):" "/mnt/\\1" CROMO_SDL_SOURCE_WSL "${CMAKE_SOURCE_DIR}/third_party/SDL")
  string(REGEX REPLACE "^([A-Za-z]):" "/mnt/\\1" CROMO_SDL_OUT_WSL "${CROMO_SDL_DIR}/libSDL3.a")
  string(REGEX REPLACE "^([A-Za-z]):" "/mnt/\\1" CROMO_SDL_SCRIPT_WSL "${CMAKE_SOURCE_DIR}/runtime/sdl/linux.sh")
  string(TOLOWER "${CROMO_SDL_SOURCE_WSL}" CROMO_SDL_SOURCE_WSL)
  string(TOLOWER "${CROMO_SDL_OUT_WSL}" CROMO_SDL_OUT_WSL)
  string(TOLOWER "${CROMO_SDL_SCRIPT_WSL}" CROMO_SDL_SCRIPT_WSL)
  add_custom_command(
    OUTPUT "${CROMO_SDL_DIR}/libSDL3.a"
    COMMAND "${CROMO_WSL}" -e sh "${CROMO_SDL_SCRIPT_WSL}" "${CROMO_SDL_SOURCE_WSL}" "${CROMO_SDL_OUT_WSL}"
    DEPENDS ${CROMO_SDL_SOURCES} runtime/sdl/linux.sh
    COMMENT "Compiling SDL3 for Linux in WSL")
  list(APPEND CROMO_SDL_FILES "${CROMO_SDL_DIR}/libSDL3.a")
  list(APPEND CROMO_SDL_PLACE COMMAND "${CMAKE_COMMAND}" -E copy "${CROMO_SDL_DIR}/libSDL3.a" "${CROMO_LIB_DIR}/linux")
else()
  message(STATUS "WSL not found: lib/linux will have no libSDL3.a (D429)")
endif()

# For Android (D452, D453): SDL compiled with the NDK as libSDL3.so for each
# architecture, which its activity loads with the program, and its classes of Java
# compiled once into classes.dex, with javac and d8 of the SDK, for the APKs
# that cromo writes, with the activity of Cromo (runtime/android). Without the NDK, or without the SDK and Java, there are none.
set(CROMO_ANDROID_NDK "" CACHE PATH "The NDK of Android, for libSDL3.so")
set(CROMO_ANDROID_SDK "" CACHE PATH "The SDK of Android, for classes.dex")
set(CROMO_JAVA_HOME "$ENV{JAVA_HOME}" CACHE PATH "The JDK, for classes.dex")
if(CROMO_ANDROID_NDK AND EXISTS "${CROMO_ANDROID_NDK}/build/cmake/android.toolchain.cmake")
  foreach(arch arm64 x86_64)
    if(arch STREQUAL "arm64")
      set(abi arm64-v8a)
    else()
      set(abi x86_64)
    endif()
    ExternalProject_Add(sdl_android_${arch}
      SOURCE_DIR "${CMAKE_SOURCE_DIR}/third_party/SDL"
      BINARY_DIR "${CROMO_SDL_DIR}/android_${arch}"
      CMAKE_GENERATOR Ninja
      CMAKE_ARGS
        -DCMAKE_BUILD_TYPE=MinSizeRel
        "-DCMAKE_TOOLCHAIN_FILE=${CROMO_ANDROID_NDK}/build/cmake/android.toolchain.cmake"
        -DANDROID_ABI=${abi} -DANDROID_PLATFORM=android-26 -DANDROID_SUPPORT_FLEXIBLE_PAGE_SIZES=ON
        -DSDL_SHARED=ON -DSDL_STATIC=OFF -DSDL_TEST_LIBRARY=OFF -DSDL_TESTS=OFF -DSDL_EXAMPLES=OFF -DSDL_INSTALL=OFF
      BUILD_ALWAYS ON
      INSTALL_COMMAND ""
      BUILD_BYPRODUCTS "${CROMO_SDL_DIR}/android_${arch}/libSDL3.so")
    list(APPEND CROMO_SDL_FILES "${CROMO_SDL_DIR}/android_${arch}/libSDL3.so")
    # Without the debugging information that the NDK always adds, as Gradle strips it.
    list(APPEND CROMO_SDL_PLACE COMMAND "${CMAKE_COMMAND}" -E make_directory "${CROMO_LIB_DIR}/android_${arch}"
         COMMAND "${CROMO_LLVM_ROOT}/bin/llvm-strip.exe" --strip-unneeded -o "${CROMO_LIB_DIR}/android_${arch}/libSDL3.so"
                 "${CROMO_SDL_DIR}/android_${arch}/libSDL3.so")
  endforeach()
else()
  message(STATUS "No NDK in CROMO_ANDROID_NDK: lib/android_* will have no libSDL3.so (D452)")
endif()
file(GLOB CROMO_SDL_JAVA CONFIGURE_DEPENDS
     "${CMAKE_SOURCE_DIR}/third_party/SDL/android-project/app/src/main/java/org/libsdl/app/*.java"
     "${CMAKE_SOURCE_DIR}/runtime/android/org/cromo/*.java")
file(GLOB CROMO_ANDROID_PLATFORMS LIST_DIRECTORIES true "${CROMO_ANDROID_SDK}/platforms/android-*")
file(GLOB CROMO_ANDROID_D8 "${CROMO_ANDROID_SDK}/build-tools/*/d8.bat")
list(SORT CROMO_ANDROID_PLATFORMS COMPARE NATURAL)
list(SORT CROMO_ANDROID_D8 COMPARE NATURAL)
list(FILTER CROMO_ANDROID_D8 EXCLUDE REGEX "-rc")
if(CROMO_ANDROID_SDK AND CROMO_ANDROID_PLATFORMS AND CROMO_ANDROID_D8 AND EXISTS "${CROMO_JAVA_HOME}/bin/javac.exe")
  list(GET CROMO_ANDROID_PLATFORMS -1 platform)
  list(GET CROMO_ANDROID_D8 -1 d8)
  set(classes "${CROMO_SDL_DIR}/android_classes")
  add_custom_command(
    OUTPUT "${CROMO_SDL_DIR}/classes.dex"
    COMMAND "${CMAKE_COMMAND}" -E rm -rf "${classes}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${classes}"
    COMMAND "${CROMO_JAVA_HOME}/bin/javac.exe" -nowarn -encoding UTF-8 --release 11 -classpath "${platform}/android.jar"
            -d "${classes}" ${CROMO_SDL_JAVA}
    COMMAND "${CROMO_JAVA_HOME}/bin/jar.exe" cf "${CROMO_SDL_DIR}/android_classes.jar" -C "${classes}" .
    COMMAND "${d8}" --release --min-api 26 --lib "${platform}/android.jar" --output "${CROMO_SDL_DIR}"
            "${CROMO_SDL_DIR}/android_classes.jar"
    DEPENDS ${CROMO_SDL_JAVA}
    COMMENT "Compiling the classes of Java of SDL into classes.dex")
  list(APPEND CROMO_SDL_FILES "${CROMO_SDL_DIR}/classes.dex")
  list(APPEND CROMO_SDL_PLACE COMMAND "${CMAKE_COMMAND}" -E make_directory "${CROMO_LIB_DIR}/android"
       COMMAND "${CMAKE_COMMAND}" -E copy "${CROMO_SDL_DIR}/classes.dex" "${CROMO_LIB_DIR}/android")
else()
  message(STATUS "No SDK in CROMO_ANDROID_SDK or no JDK: lib/android will have no classes.dex (D453)")
endif()

file(GLOB CROMO_STD_MODULES CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/library/std/*.cro")
# The programs of cromo written in Cromo, as cromo update (D403).
file(GLOB CROMO_TOOLS CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/library/tools/*.cro")
add_custom_command(
  OUTPUT "${CROMO_RT_DIR}/library.stamp"
  COMMAND "${CMAKE_COMMAND}" -E rm -rf "${CROMO_LIB_DIR}/std" "${CROMO_LIB_DIR}/tools"
  COMMAND "${CMAKE_COMMAND}" -E make_directory "${CROMO_LIB_DIR}/std" "${CROMO_LIB_DIR}/tools" "${CROMO_LIB_DIR}/windows"
          "${CROMO_LIB_DIR}/linux" "${CROMO_LIB_DIR}/linux/glibc" "${CROMO_LIB_DIR}/linux_arm64"
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
foreach(arch arm64 x86_64)
  add_custom_command(
    OUTPUT "${CROMO_RT_DIR}/android_${arch}/runtime_cromo.o"
    COMMAND cromo build --runtime --target android-${arch} -o "${CROMO_RT_DIR}/android_${arch}/runtime_cromo.o"
    DEPENDS cromo "${CROMO_RT_DIR}/library.stamp"
    COMMENT "Compiling the runtime written in Cromo for Android on ${arch}")
endforeach()
add_custom_command(
  OUTPUT "${CROMO_RT_DIR}/runtime.stamp"
  COMMAND "${CMAKE_COMMAND}" -E copy "${CROMO_RT_DIR}/cromo_rt.lib" "${CROMO_LIB_DIR}/windows"
  COMMAND "${CMAKE_COMMAND}" -E copy "${CROMO_RT_DIR}/libcromo_rt.a" "${CROMO_LIB_DIR}/linux"
  COMMAND "${CMAKE_COMMAND}" -E copy "${CROMO_RT_DIR}/libcromo_rt_arm64.a" "${CROMO_LIB_DIR}/linux_arm64/libcromo_rt.a"
  COMMAND "${CMAKE_COMMAND}" -E copy "${CROMO_RT_DIR}/libcromo_rt_glibc.a" "${CROMO_LIB_DIR}/linux"
  COMMAND "${CMAKE_COMMAND}" -E copy ${CROMO_GLIBC_STUB_FILES} "${CROMO_LIB_DIR}/linux/glibc"
  ${CROMO_SDL_PLACE}
  ${CROMO_ANDROID_PLACE}
  COMMAND "${CMAKE_COMMAND}" -E touch "${CROMO_RT_DIR}/runtime.stamp"
  DEPENDS "${CROMO_RT_DIR}/cromo_rt.lib" "${CROMO_RT_DIR}/libcromo_rt.a" "${CROMO_RT_DIR}/libcromo_rt_arm64.a"
          "${CROMO_RT_DIR}/libcromo_rt_glibc.a" ${CROMO_GLIBC_STUB_FILES} ${CROMO_SDL_FILES} ${CROMO_ANDROID_ARCHIVES}
          "${CROMO_RT_DIR}/library.stamp"
  COMMENT "Placing the runtime in lib/")
add_custom_target(cromo_runtime ALL DEPENDS "${CROMO_RT_DIR}/runtime.stamp")

endif()

add_executable(cromo
  src/main.cpp
  src/ast.cpp
  src/diagnostics.cpp
  src/docs.cpp
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
