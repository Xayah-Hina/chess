module chess.ai.encoding;
import std;

namespace chess::ai {
    namespace {
        constexpr std::array<std::array<int, 2>, 50> displacements = [] {
            std::array<std::array<int, 2>, 50> values{};
            int index{};
            for (int distance = 1; distance <= 8; ++distance) {
                values[index++] = {distance, 0};
                values[index++] = {-distance, 0};
            }
            for (int distance = 1; distance <= 9; ++distance) {
                values[index++] = {0, distance};
                values[index++] = {0, -distance};
            }
            for (const auto offset : std::array{std::array{1, 2}, std::array{2, 1}})
                for (const int x : {-1, 1})
                    for (const int y : {-1, 1}) values[index++] = {x * offset[0], y * offset[1]};
            for (const int distance : {2, 1})
                for (const int x : {-1, 1})
                    for (const int y : {-1, 1}) values[index++] = {x * distance, y * distance};
            return values;
        }();
    } // namespace
    Observation observe(const Game& game) {
        Observation observation;
        const auto turn = game.position.turn;
        for (std::size_t age = 0; age < 8 && age <= game.history.size(); ++age) {
            const auto& position = age ? game.history[game.history.size() - age].before : game.position;
            for (int square = 0; square < 90; ++square) {
                const auto piece = position.board[square];
                if (piece.kind == Kind::none) continue;
                const int view                      = turn == Color::red ? square : 89 - square;
                observation.boards[age * 90 + view] = std::uint8_t(int(piece.kind) + (piece.color == turn ? 0 : 7));
            }
        }
        std::size_t capture{};
        for (std::size_t i = game.history.size(); i > 0; --i)
            if (game.history[i - 1].undo.captured.kind != Kind::none) {
                capture = i;
                break;
            }
        std::array<int, 2> turns{}, counts{}, checks{};
        for (std::size_t i = capture; i < game.history.size(); ++i) {
            const auto& after = i + 1 == game.history.size() ? game.position : game.history[i + 1].before;
            const int side = int(game.history[i].before.turn);
            ++turns[side];
            if (!in_check(after, after.turn) || checks[side]++ < 10) ++counts[side];
        }
        int repetitions{};
        for (const auto& step : game.history) repetitions += same_position(step.before, game.position);
        const int us = int(turn), them = 1 - us;
        std::ranges::copy(std::array{turn == Color::red ? 1.0F : 0.0F, float(game.history.size()) / 256, float(counts[us]) / 50, float(counts[them]) / 50, float(turns[us]) / 50, float(turns[them]) / 50, float(checks[us]) / 10, float(checks[them]) / 10, float(repetitions) / 3, float(in_check(game.position, turn))}, observation.rules);
        return observation;
    }
    int action(Move move, const Color turn) {
        if (turn == Color::black) move = {89 - move.from, 89 - move.to};
        const std::array offset{move.to % 9 - move.from % 9, move.to / 9 - move.from / 9};
        return move.from * 50 + int(std::ranges::find(displacements, offset) - displacements.begin());
    }
    float terminal_value(const Game& game) {
        if (game.decision.outcome == Outcome::draw) return 0;
        const bool red = game.decision.outcome == Outcome::red_win;
        return red == (game.position.turn == Color::red) ? 1.0F : -1.0F;
    }
} // namespace chess::ai
