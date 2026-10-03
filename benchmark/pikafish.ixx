module;
#include <Windows.h>
export module chess.benchmark.pikafish;
export import chess.game;
import std;
export namespace chess::benchmark {
    struct Pikafish final {
        std::string identity, options;
        std::uint64_t nodes{};
        int depth{};
        Pikafish(const std::filesystem::path& executable, const std::filesystem::path& model);
        ~Pikafish();
        Move choose(const Game& game, std::size_t opening_length, Color color, int budget);

    private:
        std::unique_ptr<void, decltype(&CloseHandle)> process{nullptr, CloseHandle}, input{nullptr, CloseHandle}, output{nullptr, CloseHandle};
        std::string buffered;
        bool reconstructed{};
        void send(std::string text);
        std::string line(bool cancellable = true);
        Move calculate(std::span<const Step> history, int budget);
    };
} // namespace chess::benchmark
