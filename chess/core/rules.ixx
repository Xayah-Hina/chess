export module chess.rules;
export import chess.position;
import std;

export namespace chess {
    bool attacks(const Position& position, Move move);
    bool in_check(const Position& position, Color color);
    std::vector<Move> legal_moves(const Position& position);
} // namespace chess
