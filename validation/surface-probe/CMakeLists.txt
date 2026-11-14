cmake_minimum_required(VERSION 3.28)
project(surface_probe LANGUAGES CXX ASM)
include(SymbianApp)
symbian_add_executable(surface_probe main.cc)
# ROM code traps its own leaves, which needs the thread exception globals this turns on.
symbian_enable_native_leaves(surface_probe SOURCES main.cc)
target_include_directories(surface_probe SYSTEM PRIVATE "${SYMBIAN_SDK_PREFIX}/include/native"
  "${CMAKE_CURRENT_SOURCE_DIR}/../../native/posting/include")
target_include_directories(surface_probe PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../../native/posting")
target_link_libraries(surface_probe PRIVATE Symbian::Stackless Symbian::EUser Symbian::FileServer
  Symbian::WindowServer "${SYMBIAN_SDK_PREFIX}/proxies/fbscli/fbscli.dso"
  "${SYMBIAN_SDK_PREFIX}/proxies/bitgdi/bitgdi.dso" Symbian::Gdi)
target_link_options(surface_probe PRIVATE --gc-sections)
symbian_publish_executable(surface_probe UID3 0xe000099a)
