cmake_minimum_required(VERSION 3.28)
project(ngage_recognizer LANGUAGES CXX ASM)
include(SymbianPic)
include(SymbianPlatform)
symbian_add_dynamic_library(ngdatrecognizer SOURCES main.cc
  LINKER_SCRIPT "${SYMBIAN_SDK_PREFIX}/cmake/dll_image.ld"
  UID3 0xe0000966 EXPORT_DEFINITION exports.def)
set_source_files_properties(main.cc PROPERTIES COMPILE_OPTIONS "-fvisibility=hidden")
target_include_directories(ngdatrecognizer SYSTEM PRIVATE
  "${SYMBIAN_SDK_PREFIX}/include/native"
  "${SYMBIAN_SDK_PREFIX}/include/c++"
  "${SYMBIAN_SDK_PREFIX}/include/config"
  "${SYMBIAN_SDK_PREFIX}/include/compiler")
target_compile_options(ngdatrecognizer PRIVATE -fshort-wchar -fno-exceptions -fno-rtti
  "$<$<COMPILE_LANGUAGE:CXX>:SHELL:-include ${SYMBIAN_SDK_PREFIX}/cmake/native_cpp_compat.h>")
target_link_libraries(ngdatrecognizer PRIVATE Symbian::EUser
  "${SYMBIAN_SDK_PREFIX}/proxies/apmime/apmime.dso")
