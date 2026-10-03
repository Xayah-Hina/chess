module chess.benchmark.opponents;
import std;
namespace chess::benchmark {
    namespace {
        int minimax(Game& game, const int depth, int alpha, const int beta) {
            if (game.decision.outcome != Outcome::ongoing) {
                if (game.decision.outcome == Outcome::draw) return 0;
                return (game.decision.outcome == Outcome::red_win) == (game.position.turn == Color::red) ? 100000 + depth : -100000 - depth;
            }
            if (!depth) {
                constexpr std::array values{0, 0, 2, 2, 4, 9, 4, 1};
                int result{};
                for (const auto piece : game.position.board) result += values[int(piece.kind)] * (piece.color == game.position.turn ? 1 : -1);
                return result;
            }
            const auto moves = game.moves;
            int best         = -1000000;
            for (const auto move : moves) {
                game.play(move);
                const int value = -minimax(game, depth - 1, -beta, -alpha);
                game.unplay();
                best  = std::max(best, value);
                alpha = std::max(alpha, value);
                if (alpha >= beta) break;
            }
            return best;
        }
        bool forced(Game& game, const Color winner, const int remaining) {
            if (game.decision.outcome != Outcome::ongoing) return game.decision.outcome != Outcome::draw && (game.decision.outcome == Outcome::red_win) == (winner == Color::red);
            if (!remaining) return false;
            const bool attacking = game.position.turn == winner;
            const auto moves     = game.moves;
            for (const auto move : moves) {
                game.play(move);
                const bool result = forced(game, winner, remaining - 1);
                game.unplay();
                if (result == attacking) return attacking;
            }
            return !attacking;
        }
    } // namespace
    Move baseline(Game game, const int depth, std::mt19937_64& random) {
        if (!depth) return game.moves[std::uniform_int_distribution<std::size_t>{0, game.moves.size() - 1}(random)];
        int best = -1000000;
        std::vector<Move> choices;
        const auto moves = game.moves;
        for (const auto move : moves) {
            game.play(move);
            const int value = -minimax(game, depth - 1, -1000000, 1000000);
            game.unplay();
            if (value > best) {
                best = value;
                choices.clear();
            }
            if (value == best) choices.push_back(move);
        }
        return choices[std::uniform_int_distribution<std::size_t>{0, choices.size() - 1}(random)];
    }
    std::vector<Move> winning_moves(Game game, const int plies) {
        const auto moves  = game.moves;
        const auto winner = game.position.turn;
        std::vector<Move> result;
        for (const auto move : moves) {
            game.play(move);
            if (forced(game, winner, plies - 1)) result.push_back(move);
            game.unplay();
        }
        return result;
    }
    std::vector<Move> safe_moves(Game game) {
        const auto moves = game.moves;
        std::vector<Move> result;
        for (const auto move : moves) {
            game.play(move);
            const bool lost = forced(game, game.position.turn, 1);
            game.unplay();
            if (!lost) result.push_back(move);
        }
        return result;
    }
} // namespace chess::benchmark
