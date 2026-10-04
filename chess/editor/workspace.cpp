module;
#include <Windows.h>
#include <imgui.h>
module chess.editor.workspace;
import chess.game;
import tools.editor.style;
import tools.editor.platform.window;
import std;

namespace chess::editor {
    Workspace::Workspace() {
        tools::editor::apply_style();
    }

    void Workspace::draw(tools::editor::WindowPlatform& window) {
        const auto& viewport = *ImGui::GetMainViewport();
        const float scale    = ImGui::GetStyle().FontScaleDpi;
        ImGui::SetNextWindowPos(viewport.Pos);
        ImGui::SetNextWindowSize(viewport.Size);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{0, 0});
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0);
        ImGui::Begin("##Chess", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollWithMouse);
        tools::editor::draw_window_controls(window, "中国象棋", scale);

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
            const char* state = ongoing ? active ? "行棋中" : "等待" : game.decision.outcome == Outcome::draw ? "和棋" : winner ? "获胜" : "落败";
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
            constexpr std::array results{"", "红方获胜", "黑方获胜", "和棋"};
            ImGui::TextColored({0.73F, 0.70F, 1, 1}, "%s", results[int(game.decision.outcome)]);
        }
        ImGui::SetCursorScreenPos({minimum.x, footer + 27 * scale});
        ImGui::PushFont(nullptr, 13.5F);
        ImGui::PushTextWrapPos(maximum.x - viewport.Pos.x - 136 * scale);
        if (!ongoing) {
            constexpr std::array reasons{"", "将死", "困毙", "长将", "长捉", "重复局面", "自然限着", "双方均无法获胜"};
            ImGui::TextColored(ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled), "%s", reasons[int(game.decision.reason)]);
        }
        else ImGui::TextDisabled("%s", selected ? "请选择落子位置" : "选择棋子，再选择落子位置");
        ImGui::PopTextWrapPos();
        ImGui::PopFont();
        ImGui::SetCursorScreenPos({maximum.x - 116 * scale, footer + 2 * scale});
        if (ImGui::Button("重新开局", {116 * scale, 42 * scale})) {
            game = Game{};
            selected.reset();
        }
        ImGui::End();
        ImGui::PopStyleVar(2);
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
