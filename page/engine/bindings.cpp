module;
#include <emscripten/bind.h>
module chess.page.bindings;
import std;

namespace chess::page {
    std::string Engine::state() const {
        std::string result = std::format("{{\"turn\":{},\"outcome\":{},\"reason\":{},\"check\":{},\"plies\":{},\"board\":[", int(game.position.turn), int(game.decision.outcome), int(game.decision.reason), in_check(game.position, game.position.turn), game.history.size());
        for (int square = 0; square < 90; ++square) {
            const auto piece = game.position.board[square];
            result += std::format("{}{}", square ? "," : "", piece.kind == Kind::none ? 0 : int(piece.kind) * (piece.color == Color::red ? 1 : -1));
        }
        result += "],\"moves\":[";
        for (std::size_t index = 0; index < game.moves.size(); ++index) result += std::format("{}[{},{}]", index ? "," : "", game.moves[index].from, game.moves[index].to);
        result += "],\"last\":";
        if (game.history.empty()) result += "null";
        else result += std::format("[{},{}]", game.history.back().undo.move.from, game.history.back().undo.move.to);
        return result + "}";
    }
    void Engine::restart() {
        tree.reset();
        completed = 0;
        game = Game{};
    }
    void Engine::play(const int from, const int to) {
        const Move move{from, to};
        if (game.position.turn != Color::red || game.decision.outcome != Outcome::ongoing || std::ranges::find(game.moves, move) == game.moves.end()) throw std::runtime_error{"The requested move is not legal"};
        game.play(move);
    }
    void Engine::begin() {
        ai::SearchConfig settings;
        settings.exploration = false;
        tree.emplace(game, settings, 1);
        completed = 0;
    }
    bool Engine::advance() {
        if (tree->nodes[0].expanded && tree->simulation >= tree->config.simulations) return false;
        tree->prepare();
        completed = tree->simulation;
        return true;
    }
    std::uintptr_t Engine::observation() {
        if (!tree->request) return 0;
        const auto& observation = *tree->request;
        for (int plane = 0; plane < 128; ++plane)
            for (int square = 0; square < 90; ++square) input[plane * 90 + square] = plane < 112 ? float(observation.boards[plane / 14 * 90 + square] == plane % 14 + 1) : plane < 122 ? observation.rules[plane - 112] : 0;
        return reinterpret_cast<std::uintptr_t>(input.data());
    }
    void Engine::accept() {
        tree->accept(prediction);
        completed = tree->simulation;
    }
    void Engine::commit() {
        game.play(tree->finish().move);
        tree.reset();
    }
}

EMSCRIPTEN_BINDINGS(chess_page) {
    emscripten::class_<chess::page::Engine>("Engine")
        .constructor<>()
        .function("state", &chess::page::Engine::state)
        .function("restart", &chess::page::Engine::restart)
        .function("play", &chess::page::Engine::play)
        .function("begin", &chess::page::Engine::begin)
        .function("advance", &chess::page::Engine::advance)
        .function("observation", &chess::page::Engine::observation)
        .property("predictionAddress", &chess::page::Engine::prediction_address)
        .property("completed", &chess::page::Engine::completed)
        .function("accept", &chess::page::Engine::accept)
        .function("commit", &chess::page::Engine::commit);
}
