# nierverify: compara funções reconstruídas com as traduzidas (host x86-64).
# Uso:
#   cmake -S tools/verify -B <build> -G Ninja -DCMAKE_CXX_COMPILER=clang++ \
#     -DNIER_GENERATED_DIR=<generated/default privado> -Drexglue_DIR=<sdk>/lib/cmake/rexglue
#   cmake --build <build> && NIER_PE=<default.pe> <build>/nierverify
cmake_minimum_required(VERSION 3.25)
project(nierverify LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(NIER_GENERATED_DIR "" CACHE PATH "Código gerado privado (generated/default)")
if(NOT EXISTS "${NIER_GENERATED_DIR}/nier_pch.h")
  message(FATAL_ERROR "NIER_GENERATED_DIR precisa apontar para generated/default com nier_pch.h")
endif()
find_package(rexglue 0.10.0 REQUIRED CONFIG)

file(GLOB NIER_GEN_SOURCES "${NIER_GENERATED_DIR}/nier_recomp.*.cpp")
list(APPEND NIER_GEN_SOURCES "${NIER_GENERATED_DIR}/nier_init.cpp")
add_library(nier_gen OBJECT ${NIER_GEN_SOURCES})
target_include_directories(nier_gen PRIVATE "${NIER_GENERATED_DIR}")
target_link_libraries(nier_gen PRIVATE rex::runtime)
# Nível de otimização dos DOIS lados (oráculo e reconstrução): igual ao build do jogo
# (Release = -O3), para a medição de tempo comparar código e não flags. -DNIER_VERIFY_OPT=-O1 compila rápido.
set(NIER_VERIFY_OPT "-O3" CACHE STRING "Otimização comum do oráculo e das reconstruções")
# Oráculo: mesma semântica do build do jogo.
target_compile_options(nier_gen PRIVATE ${NIER_VERIFY_OPT} -g0 -mcmodel=large -msse4.1 -w)

file(GLOB_RECURSE NIER_DECOMP_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/../../src/decomp/*.cpp")
add_executable(nierverify verify_main.cpp ${NIER_DECOMP_SOURCES} $<TARGET_OBJECTS:nier_gen>)
target_compile_definitions(nierverify PRIVATE NIER_VERIFY=1)
target_include_directories(nierverify PRIVATE "${NIER_GENERATED_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/../../include")
target_compile_options(nierverify PRIVATE ${NIER_VERIFY_OPT} -g -mcmodel=large -msse4.1)
target_link_libraries(nierverify PRIVATE rex::runtime)
set_target_properties(nierverify PROPERTIES BUILD_RPATH "$<TARGET_FILE_DIR:rex::runtime>")
