include_guard(GLOBAL)
include(FetchContent)
set(FETCHCONTENT_TRY_FIND_PACKAGE_MODE NEVER)

FetchContent_Declare(glfw
        URL "https://codeload.github.com/glfw/glfw/zip/refs/tags/3.4"
        URL_HASH SHA256=A133DDC3D3C66143EBA9035621DB8E0BCF34DBA1EE9514A9E23E96AFD39FD57A
        SYSTEM EXCLUDE_FROM_ALL)
FetchContent_Declare(imgui
        URL "https://codeload.github.com/ocornut/imgui/zip/b334d19b667958ed970000073644d911fae17e57"
        URL_HASH SHA256=504BC8171B80B8C92F035EBC899F6B3086C9CFA56EFADEE4962753DEB38626A2)

set(CHESS_DEPENDENCIES_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}/dependencies")
macro(chess_require_dependency dependency)
    include("${CHESS_DEPENDENCIES_DIRECTORY}/${dependency}.cmake")
endmacro()
