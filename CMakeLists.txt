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

option(CHESS_BUILD_UI "Build the Windows chess editor and Trainer panel" ON)
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
chess_require_dependency(cudnn)
chess_require_dependency(nlohmann_json)
chess_require_dependency(cudnn_frontend)
chess_require_dependency(pikafish)
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
# Source Identity
# ============================================================================
execute_process(COMMAND git rev-parse HEAD WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
        OUTPUT_VARIABLE CHESS_SOURCE_REVISION OUTPUT_STRIP_TRAILING_WHITESPACE COMMAND_ERROR_IS_FATAL ANY)
file(GLOB_RECURSE CHESS_IDENTITY_SOURCES CONFIGURE_DEPENDS
        "${PROJECT_SOURCE_DIR}/chess/*.ixx" "${PROJECT_SOURCE_DIR}/chess/*.cpp"
        "${PROJECT_SOURCE_DIR}/chess/*.cu" "${PROJECT_SOURCE_DIR}/chess/*.h"
        "${PROJECT_SOURCE_DIR}/benchmark/*.ixx" "${PROJECT_SOURCE_DIR}/benchmark/*.cpp"
        "${PROJECT_SOURCE_DIR}/cmake/*.cmake"
        "${PROJECT_SOURCE_DIR}/tools/*.cpp" "${PROJECT_SOURCE_DIR}/tools/*.ixx")
list(APPEND CHESS_IDENTITY_SOURCES "${PROJECT_SOURCE_DIR}/CMakeLists.txt" "${PROJECT_SOURCE_DIR}/main.cpp")
list(SORT CHESS_IDENTITY_SOURCES)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${CHESS_IDENTITY_SOURCES})
foreach(source IN LISTS CHESS_IDENTITY_SOURCES)
    file(SHA256 "${source}" digest)
    file(RELATIVE_PATH name "${PROJECT_SOURCE_DIR}" "${source}")
    string(APPEND CHESS_SOURCE_IDENTITY "${name}:${digest}\n")
endforeach()
string(SHA256 CHESS_SOURCE_HASH "${CHESS_SOURCE_IDENTITY}")

# ============================================================================
# AI
# ============================================================================
add_library(chess_ai STATIC)
target_sources(chess_ai PRIVATE
        chess/ai/encoding.cpp
        chess/ai/device.cpp
        chess/ai/network.cpp
        chess/ai/tensors.h
        chess/ai/kernels.h
        chess/ai/kernels.cu
        chess/ai/search.cpp
        chess/ai/selfplay.cpp
        chess/ai/replay.cpp
        chess/ai/checkpoint.cpp
        chess/ai/training.cpp
        chess/ai/headless.cpp
        PUBLIC FILE_SET cxx_modules TYPE CXX_MODULES FILES
        chess/ai/encoding.ixx
        chess/ai/config.ixx
        chess/ai/device.ixx
        chess/ai/network.ixx
        chess/ai/search.ixx
        chess/ai/selfplay.ixx
        chess/ai/replay.ixx
        chess/ai/checkpoint.ixx
        chess/ai/training.ixx
        chess/ai/headless.ixx)
target_link_libraries(chess_ai PUBLIC chess_core PRIVATE chess::cudnn-frontend CUDA::cudart CUDA::cublasLt CUDA::nvrtc CUDA::cuda_driver)
if(MSVC)
    target_compile_options(chess_ai PRIVATE $<$<COMPILE_LANGUAGE:CUDA>:-Xcompiler=/utf-8>)
endif()

# ============================================================================
# Benchmark
# ============================================================================
add_library(chess_benchmark STATIC)
target_sources(chess_benchmark PRIVATE
        benchmark/config.cpp
        benchmark/artifact.cpp
        benchmark/probe.cpp
        benchmark/opponents.cpp
        benchmark/pikafish.cpp
        benchmark/arena.cpp
        benchmark/evaluation.cpp
        PUBLIC FILE_SET cxx_modules TYPE CXX_MODULES FILES
        benchmark/config.ixx
        benchmark/artifact.ixx
        benchmark/probe.ixx
        benchmark/opponents.ixx
        benchmark/pikafish.ixx
        benchmark/arena.ixx
        benchmark/evaluation.ixx)
target_link_libraries(chess_benchmark PUBLIC chess_ai PRIVATE chess::cudnn-frontend nlohmann_json::nlohmann_json CUDA::cudart bcrypt)
target_compile_definitions(chess_benchmark PRIVATE CHESS_SOURCE_REVISION="${CHESS_SOURCE_REVISION}" CHESS_SOURCE_HASH="${CHESS_SOURCE_HASH}"
        CHESS_ASSET_DIRECTORY="${PROJECT_SOURCE_DIR}/assets")

# ============================================================================
# Trainer Core
# ============================================================================
add_library(trainer_core STATIC)
target_sources(trainer_core PRIVATE
        tools/trainer/core/session.cpp
        tools/trainer/core/headless.cpp
        PUBLIC FILE_SET cxx_modules TYPE CXX_MODULES FILES
        tools/trainer/core/session.ixx
        tools/trainer/core/headless.ixx)
target_link_libraries(trainer_core PUBLIC chess_ai chess_benchmark PRIVATE nlohmann_json::nlohmann_json)

# ============================================================================
# Shared Editor
# ============================================================================
if(CHESS_BUILD_UI)
    add_library(tools_editor STATIC)
    target_sources(tools_editor PRIVATE
            tools/common/editor/platform/window.cpp
            tools/common/editor/graphics/device.cpp
            tools/common/editor/graphics/resources.cpp
            tools/common/editor/graphics/renderer.cpp
            tools/common/editor/style.cpp
            PUBLIC FILE_SET cxx_modules TYPE CXX_MODULES FILES
            tools/common/editor/platform/window.ixx
            tools/common/editor/graphics/device.ixx
            tools/common/editor/graphics/resources.ixx
            tools/common/editor/graphics/renderer.ixx
            tools/common/editor/style.ixx)
    target_compile_definitions(tools_editor PRIVATE GLFW_INCLUDE_NONE GLFW_EXPOSE_NATIVE_WIN32
            CHESS_SHADER_DIRECTORY="${CHESS_SHADER_DIRECTORY}")
    target_link_libraries(tools_editor PUBLIC chess::vulkan chess::imgui PRIVATE dwmapi)
    add_dependencies(tools_editor chess_shaders)

    add_library(chess_editor STATIC)
    target_sources(chess_editor PRIVATE
            chess/editor/editor.cpp
            chess/editor/workspace.cpp
            PUBLIC FILE_SET cxx_modules TYPE CXX_MODULES FILES
            chess/editor/editor.ixx
            PRIVATE FILE_SET editor_modules TYPE CXX_MODULES FILES
            chess/editor/workspace.ixx)
    target_link_libraries(chess_editor PRIVATE chess_core tools_editor)
endif()

# ============================================================================
# Executables
# ============================================================================
add_executable(chess main.cpp)
target_link_libraries(chess PRIVATE chess_core)
if(CHESS_BUILD_UI)
    set_target_properties(chess PROPERTIES WIN32_EXECUTABLE TRUE)
    target_compile_definitions(chess PRIVATE CHESS_HAS_EDITOR)
    target_link_libraries(chess PRIVATE chess_editor)
endif()

add_executable(trainer-headless tools/trainer/headless_main.cpp)
target_link_libraries(trainer-headless PRIVATE trainer_core)
set_target_properties(trainer-headless PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/tools")
set(CHESS_TRAINER_TARGETS trainer-headless)
if(CHESS_BUILD_UI)
    add_executable(trainer WIN32 tools/trainer/main.cpp)
    target_sources(trainer PRIVATE
            tools/trainer/editor/editor.cpp
            tools/trainer/editor/workspace.cpp
            PRIVATE FILE_SET cxx_modules TYPE CXX_MODULES FILES
            tools/trainer/editor/editor.ixx
            tools/trainer/editor/workspace.ixx)
    target_link_libraries(trainer PRIVATE trainer_core tools_editor ole32 shell32)
    set_target_properties(trainer PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/tools")
    list(APPEND CHESS_TRAINER_TARGETS trainer)
endif()
foreach(target IN LISTS CHESS_TRAINER_TARGETS)
    add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CHESS_CUDNN_DLLS} "$<TARGET_FILE_DIR:${target}>"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CHESS_CUBLASLT_DLL}" "$<TARGET_FILE_DIR:${target}>"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CHESS_NVRTC_DLLS} "${CHESS_CUDART_DLL}" "$<TARGET_FILE_DIR:${target}>"
            COMMAND_EXPAND_LISTS)
endforeach()

# ============================================================================
# Standard Library Modules
# ============================================================================
# MSVC interprets a bare @cmake_cxx_std.lib as a response file.
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/cxx-modules")
