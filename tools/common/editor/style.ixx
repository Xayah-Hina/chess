export module tools.editor.style;
import tools.editor.platform.window;
export namespace tools::editor {
    void apply_style();
    void draw_window_controls(WindowPlatform& window, const char* title, float scale);
} // namespace tools::editor
