#ifndef CHESS_AI_TENSORS_H
#define CHESS_AI_TENSORS_H
#include <cstdint>

namespace chess::ai {
    // 112 history planes and 10 rule planes, padded for Tensor Core convolutions.
    inline constexpr int input_channels = 128, rule_channels = 10, action_count = 4500;
    struct Observation final {
        std::uint8_t boards[720]{};
        float rules[rule_channels]{};
    };
    struct PolicyEntry final {
        std::uint16_t action{};
        float probability{};
    };
    struct ReplayRecord final {
        Observation observation;
        std::uint64_t offset{}, version{};
        std::uint32_t count{};
        std::uint8_t result{};
    };
    struct TrainingArguments final {
        const ReplayRecord* records;
        const PolicyEntry* policies;
        std::uint64_t seed{}, capacity{}, cursor{}, size{}, policy_capacity{};
        float learning_rate{}, weight_decay{}, correction1{}, correction2{};
    };
} // namespace chess::ai
#endif
