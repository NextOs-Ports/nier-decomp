# Renderizador GLES2 do NieR (Mali-450). OBJECT de propósito: os ganchos REX_HOOK_RAW substituem os
# símbolos fracos sub_XXXXXXXX do código gerado, e uma biblioteca estática não seria puxada pelo linker.
#
# No projeto do runtime (port/CMakeLists.txt, X1):
#   add_subdirectory(render)
#   target_sources(nier PRIVATE $<TARGET_OBJECTS:nier_render>)
#   target_link_libraries(nier PRIVATE nier_render_deps)
# e chamar nier::render::Init() em NierApp::OnPostSetup quando NIER_NATIVE_RENDER estiver definido.
cmake_minimum_required(VERSION 3.25)
if(NOT PROJECT_NAME)
  project(nier_render LANGUAGES CXX)
  set(CMAKE_CXX_STANDARD 23)
endif()

find_package(SDL3 REQUIRED CONFIG)
find_library(NIER_GLESV2 GLESv2 REQUIRED)

add_library(nier_render_deps INTERFACE)
target_link_libraries(nier_render_deps INTERFACE SDL3::SDL3 ${NIER_GLESV2})

set(NIER_RENDER_CORE ${CMAKE_CURRENT_SOURCE_DIR}/renderer.cpp)

if(TARGET rex::runtime)
  add_library(nier_render OBJECT ${NIER_RENDER_CORE} ${CMAKE_CURRENT_SOURCE_DIR}/d3d_hooks.cpp)
  target_link_libraries(nier_render PRIVATE rex::runtime nier_render_deps)
else()
  # Sem o SDK: compila só o núcleo e o autoteste (prova de compilação host/aarch64).
  add_library(nier_render OBJECT ${NIER_RENDER_CORE})
  target_link_libraries(nier_render PRIVATE nier_render_deps)
endif()
target_include_directories(nier_render PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(nier_render_selftest ${CMAKE_CURRENT_SOURCE_DIR}/selftest.cpp $<TARGET_OBJECTS:nier_render>)
target_link_libraries(nier_render_selftest PRIVATE nier_render_deps)
target_include_directories(nier_render_selftest PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
