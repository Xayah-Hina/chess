include_guard(GLOBAL)

set(CHESS_PIKAFISH_RELEASE "2026-09-06")
set(CHESS_PIKAFISH_DIRECTORY "${PROJECT_SOURCE_DIR}/assets/benchmark/engines/pikafish")
if(NOT EXISTS "${CHESS_PIKAFISH_DIRECTORY}/Pikafish-Windows-x86-64-universal.exe"
        OR NOT EXISTS "${CHESS_PIKAFISH_DIRECTORY}/pikafish.nnue")
    file(MAKE_DIRECTORY "${CHESS_PIKAFISH_DIRECTORY}")
    file(MAKE_DIRECTORY "${PROJECT_BINARY_DIR}/downloads")
    set(CHESS_PIKAFISH_ARCHIVE "${PROJECT_BINARY_DIR}/downloads/Pikafish.2026-09-06.7z")
    file(DOWNLOAD "https://github.com/official-pikafish/Pikafish/releases/download/Pikafish-${CHESS_PIKAFISH_RELEASE}/Pikafish.2026-09-06.7z"
            "${CHESS_PIKAFISH_ARCHIVE}"
            EXPECTED_HASH SHA256=41952BBFE2520FACEB5902C69E6AB4845CC999841D2B49A95CC1BE7867A25E5B)
    file(ARCHIVE_EXTRACT INPUT "${CHESS_PIKAFISH_ARCHIVE}"
            DESTINATION "${CHESS_PIKAFISH_DIRECTORY}"
            PATTERNS Pikafish-Windows-x86-64-universal.exe pikafish.nnue Copying.txt NNUE-License.md)
endif()
