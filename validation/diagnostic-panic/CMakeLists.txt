cmake_minimum_required(VERSION 3.28)
project(diagnostic_panic_probe LANGUAGES CXX ASM)
include(SymbianApp)
symbian_add_executable(diagnostic_panic_probe main.cc)
set(apgrfx_proxy "${SYMBIAN_SDK_PREFIX}/proxies/apgrfx/apgrfx.dso")
target_include_directories(diagnostic_panic_probe SYSTEM PRIVATE "${SYMBIAN_SDK_PREFIX}/include/native")
target_link_libraries(diagnostic_panic_probe PRIVATE Symbian::Stackless Symbian::EUser
  Symbian::WindowServer Symbian::Gdi Symbian::FileServer "${apgrfx_proxy}")
target_link_options(diagnostic_panic_probe PRIVATE --gc-sections)
symbian_publish_executable(diagnostic_panic_probe UID3 0xe0000994
  IMPORT_PROXIES "${apgrfx_proxy}")
