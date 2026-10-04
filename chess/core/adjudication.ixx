export module chess.adjudication;
export import chess.rules;
import std;

export namespace chess {
    struct Interrupted final {};
    struct Computation final {
        static thread_local std::chrono::steady_clock::time_point deadline;
        static thread_local std::stop_token cancellation;
        std::chrono::steady_clock::time_point previous;
        std::stop_token previous_cancellation;
        explicit Computation(std::chrono::steady_clock::time_point limit, std::stop_token token = cancellation);
        ~Computation();
    };
    enum class Outcome : std::uint8_t { ongoing, red_win, black_win, draw };
    enum class Reason : std::uint8_t { none, checkmate, stalemate, perpetual_check, perpetual_chase, repetition, move_limit, dead_position };

    struct Decision final {
        Outcome outcome{};
        Reason reason{};
        bool operator==(const Decision&) const = default;
    };

    struct Step final {
        Position before;
        Undo undo;
        Decision decision;
    };

    Decision adjudicate(const Position& position, std::span<const Step> history, std::span<const Move> moves);
    std::string_view describe(Reason reason);
} // namespace chess
