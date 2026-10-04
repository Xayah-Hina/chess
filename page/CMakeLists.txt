# ============================================================================
# Browser engine.
# ============================================================================
cmake_minimum_required(VERSION 4.4)
set(CMAKE_EXPERIMENTAL_CXX_IMPORT_STD "f35a9ac6-8463-4d38-8eec-5d6008153e7d")
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_MODULE_STD ON)
project(chess_page LANGUAGES C CXX)
if(NOT EMSCRIPTEN)
    message(FATAL_ERROR "The page engine requires Emscripten 6.0.11.")
endif()

# ============================================================================
# Shared rules and search.
# ============================================================================
include(../cmake/Core.cmake)
include(../cmake/Inference.cmake)

# ============================================================================
# JavaScript interface.
# ============================================================================
add_executable(engine engine/bindings.cpp)
target_sources(engine PRIVATE FILE_SET cxx_modules TYPE CXX_MODULES FILES engine/bindings.ixx)
target_link_libraries(engine PRIVATE chess_inference)
target_compile_options(engine PRIVATE -O3 -msimd128)
target_compile_options(chess_core PRIVATE -O3 -msimd128)
target_compile_options(chess_inference PRIVATE -O3 -msimd128)
target_link_options(engine PRIVATE --bind -O3 -msimd128
        -sMODULARIZE=1 -sEXPORT_ES6=1 -sENVIRONMENT=web,worker,node
        -sALLOW_MEMORY_GROWTH=1 -sDISABLE_EXCEPTION_CATCHING=0
        -sEXPORTED_RUNTIME_METHODS=HEAPF32)
set_target_properties(engine PROPERTIES SUFFIX ".mjs")
