export module chess.page.bindings;
export import chess.ai.tree;
import std;

export namespace chess::page {
    struct Engine final {
        Game game;
        std::optional<ai::Tree> tree;
        ai::Prediction prediction;
        std::array<float, 128 * 90> input;
        std::uintptr_t prediction_address{reinterpret_cast<std::uintptr_t>(&prediction)};
        int completed{};
        std::string state() const;
        void restart();
        void play(int from, int to);
        void begin();
        bool advance();
        std::uintptr_t observation();
        void accept();
        void commit();
    };
}
