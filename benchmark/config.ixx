export module chess.benchmark.config;
export import chess.ai.config;
import std;
export namespace chess::benchmark {
    struct Config final {
        ai::Config training;
        double save_seconds{60};
        int opening_pairs{10}, arena_batch{32}, diagnostic_positions{128};
        std::filesystem::path run{std::filesystem::path{CHESS_ASSET_DIRECTORY} / "ai/runs/default"};
        std::filesystem::path suite{std::filesystem::path{CHESS_ASSET_DIRECTORY} / "benchmark/suites/default.bin"};
        std::filesystem::path engine{std::filesystem::path{CHESS_ASSET_DIRECTORY} / "benchmark/engines/pikafish/Pikafish-Windows-x86-64-universal.exe"};
        std::filesystem::path engine_model{std::filesystem::path{CHESS_ASSET_DIRECTORY} / "benchmark/engines/pikafish/pikafish.nnue"};
    };
    Config load_config(const std::filesystem::path& path);
    void save_config(const Config& config, const std::filesystem::path& path);
} // namespace chess::benchmark
