module;
#include <Windows.h>
#include <GLFW/glfw3.h>
export module chess.editor.platform.window;
import std;

export namespace chess::editor {
    struct WindowPlatform final {
        explicit WindowPlatform();
        ~WindowPlatform();
        WindowPlatform(const WindowPlatform&)            = delete;
        WindowPlatform& operator=(const WindowPlatform&) = delete;

        GLFWwindow* window{};
        HWND native_window{};
        std::array<float, 4> drag_region{};

    private:
        struct GlfwLifetime final {
            GlfwLifetime();
            ~GlfwLifetime();
        };
        static LRESULT CALLBACK window_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam);
        GlfwLifetime glfw;
        std::unique_ptr<GLFWwindow, decltype(&glfwDestroyWindow)> owned{nullptr, glfwDestroyWindow};
        WNDPROC original_window_proc{};
    };
} // namespace chess::editor
