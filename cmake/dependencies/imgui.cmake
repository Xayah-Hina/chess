include_guard(GLOBAL)
chess_require_dependency(glfw)
FetchContent_MakeAvailable(imgui)
add_library(chess_imgui STATIC
        "${imgui_SOURCE_DIR}/imgui.cpp"
        "${imgui_SOURCE_DIR}/imgui_draw.cpp"
        "${imgui_SOURCE_DIR}/imgui_tables.cpp"
        "${imgui_SOURCE_DIR}/imgui_widgets.cpp"
        "${imgui_SOURCE_DIR}/misc/cpp/imgui_stdlib.cpp"
        "${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp")
add_library(chess::imgui ALIAS chess_imgui)
target_include_directories(chess_imgui PUBLIC "${imgui_SOURCE_DIR}" "${imgui_SOURCE_DIR}/backends")
target_compile_definitions(chess_imgui PRIVATE GLFW_INCLUDE_NONE)
target_link_libraries(chess_imgui PUBLIC glfw)
