module;
#include <Windows.h>
#include <imgui.h>
module tools.editor.style;
import tools.editor.platform.window;
import std;
namespace tools::editor {
    void apply_style() {
        ImGui::StyleColorsDark();
        auto& style            = ImGui::GetStyle();
        style.WindowPadding    = {20, 16};
        style.FramePadding     = {12, 9};
        style.ItemSpacing      = {8, 10};
        style.WindowBorderSize = 0;
        style.FrameBorderSize  = 0;
        style.PopupBorderSize  = 0;
        style.WindowRounding   = 16;
        style.ChildRounding = style.FrameRounding = style.GrabRounding = 8;
        style.PopupRounding                                            = 12;
        style.ScrollbarSize                                            = 8;
        style.ScrollbarRounding                                        = 8;
        style.Colors[ImGuiCol_Text]                                    = {0.93F, 0.93F, 0.96F, 1};
        style.Colors[ImGuiCol_TextDisabled]                            = {0.57F, 0.58F, 0.64F, 1};
        style.Colors[ImGuiCol_WindowBg]                                = {0.095F, 0.10F, 0.125F, 1};
        style.Colors[ImGuiCol_PopupBg]                                 = {0.12F, 0.125F, 0.15F, 1};
        style.Colors[ImGuiCol_Border]                                  = {0.70F, 0.72F, 0.85F, 0.10F};
        style.Colors[ImGuiCol_FrameBg]                                 = {0.07F, 0.075F, 0.095F, 1};
        style.Colors[ImGuiCol_FrameBgHovered]                          = {0.14F, 0.145F, 0.18F, 1};
        style.Colors[ImGuiCol_FrameBgActive]                           = {0.16F, 0.16F, 0.21F, 1};
        style.Colors[ImGuiCol_Button]                                  = {0.17F, 0.175F, 0.215F, 1};
        style.Colors[ImGuiCol_ButtonHovered]                           = {0.23F, 0.23F, 0.29F, 1};
        style.Colors[ImGuiCol_ButtonActive]                            = {0.30F, 0.29F, 0.38F, 1};
        style.Colors[ImGuiCol_CheckMark] = style.Colors[ImGuiCol_NavCursor] = {0.63F, 0.62F, 1, 1};
    }

    void draw_window_controls(WindowPlatform& window, const char* title, const float scale) {
        const auto& viewport = *ImGui::GetMainViewport();
        auto* draw           = ImGui::GetWindowDrawList();
        draw->AddText({viewport.Pos.x + 24 * scale, viewport.Pos.y + 18 * scale}, ImGui::GetColorU32(ImGuiCol_TextDisabled), title);
        const float left   = viewport.Pos.x + viewport.Size.x - 134 * scale;
        window.drag_region = {0, 0, left - viewport.Pos.x - 8 * scale, 48 * scale};
        constexpr std::array ids{"##Minimize", "##Maximize", "##Close"};
        constexpr std::array labels{"最小化", "最大化", "关闭"};
        const bool maximized = IsZoomed(window.native_window);
        for (int index = 0; index < 3; ++index) {
            const ImVec2 origin{left + index * 42 * scale, viewport.Pos.y + 8 * scale};
            ImGui::SetCursorScreenPos(origin);
            const bool clicked = ImGui::InvisibleButton(ids[index], {38 * scale, 32 * scale}, ImGuiButtonFlags_EnableNav);
            const bool hovered = ImGui::IsItemHovered() || ImGui::IsItemFocused();
            auto& storage      = *ImGui::GetStateStorage();
            const auto key     = ImGui::GetItemID();
            const float alpha  = std::lerp(storage.GetFloat(key), hovered ? 1.0F : 0.0F, std::min(1.0F, ImGui::GetIO().DeltaTime / 0.12F));
            if (std::abs(alpha - (hovered ? 1.0F : 0.0F)) > 0.01F) window.redraw = true;
            storage.SetFloat(key, alpha);
            draw->AddRectFilled(origin, {origin.x + 38 * scale, origin.y + 32 * scale}, ImGui::GetColorU32(index == 2 ? ImVec4{0.84F, 0.30F, 0.34F, alpha * 0.70F} : ImVec4{0.30F, 0.29F, 0.38F, alpha * 0.55F}), 8 * scale);
            const ImU32 ink = ImGui::GetColorU32(ImVec4{0.57F + 0.36F * alpha, 0.58F + 0.35F * alpha, 0.64F + 0.32F * alpha, 1});
            const ImVec2 center{origin.x + 19 * scale, origin.y + 16 * scale};
            const float radius = 4.5F * scale;
            if (index == 0) draw->AddLine({center.x - radius, center.y + 2 * scale}, {center.x + radius, center.y + 2 * scale}, ink, 1.5F * scale);
            else if (index == 1) {
                if (maximized) {
                    draw->AddLine({center.x - radius + 2 * scale, center.y - radius}, {center.x + radius, center.y - radius}, ink, 1.3F * scale);
                    draw->AddLine({center.x + radius, center.y - radius}, {center.x + radius, center.y + radius - 2 * scale}, ink, 1.3F * scale);
                }
                draw->AddRect({center.x - radius, center.y - radius + (maximized ? 2 * scale : 0)}, {center.x + radius - (maximized ? 2 * scale : 0), center.y + radius}, ink, 0, 0, 1.3F * scale);
            } else {
                draw->AddLine({center.x - radius, center.y - radius}, {center.x + radius, center.y + radius}, ink, 1.5F * scale);
                draw->AddLine({center.x + radius, center.y - radius}, {center.x - radius, center.y + radius}, ink, 1.5F * scale);
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", index == 1 && maximized ? "还原" : labels[index]);
            if (clicked) SendMessageW(window.native_window, WM_SYSCOMMAND, index == 0 ? SC_MINIMIZE : index == 1 ? maximized ? SC_RESTORE : SC_MAXIMIZE : SC_CLOSE, 0);
        }
    }

} // namespace tools::editor
