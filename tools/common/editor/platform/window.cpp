module;
#include <Windows.h>
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>
#include <dwmapi.h>
#include <windowsx.h>
module tools.editor.platform.window;
import std;

namespace tools::editor {
    WindowPlatform::WindowPlatform(const char* title, const std::array<int, 2> extent, const std::array<int, 2> size) : minimum{size} {
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        glfwWindowHint(GLFW_DECORATED, GLFW_FALSE);
        glfwWindowHint(GLFW_SCALE_TO_MONITOR, GLFW_TRUE);
        glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
        owned.reset(glfwCreateWindow(extent[0], extent[1], title, nullptr, nullptr));
        if (!owned) throw std::runtime_error{"Window creation failed"};
        window        = owned.get();
        native_window = glfwGetWin32Window(window);
        SetPropW(native_window, L"ToolsWindow", this);
        original_window_proc = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(native_window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&WindowPlatform::window_proc)));
        SetWindowLongPtrW(native_window, GWL_STYLE, WS_POPUP | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_SYSMENU);
        constexpr BOOL dark = TRUE;
        DwmSetWindowAttribute(native_window, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));
        constexpr DWM_WINDOW_CORNER_PREFERENCE corners = DWMWCP_ROUND;
        DwmSetWindowAttribute(native_window, DWMWA_WINDOW_CORNER_PREFERENCE, &corners, sizeof(corners));
        MONITORINFO monitor{sizeof(MONITORINFO)};
        GetMonitorInfoW(MonitorFromWindow(native_window, MONITOR_DEFAULTTONEAREST), &monitor);
        RECT bounds{};
        GetWindowRect(native_window, &bounds);
        const auto& area = monitor.rcWork;
        const int x      = area.left + ((area.right - area.left) - (bounds.right - bounds.left)) / 2;
        const int y      = area.top + ((area.bottom - area.top) - (bounds.bottom - bounds.top)) / 2;
        SetWindowPos(native_window, nullptr, x, y, 0, 0, SWP_FRAMECHANGED | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
        glfwShowWindow(window);
    }

    WindowPlatform::~WindowPlatform() {
        SetWindowLongPtrW(native_window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(original_window_proc));
        RemovePropW(native_window, L"ToolsWindow");
    }

    WindowPlatform::GlfwLifetime::GlfwLifetime() {
        if (glfwInit() != GLFW_TRUE) throw std::runtime_error{"GLFW initialization failed"};
    }

    WindowPlatform::GlfwLifetime::~GlfwLifetime() {
        glfwTerminate();
    }

    LRESULT CALLBACK WindowPlatform::window_proc(HWND window, const UINT message, const WPARAM wparam, const LPARAM lparam) {
        auto& platform  = *static_cast<WindowPlatform*>(GetPropW(window, L"ToolsWindow"));
        platform.redraw = true;
        switch (message) {
        case WM_KEYDOWN:
            if (wparam == 'W' && GetKeyState(VK_CONTROL) < 0 && GetKeyState(VK_SHIFT) >= 0 && GetKeyState(VK_MENU) >= 0 && GetKeyState(VK_LWIN) >= 0 && GetKeyState(VK_RWIN) >= 0) {
                if (!(lparam & (1LL << 30))) SendMessageW(window, WM_CLOSE, 0, 0);
                return 0;
            }
            break;
        case WM_CLOSE:
            platform.close_requested = true;
            glfwPostEmptyEvent();
            return 0;
        case WM_NCCALCSIZE:
            if (wparam != 0) return 0;
            break;
        case WM_NCHITTEST:
            {
                POINT point{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
                ScreenToClient(window, &point);
                RECT client{};
                GetClientRect(window, &client);
                if (!IsZoomed(window)) {
                    const auto dpi   = GetDpiForWindow(window);
                    const int border = GetSystemMetricsForDpi(SM_CXSIZEFRAME, dpi) + GetSystemMetricsForDpi(SM_CXPADDEDBORDER, dpi);
                    const bool left = point.x < border, right = point.x >= client.right - border;
                    const bool top = point.y < border, bottom = point.y >= client.bottom - border;
                    if (top && left) return HTTOPLEFT;
                    if (top && right) return HTTOPRIGHT;
                    if (bottom && left) return HTBOTTOMLEFT;
                    if (bottom && right) return HTBOTTOMRIGHT;
                    if (left) return HTLEFT;
                    if (right) return HTRIGHT;
                    if (top) return HTTOP;
                    if (bottom) return HTBOTTOM;
                }
                const auto& region = platform.drag_region;
                if (point.x >= region[0] && point.y >= region[1] && point.x < region[2] && point.y < region[3]) return HTCAPTION;
                return HTCLIENT;
            }
        case WM_GETMINMAXINFO:
            {
                MONITORINFO monitor{sizeof(MONITORINFO)};
                GetMonitorInfoW(MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST), &monitor);
                auto& minmax          = *reinterpret_cast<MINMAXINFO*>(lparam);
                const auto& area      = monitor.rcWork;
                minmax.ptMaxPosition  = {area.left - monitor.rcMonitor.left, area.top - monitor.rcMonitor.top};
                minmax.ptMaxSize      = {area.right - area.left, area.bottom - area.top};
                const auto dpi        = GetDpiForWindow(window);
                minmax.ptMinTrackSize = {MulDiv(platform.minimum[0], dpi, 96), MulDiv(platform.minimum[1], dpi, 96)};
                return 0;
            }
        }
        return CallWindowProcW(platform.original_window_proc, window, message, wparam, lparam);
    }
} // namespace tools::editor
