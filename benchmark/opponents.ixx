export module chess.benchmark.opponents;
export import chess.game;
import std;
export namespace chess::benchmark {
    enum class Opponent : std::uint8_t { random, material, checkpoint, pikafish };
    Move baseline(Game game, int depth, std::mt19937_64& random);
    std::vector<Move> winning_moves(Game game, int plies);
    std::vector<Move> safe_moves(Game game);
} // namespace chess::benchmark
