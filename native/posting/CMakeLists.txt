cmake_minimum_required(VERSION 3.28)
project(ngage_posting LANGUAGES CXX ASM)
include(SymbianPic)
include(SymbianPlatform)
symbian_add_dynamic_library(PostingSurfaceFactory SOURCES main.cc
  LINKER_SCRIPT "${SYMBIAN_SDK_PREFIX}/cmake/dll_image.ld"
  UID3 0xe0000962 EXPORT_DEFINITION exports.def)
# The SDK has no import libraries for the Symbian^3 surface DLLs; these come from their frozen exports.
set(surface_proxies)
foreach(name surfacemanager surfaceupdateclient)
  set(dir "${CMAKE_CURRENT_BINARY_DIR}/${name}-import")
  add_custom_command(OUTPUT "${dir}/${name}.dso"
    COMMAND "${SYMBIAN_SDK_PREFIX}/bin/symbian-native" proxy-sources
      --definition "${CMAKE_CURRENT_SOURCE_DIR}/defs/${name}.def" --target-dll "${name}.dll" --output "${dir}"
    COMMAND "${CMAKE_CXX_COMPILER}" --target=armv5t-none-eabi -march=armv5t
      -c "${dir}/exports.S" -o "${dir}/exports.o"
    COMMAND "${CMAKE_LINKER}" -m armelf -shared --hash-style=sysv --build-id=none "--soname=${name}.dso"
      "--version-script=${dir}/exports.map" -T "${dir}/proxy.ld" "${dir}/exports.o" -o "${dir}/${name}.dso"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/defs/${name}.def" VERBATIM)
  list(APPEND surface_proxies "${dir}/${name}.dso")
endforeach()
add_custom_target(surface_proxies DEPENDS ${surface_proxies})
add_dependencies(PostingSurfaceFactory surface_proxies)
set_property(TARGET PostingSurfaceFactory APPEND PROPERTY SYMBIAN_EXTRA_PROXIES
  "${SYMBIAN_SDK_PREFIX}/proxies/fbscli/fbscli.dso" "${SYMBIAN_SDK_PREFIX}/proxies/bitgdi/bitgdi.dso"
  ${surface_proxies})
set_source_files_properties(main.cc PROPERTIES COMPILE_OPTIONS "-fvisibility=hidden")
target_include_directories(PostingSurfaceFactory SYSTEM PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/include"
  "${SYMBIAN_SDK_PREFIX}/include/native"
  "${SYMBIAN_SDK_PREFIX}/include/c++"
  "${SYMBIAN_SDK_PREFIX}/include/config"
  "${SYMBIAN_SDK_PREFIX}/include/compiler")
target_compile_options(PostingSurfaceFactory PRIVATE -fshort-wchar -fno-exceptions -fno-rtti
  "$<$<COMPILE_LANGUAGE:CXX>:SHELL:-include ${SYMBIAN_SDK_PREFIX}/cmake/native_cpp_compat.h>")
target_link_libraries(PostingSurfaceFactory PRIVATE Symbian::EUser
  Symbian::FileServer Symbian::WindowServer "${SYMBIAN_SDK_PREFIX}/proxies/fbscli/fbscli.dso"
  "${SYMBIAN_SDK_PREFIX}/proxies/bitgdi/bitgdi.dso" Symbian::Gdi ${surface_proxies})
