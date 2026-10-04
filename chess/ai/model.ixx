export module chess.ai.model;
export import chess.ai.encoding;
export import chess.ai.archive;
import std;

export namespace chess::ai {
    struct Prediction final {
        std::array<float, action_count> logits;
        std::array<float, 3> wdl;
    };
    struct Weights final {
        std::vector<float> parameters, running;
        std::uint64_t version{};
    };
    struct LayerLayout {
        int input{128}, output{}, kernel{};
        std::size_t weight{}, bias{}, affine{}, running{};
    };
    struct ModelLayout final {
        std::array<LayerLayout, 15> layers;
        std::size_t policy_weight{}, policy_bias{}, hidden_weight{}, hidden_bias{}, value_weight{}, value_bias{};
        std::size_t parameter_count{}, running_count{};
        ModelLayout();
    };
    void serialize(Archive& archive, Weights& weights);
    void save_weights(const std::filesystem::path& path, Weights weights);
    Weights load_weights(const std::filesystem::path& path);
}
