module;
#include <GLFW/glfw3.h>
module chess.editor;
import tools.editor.platform.window;
import tools.editor.graphics.renderer;
import chess.editor.workspace;

namespace chess::editor {
    int run() {
        tools::editor::WindowPlatform window{"中国象棋", {780, 900}, {520, 680}};
        tools::editor::Renderer renderer{window};
        Workspace workspace;
        while (!window.close_requested) {
            glfwPollEvents();
            if (!renderer.begin()) continue;
            workspace.draw(window);
            renderer.present();
        }
        return 0;
    }
} // namespace chess::editor
