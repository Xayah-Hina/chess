#include <Windows.h>
#include <cstdlib>
import tools.trainer.editor;
import std;
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
    try {
        const std::vector<std::string_view> arguments{__argv + 1, __argv + __argc};
        return tools::trainer::editor::run(arguments);
    } catch (const std::exception& error) {
        std::wstring message(MultiByteToWideChar(CP_UTF8, 0, error.what(), -1, nullptr, 0), L'\0');
        MultiByteToWideChar(CP_UTF8, 0, error.what(), -1, message.data(), int(message.size()));
        MessageBoxW(nullptr, message.c_str(), L"象棋训练面板", MB_OK | MB_ICONERROR);
        return 1;
    }
}
