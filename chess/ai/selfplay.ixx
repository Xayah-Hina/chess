export module chess.ai.selfplay;
export import chess.ai.search;
export import chess.ai.replay;
import std;

export namespace chess::ai {
    struct Actor final {
        Game game{};
        std::vector<Sample> pending;
        std::uint64_t seed{};
        std::shared_ptr<const DeviceWeights> model;
    };
    struct SelfPlay final {
        std::vector<Actor> actors;
        std::mt19937_64 random;
        std::uint64_t games{}, plies{}, leaves{};
        std::array<std::uint64_t, 3> outcomes{};
        SearchActivity activity;
        SelfPlay(int count, std::uint64_t seed);
        std::size_t advance(Network& network, Replay& replay, SearchConfig config);
        void stop();

    private:
        std::optional<Search> searches;
        std::optional<Network> evaluator;
        std::shared_ptr<const DeviceWeights> snapshot;
    };
} // namespace chess::ai
