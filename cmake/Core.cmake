# ============================================================================
# Shared game rules.
# ============================================================================
get_filename_component(CHESS_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
add_library(chess_core STATIC)
target_sources(chess_core PRIVATE
        "${CHESS_ROOT}/chess/core/position.cpp"
        "${CHESS_ROOT}/chess/core/rules.cpp"
        "${CHESS_ROOT}/chess/core/adjudication.cpp"
        "${CHESS_ROOT}/chess/core/game.cpp"
        PUBLIC FILE_SET cxx_modules TYPE CXX_MODULES BASE_DIRS "${CHESS_ROOT}" FILES
        "${CHESS_ROOT}/chess/core/position.ixx"
        "${CHESS_ROOT}/chess/core/rules.ixx"
        "${CHESS_ROOT}/chess/core/adjudication.ixx"
        "${CHESS_ROOT}/chess/core/game.ixx")
