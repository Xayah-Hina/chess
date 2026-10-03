export module chess.benchmark.artifact;
import std;
export namespace chess::benchmark {
    void publish(const std::filesystem::path& temporary, const std::filesystem::path& destination);
} // namespace chess::benchmark
