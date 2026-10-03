export module chess.benchmark.arena;
export import chess.benchmark.config;
export import chess.ai.checkpoint;
export import chess.benchmark.opponents;
export import chess.benchmark.pikafish;
import std;
export namespace chess::benchmark {
    struct Diagnostic final {
        Game game{};
        std::vector<Move> correct;
        int category{};
    };
    enum class Advance { idle, progress, interrupted };
    struct Match final {
        Game game{};
        std::mt19937_64 random;
        bool candidate_red{}, complete{};
        std::size_t opening_length{};
        std::uint64_t engine_nodes{};
        Move pending_move{};
        bool pending{};
        std::unique_ptr<Pikafish> engine;
    };
    struct ArenaJob final {
        std::string name, candidate, reference, candidate_hash, reference_hash;
        Opponent opponent{};
        int depth{}, nodes{}, pairs{};
        bool raw_policy{}, holdout{}, reported{};
        std::size_t cursor{};
        double seconds{};
        std::vector<Match> matches;
    };
    struct Statistics final {
        std::array<std::uint64_t, 5> frequencies{};
        std::array<std::uint64_t, 3> wdl{}, red{}, black{};
        double score{}, lower{}, upper{};
        std::uint64_t pairs{}, plies{};
    };
    std::string fingerprint(const std::filesystem::path& path);
    Statistics statistics(const ArenaJob& job);
    struct Arena final {
        Config config;
        std::vector<std::vector<Move>> openings, holdout;
        std::vector<Diagnostic> diagnostics, holdout_diagnostics;
        Game miner;
        std::mt19937_64 random;
        std::vector<ArenaJob> jobs;
        double seconds{};
        bool suite_ready{}, diagnostic_holdout{};
        std::string initial, engine_identity, engine_options, engine_hash, model_hash, suite_hash;
        explicit Arena(const Config& settings, bool preparation = false);
        void prepare();
        void persist_suite(ai::Archive& archive);
        void enqueue(const std::filesystem::path& candidate, const std::filesystem::path& reference);
        Advance advance(ai::Network& model, std::function<void(std::string_view, const ai::SearchActivity&)> progress = {});
        void persist(ai::Archive& archive);
        void report();

    private:
        std::unique_ptr<ai::Network> reference;
        std::string loaded_reference;
        void mine();
        void write_job(ArenaJob& job);
    };
} // namespace chess::benchmark
