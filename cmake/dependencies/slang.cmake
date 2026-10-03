include_guard(GLOBAL)
chess_require_dependency(vulkan)
find_program(CHESS_SLANG_COMPILER NAMES slangc HINTS "${CHESS_VULKAN_SDK_DIRECTORY}/Bin" REQUIRED NO_DEFAULT_PATH)
