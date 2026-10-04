module chess.game;
import std;

namespace chess {
    Game::Game(Position initial) : position{std::move(initial)}, moves{legal_moves(position)}, decision{adjudicate(position, {}, moves)} {
        if (decision.outcome != Outcome::ongoing) moves.clear();
    }

    void Game::play(const Move move) {
        const auto before = position;
        const auto undo   = make_move(position, move);
        history.push_back({before, undo, decision});
        try {
            auto next_moves = legal_moves(position);
            decision        = adjudicate(position, history, next_moves);
            moves           = std::move(next_moves);
        } catch (...) {
            unmake_move(position, undo);
            history.pop_back();
            throw;
        }
        if (decision.outcome != Outcome::ongoing) moves.clear();
    }
    void Game::unplay() {
        const auto step = history.back();
        unmake_move(position, step.undo);
        decision = step.decision;
        history.pop_back();
        moves = decision.outcome == Outcome::ongoing ? legal_moves(position) : std::vector<Move>{};
    }
} // namespace chess
