# Renderizador GLES2 do NieR (Mali-450). OBJECT de propósito: os ganchos REX_HOOK_RAW substituem os
# símbolos fracos sub_XXXXXXXX do código gerado, e uma biblioteca estática não seria puxada pelo linker.
#
# Tradutor Xenos -> ESSL: dependência PRIVADA fixada (nunca neste repositório público).
#   NIER_XENOS_TRANSLATOR_DIR = diretório com xenos_shader.{h,cpp} e depth_codec.h de
#   NextOs-Ports/dmc3-hd-nextos commit c76b074fc36fa4639ce9f167d5dcfbf9a7ba565e (runtime/).
#   O build confere o SHA-256 de cada arquivo; patches privados ficam em NIER_XENOS_TRANSLATOR_PATCHED.
cmake_minimum_required(VERSION 3.25)
if(NOT PROJECT_NAME)
  project(nier_render LANGUAGES CXX)
  set(CMAKE_CXX_STANDARD 23)
endif()

find_package(SDL3 REQUIRED CONFIG)
find_library(NIER_GLESV2 GLESv2 REQUIRED)
find_library(NIER_EGL EGL REQUIRED)

set(NIER_XENOS_TRANSLATOR_DIR "" CACHE PATH "Tradutor Xenos->ESSL privado (dmc3-hd-nextos c76b074 runtime/)")
set(NIER_XENOS_TRANSLATOR_PATCHED OFF CACHE BOOL "Aceitar xenos_shader.cpp com patch privado (hash registrado no diário)")

add_library(nier_render_deps INTERFACE)
target_link_libraries(nier_render_deps INTERFACE SDL3::SDL3 ${NIER_GLESV2} ${NIER_EGL})

set(NIER_RENDER_CORE ${CMAKE_CURRENT_SOURCE_DIR}/renderer.cpp ${CMAKE_CURRENT_SOURCE_DIR}/shader_runtime.cpp ${CMAKE_CURRENT_SOURCE_DIR}/test_input.cpp)
if(NIER_XENOS_TRANSLATOR_DIR)
  set(_pins
    "xenos_shader.h=d822eb324c1b69ceaf047a47e6ff3b773d541d19df9da2848eb70e468f82ba4d"
    "depth_codec.h=8412c715a1e75f9e0f92bda523f53137c3eb6611edd9e44c06816d338f072571"
    "xenos_shader.cpp=d46a61fe7e5a062e5f069dccfcb8b6d9937a10d0ed71919aeff13884b7bdab7a")
  foreach(_pin ${_pins})
    string(REPLACE "=" ";" _kv ${_pin})
    list(GET _kv 0 _file)
    list(GET _kv 1 _want)
    file(SHA256 "${NIER_XENOS_TRANSLATOR_DIR}/${_file}" _got)
    if(NOT _got STREQUAL _want)
      if(_file STREQUAL "xenos_shader.cpp" AND NIER_XENOS_TRANSLATOR_PATCHED)
        message(STATUS "nier_render: xenos_shader.cpp com patch privado, sha256 ${_got}")
      else()
        message(FATAL_ERROR "nier_render: ${_file} sha256 ${_got} difere do pino c76b074 (${_want})")
      endif()
    endif()
  endforeach()
  list(APPEND NIER_RENDER_CORE ${NIER_XENOS_TRANSLATOR_DIR}/xenos_shader.cpp)
endif()

if(TARGET rex::runtime)
  add_library(nier_render OBJECT ${NIER_RENDER_CORE} ${CMAKE_CURRENT_SOURCE_DIR}/d3d_hooks.cpp)
  target_link_libraries(nier_render PRIVATE rex::runtime nier_render_deps)
else()
  add_library(nier_render OBJECT ${NIER_RENDER_CORE})
  target_link_libraries(nier_render PRIVATE nier_render_deps)
endif()
target_include_directories(nier_render PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
if(NIER_XENOS_TRANSLATOR_DIR)
  target_include_directories(nier_render PRIVATE ${NIER_XENOS_TRANSLATOR_DIR})
  target_compile_definitions(nier_render PRIVATE NIER_HAVE_XENOS_TRANSLATOR NEXTOS_GLES2)
endif()

if(NOT TARGET rex::runtime)
  add_executable(nier_render_selftest ${CMAKE_CURRENT_SOURCE_DIR}/selftest.cpp $<TARGET_OBJECTS:nier_render>)
  target_link_libraries(nier_render_selftest PRIVATE nier_render_deps)
  target_include_directories(nier_render_selftest PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
endif()
