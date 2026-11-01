cmake_minimum_required(VERSION 3.28)
project(diagnostic_selftest LANGUAGES CXX ASM)
include(SymbianApp)
symbian_add_executable(diagnostic_selftest main.cc)
set(apgrfx_proxy "${SYMBIAN_SDK_PREFIX}/proxies/apgrfx/apgrfx.dso")
target_include_directories(diagnostic_selftest SYSTEM PRIVATE "${SYMBIAN_SDK_PREFIX}/include/native")
target_link_libraries(diagnostic_selftest PRIVATE Symbian::Stackless Symbian::EUser
  Symbian::WindowServer Symbian::Gdi Symbian::FileServer "${apgrfx_proxy}")
target_link_options(diagnostic_selftest PRIVATE --gc-sections)
symbian_publish_executable(diagnostic_selftest UID3 0xe0000996
  IMPORT_PROXIES "${apgrfx_proxy}")
