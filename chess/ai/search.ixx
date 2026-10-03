export module chess.ai.search;
export import chess.ai.network;
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
    struct SearchCompleted final {
        int actor{};
        std::uint64_t version{};
        SearchResult result;
        Game game;
    };
    struct SearchActivity final {
        std::size_t adjudicating{};
        double longest_seconds{};
        std::uint64_t rule_nodes{}, simulations{};
        std::size_t completed{}, total{};
    };
    struct Search final {
    private:
        struct Edge final {
            Move move;
            float logit{}, prior{}, gumbel{}, sum{};
            int visits{}, child{-1};
        };
        struct Node final {
            std::vector<Edge> edges;
            float raw{};
            bool expanded{}, terminal{};
        };
        struct Tree final {
            Game game;
            SearchConfig config;
            std::mt19937_64 random;
            std::vector<Node> nodes{1};
            std::vector<std::pair<int, int>> path;
            std::vector<int> schedule;
            std::optional<Observation> request;
            int pending{}, simulation{};
            std::uint64_t leaves{};

            Tree(const Game& initial, SearchConfig settings, std::uint64_t seed);
            std::vector<float> completed(const Node& node) const;
            Task<bool> prepare();
            void accept(const Prediction& prediction);
            void backup(float value);
            SearchResult finish() const;
        };
        enum class Stage { idle, queued, working, inference, complete };
        struct Job final {
            std::optional<Tree> tree;
            std::optional<Task<bool>> operation;
            std::shared_ptr<const DeviceWeights> model;
            SearchResult result;
            Stage stage{Stage::idle};
            bool commit{}, finishing{}, adjudicating{};
            std::chrono::steady_clock::time_point rule_started{};
            std::uint64_t rule_nodes{};
            int simulations{};
        };

    public:
        std::vector<bool> busy;
        SearchActivity activity;
        Search(int count, int threads);
        ~Search();
        void submit(int actor, const Game& game, SearchConfig config, std::uint64_t seed, std::shared_ptr<const DeviceWeights> model, bool commit = false);
        std::vector<SearchCompleted> advance(Network& network);

    private:
        std::vector<Job> jobs;
        std::mutex mutex;
        std::condition_variable condition;
        std::deque<int> pending;
        std::exception_ptr error;
        std::vector<std::jthread> workers;
    };
    std::vector<SearchResult> search(Network& network, std::span<Game* const> games, SearchConfig config, std::mt19937_64& random, std::function<void(const SearchActivity&)> progress = {});
} // namespace chess::ai
