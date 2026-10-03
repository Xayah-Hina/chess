module;
#include <Windows.h>
#include <imgui.h>
module chess.editor.workspace;
import chess.game;
import chess.editor.platform.window;
import std;

namespace chess::editor {
    Workspace::Workspace() {
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

    void Workspace::draw(WindowPlatform& window) {
        const auto& viewport = *ImGui::GetMainViewport();
        const float scale    = ImGui::GetStyle().FontScaleDpi;
        ImGui::SetNextWindowPos(viewport.Pos);
        ImGui::SetNextWindowSize(viewport.Size);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{0, 0});
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0);
        ImGui::Begin("##Chess", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollWithMouse);
        draw_window_controls(window, scale);

        const float cell = std::min((viewport.Size.x - 48 * scale) / 10, (viewport.Size.y - 224 * scale) / 11);
        const float top  = 84 * scale + (viewport.Size.y - 224 * scale - 11 * cell) / 2;
        const ImVec2 origin{viewport.Pos.x + (viewport.Size.x - 8 * cell) / 2, viewport.Pos.y + top + cell};
        const ImVec2 minimum{origin.x - 0.72F * cell, origin.y - 0.72F * cell};
        const ImVec2 maximum{origin.x + 8.72F * cell, origin.y + 9.72F * cell};
        draw_board(origin, cell, scale);

        auto* draw         = ImGui::GetWindowDrawList();
        const bool ongoing = game.decision.outcome == Outcome::ongoing;
        const bool checked = ongoing && in_check(game.position, game.position.turn);
        for (const Color side : {Color::black, Color::red}) {
            const bool active = ongoing && game.position.turn == side;
            const bool winner = game.decision.outcome == (side == Color::red ? Outcome::red_win : Outcome::black_win);
            const float y     = side == Color::black ? minimum.y - 24 * scale : maximum.y + 24 * scale;
            const ImU32 ink   = side == Color::red ? IM_COL32(233, 161, 153, 255) : IM_COL32(208, 210, 227, 255);
            draw->AddCircleFilled({minimum.x + 12 * scale, y}, 12 * scale, side == Color::red ? IM_COL32(233, 161, 153, 24) : IM_COL32(208, 210, 227, 24));
            const char* avatar = side == Color::red ? "帅" : "将";
            const auto glyph   = ImGui::CalcTextSize(avatar);
            draw->AddText({minimum.x + 12 * scale - glyph.x / 2, y - glyph.y / 2}, ink, avatar);
            draw->AddText({minimum.x + 34 * scale, y - ImGui::GetFontSize() / 2}, active || winner ? ImGui::GetColorU32(ImGuiCol_Text) : ImGui::GetColorU32(ImGuiCol_TextDisabled), side == Color::red ? "红方" : "黑方");
            const char* state = ongoing ? active ? "行棋" : "等待" : game.decision.outcome == Outcome::draw ? "和棋" : winner ? "胜方" : "负方";
            const auto text   = ImGui::CalcTextSize(state);
            const float left  = maximum.x - text.x - 18 * scale;
            if (active || winner) draw->AddRectFilled({left, y - 13 * scale}, {maximum.x, y + 13 * scale}, IM_COL32(161, 158, 255, 24), 13 * scale);
            draw->AddText({left + 9 * scale, y - text.y / 2}, active || winner ? ImGui::GetColorU32(ImGuiCol_CheckMark) : ImGui::GetColorU32(ImGuiCol_TextDisabled), state);
        }

        const float footer = maximum.y + 64 * scale;
        ImGui::SetCursorScreenPos({minimum.x, footer});
        if (ongoing) {
            ImGui::TextUnformatted(game.position.turn == Color::red ? "红方行棋" : "黑方行棋");
            if (checked) {
                ImGui::SameLine(0, 12 * scale);
                ImGui::TextColored({0.93F, 0.57F, 0.56F, 1}, "将军");
            }
        } else {
            constexpr std::array results{"", "红方胜", "黑方胜", "和棋"};
            ImGui::TextColored({0.73F, 0.70F, 1, 1}, "%s", results[int(game.decision.outcome)]);
        }
        ImGui::SetCursorScreenPos({minimum.x, footer + 27 * scale});
        ImGui::PushFont(nullptr, 13.5F);
        ImGui::PushTextWrapPos(maximum.x - viewport.Pos.x - 136 * scale);
        if (!ongoing) {
            const auto reason = describe(game.decision.reason);
            ImGui::TextColored(ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled), "%.*s", int(reason.size()), reason.data());
        } else if (game.decision.change) ImGui::TextColored({0.84F, 0.72F, 0.49F, 1}, "%s", game.decision.change == 1 ? "红方须变着" : game.decision.change == 2 ? "黑方须变着" : "双方须变着");
        else ImGui::TextDisabled("%s", selected ? "请选择落点" : "选择棋子，点击落点");
        ImGui::PopTextWrapPos();
        ImGui::PopFont();
        ImGui::SetCursorScreenPos({maximum.x - 116 * scale, footer + 2 * scale});
        if (ImGui::Button("开始新局", {116 * scale, 42 * scale})) {
            game = Game{};
            selected.reset();
        }
        ImGui::End();
        ImGui::PopStyleVar(2);
    }

    void Workspace::draw_window_controls(WindowPlatform& window, const float scale) {
        const auto& viewport = *ImGui::GetMainViewport();
        auto* draw           = ImGui::GetWindowDrawList();
        draw->AddText({viewport.Pos.x + 24 * scale, viewport.Pos.y + 18 * scale}, ImGui::GetColorU32(ImGuiCol_TextDisabled), "象棋");
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

    void Workspace::draw_board(const ImVec2 origin, const float cell, const float scale) {
        ImGui::SetCursorScreenPos({origin.x - cell, origin.y - cell});
        ImGui::InvisibleButton("##Board", {cell * 10, cell * 11});
        int hovered = -1;
        bool interactive{};
        if (game.decision.outcome == Outcome::ongoing && ImGui::IsItemHovered()) {
            const auto mouse = ImGui::GetMousePos();
            const int file   = int(std::round((mouse.x - origin.x) / cell));
            const int rank   = 9 - int(std::round((mouse.y - origin.y) / cell));
            if (file >= 0 && file < 9 && rank >= 0 && rank < 10) {
                hovered               = rank * 9 + file;
                const auto piece      = game.position.board[hovered];
                const bool playable   = selected && std::ranges::find(game.moves, Move{*selected, hovered}) != game.moves.end();
                const bool selectable = piece.kind != Kind::none && piece.color == game.position.turn;
                interactive           = playable || selectable;
                if (interactive) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
                if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                    if (playable) {
                        game.play({*selected, hovered});
                        selected.reset();
                    } else if (selectable) selected = hovered;
                    else selected.reset();
                }
            }
        }

        auto* draw = ImGui::GetWindowDrawList();
        const ImVec2 minimum{origin.x - 0.72F * cell, origin.y - 0.72F * cell};
        const ImVec2 maximum{origin.x + 8.72F * cell, origin.y + 9.72F * cell};
        draw->AddRectFilled({minimum.x - 3 * scale, minimum.y + 5 * scale}, {maximum.x + 3 * scale, maximum.y + 8 * scale}, IM_COL32(0, 0, 0, 32), 20 * scale);
        draw->AddRectFilled(minimum, maximum, IM_COL32(32, 34, 42, 255), 18 * scale);
        draw->AddRect(minimum, maximum, IM_COL32(184, 184, 213, 22), 18 * scale, 0, scale);
        constexpr ImU32 grid = IM_COL32(87, 89, 106, 255);
        for (int rank = 0; rank < 10; ++rank) draw->AddLine({origin.x, origin.y + rank * cell}, {origin.x + 8 * cell, origin.y + rank * cell}, grid, (rank == 0 || rank == 9 ? 1.5F : 1.0F) * scale);
        for (int file = 0; file < 9; ++file) {
            const float x = origin.x + file * cell;
            if (file == 0 || file == 8) draw->AddLine({x, origin.y}, {x, origin.y + 9 * cell}, grid, 1.5F * scale);
            else {
                draw->AddLine({x, origin.y}, {x, origin.y + 4 * cell}, grid, scale);
                draw->AddLine({x, origin.y + 5 * cell}, {x, origin.y + 9 * cell}, grid, scale);
            }
        }
        for (const int rank : {0, 7}) {
            draw->AddLine({origin.x + 3 * cell, origin.y + rank * cell}, {origin.x + 5 * cell, origin.y + (rank + 2) * cell}, grid, scale);
            draw->AddLine({origin.x + 5 * cell, origin.y + rank * cell}, {origin.x + 3 * cell, origin.y + (rank + 2) * cell}, grid, scale);
        }
        for (const int square : {19, 25, 27, 29, 31, 33, 35, 54, 56, 58, 60, 62, 64, 70}) {
            const ImVec2 center{origin.x + square % 9 * cell, origin.y + (9 - square / 9) * cell};
            for (const int horizontal : {-1, 1}) {
                if ((square % 9 == 0 && horizontal == -1) || (square % 9 == 8 && horizontal == 1)) continue;
                for (const int vertical : {-1, 1}) {
                    const ImVec2 corner{center.x + horizontal * cell * 0.09F, center.y + vertical * cell * 0.09F};
                    draw->AddLine(corner, {corner.x + horizontal * cell * 0.10F, corner.y}, grid, scale);
                    draw->AddLine(corner, {corner.x, corner.y + vertical * cell * 0.10F}, grid, scale);
                }
            }
        }
        ImGui::PushFont(nullptr, cell * 0.27F / scale);
        for (const int side : {0, 1}) {
            const char* text = side == 0 ? "楚  河" : "汉  界";
            const auto size  = ImGui::CalcTextSize(text);
            draw->AddText({origin.x + (2 + 4 * side) * cell - size.x / 2, origin.y + 4.5F * cell - size.y / 2}, IM_COL32(135, 136, 155, 255), text);
        }
        ImGui::PopFont();
        if (!game.history.empty()) {
            const auto move = game.history.back().undo.move;
            for (const int square : {move.from, move.to}) {
                const ImVec2 center{origin.x + square % 9 * cell, origin.y + (9 - square / 9) * cell};
                draw->AddCircleFilled(center, cell * 0.43F, IM_COL32(161, 158, 255, 24));
                draw->AddCircle(center, cell * 0.43F, IM_COL32(161, 158, 255, 115), 0, 1.5F * scale);
                if (square == move.from) draw->AddCircleFilled(center, cell * 0.065F, IM_COL32(161, 158, 255, 150));
            }
        }
        if (interactive) draw->AddCircle({origin.x + hovered % 9 * cell, origin.y + (9 - hovered / 9) * cell}, cell * 0.44F, IM_COL32(207, 204, 255, 105), 0, scale);

        constexpr std::array red{"", "帅", "仕", "相", "马", "车", "炮", "兵"};
        constexpr std::array black{"", "将", "士", "象", "马", "车", "炮", "卒"};
        const bool checked = game.decision.outcome == Outcome::ongoing && in_check(game.position, game.position.turn);
        ImGui::PushFont(nullptr, cell * 0.47F / scale);
        for (int square = 0; square < 90; ++square) {
            const auto piece = game.position.board[square];
            if (piece.kind == Kind::none) continue;
            const ImVec2 center{origin.x + square % 9 * cell, origin.y + (9 - square / 9) * cell};
            const ImU32 ink  = piece.color == Color::red ? IM_COL32(167, 64, 65, 255) : IM_COL32(53, 57, 75, 255);
            const ImU32 face = piece.color == Color::red ? IM_COL32(242, 215, 209, 255) : IM_COL32(223, 225, 237, 255);
            draw->AddCircleFilled({center.x, center.y + 3 * scale}, cell * 0.375F, IM_COL32(0, 0, 0, 85));
            draw->AddCircleFilled(center, cell * 0.37F, face);
            draw->AddCircle(center, cell * 0.315F, piece.color == Color::red ? IM_COL32(167, 64, 65, 115) : IM_COL32(53, 57, 75, 100), 0, scale);
            const char* text = piece.color == Color::red ? red[int(piece.kind)] : black[int(piece.kind)];
            const auto size  = ImGui::CalcTextSize(text);
            draw->AddText({center.x - size.x / 2, center.y - size.y / 2}, ink, text);
            if (selected == square) {
                draw->AddCircleFilled(center, cell * 0.40F, IM_COL32(161, 158, 255, 18));
                draw->AddCircle(center, cell * 0.43F, ImGui::GetColorU32(ImGuiCol_CheckMark), 0, 2.5F * scale);
            }
            if (checked && piece.kind == Kind::general && piece.color == game.position.turn) draw->AddCircle(center, cell * 0.455F, IM_COL32(237, 145, 143, 255), 0, 2 * scale);
        }
        ImGui::PopFont();
        if (selected) {
            for (const auto move : game.moves) {
                if (move.from != *selected) continue;
                const ImVec2 destination{origin.x + move.to % 9 * cell, origin.y + (9 - move.to / 9) * cell};
                if (game.position.board[move.to].kind == Kind::none) {
                    draw->AddCircleFilled(destination, cell * 0.13F, IM_COL32(161, 158, 255, 36));
                    draw->AddCircleFilled(destination, cell * 0.065F, ImGui::GetColorU32(ImGuiCol_CheckMark));
                } else draw->AddCircle(destination, cell * 0.43F, ImGui::GetColorU32(ImGuiCol_CheckMark), 0, 2 * scale);
            }
        }
    }
} // namespace chess::editor
