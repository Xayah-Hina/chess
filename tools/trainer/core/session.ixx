export module tools.trainer.session;
export import chess.ai.training;
export import chess.benchmark.evaluation;
import std;
export namespace tools::trainer {
    enum class Phase { idle, loading, collecting, selfplay, updating, pausing, paused, benchmark, saving, closing, failed };
    inline constexpr std::array<const char*, 11> phases{"Idle", "Loading", "Collecting samples", "Self-play", "Updating model", "Pausing", "Paused", "Benchmarking", "Saving", "Closing", "Error"};
    enum class Action { open, start, pause, save, short_benchmark, deep_benchmark, resume_benchmark, remove_benchmark, close };
    struct Request final {
        Action action;
        chess::benchmark::Config config;
        std::uint64_t id{};
    };
    struct BenchmarkRequest final {
        std::uint64_t id{};
        bool deep{};
    };
    struct MatchResult final {
        std::string name;
        chess::benchmark::Statistics statistics;
        int target_pairs{}, completed_games{}, pikafish_nodes{}, simulations{};
        bool complete{};
        std::uint64_t plies{};
    };
    struct BenchmarkResult final {
        BenchmarkRequest request;
        std::uint64_t version{};
        bool complete{};
        int probe_index{}, probe_target{};
        double seconds{};
        chess::ai::SearchActivity activity;
        std::string stage;
        std::array<chess::benchmark::Probe, 3> probes;
        std::vector<MatchResult> matches;
        std::filesystem::path directory;
    };
    struct MetricFrame final {
        std::uint64_t steps{};
        std::array<float, 4> metrics;
    };
    struct Snapshot final {
        std::uint64_t revision{};
        Phase phase{Phase::idle};
        bool loaded{}, training{}, closed{};
        chess::benchmark::Config config;
        std::uint64_t version{}, steps{}, games{}, plies{}, replay{}, generated{}, presented{}, pending_samples{}, minimum_samples{};
        chess::ai::SearchActivity search;
        std::array<std::uint64_t, 3> outcomes{};
        std::array<float, 4> metrics{};
        double training_seconds{}, benchmark_seconds{}, plies_per_second{}, steps_per_second{};
        std::chrono::system_clock::time_point saved{};
        std::deque<BenchmarkRequest> queue;
        std::optional<BenchmarkResult> active, latest;
        std::vector<MetricFrame> history;
        std::vector<std::string> events;
        std::string error;
    };
    struct Session final {
        explicit Session(std::function<void()> notify = {});
        ~Session();
        void submit(Request request);
        Snapshot drain();

    private:
        std::function<void()> notify;
        std::mutex mutex;
        std::condition_variable condition;
        std::deque<Request> requests;
        std::stop_source cancellation;
        Snapshot state;
        chess::benchmark::Config config;
        std::unique_ptr<chess::ai::Training> training;
        std::unique_ptr<chess::benchmark::Evaluation> evaluation;
        std::deque<BenchmarkRequest> queue;
        std::optional<BenchmarkRequest> active;
        std::optional<BenchmarkResult> latest;
        bool enabled{}, hold_jobs{true}, closing{}, opening{};
        double training_seconds{}, benchmark_seconds{}, next_save{60}, next_record{};
        std::uint64_t next_id{1}, sampled_steps{}, prior_plies{}, prior_steps{};
        double prior_seconds{};
        std::chrono::system_clock::time_point saved{};
        std::filesystem::path latest_weights;
        std::vector<MetricFrame> history;
        std::vector<std::string> events;
        std::jthread worker;
        void open(chess::benchmark::Config settings);
        void publish(Phase phase, std::string event = {});
        void save();
        void run();
    };
} // namespace tools::trainer
