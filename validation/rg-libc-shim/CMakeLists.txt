cmake_minimum_required(VERSION 3.28)
project(rg_libc_shim LANGUAGES CXX ASM)
include(SymbianPic)
include(SymbianPlatform)
symbian_add_dynamic_library(rgc SOURCES main.cc forward.S
  LINKER_SCRIPT "${SYMBIAN_SDK_PREFIX}/cmake/dll_image.ld"
  UID3 0xe00009a1 EXPORT_DEFINITION exports.def)
set_source_files_properties(main.cc PROPERTIES COMPILE_OPTIONS "-fvisibility=hidden")
target_include_directories(rgc SYSTEM PRIVATE
  "${SYMBIAN_SDK_PREFIX}/include/native"
  "${SYMBIAN_SDK_PREFIX}/include/c++"
  "${SYMBIAN_SDK_PREFIX}/include/config"
  "${SYMBIAN_SDK_PREFIX}/include/compiler")
target_compile_options(rgc PRIVATE -fshort-wchar -fno-exceptions -fno-rtti
  "$<$<COMPILE_LANGUAGE:CXX>:SHELL:-include ${SYMBIAN_SDK_PREFIX}/cmake/native_cpp_compat.h>")
target_link_libraries(rgc PRIVATE Symbian::EUser)
