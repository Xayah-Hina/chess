module;
#include <cublasLt.h>
#include <cuda_runtime.h>
#include <cudnn_frontend.h>
export module chess.ai.device;
import std;

export namespace chess::ai {
    void check(cudaError_t status);
    void check(cublasStatus_t status);
    void check(cudnnStatus_t status);
    void check(cudnn_frontend::error_t status);
    struct Buffer final {
        void* data{};
        std::size_t bytes{};
        Buffer() = default;
        explicit Buffer(std::size_t size);
        ~Buffer();
        Buffer(Buffer&& other) noexcept;
        Buffer& operator=(Buffer&& other) noexcept;
        Buffer(const Buffer&)            = delete;
        Buffer& operator=(const Buffer&) = delete;
    };
    struct HostBuffer final {
        void* data{};
        std::size_t bytes{};
        HostBuffer() = default;
        explicit HostBuffer(std::size_t size);
        ~HostBuffer();
        HostBuffer(HostBuffer&& other) noexcept;
        HostBuffer& operator=(HostBuffer&& other) noexcept;
        HostBuffer(const HostBuffer&)            = delete;
        HostBuffer& operator=(const HostBuffer&) = delete;
    };
    struct ConvPlan final {
        std::array<int, 5> shape;
        cudnn_frontend::graph::Graph graph;
        ConvPlan(std::array<int, 5> dimensions, cudnnHandle_t handle);
    };
    struct MatrixPlan final {
        std::array<int, 6> shape;
        std::unique_ptr<std::remove_pointer_t<cublasLtMatmulDesc_t>, decltype(&cublasLtMatmulDescDestroy)> operation{nullptr, cublasLtMatmulDescDestroy};
        std::unique_ptr<std::remove_pointer_t<cublasLtMatrixLayout_t>, decltype(&cublasLtMatrixLayoutDestroy)> a{nullptr, cublasLtMatrixLayoutDestroy}, b{nullptr, cublasLtMatrixLayoutDestroy}, output{nullptr, cublasLtMatrixLayoutDestroy};
        cublasLtMatmulAlgo_t algorithm{};
        MatrixPlan(std::array<int, 6> dimensions, cublasLtHandle_t handle);
    };
    struct Device final {
        std::unique_ptr<std::remove_pointer_t<cudaStream_t>, decltype(&cudaStreamDestroy)> stream{nullptr, cudaStreamDestroy};
        std::unique_ptr<std::remove_pointer_t<cudnnHandle_t>, decltype(&cudnnDestroy)> dnn{nullptr, cudnnDestroy};
        std::unique_ptr<std::remove_pointer_t<cublasLtHandle_t>, decltype(&cublasLtDestroy)> blas{nullptr, cublasLtDestroy};
        Buffer workspace{256uz << 20};
        Buffer input_fp32, derivative_fp32;
        std::vector<std::unique_ptr<ConvPlan>> convolutions;
        std::vector<std::unique_ptr<MatrixPlan>> matrices;
        Device();
        void convolution(int mode, int batch, int input, int output, int kernel, void* x, void* weight, void* y);
        void matrix(std::array<int, 6> shape, void* a, void* b, void* output);
        void synchronize();
    };
} // namespace chess::ai
