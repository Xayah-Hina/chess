#if defined(CHESS_HAS_EDITOR)
#include <Windows.h>
#include <cstdio>
#include <cstdlib>
#include <io.h>
import chess.editor;
#endif
import chess.headless;
import std;

#if defined(CHESS_HAS_EDITOR)
namespace {
    void attach_console() {
        constexpr std::array handles{STD_INPUT_HANDLE, STD_OUTPUT_HANDLE, STD_ERROR_HANDLE};
        std::array<HANDLE, 3> inherited;
        for (std::size_t i = 0; i < handles.size(); ++i) inherited[i] = GetStdHandle(handles[i]);
        if (!AttachConsole(ATTACH_PARENT_PROCESS)) {
            const auto error = GetLastError();
            if (error != ERROR_ACCESS_DENIED && error != ERROR_INVALID_HANDLE) throw std::system_error{static_cast<int>(error), std::system_category(), "Attach parent console"};
        }
        const std::array streams{stdin, stdout, stderr};
        for (std::size_t i = 0; i < streams.size(); ++i) {
            if (_fileno(streams[i]) < 0) {
                FILE* stream{};
                const auto error = freopen_s(&stream, i == 0 ? "CONIN$" : "CONOUT$", i == 0 ? "r" : "w", streams[i]);
                if (error) throw std::system_error{error, std::generic_category(), "Connect standard stream"};
            }
            SetStdHandle(handles[i], inherited[i] && inherited[i] != INVALID_HANDLE_VALUE ? inherited[i] : reinterpret_cast<HANDLE>(_get_osfhandle(_fileno(streams[i]))));
        }
    }
} // namespace

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
    const int argc = __argc;
    char** argv    = __argv;
    bool editor    = true;
#else
int main(const int argc, char** argv) {
#endif
    try {
        const std::vector<std::string_view> values{argv + 1, argv + argc};
        std::span<const std::string_view> arguments{values};
        if (!arguments.empty() && arguments.front() == "--headless") {
#if defined(CHESS_HAS_EDITOR)
            editor = false;
#endif
            arguments = arguments.subspan(1);
        }
#if defined(CHESS_HAS_EDITOR)
        if (editor) return chess::editor::run();
        attach_console();
#endif
        return chess::headless::run(arguments);
    } catch (const std::exception& error) {
#if defined(CHESS_HAS_EDITOR)
        if (editor) {
            std::wstring message(MultiByteToWideChar(CP_UTF8, 0, error.what(), -1, nullptr, 0), L'\0');
            MultiByteToWideChar(CP_UTF8, 0, error.what(), -1, message.data(), int(message.size()));
            MessageBoxW(nullptr, message.c_str(), L"中国象棋", MB_OK | MB_ICONERROR);
            return 1;
        }
#endif
        std::println(std::cerr, "Chess: {}", error.what());
        return 1;
    }
}
