module chess.ai.headless;
import chess.ai.config;
import chess.ai.checkpoint;
import std;
namespace chess::ai {
    int run(const std::span<const std::string_view> arguments) {
        Config config;
        std::filesystem::path model;
        std::vector<Move> prefix;
        for (std::size_t index = 1; index < arguments.size(); ++index) {
            const auto option = arguments[index];
            const std::string value{arguments[++index]};
            if (option == "--model") model = value;
            else if (option == "--simulations") config.simulations = std::stoi(value);
            else if (option == "--candidates") config.candidates = std::stoi(value);
            else if (option == "--threads") config.threads = std::stoi(value);
            else if (option == "--seed") config.seed = std::stoull(value);
            else if (option == "--moves") {
                std::istringstream moves{value};
                for (std::string move; moves >> move;) prefix.push_back(coordinate(move));
            } else throw std::runtime_error{std::format("Unknown inference option: {}", option)};
        }
        Network network{1, config.seed};
        network.restore(load_weights(std::filesystem::absolute(model)));
        Game game;
        for (const auto move : prefix) game.play(move);
        const std::array states{&game};
        std::mt19937_64 random{config.seed};
        const auto result = search(network, states, {config.simulations, config.candidates, config.threads, false}, random).front();
        std::println("move={} value={:.6f} leaves={} version={}", coordinate(result.move), result.value, result.leaves, network.version);
        for (const auto entry : result.policy) std::println("action={} probability={:.6f}", entry.action, entry.probability);
        return 0;
    }
} // namespace chess::ai
