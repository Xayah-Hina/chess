module;
#include <GLFW/glfw3.h>
module chess.editor;
import chess.editor.platform.window;
import chess.editor.graphics.renderer;
import chess.editor.workspace;

namespace chess::editor {
    int run() {
        WindowPlatform window;
        Renderer renderer{window};
        Workspace workspace;
        while (!glfwWindowShouldClose(window.window)) {
            glfwPollEvents();
            if (!renderer.begin()) continue;
            workspace.draw(window);
            renderer.present();
        }
        return 0;
    }
} // namespace chess::editor
