# ============================================================================
# Shared encoding, model layout, artifacts and search tree.
# ============================================================================
add_library(chess_inference STATIC)
target_sources(chess_inference PRIVATE
        "${CHESS_ROOT}/chess/ai/encoding.cpp"
        "${CHESS_ROOT}/chess/ai/archive.cpp"
        "${CHESS_ROOT}/chess/ai/model.cpp"
        "${CHESS_ROOT}/chess/ai/tree.cpp"
        PUBLIC FILE_SET cxx_modules TYPE CXX_MODULES BASE_DIRS "${CHESS_ROOT}" FILES
        "${CHESS_ROOT}/chess/ai/encoding.ixx"
        "${CHESS_ROOT}/chess/ai/archive.ixx"
        "${CHESS_ROOT}/chess/ai/model.ixx"
        "${CHESS_ROOT}/chess/ai/config.ixx"
        "${CHESS_ROOT}/chess/ai/tree.ixx")
target_link_libraries(chess_inference PUBLIC chess_core)
