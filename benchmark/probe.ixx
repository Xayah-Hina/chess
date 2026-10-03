export module chess.benchmark.probe;
export import chess.benchmark.arena;
import std;
export namespace chess::benchmark {
    struct Probe final {
        std::uint64_t version{}, positions{}, value_positions{};
        int simulations{};
        double elapsed{}, seconds{}, mass{}, brier{};
        std::array<std::uint64_t, 3> correct{}, total{};
        std::array<std::uint64_t, 10> calibration_count{};
        std::array<double, 10> calibration_prediction{};
    };
    void measure(ai::Network& network, std::span<const Diagnostic> diagnostics, ai::SearchConfig search, Probe& probe, std::function<void(const ai::SearchActivity&)> progress = {});
    void write_probe(const Probe& probe, const Config& config, std::string_view suite_hash, bool holdout, const std::filesystem::path& destination, std::string_view candidate_hash);
} // namespace chess::benchmark
