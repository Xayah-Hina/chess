export module chess.adjudication;
export import chess.rules;
import std;

export namespace chess {
    enum class Outcome : std::uint8_t { ongoing, red_win, black_win, draw };
    enum class Reason : std::uint8_t { none, checkmate, stalemate, perpetual_check, perpetual_attack, repetition, move_limit, dead_position };

    struct Decision final {
        Outcome outcome{};
        Reason reason{};
        std::uint8_t change{};
        std::size_t started{}, period{};
        bool require_idle{}, pending_draw{};
        bool operator==(const Decision&) const = default;
    };

    struct Step final {
        Position before;
        Undo undo;
        Decision decision;
    };

    struct Chase final {
        std::uint8_t target{};
        Kind kind{};
        std::uint32_t attackers{};
        bool unprotected{};
    };

    struct Nature final {
        bool check{}, kill{};
        std::vector<Chase> chases;
    };

    Nature classify(const Position& before, Move move, std::uint32_t involved = 0);
    Decision adjudicate(const Position& position, std::span<const Step> history, Decision previous, std::span<const Move> moves);
    std::string_view describe(Reason reason);
} // namespace chess
