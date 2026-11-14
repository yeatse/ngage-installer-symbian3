cmake_minimum_required(VERSION 3.28)
project(import_probe LANGUAGES CXX ASM)
include(SymbianApp)
symbian_add_executable(import_probe main.cc)
target_include_directories(import_probe SYSTEM PRIVATE "${SYMBIAN_SDK_PREFIX}/include/native"
  "${CMAKE_CURRENT_SOURCE_DIR}/../../native/posting/include")
# Only euser and efsrv are imported, so the loader cannot refuse the probe for what it is probing.
target_link_libraries(import_probe PRIVATE Symbian::Stackless Symbian::EUser Symbian::FileServer)
target_link_options(import_probe PRIVATE --gc-sections)
symbian_publish_executable(import_probe UID3 0xe000099c)
