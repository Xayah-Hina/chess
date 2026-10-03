export module chess.ai.training;
export import chess.ai.config;
export import chess.ai.checkpoint;
import std;
export namespace chess::ai {
    struct Progress final {
        double selfplay_seconds{}, training_seconds{};
        std::uint64_t presented{};
        int generation_round{};
        bool training_phase{};
        std::array<float, 4> metrics;
    };
    struct Training final {
        Config config;
        Network network;
        SelfPlay selfplay;
        Replay replay;
        Progress progress;
        std::mt19937_64 random;
        explicit Training(Config settings, int capacity = 1);
        void advance(std::chrono::steady_clock::time_point deadline);
        void persist(Archive& archive);
    };
} // namespace chess::ai
