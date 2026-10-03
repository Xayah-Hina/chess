module;
#include <cuda_runtime.h>
export module chess.ai.network;
export import chess.ai.device;
export import chess.ai.encoding;
export import chess.ai.replay;
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
    struct Optimizer final {
        std::vector<float> first, second;
        std::uint64_t steps{};
    };
    struct DeviceWeights final {
        Buffer parameters, reduced, running;
        std::uint64_t version{};
        std::unique_ptr<std::remove_pointer_t<cudaEvent_t>, decltype(&cudaEventDestroy)> ready{nullptr, cudaEventDestroy};
        DeviceWeights(std::size_t parameters, std::size_t running, std::uint64_t version);
    };
    struct ConvLayer final {
        int input{}, output{}, kernel{};
        bool normalized{};
        std::size_t weight{}, affine{}, running{};
        Buffer raw, values, derivative, input_derivative, statistics;
    };
    struct Network final {
        Device device;
        std::uint64_t version{}, steps{};
        std::size_t parameter_count{}, running_count{};
        std::vector<ConvLayer> layers;
        explicit Network(int capacity, std::uint64_t seed);
        std::vector<Prediction> infer(std::span<const Observation> observations, std::shared_ptr<const DeviceWeights> weights = {});
        std::array<float, 4> train(const Replay& replay, int batch, std::mt19937_64& random, float learning_rate, float weight_decay);
        std::shared_ptr<const DeviceWeights> freeze();
        Weights snapshot();
        static Weights snapshot(const DeviceWeights& weights);
        static std::shared_ptr<const DeviceWeights> upload(const Weights& weights);
        Optimizer optimizer();
        void restore(const Weights& weights, const Optimizer* optimizer = nullptr);

    private:
        struct InferenceGraph final {
            int batch{};
            std::unique_ptr<std::remove_pointer_t<cudaGraphExec_t>, decltype(&cudaGraphExecDestroy)> graph{nullptr, cudaGraphExecDestroy};
        };
        int capacity;
        std::size_t hidden_weight{}, hidden_bias{}, value_weight{}, value_bias{};
        Buffer parameters, reduced, gradients, first, second, decay, running;
        Buffer observations, input, targets, results, metrics, arguments, hidden, value, hidden_derivative, value_derivative, residual_derivative;
        HostBuffer input_staging, output_staging, argument_staging, metric_staging;
        Buffer inference_parameters, inference_reduced, inference_running;
        std::shared_ptr<const DeviceWeights> resident;
        std::optional<std::uint64_t> bound_version;
        std::vector<InferenceGraph> inference_graphs;
        int prepared_batch{};
        std::unique_ptr<std::remove_pointer_t<cudaGraphExec_t>, decltype(&cudaGraphExecDestroy)> training_graph{nullptr, cudaGraphExecDestroy};
        void update(int batch);
        void forward(int batch, bool training);
        void backward(ConvLayer& layer, void* input, int batch);
    };
} // namespace chess::ai
