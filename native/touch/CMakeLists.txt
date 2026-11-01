cmake_minimum_required(VERSION 3.28)
project(ngage_touch LANGUAGES CXX ASM)
include(SymbianApp)
symbian_add_executable(ngage_touch main.cc)
set(apgrfx_proxy "${SYMBIAN_SDK_PREFIX}/proxies/apgrfx/apgrfx.dso")
target_include_directories(ngage_touch SYSTEM PRIVATE "${SYMBIAN_SDK_PREFIX}/include/native")
target_link_libraries(ngage_touch PRIVATE Symbian::Stackless Symbian::EUser
  Symbian::WindowServer Symbian::Gdi Symbian::FileServer "${apgrfx_proxy}")
target_link_options(ngage_touch PRIVATE --gc-sections)
symbian_publish_executable(ngage_touch UID3 0xe0000960
  IMPORT_PROXIES "${apgrfx_proxy}")

option(NGAGE_DIAGNOSTIC "Flush diagnostic records to the phone filesystem" OFF)
target_compile_definitions(ngage_touch PRIVATE NGAGE_DIAGNOSTIC=$<BOOL:${NGAGE_DIAGNOSTIC}>)
