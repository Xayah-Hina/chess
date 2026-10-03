export module chess.benchmark.evaluation;
export import chess.benchmark.probe;
import std;
export namespace chess::benchmark {
    struct Evaluation final {
        Config config;
        Arena arena;
        bool deep{}, complete{};
        std::array<Probe, 3> probes;
        int probe_index{};
        double seconds{}, running_seconds{};
        ai::SearchActivity activity;
        std::string stage;
        std::function<void()> heartbeat;
        std::chrono::steady_clock::time_point next_heartbeat{};
        std::unique_ptr<ai::Network> network;
        std::string model;
        explicit Evaluation(Config settings, bool deep, bool resume = false);
        void begin(const std::filesystem::path& model, const std::filesystem::path& comparison = {}, const std::filesystem::path& initial = {}, const std::string& opponent = {});
        void advance();
        void persist(ai::Archive& archive);
        void save();
        void report(std::string_view state);
    };
} // namespace chess::benchmark
