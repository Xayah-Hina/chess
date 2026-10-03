# ============================================================================
# Project
# ============================================================================
cmake_minimum_required(VERSION 4.4)
set(CMAKE_EXPERIMENTAL_CXX_IMPORT_STD "f35a9ac6-8463-4d38-8eec-5d6008153e7d")
file(STRINGS "${CMAKE_CURRENT_LIST_DIR}/VERSION" CHESS_VERSION LIMIT_COUNT 1)
project(chess VERSION "${CHESS_VERSION}" LANGUAGES C CXX CUDA)

# ============================================================================
# Project Configuration
# ============================================================================
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_MODULE_STD ON)

set(CMAKE_CUDA_STANDARD 20)
set(CMAKE_CUDA_STANDARD_REQUIRED ON)
set(CMAKE_CUDA_EXTENSIONS OFF)
set(CMAKE_CUDA_RUNTIME_LIBRARY Shared)
set(CMAKE_CUDA_ARCHITECTURES 120a-real)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(CHESS_BUILD_UI "Build the Windows chess editor" ON)
if(CHESS_BUILD_UI AND NOT WIN32)
    message(FATAL_ERROR "The editor requires Windows. Use CHESS_BUILD_UI=OFF for headless builds.")
endif()

# ============================================================================
# Compiler Configuration
# ============================================================================
if(WIN32)
    add_compile_definitions(NOMINMAX WIN32_LEAN_AND_MEAN)
endif()
if(MSVC)
    add_compile_options($<$<COMPILE_LANGUAGE:CXX>:/utf-8>
            $<$<COMPILE_LANGUAGE:CXX>:/Zc:__cplusplus>
            $<$<COMPILE_LANGUAGE:CXX>:/Zc:preprocessor>
            $<$<COMPILE_LANGUAGE:CXX>:/bigobj>)
endif()

# ============================================================================
# Dependencies
# ============================================================================
include(cmake/Dependencies.cmake)
chess_require_dependency(cuda)
if(CHESS_BUILD_UI)
    chess_require_dependency(vulkan)
    chess_require_dependency(slang)
    chess_require_dependency(imgui)
    include(cmake/Shaders.cmake)
endif()

# ============================================================================
# Core
# ============================================================================
add_library(chess_core STATIC)
target_sources(chess_core PRIVATE
        chess/core/position.cpp
        chess/core/rules.cpp
        chess/core/adjudication.cpp
        chess/core/game.cpp
        chess/core/headless.cpp
        PUBLIC FILE_SET cxx_modules TYPE CXX_MODULES FILES
        chess/core/position.ixx
        chess/core/rules.ixx
        chess/core/adjudication.ixx
        chess/core/game.ixx
        chess/core/headless.ixx)

# ============================================================================
# Editor
# ============================================================================
if(CHESS_BUILD_UI)
    add_library(chess_editor STATIC)
    target_sources(chess_editor PRIVATE
            chess/editor/editor.cpp
            chess/editor/workspace.cpp
            chess/editor/platform/window.cpp
            chess/editor/graphics/device.cpp
            chess/editor/graphics/resources.cpp
            chess/editor/graphics/renderer.cpp
            PUBLIC FILE_SET cxx_modules TYPE CXX_MODULES FILES
            chess/editor/editor.ixx
            PRIVATE FILE_SET editor_modules TYPE CXX_MODULES FILES
            chess/editor/workspace.ixx
            chess/editor/platform/window.ixx
            chess/editor/graphics/device.ixx
            chess/editor/graphics/resources.ixx
            chess/editor/graphics/renderer.ixx)
    target_compile_definitions(chess_editor PRIVATE GLFW_INCLUDE_NONE GLFW_EXPOSE_NATIVE_WIN32
            CHESS_SHADER_DIRECTORY="${CHESS_SHADER_DIRECTORY}")
    target_link_libraries(chess_editor PRIVATE chess_core chess::vulkan chess::imgui dwmapi)
    add_dependencies(chess_editor chess_shaders)
endif()

# ============================================================================
# Executable
# ============================================================================
add_executable(chess main.cpp)
target_link_libraries(chess PRIVATE chess_core)
if(CHESS_BUILD_UI)
    set_target_properties(chess PROPERTIES WIN32_EXECUTABLE TRUE)
    target_compile_definitions(chess PRIVATE CHESS_HAS_EDITOR)
    target_link_libraries(chess PRIVATE chess_editor)
endif()

# ============================================================================
# Standard Library Modules
# ============================================================================
# MSVC interprets a bare @cmake_cxx_std.lib as a response file.
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/cxx-modules")
