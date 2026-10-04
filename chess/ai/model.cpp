module chess.ai.model;
import std;

namespace chess::ai {
    ModelLayout::ModelLayout() {
        const auto allocate = [&](const std::size_t count) {
            const auto offset = (parameter_count + 63) / 64 * 64;
            parameter_count = offset + count;
            return offset;
        };
        for (int index = 0; index < 15; ++index) {
            auto& layer = layers[index];
            layer.output = index == 13 ? 50 : index == 14 ? 1 : 128;
            layer.kernel = index < 13 ? 3 : 1;
            layer.weight = allocate(layer.output * layer.input * layer.kernel * layer.kernel);
            layer.bias = allocate(layer.output);
            layer.affine = allocate(layer.output * 2);
            layer.running = running_count;
            running_count += layer.output * 2;
        }
        policy_weight = allocate(action_count * action_count);
        policy_bias = allocate(action_count);
        hidden_weight = allocate(128 * 90);
        hidden_bias = allocate(128);
        value_weight = allocate(3 * 128);
        value_bias = allocate(3);
    }
    void serialize(Archive& archive, Weights& weights) {
        archive.sequence(weights.parameters);
        archive.sequence(weights.running);
        archive.pod(weights.version);
    }
    void save_weights(const std::filesystem::path& path, Weights weights) {
        Archive archive{path, false, 1};
        serialize(archive, weights);
    }
    Weights load_weights(const std::filesystem::path& path) {
        Archive archive{path, true, 1};
        Weights weights;
        serialize(archive, weights);
        return weights;
    }
}
