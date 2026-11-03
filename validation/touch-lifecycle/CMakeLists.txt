cmake_minimum_required(VERSION 3.28)
project(touch_lifecycle LANGUAGES CXX ASM)
include(SymbianApp)
symbian_add_executable(touch_lifecycle main.cc)
set(apgrfx_proxy "${SYMBIAN_SDK_PREFIX}/proxies/apgrfx/apgrfx.dso")
target_include_directories(touch_lifecycle SYSTEM PRIVATE "${SYMBIAN_SDK_PREFIX}/include/native")
target_link_libraries(touch_lifecycle PRIVATE Symbian::Stackless Symbian::EUser
  Symbian::WindowServer Symbian::Gdi Symbian::FileServer "${apgrfx_proxy}")
target_link_options(touch_lifecycle PRIVATE --gc-sections)
symbian_publish_executable(touch_lifecycle UID3 0xe0000998
  IMPORT_PROXIES "${apgrfx_proxy}")
