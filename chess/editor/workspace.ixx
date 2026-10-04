module;
#include <imgui.h>
export module chess.editor.workspace;
import chess.game;
import chess.editor.opponent;
import tools.editor.platform.window;
import std;

export namespace chess::editor {
    struct Workspace final {
        explicit Workspace();
        Game game;
        std::optional<int> selected;
        void draw(tools::editor::WindowPlatform& window);

    private:
        std::filesystem::path model;
        Opponent opponent;
        void draw_board(ImVec2 origin, float cell, float scale);
    };
} // namespace chess::editor
