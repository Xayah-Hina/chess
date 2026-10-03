module;
#include <cstdint>
export module chess.ai.encoding;
export import chess.game;
import std;
export {
#include "tensors.h"
}

export namespace chess::ai {
    struct Sample final {
        Observation observation;
        std::vector<PolicyEntry> policy;
        std::uint64_t version{};
        std::uint8_t result{};
    };
    Observation observe(const Game& game);
    int action(Move move, Color turn);
    float terminal_value(const Game& game);
} // namespace chess::ai
