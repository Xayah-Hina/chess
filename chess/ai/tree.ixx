export module chess.ai.tree;
export import chess.ai.model;
import chess.ai.config;
import std;

export namespace chess::ai {
    struct SearchConfig final {
        int simulations{Config{}.simulations}, candidates{Config{}.candidates}, threads{Config{}.threads};
        bool exploration{true};
        std::chrono::steady_clock::time_point deadline{std::chrono::steady_clock::time_point::max()};
        std::stop_token cancellation{Computation::cancellation};
    };
    struct SearchResult final {
        Move move;
        std::vector<PolicyEntry> policy;
        float value{};
        std::uint64_t leaves{};
    };
    struct Tree final {
        struct Edge final {
            Move move;
            float logit{}, prior{}, gumbel{}, sum{};
            int visits{}, child{-1};
        };
        struct Node final {
            std::vector<Edge> edges;
            float raw{};
            bool expanded{};
            Decision decision;
        };

        Game game;
        SearchConfig config;
        std::mt19937_64 random;
        std::vector<Node> nodes{1};
        std::vector<std::pair<int, int>> path;
        std::vector<int> candidates;
        std::optional<Observation> request;
        int pending{}, simulation{}, sample_size{}, budget{};
        std::uint64_t leaves{};

        Tree(const Game& initial, SearchConfig settings, std::uint64_t seed);
        std::vector<float> completed(const Node& node) const;
        void play_cached(Move move, const Decision& decision);
        void prepare();
        void accept(const Prediction& prediction);
        void backup(float value);
        SearchResult finish() const;
    };
}
