export module chess.game;
export import chess.adjudication;
import std;

export namespace chess {
    struct Game final {
        Position position;
        std::vector<Step> history;
        std::vector<Move> moves;
        Decision decision;

        explicit Game(Position initial = initial_position());
        Task<bool> play_async(Move move);
        void play(Move move);
        // Search branches use this; there is no player-facing undo command.
        void unplay();
    };
} // namespace chess
