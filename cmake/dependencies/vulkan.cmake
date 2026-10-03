include_guard(GLOBAL)
find_package(Vulkan 1.4 REQUIRED GLOBAL)
list(GET Vulkan_INCLUDE_DIRS 0 CHESS_VULKAN_INCLUDE_DIRECTORY)
cmake_path(GET CHESS_VULKAN_INCLUDE_DIRECTORY PARENT_PATH CHESS_VULKAN_SDK_DIRECTORY)
add_library(chess_vulkan STATIC)
add_library(chess::vulkan ALIAS chess_vulkan)
target_sources(chess_vulkan PUBLIC FILE_SET cxx_modules TYPE CXX_MODULES
        BASE_DIRS "${CHESS_VULKAN_INCLUDE_DIRECTORY}"
        FILES "${CHESS_VULKAN_INCLUDE_DIRECTORY}/vulkan/vulkan.cppm")
target_link_libraries(chess_vulkan PUBLIC Vulkan::Vulkan)
target_compile_definitions(chess_vulkan PUBLIC VK_USE_PLATFORM_WIN32_KHR NOMINMAX)
