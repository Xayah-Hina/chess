export module chess.ai.config;
import std;
export namespace chess::ai {
    struct Config final {
        std::uint64_t seed{1};
        // Calibrated on RTX 5090 / Ryzen 9 9950X with the complete training pipeline.
        int actors{128}, threads{4}, batch{512}, simulations{32}, candidates{8}, generation_plies{16};
        std::size_t replay_capacity{500000};
        float learning_rate{3e-4F}, weight_decay{1e-4F}, presentations{2};
    };
} // namespace chess::ai
