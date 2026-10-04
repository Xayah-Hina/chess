#include <Windows.h>
#include <bcrypt.h>
import chess.ai.model;
import std;

namespace {
    // ONNX uses the standard protobuf wire format. Only graph construction is
    // needed here; the trained tensor layout comes from chess.ai.model.
    struct Proto final {
        std::string bytes;
        void varint(std::uint64_t value) {
            while (value >= 128) {
                bytes.push_back(char((value & 127) | 128));
                value >>= 7;
            }
            bytes.push_back(char(value));
        }
        void integer(const int field, const std::uint64_t value) {
            varint(field * 8);
            varint(value);
        }
        void data(const int field, const std::string_view value) {
            varint(field * 8 + 2);
            varint(value.size());
            bytes.append(value);
        }
    };
    Proto attribute(const std::string_view name, const std::initializer_list<int> values, const bool scalar = false) {
        Proto result;
        result.data(1, name);
        result.integer(20, scalar ? 2 : 7);
        for (const int value : values) result.integer(scalar ? 3 : 8, value);
        return result;
    }
    void node(Proto& graph, const std::string_view operation, const std::initializer_list<std::string> inputs, const std::string& output, const std::initializer_list<Proto> attributes = {}) {
        Proto result;
        for (const auto& input : inputs) result.data(1, input);
        result.data(2, output);
        result.data(3, output);
        result.data(4, operation);
        for (const auto& value : attributes) result.data(5, value.bytes);
        graph.data(1, result.bytes);
    }
    void tensor(Proto& graph, const std::string& name, const std::initializer_list<int> shape, const std::span<const float> values) {
        Proto result;
        for (const int dimension : shape) result.integer(1, dimension);
        result.integer(2, 1);
        result.data(8, name);
        result.data(9, {reinterpret_cast<const char*>(values.data()), values.size_bytes()});
        graph.data(5, result.bytes);
    }
    void interface(Proto& graph, const int field, const std::string_view name, const std::initializer_list<int> shape) {
        Proto dimensions;
        for (const int size : shape) {
            Proto dimension;
            dimension.integer(1, size);
            dimensions.data(1, dimension.bytes);
        }
        Proto tensor_type;
        tensor_type.integer(1, 1);
        tensor_type.data(2, dimensions.bytes);
        Proto type;
        type.data(1, tensor_type.bytes);
        Proto value;
        value.data(1, name);
        value.data(2, type.bytes);
        graph.data(field, value.bytes);
    }
    std::string sha256(const std::string& bytes) {
        BCRYPT_ALG_HANDLE algorithm{};
        BCRYPT_HASH_HANDLE hash{};
        NTSTATUS status = BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0);
        if (status >= 0) status = BCryptCreateHash(algorithm, &hash, nullptr, 0, nullptr, 0, 0);
        if (status >= 0) status = BCryptHashData(hash, reinterpret_cast<PUCHAR>(const_cast<char*>(bytes.data())), ULONG(bytes.size()), 0);
        std::array<unsigned char, 32> digest;
        if (status >= 0) status = BCryptFinishHash(hash, digest.data(), ULONG(digest.size()), 0);
        if (hash) BCryptDestroyHash(hash);
        if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0);
        if (status < 0) throw std::runtime_error{std::format("SHA256 failed: 0x{:08X}", unsigned(status))};
        std::string result;
        for (const auto byte : digest) result += std::format("{:02x}", byte);
        return result;
    }
}

int main(const int argc, const char* const* argv) {
    try {
        if (argc != 3) throw std::runtime_error{"Usage: model-export <weights.bin> <output-directory>"};
        const auto weights = chess::ai::load_weights(argv[1]);
        const chess::ai::ModelLayout layout;
        const auto parameters = std::span{weights.parameters};
        Proto graph;
        graph.data(2, "xiangqi-resnet-128x6");
        interface(graph, 11, "observation", {1, 128, 10, 9});
        interface(graph, 12, "policy", {1, 4500});
        interface(graph, 12, "wdl", {1, 3});
        for (int index = 0; index < 15; ++index) {
            const auto& layer = layout.layers[index];
            const auto prefix = std::format("layer{}", index);
            std::vector<float> kernel(layer.output * layer.input * layer.kernel * layer.kernel), bias(layer.output);
            for (int output = 0; output < layer.output; ++output) {
                const float scale = weights.parameters[layer.affine + output] / std::sqrt(weights.running[layer.running + layer.output + output] + 1e-5F);
                bias[output] = (weights.parameters[layer.bias + output] - weights.running[layer.running + output]) * scale + weights.parameters[layer.affine + layer.output + output];
                for (int input = 0; input < layer.input; ++input)
                    for (int y = 0; y < layer.kernel; ++y)
                        for (int x = 0; x < layer.kernel; ++x) {
                            const auto source = ((output * layer.kernel + y) * layer.kernel + x) * layer.input + input;
                            const auto target = ((output * layer.input + input) * layer.kernel + y) * layer.kernel + x;
                            kernel[target] = weights.parameters[layer.weight + source] * scale;
                        }
            }
            tensor(graph, prefix + ".weight", {layer.output, layer.input, layer.kernel, layer.kernel}, kernel);
            tensor(graph, prefix + ".bias", {layer.output}, bias);
            const auto source = index == 0 ? std::string{"observation"} : std::format("layer{}", index < 13 ? index - 1 : 12);
            const int padding = layer.kernel / 2;
            node(graph, "Conv", {source, prefix + ".weight", prefix + ".bias"}, prefix + ".conv", {attribute("pads", {padding, padding, padding, padding})});
            if (index > 0 && index < 13 && index % 2 == 0) node(graph, "Add", {prefix + ".conv", std::format("layer{}", index - 2)}, prefix + ".skip");
            node(graph, "Relu", {prefix + (index > 0 && index < 13 && index % 2 == 0 ? ".skip" : ".conv")}, prefix);
        }
        tensor(graph, "policy.weight", {4500, 4500}, parameters.subspan(layout.policy_weight, 4500 * 4500));
        tensor(graph, "policy.bias", {4500}, parameters.subspan(layout.policy_bias, 4500));
        // Native cuDNN activations are NHWC. Flattening NCHW directly would
        // silently feed the policy matrix a different ordering of features.
        node(graph, "Transpose", {"layer13"}, "policy.nhwc", {attribute("perm", {0, 2, 3, 1})});
        node(graph, "Flatten", {"policy.nhwc"}, "policy.flat", {attribute("axis", {1}, true)});
        node(graph, "Gemm", {"policy.flat", "policy.weight", "policy.bias"}, "policy", {attribute("transB", {1}, true)});
        tensor(graph, "hidden.weight", {128, 90}, parameters.subspan(layout.hidden_weight, 128 * 90));
        tensor(graph, "hidden.bias", {128}, parameters.subspan(layout.hidden_bias, 128));
        tensor(graph, "value.weight", {3, 128}, parameters.subspan(layout.value_weight, 3 * 128));
        tensor(graph, "value.bias", {3}, parameters.subspan(layout.value_bias, 3));
        node(graph, "Flatten", {"layer14"}, "value.flat", {attribute("axis", {1}, true)});
        node(graph, "Gemm", {"value.flat", "hidden.weight", "hidden.bias"}, "hidden.raw", {attribute("transB", {1}, true)});
        node(graph, "Relu", {"hidden.raw"}, "hidden");
        node(graph, "Gemm", {"hidden", "value.weight", "value.bias"}, "value", {attribute("transB", {1}, true)});
        node(graph, "Softmax", {"value"}, "wdl", {attribute("axis", {1}, true)});
        Proto opset;
        opset.integer(2, 23);
        Proto model;
        model.integer(1, 12);
        model.data(2, "chess-model-export");
        model.integer(5, weights.version);
        model.data(7, graph.bytes);
        model.data(8, opset.bytes);
        const auto digest = sha256(model.bytes);
        const auto filename = std::format("model-v{}-{}.onnx", weights.version, digest.substr(0, 12));
        const std::filesystem::path directory{argv[2]};
        std::filesystem::create_directories(directory);
        std::ofstream output;
        output.exceptions(std::ios::badbit | std::ios::failbit);
        output.open(directory / filename, std::ios::binary);
        output.write(model.bytes.data(), std::streamsize(model.bytes.size()));
        output.close();
        output.open(directory / "model.json", std::ios::binary);
        output << std::format("{{\n  \"version\": {},\n  \"file\": \"{}\",\n  \"sha256\": \"{}\",\n  \"bytes\": {},\n  \"encoding\": \"history8-rules10-actions4500-v1\",\n  \"rules\": \"WXF-2018\",\n  \"simulations\": 32\n}}\n", weights.version, filename, digest, model.bytes.size());
        std::println("Exported model v{}: {} ({} bytes)", weights.version, (directory / filename).string(), model.bytes.size());
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
