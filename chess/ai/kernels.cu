#include "kernels.h"
#include "tensors.h"
#include <cmath>
#include <cuda_bf16.h>

namespace chess::ai::kernels {
    namespace {
        __global__ void convert_kernel(__nv_bfloat16* output, const float* input, const std::size_t count) {
            const std::size_t index = std::size_t(blockIdx.x) * blockDim.x + threadIdx.x;
            if (index < count) output[index] = __float2bfloat16(input[index]);
        }
        __global__ void expand_precision_kernel(float* output, const __nv_bfloat16* input, const std::size_t count) {
            const std::size_t index = std::size_t(blockIdx.x) * blockDim.x + threadIdx.x;
            if (index < count) output[index] = __bfloat162float(input[index]);
        }
        __device__ unsigned long long draw(unsigned long long& state) {
            state += 0x9e3779b97f4a7c15ULL;
            auto value = state;
            value      = (value ^ (value >> 30)) * 0xbf58476d1ce4e5b9ULL;
            value      = (value ^ (value >> 27)) * 0x94d049bb133111ebULL;
            return value ^ (value >> 31);
        }
        __device__ float observation_value(const Observation& observation, const int index, const bool reflected) {
            const int square = index / input_channels, plane = index % input_channels;
            const int source = reflected ? square / 9 * 9 + 8 - square % 9 : square;
            if (plane < 112) return observation.boards[plane / 14 * 90 + source] == plane % 14 + 1 ? 1.0F : 0.0F;
            if (plane < 112 + rule_channels) return observation.rules[plane - 112];
            return 0.0F;
        }
        __global__ void expand_observations_kernel(__nv_bfloat16* output, const Observation* observations, const int count) {
            const int index = blockIdx.x * blockDim.x + threadIdx.x;
            if (index < count) output[index] = __float2bfloat16(observation_value(observations[index / (90 * input_channels)], index % (90 * input_channels), false));
        }
        __global__ void replay_batch_kernel(__nv_bfloat16* input, float* targets, unsigned char* results, const TrainingArguments* arguments) {
            __shared__ const ReplayRecord* record;
            __shared__ bool reflected;
            const int sample = blockIdx.x;
            if (threadIdx.x == 0) {
                unsigned long long state = arguments->seed ^ (0xd2b74407b1ce6e93ULL * (sample + 1));
                auto bits                = draw(state);
                const auto size          = static_cast<unsigned long long>(arguments->size);
                const auto threshold     = (0ULL - size) % size;
                while (bits * size < threshold) bits = draw(state);
                const auto selected = __umul64hi(bits, size);
                const auto slot     = (arguments->cursor + arguments->capacity - size + selected) % arguments->capacity;
                record              = arguments->records + slot;
                reflected           = (draw(state) & 1) != 0;
                results[sample]     = record->result;
            }
            __syncthreads();
            for (int index = threadIdx.x; index < 90 * input_channels; index += blockDim.x) input[sample * 90 * input_channels + index] = __float2bfloat16(observation_value(record->observation, index, reflected));
            for (int index = threadIdx.x; index < action_count; index += blockDim.x) targets[sample * action_count + index] = 0.0F;
            __syncthreads();
            for (std::uint32_t index = threadIdx.x; index < record->count; index += blockDim.x) {
                const auto entry = arguments->policies[(record->offset + index) % arguments->policy_capacity];
                int action       = entry.action;
                if (reflected) {
                    const int square = action / 50, displacement = action % 50;
                    const int mirrored = displacement < 16 ? displacement ^ 1 : displacement < 34 ? displacement : 34 + ((displacement - 34) ^ 2);
                    action             = (square / 9 * 9 + 8 - square % 9) * 50 + mirrored;
                }
                targets[sample * action_count + action] = entry.probability;
            }
        }
        __global__ void rebase_replay_kernel(ReplayRecord* records, const std::size_t capacity, const std::size_t cursor, const std::size_t size, const std::size_t offset) {
            const auto index = std::size_t(blockIdx.x) * blockDim.x + threadIdx.x;
            if (index < size) records[(cursor + capacity - size + index) % capacity].offset -= offset;
        }
        __device__ float sum(float value, float* shared) {
            shared[threadIdx.x] = value;
            __syncthreads();
            for (int width = blockDim.x / 2; width; width /= 2) {
                if (threadIdx.x < width) shared[threadIdx.x] += shared[threadIdx.x + width];
                __syncthreads();
            }
            return shared[0];
        }
        __global__ void normalization_inference_kernel(__nv_bfloat16* output, const __nv_bfloat16* input, const float* parameters, const float* running, float* statistics, const int count, const int channels, const __nv_bfloat16* residual) {
            const int index = blockIdx.x * blockDim.x + threadIdx.x;
            if (index >= count) return;
            const int channel   = index % channels;
            const float average = running[channel], inverse = rsqrtf(running[channels + channel] + 1e-5F);
            if (index < channels) {
                statistics[channel]            = average;
                statistics[channels + channel] = inverse;
            }
            float value = (__bfloat162float(input[index]) - average) * inverse * parameters[channel] + parameters[channels + channel];
            if (residual) value += __bfloat162float(residual[index]);
            output[index] = __float2bfloat16(fmaxf(value, 0.0F));
        }
        __global__ void normalization_kernel(__nv_bfloat16* output, const __nv_bfloat16* input, const float* parameters, float* running, float* statistics, const int rows, const int channels, const __nv_bfloat16* residual) {
            __shared__ float shared[256];
            const int channel = blockIdx.x;
            float total{};
            for (int row = threadIdx.x; row < rows; row += blockDim.x) total += __bfloat162float(input[row * channels + channel]);
            const float average = sum(total, shared) / rows;
            __syncthreads();
            float squares{};
            for (int row = threadIdx.x; row < rows; row += blockDim.x) {
                const float difference = __bfloat162float(input[row * channels + channel]) - average;
                squares += difference * difference;
            }
            const float variance = sum(squares, shared) / rows;
            const float inverse  = rsqrtf(variance + 1e-5F);
            if (threadIdx.x == 0) {
                statistics[channel]            = average;
                statistics[channels + channel] = inverse;
                running[channel]               = 0.1F * average + 0.9F * running[channel];
                running[channels + channel]    = 0.1F * variance * rows / (rows - 1) + 0.9F * running[channels + channel];
            }
            for (int row = threadIdx.x; row < rows; row += blockDim.x) {
                const int index = row * channels + channel;
                float value     = (__bfloat162float(input[index]) - average) * inverse * parameters[channel] + parameters[channels + channel];
                if (residual) value += __bfloat162float(residual[index]);
                output[index] = __float2bfloat16(fmaxf(value, 0.0F));
            }
        }
        __global__ void normalization_backward_kernel(__nv_bfloat16* gradient, const __nv_bfloat16* input, const float* parameters, const float* statistics, float* parameter_gradient, const int rows, const int channels) {
            __shared__ float shared[256];
            const int channel   = blockIdx.x;
            const float average = statistics[channel], inverse = statistics[channels + channel];
            float beta{}, gamma{};
            for (int row = threadIdx.x; row < rows; row += blockDim.x) {
                const int index        = row * channels + channel;
                const float derivative = __bfloat162float(gradient[index]);
                beta += derivative;
                gamma += derivative * (__bfloat162float(input[index]) - average) * inverse;
            }
            beta = sum(beta, shared);
            __syncthreads();
            gamma = sum(gamma, shared);
            if (threadIdx.x == 0) {
                parameter_gradient[channel]            = gamma;
                parameter_gradient[channels + channel] = beta;
            }
            for (int row = threadIdx.x; row < rows; row += blockDim.x) {
                const int index        = row * channels + channel;
                const float normalized = (__bfloat162float(input[index]) - average) * inverse;
                gradient[index]        = __float2bfloat16(parameters[channel] * inverse * (__bfloat162float(gradient[index]) - (beta + normalized * gamma) / rows));
            }
        }
        __global__ void activation_kernel(__nv_bfloat16* values, const float* bias, const int count, const int channels, const bool relu) {
            const int index = blockIdx.x * blockDim.x + threadIdx.x;
            if (index >= count) return;
            const float value = __bfloat162float(values[index]) + bias[index % channels];
            values[index]     = __float2bfloat16(relu ? fmaxf(value, 0.0F) : value);
        }
        __global__ void activation_backward_kernel(__nv_bfloat16* gradient, const __nv_bfloat16* output, const int count) {
            const int index = blockIdx.x * blockDim.x + threadIdx.x;
            if (index < count && __bfloat162float(output[index]) <= 0) gradient[index] = __float2bfloat16(0.0F);
        }
        __global__ void bias_backward_kernel(float* bias_gradient, const __nv_bfloat16* gradient, const int rows, const int channels) {
            __shared__ float shared[256];
            float total{};
            for (int row = threadIdx.x; row < rows; row += blockDim.x) total += __bfloat162float(gradient[row * channels + blockIdx.x]);
            total = sum(total, shared);
            if (threadIdx.x == 0) bias_gradient[blockIdx.x] = total;
        }
        __global__ void add_kernel(__nv_bfloat16* output, const __nv_bfloat16* other, const int count) {
            const int index = blockIdx.x * blockDim.x + threadIdx.x;
            if (index < count) output[index] = __float2bfloat16(__bfloat162float(output[index]) + __bfloat162float(other[index]));
        }
        __global__ void loss_kernel(const __nv_bfloat16* policy, const __nv_bfloat16* value, const float* targets, const unsigned char* results, __nv_bfloat16* policy_gradient, __nv_bfloat16* value_gradient, float* metrics, const int batch) {
            __shared__ float shared[256];
            const int sample = blockIdx.x, offset = sample * 4500;
            float maximum = -INFINITY;
            for (int action = threadIdx.x; action < 4500; action += blockDim.x) maximum = fmaxf(maximum, __bfloat162float(policy[offset + action]));
            shared[threadIdx.x] = maximum;
            __syncthreads();
            for (int width = blockDim.x / 2; width; width /= 2) {
                if (threadIdx.x < width) shared[threadIdx.x] = fmaxf(shared[threadIdx.x], shared[threadIdx.x + width]);
                __syncthreads();
            }
            maximum = shared[0];
            __syncthreads();
            float denominator{};
            for (int action = threadIdx.x; action < 4500; action += blockDim.x) denominator += expf(__bfloat162float(policy[offset + action]) - maximum);
            denominator = sum(denominator, shared);
            __syncthreads();
            float cross_entropy{};
            for (int action = threadIdx.x; action < 4500; action += blockDim.x) {
                const float target = targets[offset + action];
                const float log_probability = __bfloat162float(policy[offset + action]) - maximum - logf(denominator);
                const float derivative = expf(log_probability) - target;
                if (target > 0) cross_entropy += target * (logf(target) - log_probability);
                policy_gradient[offset + action] = __float2bfloat16(derivative / batch);
            }
            cross_entropy = sum(cross_entropy, shared);
            if (threadIdx.x == 0) {
                atomicAdd(metrics, cross_entropy / batch);
                float logits[3], total{};
                for (int category = 0; category < 3; ++category) logits[category] = __bfloat162float(value[sample * 3 + category]);
                const float largest = fmaxf(logits[0], fmaxf(logits[1], logits[2]));
                for (int category = 0; category < 3; ++category) total += expf(logits[category] - largest);
                for (int category = 0; category < 3; ++category) value_gradient[sample * 3 + category] = __float2bfloat16((expf(logits[category] - largest) / total - (results[sample] == category)) / batch);
                atomicAdd(metrics + 1, (largest + logf(total) - logits[results[sample]]) / batch);
            }
        }
        __global__ void adam_kernel(float* parameters, __nv_bfloat16* reduced, const float* gradient, float* first, float* second, const float* decay, float* metrics, const std::size_t count, const TrainingArguments* arguments) {
            const std::size_t index = std::size_t(blockIdx.x) * blockDim.x + threadIdx.x;
            float square{}, change{};
            if (index < count) {
                const float g      = gradient[index];
                first[index]       = 0.9F * first[index] + 0.1F * g;
                second[index]      = 0.999F * second[index] + 0.001F * g * g;
                const float update = arguments->learning_rate * (first[index] / arguments->correction1 / (sqrtf(second[index] / arguments->correction2) + 1e-8F) + arguments->weight_decay * decay[index] * parameters[index]);
                parameters[index] -= update;
                reduced[index] = __float2bfloat16(parameters[index]);
                square         = g * g;
                change         = update * update;
            }
            __shared__ float shared[256];
            square = sum(square, shared);
            __syncthreads();
            change = sum(change, shared);
            if (threadIdx.x == 0) {
                atomicAdd(metrics + 2, square);
                atomicAdd(metrics + 3, change);
            }
        }
    } // namespace
    void convert(const cudaStream_t stream, void* output, const float* input, const std::size_t count) {
        convert_kernel<<<unsigned((count + 255) / 256), 256, 0, stream>>>(static_cast<__nv_bfloat16*>(output), input, count);
    }
    void expand_precision(const cudaStream_t stream, float* output, const void* input, const std::size_t count) {
        expand_precision_kernel<<<unsigned((count + 255) / 256), 256, 0, stream>>>(output, static_cast<const __nv_bfloat16*>(input), count);
    }
    void expand_observations(const cudaStream_t stream, void* output, const void* observations, const int batch) {
        const int count = batch * 90 * input_channels;
        expand_observations_kernel<<<(count + 255) / 256, 256, 0, stream>>>(static_cast<__nv_bfloat16*>(output), static_cast<const Observation*>(observations), count);
    }
    void replay_batch(const cudaStream_t stream, void* input, float* targets, unsigned char* results, const void* arguments, const int batch) {
        replay_batch_kernel<<<batch, 256, 0, stream>>>(static_cast<__nv_bfloat16*>(input), targets, results, static_cast<const TrainingArguments*>(arguments));
    }
    void rebase_replay(const cudaStream_t stream, void* records, const std::size_t capacity, const std::size_t cursor, const std::size_t size, const std::size_t offset) {
        if (size) rebase_replay_kernel<<<(size + 255) / 256, 256, 0, stream>>>(static_cast<ReplayRecord*>(records), capacity, cursor, size, offset);
    }
    void normalization(const cudaStream_t stream, void* output, const void* input, const float* parameters, float* running, float* statistics, const int rows, const int channels, const bool training, const void* residual) {
        if (training) normalization_kernel<<<channels, 256, 0, stream>>>(static_cast<__nv_bfloat16*>(output), static_cast<const __nv_bfloat16*>(input), parameters, running, statistics, rows, channels, static_cast<const __nv_bfloat16*>(residual));
        else normalization_inference_kernel<<<(rows * channels + 255) / 256, 256, 0, stream>>>(static_cast<__nv_bfloat16*>(output), static_cast<const __nv_bfloat16*>(input), parameters, running, statistics, rows * channels, channels, static_cast<const __nv_bfloat16*>(residual));
    }
    void normalization_backward(const cudaStream_t stream, void* gradient, const void* input, const float* parameters, const float* statistics, float* parameter_gradient, const int rows, const int channels) {
        normalization_backward_kernel<<<channels, 256, 0, stream>>>(static_cast<__nv_bfloat16*>(gradient), static_cast<const __nv_bfloat16*>(input), parameters, statistics, parameter_gradient, rows, channels);
    }
    void activation(const cudaStream_t stream, void* values, const float* bias, const int rows, const int channels, const bool relu) {
        activation_kernel<<<(rows * channels + 255) / 256, 256, 0, stream>>>(static_cast<__nv_bfloat16*>(values), bias, rows * channels, channels, relu);
    }
    void activation_backward(const cudaStream_t stream, void* gradient, const void* output, const int count) {
        activation_backward_kernel<<<(count + 255) / 256, 256, 0, stream>>>(static_cast<__nv_bfloat16*>(gradient), static_cast<const __nv_bfloat16*>(output), count);
    }
    void bias_backward(const cudaStream_t stream, float* bias_gradient, const void* gradient, const int rows, const int channels) {
        bias_backward_kernel<<<channels, 256, 0, stream>>>(bias_gradient, static_cast<const __nv_bfloat16*>(gradient), rows, channels);
    }
    void add(const cudaStream_t stream, void* output, const void* other, const int count) {
        add_kernel<<<(count + 255) / 256, 256, 0, stream>>>(static_cast<__nv_bfloat16*>(output), static_cast<const __nv_bfloat16*>(other), count);
    }
    void loss(const cudaStream_t stream, const void* policy, const void* value, const float* targets, const unsigned char* results, void* policy_gradient, void* value_gradient, float* metrics, const int batch) {
        loss_kernel<<<batch, 256, 0, stream>>>(static_cast<const __nv_bfloat16*>(policy), static_cast<const __nv_bfloat16*>(value), targets, results, static_cast<__nv_bfloat16*>(policy_gradient), static_cast<__nv_bfloat16*>(value_gradient), metrics, batch);
    }
    void adam(const cudaStream_t stream, float* parameters, void* reduced, const float* gradient, float* first, float* second, const float* decay, float* metrics, const std::size_t count, const void* arguments) {
        adam_kernel<<<unsigned((count + 255) / 256), 256, 0, stream>>>(parameters, static_cast<__nv_bfloat16*>(reduced), gradient, first, second, decay, metrics, count, static_cast<const TrainingArguments*>(arguments));
    }
} // namespace chess::ai::kernels
