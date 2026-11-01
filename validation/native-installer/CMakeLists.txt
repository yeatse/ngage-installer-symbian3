cmake_minimum_required(VERSION 3.28)
project(swi_install_probe LANGUAGES CXX ASM)
include(SymbianApp)
symbian_add_executable(swi_install_probe main.cc)
set(apgrfx_proxy "${SYMBIAN_SDK_PREFIX}/proxies/apgrfx/apgrfx.dso")
target_include_directories(swi_install_probe SYSTEM PRIVATE "${SYMBIAN_SDK_PREFIX}/include/native")
target_link_libraries(swi_install_probe PRIVATE Symbian::Stackless Symbian::EUser
  Symbian::WindowServer Symbian::Gdi Symbian::FileServer "${apgrfx_proxy}")
target_link_options(swi_install_probe PRIVATE --gc-sections)
symbian_publish_executable(swi_install_probe UID3 0xe0000990
  IMPORT_PROXIES "${apgrfx_proxy}")
