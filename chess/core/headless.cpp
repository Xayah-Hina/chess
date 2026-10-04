module chess.headless;
import chess.game;
import std;

namespace chess::headless {
    int run(const std::span<const std::string_view> arguments) {
        Game game;
        const auto execute = [&](const std::string_view line) {
            std::istringstream input{std::string{line}};
            std::string command;
            input >> command;
            if (command.empty()) return;
            if (command == "new") game = Game{};
            else if (command == "move") {
                std::string text;
                input >> text;
                game.play(coordinate(text));
            } else if (command == "legal") {
                for (const auto move : game.moves) std::print("{} ", coordinate(move));
                std::println();
                return;
            } else if (command != "state") throw std::runtime_error{std::format("Unknown command: {}", command)};
            constexpr std::string_view pieces{".kaehrcp"};
            for (int rank = 9; rank >= 0; --rank) {
                std::print("{} ", rank);
                for (int file = 0; file < 9; ++file) {
                    const auto piece  = game.position.board[rank * 9 + file];
                    const char symbol = pieces[int(piece.kind)];
                    std::print("{} ", piece.kind != Kind::none && piece.color == Color::red ? char(std::toupper(symbol)) : symbol);
                }
                std::println();
            }
            std::println("  a b c d e f g h i");
            constexpr std::array results{"ongoing", "red-win", "black-win", "draw"};
            std::println("turn={} result={} reason={} plies={}", game.position.turn == Color::red ? "red" : "black", results[int(game.decision.outcome)], describe(game.decision.reason), game.history.size());
            std::cout.flush();
        };
        if (!arguments.empty()) {
            for (const auto argument : arguments) execute(argument);
            return 0;
        }
        std::string line;
        while (std::getline(std::cin, line)) execute(line);
        return 0;
    }
} // namespace chess::headless
