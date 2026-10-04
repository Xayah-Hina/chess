export module chess.ai.search;
export import chess.ai.network;
export import chess.ai.tree;
import std;

export namespace chess::ai {
    struct SearchCompleted final {
        int actor{};
        std::uint64_t version{};
        SearchResult result;
        Game game;
    };
    struct SearchActivity final {
        std::uint64_t simulations{};
        std::size_t completed{}, total{};
    };
    struct Search final {
    private:
        enum class Stage { idle, queued, working, inference, complete };
        struct Job final {
            std::optional<Tree> tree;
            std::shared_ptr<const DeviceWeights> model;
            SearchResult result;
            Stage stage{Stage::idle};
            bool commit{};
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
        std::condition_variable condition, work_condition;
        std::deque<int> pending;
        std::exception_ptr error;
        std::vector<std::jthread> workers;
    };
    std::vector<SearchResult> search(Network& network, std::span<Game* const> games, SearchConfig config, std::mt19937_64& random, std::function<void(const SearchActivity&)> progress = {});
} // namespace chess::ai
