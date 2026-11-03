# The stubs of glibc for the linker (D428): for each library of symbols.txt, a
# shared library with its soname, its functions and its variables with their
# sizes, each with its default version, and no code that runs. An executable
# linked with them loads the real libraries of the system.
#   cmake -DSYMBOLS=<symbols.txt> -DOUT=<folder> -DCLANG=<clang> -DLLD=<ld.lld> -P stubs.cmake
cmake_minimum_required(VERSION 3.16)
file(MAKE_DIRECTORY "${OUT}")
file(STRINGS "${SYMBOLS}" lines REGEX "^[^#]")
set(libraries)
foreach(line IN LISTS lines)
  string(REPLACE " " ";" fields "${line}")
  list(GET fields 0 library)
  list(GET fields 1 version)
  list(GET fields 2 kind)
  list(GET fields 3 size)
  list(GET fields 4 name)
  if(NOT library IN_LIST libraries)
    list(APPEND libraries "${library}")
    set("text_${library}" "    .text\n")
    set("data_${library}" "    .data\n")
    set("versions_${library}")
  endif()
  set(bind ".globl")
  if(kind MATCHES "^weak-")
    set(bind ".weak")
  endif()
  if(kind MATCHES "func$")
    string(APPEND "text_${library}" "    ${bind} ${name}\n    .type ${name}, @function\n${name}:\n    ud2\n")
  else()
    string(APPEND "data_${library}"
           "    ${bind} ${name}\n    .type ${name}, @object\n    .size ${name}, ${size}\n    .p2align 3\n${name}:\n    .zero ${size}\n")
  endif()
  if(NOT version IN_LIST "versions_${library}")
    list(APPEND "versions_${library}" "${version}")
    set("names_${library}_${version}")
  endif()
  string(APPEND "names_${library}_${version}" " ${name};")
endforeach()

foreach(library IN LISTS libraries)
  set(script "")
  foreach(version IN LISTS "versions_${library}")
    string(APPEND script "${version} {\n  global:${names_${library}_${version}}\n};\n")
  endforeach()
  file(WRITE "${OUT}/${library}.s" "${text_${library}}${data_${library}}    .section .note.GNU-stack, \"\", @progbits\n")
  file(WRITE "${OUT}/${library}.map" "${script}")
  execute_process(
    COMMAND "${CLANG}" --target=x86_64-unknown-linux-gnu -c "${OUT}/${library}.s" -o "${OUT}/${library}.o"
    RESULT_VARIABLE failed)
  if(failed)
    message(FATAL_ERROR "cannot assemble the stub of ${library}")
  endif()
  execute_process(
    COMMAND "${LLD}" -shared -soname "${library}" --version-script "${OUT}/${library}.map" -o "${OUT}/${library}"
            "${OUT}/${library}.o"
    RESULT_VARIABLE failed)
  if(failed)
    message(FATAL_ERROR "cannot link the stub of ${library}")
  endif()
  file(REMOVE "${OUT}/${library}.s" "${OUT}/${library}.map" "${OUT}/${library}.o")
endforeach()
