cmake_minimum_required(VERSION 3.28)
project(ngage_audio LANGUAGES CXX ASM)
include(SymbianPic)
include(SymbianPlatform)
symbian_add_dynamic_library(NgageSoundX SOURCES main.cc forward.S
  LINKER_SCRIPT "${SYMBIAN_SDK_PREFIX}/cmake/dll_image.ld"
  UID3 0xe0000964 EXPORT_DEFINITION exports.def)
set_source_files_properties(main.cc PROPERTIES COMPILE_OPTIONS "-fvisibility=hidden")
target_include_directories(NgageSoundX SYSTEM PRIVATE
  "${SYMBIAN_SDK_PREFIX}/include/native"
  "${SYMBIAN_SDK_PREFIX}/include/c++"
  "${SYMBIAN_SDK_PREFIX}/include/config"
  "${SYMBIAN_SDK_PREFIX}/include/compiler")
target_compile_options(NgageSoundX PRIVATE -fshort-wchar -fno-exceptions -fno-rtti
  "$<$<COMPILE_LANGUAGE:CXX>:SHELL:-include ${SYMBIAN_SDK_PREFIX}/cmake/native_cpp_compat.h>")
target_link_libraries(NgageSoundX PRIVATE Symbian::EUser Symbian::FileServer)
