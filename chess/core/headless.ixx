export module chess.headless;
import std;

export namespace chess::headless {
    int run(std::span<const std::string_view> arguments);
}
