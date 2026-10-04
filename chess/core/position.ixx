export module chess.position;
import std;

export namespace chess {
    enum class Color : std::uint8_t { red, black };
    enum class Kind : std::uint8_t { none, general, advisor, elephant, horse, rook, cannon, soldier };

    struct Piece final {
        Kind kind{};
        Color color{};
        std::uint8_t id{};
        auto operator<=>(const Piece&) const = default;
    };

    struct Move final {
        int from{}, to{};
        bool operator==(const Move&) const = default;
    };

    struct Position final {
        // File a is on Red's left; rank 0 is Red's home rank.
        std::array<Piece, 90> board{};
        Color turn{};
        auto operator<=>(const Position&) const = default;
    };

    struct Undo final {
        Move move;
        Piece captured;
        Color turn;
    };

    Color opposite(Color color);
    Position initial_position();
    Undo make_move(Position& position, Move move);
    void unmake_move(Position& position, Undo undo);
    bool same_position(const Position& first, const Position& second);
    std::string coordinate(Move move);
    Move coordinate(std::string_view text);
} // namespace chess
