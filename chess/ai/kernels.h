#ifndef CHESS_AI_KERNELS_H
#define CHESS_AI_KERNELS_H
#include <cstddef>
#include <cstdint>
#include <cuda_runtime.h>

namespace chess::ai::kernels {
    void convert(cudaStream_t stream, void* output, const float* input, std::size_t count);
    void expand_precision(cudaStream_t stream, float* output, const void* input, std::size_t count);
    void expand_observations(cudaStream_t stream, void* output, const void* observations, int batch);
    void replay_batch(cudaStream_t stream, void* input, float* targets, unsigned char* results, const void* arguments, int batch);
    void rebase_replay(cudaStream_t stream, void* records, std::size_t capacity, std::size_t cursor, std::size_t size, std::size_t offset);
    void normalization(cudaStream_t stream, void* output, const void* input, const float* parameters, float* running, float* statistics, int rows, int channels, bool training, const void* residual);
    void normalization_backward(cudaStream_t stream, void* gradient, const void* input, const float* parameters, const float* statistics, float* parameter_gradient, int rows, int channels);
    void activation(cudaStream_t stream, void* values, const float* bias, int rows, int channels, bool relu);
    void activation_backward(cudaStream_t stream, void* gradient, const void* output, int count);
    void bias_backward(cudaStream_t stream, float* bias_gradient, const void* gradient, int rows, int channels);
    void add(cudaStream_t stream, void* output, const void* other, int count);
    void loss(cudaStream_t stream, const void* policy, const void* value, const float* targets, const unsigned char* results, void* policy_gradient, void* value_gradient, float* metrics, int batch);
    void adam(cudaStream_t stream, float* parameters, void* reduced, const float* gradient, float* first, float* second, const float* decay, float* metrics, std::size_t count, const void* arguments);
} // namespace chess::ai::kernels
#endif
