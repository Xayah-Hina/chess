module;
#include <Windows.h>
#include <GLFW/glfw3.h>
export module tools.editor.platform.window;
import std;

export namespace tools::editor {
    struct WindowPlatform final {
        explicit WindowPlatform(const char* title, std::array<int, 2> extent, std::array<int, 2> minimum);
        ~WindowPlatform();
        WindowPlatform(const WindowPlatform&)            = delete;
        WindowPlatform& operator=(const WindowPlatform&) = delete;

        GLFWwindow* window{};
        HWND native_window{};
        bool close_requested{}, redraw{true};
        std::array<int, 2> minimum;
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
} // namespace tools::editor
