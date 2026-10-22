cmake_minimum_required(VERSION 3.24)
project(ScreamRedux LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(SDL3 REQUIRED CONFIG)

# Lua 5.1.5, patched to load the original 32-bit bytecode (third_party/lundump-size_t.patch).
file(GLOB LUA_SOURCES third_party/lua-5.1.5/src/*.c)
list(REMOVE_ITEM LUA_SOURCES
  ${CMAKE_CURRENT_SOURCE_DIR}/third_party/lua-5.1.5/src/lua.c
  ${CMAKE_CURRENT_SOURCE_DIR}/third_party/lua-5.1.5/src/luac.c
  ${CMAKE_CURRENT_SOURCE_DIR}/third_party/lua-5.1.5/src/print.c)
add_library(lua51 STATIC ${LUA_SOURCES})
target_include_directories(lua51 PUBLIC third_party/lua-5.1.5/src)
if(APPLE)
  target_compile_definitions(lua51 PRIVATE LUA_USE_MACOSX)
elseif(UNIX)
  target_compile_definitions(lua51 PRIVATE LUA_USE_LINUX)
endif()
if(NOT MSVC)
  target_compile_options(lua51 PRIVATE -w)
endif()

# libogg + libtheora (decoder only, portable C) for cutscenes.
add_library(ogg STATIC third_party/libogg-1.3.6/src/bitwise.c third_party/libogg-1.3.6/src/framing.c)
target_include_directories(ogg PUBLIC third_party/libogg-1.3.6/include)
set(THEORA_DIR third_party/libtheora-1.2.0/lib)
add_library(theoradec STATIC
  ${THEORA_DIR}/apiwrapper.c ${THEORA_DIR}/bitpack.c ${THEORA_DIR}/decapiwrapper.c
  ${THEORA_DIR}/decinfo.c ${THEORA_DIR}/decode.c ${THEORA_DIR}/dequant.c
  ${THEORA_DIR}/fragment.c ${THEORA_DIR}/huffdec.c ${THEORA_DIR}/idct.c
  ${THEORA_DIR}/info.c ${THEORA_DIR}/internal.c ${THEORA_DIR}/quant.c ${THEORA_DIR}/state.c)
target_include_directories(theoradec PUBLIC third_party/libtheora-1.2.0/include)
target_link_libraries(theoradec PUBLIC ogg)
if(NOT MSVC)
  target_compile_options(ogg PRIVATE -w)
  target_compile_options(theoradec PRIVATE -w)
endif()

add_executable(scream
  src/main.cpp
  src/resources.cpp
  src/images.cpp
  src/world.cpp
  src/script.cpp
  src/api.cpp
  src/gl.cpp
  src/renderer.cpp
  src/engine.cpp
  src/audio.cpp
  src/sound.cpp
  src/text.cpp
  src/video.cpp
  src/ui.cpp
  src/save.cpp
  src/menu.cpp
  src/effects.cpp
  src/check.cpp
  src/lua50.cpp
  src/crawl.cpp
  src/explore.cpp)
target_include_directories(scream PRIVATE src third_party/stb)
target_link_libraries(scream PRIVATE lua51 theoradec SDL3::SDL3)
if(NOT MSVC)
  target_compile_options(scream PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()

# stb headers are third-party; keep their warnings out of our build output.
set_source_files_properties(src/images.cpp src/audio.cpp PROPERTIES COMPILE_OPTIONS "-w")
