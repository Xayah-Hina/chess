module;
#include <imgui.h>
export module chess.editor.workspace;
import chess.game;
import chess.editor.platform.window;
import std;

export namespace chess::editor {
    struct Workspace final {
        explicit Workspace();
        Game game;
        std::optional<int> selected;
        void draw(WindowPlatform& window);

    private:
        void draw_window_controls(WindowPlatform& window, float scale);
        void draw_board(ImVec2 origin, float cell, float scale);
    };
} // namespace chess::editor
