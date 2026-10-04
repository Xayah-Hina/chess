module;
#include "kernels.h"
#include <cuda_bf16.h>
module chess.ai.network;
import std;

namespace chess::ai {
    DeviceWeights::DeviceWeights(const std::size_t count, const std::size_t statistics, const std::uint64_t revision) : parameters{count * 4}, reduced{count * 2}, running{statistics * 4}, version{revision} {
        check(cudaEventCreateWithFlags(std::out_ptr(ready), cudaEventDisableTiming));
    }
    Network::Network(const int maximum, const std::uint64_t seed) : capacity{maximum} {
        std::mt19937_64 random{seed};
        std::vector<float> initial, decay_values, normalization;
        const auto allocate = [&](const int count, const float deviation, const float constant, const bool decayed) {
            const auto offset = (initial.size() + 63) / 64 * 64;
            initial.resize(offset + count);
            decay_values.resize(initial.size());
            std::normal_distribution<float> distribution{0, deviation ? deviation : 1.0F};
            for (int i = 0; i < count; ++i) {
                initial[offset + i]      = deviation ? distribution(random) : constant;
                decay_values[offset + i] = decayed ? 1.0F : 0.0F;
            }
            return offset;
        };
        for (int index = 0; index < 15; ++index) {
            ConvLayer layer;
            layer.input      = 128;
            layer.output     = index == 13 ? 50 : index == 14 ? 1 : 128;
            layer.kernel     = index < 13 ? 3 : 1;
            layer.weight     = allocate(layer.output * layer.input * layer.kernel * layer.kernel, std::sqrt(2.0F / (layer.input * layer.kernel * layer.kernel)), 0, true);
            layer.bias       = allocate(layer.output, 0, 0, false);
            layer.affine     = allocate(layer.output * 2, 0, 0, false);
            std::fill_n(initial.begin() + layer.affine, layer.output, 1.0F);
            layer.running = normalization.size();
            normalization.resize(normalization.size() + layer.output * 2, 0.0F);
            std::fill_n(normalization.begin() + layer.running + layer.output, layer.output, 1.0F);
            const auto output_bytes = std::size_t(capacity) * 90 * layer.output * 2;
            layer.raw               = Buffer{output_bytes};
            layer.values            = Buffer{output_bytes};
            layer.derivative        = Buffer{output_bytes};
            if (index) layer.input_derivative = Buffer{std::size_t(capacity) * 90 * layer.input * 2};
            layer.statistics = Buffer{std::size_t(layer.output) * 2 * sizeof(float)};
            layers.push_back(std::move(layer));
        }
        policy_weight        = allocate(action_count * action_count, std::sqrt(1.0F / action_count), 0, true);
        policy_bias          = allocate(action_count, 0, 0, false);
        hidden_weight        = allocate(128 * 90, std::sqrt(2.0F / 90), 0, true);
        hidden_bias          = allocate(128, 0, 0, false);
        value_weight         = allocate(3 * 128, std::sqrt(1.0F / 128), 0, true);
        value_bias           = allocate(3, 0, 0, false);
        parameter_count      = initial.size();
        running_count        = normalization.size();
        parameters           = Buffer{parameter_count * 4};
        reduced              = Buffer{parameter_count * 2};
        inference_parameters = Buffer{parameters.bytes};
        inference_reduced    = Buffer{reduced.bytes};
        inference_running    = Buffer{running_count * 4};
        gradients            = Buffer{parameter_count * 4};
        first                = Buffer{parameter_count * 4};
        second               = Buffer{parameter_count * 4};
        decay                = Buffer{parameter_count * 4};
        running              = Buffer{running_count * 4};
        observations         = Buffer{std::size_t(capacity) * sizeof(Observation)};
        input_staging        = HostBuffer{observations.bytes};
        output_staging       = HostBuffer{std::size_t(capacity) * (action_count + 3) * 2};
        arguments            = Buffer{sizeof(TrainingArguments)};
        argument_staging     = HostBuffer{arguments.bytes};
        metric_staging       = HostBuffer{16};
        input                = Buffer{std::size_t(capacity) * 90 * 128 * 2};
        targets              = Buffer{std::size_t(capacity) * action_count * 4};
        results              = Buffer{std::size_t(capacity)};
        metrics              = Buffer{16};
        policy               = Buffer{std::size_t(capacity) * action_count * 2};
        hidden               = Buffer{std::size_t(capacity) * 128 * 2};
        value                = Buffer{std::size_t(capacity) * 3 * 2};
        policy_derivative    = Buffer{policy.bytes};
        hidden_derivative    = Buffer{hidden.bytes};
        value_derivative     = Buffer{value.bytes};
        residual_derivative  = Buffer{input.bytes};
        check(cudaMemcpy(parameters.data, initial.data(), parameters.bytes, cudaMemcpyHostToDevice));
        check(cudaMemcpy(decay.data, decay_values.data(), decay.bytes, cudaMemcpyHostToDevice));
        check(cudaMemcpy(running.data, normalization.data(), running.bytes, cudaMemcpyHostToDevice));
        check(cudaMemset(first.data, 0, first.bytes));
        check(cudaMemset(second.data, 0, second.bytes));
        kernels::convert(device.stream.get(), reduced.data, static_cast<float*>(parameters.data), parameter_count);
        device.synchronize();
    }
    std::vector<Prediction> Network::infer(const std::span<const Observation> positions, std::shared_ptr<const DeviceWeights> weights) {
        const int batch   = int(positions.size());
        const auto stream = device.stream.get();
        std::memcpy(input_staging.data, positions.data(), positions.size_bytes());
        if (weights ? resident != weights : resident || bound_version != version) {
            if (weights) check(cudaStreamWaitEvent(stream, weights->ready.get(), 0));
            check(cudaMemcpyAsync(inference_parameters.data, weights ? weights->parameters.data : parameters.data, parameters.bytes, cudaMemcpyDeviceToDevice, stream));
            check(cudaMemcpyAsync(inference_reduced.data, weights ? weights->reduced.data : reduced.data, reduced.bytes, cudaMemcpyDeviceToDevice, stream));
            check(cudaMemcpyAsync(inference_running.data, weights ? weights->running.data : running.data, running.bytes, cudaMemcpyDeviceToDevice, stream));
            resident      = std::move(weights);
            bound_version = version;
        }
        auto* policies = static_cast<__nv_bfloat16*>(output_staging.data);
        auto* values   = policies + std::size_t(batch) * action_count;
        auto graph     = std::ranges::find(inference_graphs, batch, &InferenceGraph::batch);
        if (graph == inference_graphs.end()) {
            check(cudaMemcpyAsync(observations.data, input_staging.data, positions.size_bytes(), cudaMemcpyHostToDevice, stream));
            kernels::expand_observations(stream, input.data, observations.data, batch);
            forward(batch, false);
            device.synchronize();
            std::unique_ptr<std::remove_pointer_t<cudaGraph_t>, decltype(&cudaGraphDestroy)> captured{nullptr, cudaGraphDestroy};
            check(cudaStreamBeginCapture(stream, cudaStreamCaptureModeThreadLocal));
            check(cudaMemcpyAsync(observations.data, input_staging.data, positions.size_bytes(), cudaMemcpyHostToDevice, stream));
            kernels::expand_observations(stream, input.data, observations.data, batch);
            forward(batch, false);
            check(cudaMemcpyAsync(policies, policy.data, std::size_t(batch) * action_count * 2, cudaMemcpyDeviceToHost, stream));
            check(cudaMemcpyAsync(values, value.data, std::size_t(batch) * 3 * 2, cudaMemcpyDeviceToHost, stream));
            check(cudaStreamEndCapture(stream, std::out_ptr(captured)));
            InferenceGraph executable;
            executable.batch = batch;
            check(cudaGraphInstantiate(std::out_ptr(executable.graph), captured.get(), 0));
            inference_graphs.push_back(std::move(executable));
            graph = std::prev(inference_graphs.end());
        }
        check(cudaGraphLaunch(graph->graph.get(), stream));
        device.synchronize();
        std::vector<Prediction> predictions(batch);
        for (int i = 0; i < batch; ++i) {
            for (int index = 0; index < action_count; ++index) predictions[i].logits[index] = __bfloat162float(policies[i * action_count + index]);
            float maximum = -std::numeric_limits<float>::infinity(), total{};
            for (int category = 0; category < 3; ++category) maximum = std::max(maximum, __bfloat162float(values[i * 3 + category]));
            for (int category = 0; category < 3; ++category) total += predictions[i].wdl[category] = std::exp(__bfloat162float(values[i * 3 + category]) - maximum);
            for (auto& probability : predictions[i].wdl) probability /= total;
        }
        return predictions;
    }
    std::array<float, 4> Network::train(const Replay& replay, const int batch, std::mt19937_64& random, const float learning_rate, const float weight_decay) {
        const auto stream = device.stream.get();
        auto& settings    = *static_cast<TrainingArguments*>(argument_staging.data);
        settings          = {static_cast<const ReplayRecord*>(replay.records.data), static_cast<const PolicyEntry*>(replay.policies.data), random(), replay.capacity, replay.cursor, replay.size, replay.policy_capacity, learning_rate, weight_decay, 1 - std::pow(0.9F, float(steps + 1)), 1 - std::pow(0.999F, float(steps + 1))};
        check(cudaMemcpyAsync(arguments.data, &settings, arguments.bytes, cudaMemcpyHostToDevice, stream));
        if (prepared_batch != batch) {
            training_graph.reset();
            update(batch);
            prepared_batch = batch;
        } else {
            if (!training_graph) {
                std::unique_ptr<std::remove_pointer_t<cudaGraph_t>, decltype(&cudaGraphDestroy)> graph{nullptr, cudaGraphDestroy};
                check(cudaStreamBeginCapture(stream, cudaStreamCaptureModeThreadLocal));
                update(batch);
                check(cudaStreamEndCapture(stream, std::out_ptr(graph)));
                check(cudaGraphInstantiate(std::out_ptr(training_graph), graph.get(), 0));
            }
            check(cudaGraphLaunch(training_graph.get(), stream));
        }
        ++steps;
        ++version;
        check(cudaMemcpyAsync(metric_staging.data, metrics.data, metrics.bytes, cudaMemcpyDeviceToHost, stream));
        device.synchronize();
        std::array<float, 4> result;
        std::memcpy(result.data(), metric_staging.data, metrics.bytes);
        result[2] = std::sqrt(result[2]);
        result[3] = std::sqrt(result[3]);
        return result;
    }
    std::shared_ptr<const DeviceWeights> Network::freeze() {
        auto model        = std::make_shared<DeviceWeights>(parameter_count, running_count, version);
        const auto stream = device.stream.get();
        check(cudaMemcpyAsync(model->parameters.data, parameters.data, parameters.bytes, cudaMemcpyDeviceToDevice, stream));
        check(cudaMemcpyAsync(model->reduced.data, reduced.data, reduced.bytes, cudaMemcpyDeviceToDevice, stream));
        check(cudaMemcpyAsync(model->running.data, running.data, running.bytes, cudaMemcpyDeviceToDevice, stream));
        check(cudaEventRecord(model->ready.get(), stream));
        return model;
    }
    Weights Network::snapshot() {
        device.synchronize();
        Weights result{std::vector<float>(parameter_count), std::vector<float>(running_count), version};
        check(cudaMemcpy(result.parameters.data(), parameters.data, parameters.bytes, cudaMemcpyDeviceToHost));
        check(cudaMemcpy(result.running.data(), running.data, running.bytes, cudaMemcpyDeviceToHost));
        return result;
    }
    Weights Network::snapshot(const DeviceWeights& model) {
        check(cudaEventSynchronize(model.ready.get()));
        Weights result{std::vector<float>(model.parameters.bytes / 4), std::vector<float>(model.running.bytes / 4), model.version};
        check(cudaMemcpy(result.parameters.data(), model.parameters.data, model.parameters.bytes, cudaMemcpyDeviceToHost));
        check(cudaMemcpy(result.running.data(), model.running.data, model.running.bytes, cudaMemcpyDeviceToHost));
        return result;
    }
    std::shared_ptr<const DeviceWeights> Network::upload(const Weights& weights) {
        auto model = std::make_shared<DeviceWeights>(weights.parameters.size(), weights.running.size(), weights.version);
        check(cudaMemcpy(model->parameters.data, weights.parameters.data(), model->parameters.bytes, cudaMemcpyHostToDevice));
        check(cudaMemcpy(model->running.data, weights.running.data(), model->running.bytes, cudaMemcpyHostToDevice));
        kernels::convert(nullptr, model->reduced.data, static_cast<float*>(model->parameters.data), weights.parameters.size());
        check(cudaEventRecord(model->ready.get(), nullptr));
        return model;
    }
    Optimizer Network::optimizer() {
        device.synchronize();
        Optimizer result{std::vector<float>(parameter_count), std::vector<float>(parameter_count), steps};
        check(cudaMemcpy(result.first.data(), first.data, first.bytes, cudaMemcpyDeviceToHost));
        check(cudaMemcpy(result.second.data(), second.data, second.bytes, cudaMemcpyDeviceToHost));
        return result;
    }
    void Network::restore(const Weights& weights, const Optimizer* state) {
        bound_version.reset();
        device.synchronize();
        check(cudaMemcpy(parameters.data, weights.parameters.data(), parameters.bytes, cudaMemcpyHostToDevice));
        check(cudaMemcpy(running.data, weights.running.data(), running.bytes, cudaMemcpyHostToDevice));
        version = weights.version;
        if (state) {
            check(cudaMemcpy(first.data, state->first.data(), first.bytes, cudaMemcpyHostToDevice));
            check(cudaMemcpy(second.data, state->second.data(), second.bytes, cudaMemcpyHostToDevice));
            steps = state->steps;
        }
        kernels::convert(device.stream.get(), reduced.data, static_cast<float*>(parameters.data), parameter_count);
        device.synchronize();
    }
    void Network::update(const int batch) {
        const auto stream = device.stream.get();
        kernels::replay_batch(stream, input.data, static_cast<float*>(targets.data), static_cast<unsigned char*>(results.data), arguments.data, batch);
        check(cudaMemsetAsync(gradients.data, 0, gradients.bytes, stream));
        check(cudaMemsetAsync(metrics.data, 0, metrics.bytes, stream));
        forward(batch, true);
        kernels::loss(stream, policy.data, value.data, static_cast<float*>(targets.data), static_cast<unsigned char*>(results.data), policy_derivative.data, value_derivative.data, static_cast<float*>(metrics.data), batch);
        auto* weight   = static_cast<__nv_bfloat16*>(reduced.data);
        auto* gradient = static_cast<float*>(gradients.data);
        device.matrix({3, 128, batch, 1, 0, 1}, value_derivative.data, hidden.data, gradient + value_weight);
        device.matrix({batch, 128, 3, 0, 0, 0}, value_derivative.data, weight + value_weight, hidden_derivative.data);
        kernels::bias_backward(stream, gradient + value_bias, value_derivative.data, batch, 3);
        kernels::activation_backward(stream, hidden_derivative.data, hidden.data, batch * 128);
        device.matrix({128, 90, batch, 1, 0, 1}, hidden_derivative.data, layers[14].values.data, gradient + hidden_weight);
        device.matrix({batch, 90, 128, 0, 0, 0}, hidden_derivative.data, weight + hidden_weight, layers[14].derivative.data);
        kernels::bias_backward(stream, gradient + hidden_bias, hidden_derivative.data, batch, 128);
        device.matrix({action_count, action_count, batch, 1, 0, 1}, policy_derivative.data, layers[13].values.data, gradient + policy_weight);
        device.matrix({batch, action_count, action_count, 0, 0, 0}, policy_derivative.data, weight + policy_weight, layers[13].derivative.data);
        kernels::bias_backward(stream, gradient + policy_bias, policy_derivative.data, batch, action_count);
        backward(layers[14], layers[12].values.data, batch);
        backward(layers[13], layers[12].values.data, batch);
        kernels::add(stream, layers[13].input_derivative.data, layers[14].input_derivative.data, batch * 90 * 128);
        check(cudaMemcpyAsync(layers[12].derivative.data, layers[13].input_derivative.data, std::size_t(batch) * 90 * 128 * 2, cudaMemcpyDeviceToDevice, stream));
        for (int block = 5; block >= 0; --block) {
            auto& second_layer = layers[2 + block * 2];
            auto& first_layer  = layers[1 + block * 2];
            auto& previous     = layers[block * 2];
            kernels::activation_backward(stream, second_layer.derivative.data, second_layer.values.data, batch * 90 * 128);
            check(cudaMemcpyAsync(residual_derivative.data, second_layer.derivative.data, std::size_t(batch) * 90 * 128 * 2, cudaMemcpyDeviceToDevice, stream));
            backward(second_layer, first_layer.values.data, batch);
            check(cudaMemcpyAsync(first_layer.derivative.data, second_layer.input_derivative.data, std::size_t(batch) * 90 * 128 * 2, cudaMemcpyDeviceToDevice, stream));
            backward(first_layer, previous.values.data, batch);
            kernels::add(stream, first_layer.input_derivative.data, residual_derivative.data, batch * 90 * 128);
            check(cudaMemcpyAsync(previous.derivative.data, first_layer.input_derivative.data, std::size_t(batch) * 90 * 128 * 2, cudaMemcpyDeviceToDevice, stream));
        }
        backward(layers[0], input.data, batch);
        kernels::adam(stream, static_cast<float*>(parameters.data), reduced.data, gradient, static_cast<float*>(first.data), static_cast<float*>(second.data), static_cast<float*>(decay.data), static_cast<float*>(metrics.data), parameter_count, arguments.data);
    }
    void Network::forward(const int batch, const bool training) {
        const auto stream   = device.stream.get();
        auto* weight        = static_cast<__nv_bfloat16*>(training ? reduced.data : inference_reduced.data);
        auto* master        = static_cast<float*>(training ? parameters.data : inference_parameters.data);
        auto* normalization = static_cast<float*>(training ? running.data : inference_running.data);
        for (int index = 0; index < 15; ++index) {
            auto& layer  = layers[index];
            void* source = index == 0 ? input.data : layers[index < 13 ? index - 1 : 12].values.data;
            device.convolution(0, batch, layer.input, layer.output, layer.kernel, source, weight + layer.weight, layer.raw.data);
            kernels::activation(stream, layer.raw.data, master + layer.bias, batch * 90, layer.output, false);
            void* skip = index > 0 && index < 13 && index % 2 == 0 ? layers[index - 2].values.data : nullptr;
            kernels::normalization(stream, layer.values.data, layer.raw.data, master + layer.affine, normalization + layer.running, static_cast<float*>(layer.statistics.data), batch * 90, layer.output, training, skip);
        }
        device.matrix({batch, action_count, action_count, 0, 1, 0}, layers[13].values.data, weight + policy_weight, policy.data);
        kernels::activation(stream, policy.data, master + policy_bias, batch, action_count, false);
        device.matrix({batch, 128, 90, 0, 1, 0}, layers[14].values.data, weight + hidden_weight, hidden.data);
        kernels::activation(stream, hidden.data, master + hidden_bias, batch, 128, true);
        device.matrix({batch, 3, 128, 0, 1, 0}, hidden.data, weight + value_weight, value.data);
        kernels::activation(stream, value.data, master + value_bias, batch, 3, false);
    }
    void Network::backward(ConvLayer& layer, void* source, const int batch) {
        const auto stream = device.stream.get();
        auto* gradient    = static_cast<float*>(gradients.data);
        kernels::activation_backward(stream, layer.derivative.data, layer.values.data, batch * 90 * layer.output);
        kernels::normalization_backward(stream, layer.derivative.data, layer.raw.data, static_cast<float*>(parameters.data) + layer.affine, static_cast<float*>(layer.statistics.data), gradient + layer.affine, batch * 90, layer.output);
        kernels::bias_backward(stream, gradient + layer.bias, layer.derivative.data, batch * 90, layer.output);
        device.convolution(2, batch, layer.input, layer.output, layer.kernel, source, gradient + layer.weight, layer.derivative.data);
        if (layer.input_derivative.data) device.convolution(1, batch, layer.input, layer.output, layer.kernel, layer.input_derivative.data, static_cast<__nv_bfloat16*>(reduced.data) + layer.weight, layer.derivative.data);
    }
} // namespace chess::ai
