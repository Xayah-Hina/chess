module;
#include <GLFW/glfw3.h>
#include <imgui.h>
module tools.trainer.editor;
import tools.editor.platform.window;
import tools.editor.graphics.renderer;
import tools.trainer.editor.workspace;
import std;
namespace tools::trainer::editor {
    int run(const std::span<const std::string_view> arguments) {
        if (!arguments.empty()) throw std::runtime_error{"Trainer opens as a panel; use trainer-headless for commands"};
        tools::editor::WindowPlatform window{"象棋训练面板", {1120, 820}, {840, 640}};
        tools::editor::Renderer renderer{window};
        Workspace workspace{window};
        bool closing{};
        std::uint64_t revision{};
        int redraws{2};
        for (;;) {
            glfwPollEvents();
            workspace.receive();
            if (window.close_requested && !closing) {
                closing = true;
                workspace.session.submit({Action::close});
            }
            if (closing && workspace.state.closed) break;
            if (revision != workspace.state.revision || std::exchange(window.redraw, false)) redraws = 2;
            revision = workspace.state.revision;
            if (!redraws && !ImGui::IsAnyItemActive()) {
                glfwWaitEventsTimeout(1);
                continue;
            }
            if (redraws) --redraws;
            if (!renderer.begin()) continue;
            workspace.draw(closing);
            renderer.present();
            glfwWaitEventsTimeout(1.0 / 30);
        }
        return 0;
    }
} // namespace tools::trainer::editor
