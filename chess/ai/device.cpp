module;
#include "kernels.h"
#include <cublasLt.h>
#include <cuda_runtime.h>
#include <cudnn_frontend.h>
module chess.ai.device;
import std;

namespace chess::ai {
    void check(const cudaError_t status) {
        if (status != cudaSuccess) throw std::runtime_error{cudaGetErrorString(status)};
    }
    void check(const cublasStatus_t status) {
        if (status != CUBLAS_STATUS_SUCCESS) throw std::runtime_error{std::format("cuBLASLt status {}", int(status))};
    }
    void check(const cudnnStatus_t status) {
        if (status != CUDNN_STATUS_SUCCESS) throw std::runtime_error{cudnnGetErrorString(status)};
    }
    void check(cudnn_frontend::error_t status) {
        if (status.is_bad()) throw std::runtime_error{status.get_message()};
    }
    Buffer::Buffer(const std::size_t size) : bytes{size} {
        if (bytes) check(cudaMalloc(&data, bytes));
    }
    Buffer::~Buffer() {
        if (data) cudaFree(data);
    }
    Buffer::Buffer(Buffer&& other) noexcept : data{std::exchange(other.data, nullptr)}, bytes{std::exchange(other.bytes, 0)} {}
    Buffer& Buffer::operator=(Buffer&& other) noexcept {
        if (data) cudaFree(data);
        data  = std::exchange(other.data, nullptr);
        bytes = std::exchange(other.bytes, 0);
        return *this;
    }
    HostBuffer::HostBuffer(const std::size_t size) : bytes{size} {
        if (bytes) check(cudaMallocHost(&data, bytes));
    }
    HostBuffer::~HostBuffer() {
        if (data) cudaFreeHost(data);
    }
    HostBuffer::HostBuffer(HostBuffer&& other) noexcept : data{std::exchange(other.data, nullptr)}, bytes{std::exchange(other.bytes, 0)} {}
    HostBuffer& HostBuffer::operator=(HostBuffer&& other) noexcept {
        if (data) cudaFreeHost(data);
        data  = std::exchange(other.data, nullptr);
        bytes = std::exchange(other.bytes, 0);
        return *this;
    }
    ConvPlan::ConvPlan(const std::array<int, 5> dimensions, const cudnnHandle_t handle) : shape{dimensions} {
        const auto [mode, batch, input, output, kernel] = shape;
        graph.set_io_data_type(cudnn_frontend::DataType_t::BFLOAT16).set_intermediate_data_type(cudnn_frontend::DataType_t::FLOAT).set_compute_data_type(cudnn_frontend::DataType_t::FLOAT);
        auto x            = mode != 1 ? graph.tensor(cudnn_frontend::graph::Tensor_attributes{}.set_uid(1).set_dim({batch, input, 10, 9}).set_stride({90LL * input, 1, 9LL * input, input})) : nullptr;
        auto w            = mode != 2 ? graph.tensor(cudnn_frontend::graph::Tensor_attributes{}.set_uid(2).set_dim({output, input, kernel, kernel}).set_stride({1LL * kernel * kernel * input, 1, 1LL * kernel * input, input})) : nullptr;
        auto y            = mode != 0 ? graph.tensor(cudnn_frontend::graph::Tensor_attributes{}.set_uid(3).set_dim({batch, output, 10, 9}).set_stride({90LL * output, 1, 9LL * output, output})) : nullptr;
        const int padding = kernel / 2;
        if (mode == 0) {
            auto result = graph.conv_fprop(x, w, cudnn_frontend::graph::Conv_fprop_attributes{}.set_padding({padding, padding}).set_stride({1, 1}).set_dilation({1, 1}));
            result->set_output(true).set_uid(3).set_dim({batch, output, 10, 9}).set_stride({90LL * output, 1, 9LL * output, output});
        } else if (mode == 1) {
            auto result = graph.conv_dgrad(y, w, cudnn_frontend::graph::Conv_dgrad_attributes{}.set_padding({padding, padding}).set_stride({1, 1}).set_dilation({1, 1}));
            result->set_output(true).set_uid(1).set_dim({batch, input, 10, 9}).set_stride({90LL * input, 1, 9LL * input, input});
        } else {
            x->set_data_type(cudnn_frontend::DataType_t::FLOAT);
            y->set_data_type(cudnn_frontend::DataType_t::FLOAT);
            auto result = graph.conv_wgrad(y, x, cudnn_frontend::graph::Conv_wgrad_attributes{}.set_padding({padding, padding}).set_stride({1, 1}).set_dilation({1, 1}));
            result->set_output(true).set_uid(2).set_dim({output, input, kernel, kernel}).set_stride({1LL * kernel * kernel * input, 1, 1LL * kernel * input, input}).set_data_type(cudnn_frontend::DataType_t::FLOAT);
        }
        check(graph.validate());
        check(graph.build_operation_graph(handle));
        check(graph.create_execution_plans({cudnn_frontend::HeurMode_t::A}));
        graph.deselect_numeric_notes({cudnn_frontend::NumericalNote_t::NONDETERMINISTIC});
        graph.deselect_workspace_greater_than(256uz << 20);
        check(graph.check_support(handle));
        check(graph.build_plans(cudnn_frontend::BuildPlanPolicy_t::HEURISTICS_CHOICE));
    }
    MatrixPlan::MatrixPlan(const std::array<int, 6> dimensions, const cublasLtHandle_t handle) : shape{dimensions} {
        const auto [m, n, k, transpose_a, transpose_b, fp32] = shape;
        check(cublasLtMatmulDescCreate(std::out_ptr(operation), CUBLAS_COMPUTE_32F, CUDA_R_32F));
        const cublasOperation_t ta = transpose_a ? CUBLAS_OP_T : CUBLAS_OP_N, tb = transpose_b ? CUBLAS_OP_T : CUBLAS_OP_N;
        check(cublasLtMatmulDescSetAttribute(operation.get(), CUBLASLT_MATMUL_DESC_TRANSA, &ta, sizeof(ta)));
        check(cublasLtMatmulDescSetAttribute(operation.get(), CUBLASLT_MATMUL_DESC_TRANSB, &tb, sizeof(tb)));
        check(cublasLtMatrixLayoutCreate(std::out_ptr(a), CUDA_R_16BF, transpose_a ? k : m, transpose_a ? m : k, transpose_a ? m : k));
        check(cublasLtMatrixLayoutCreate(std::out_ptr(b), CUDA_R_16BF, transpose_b ? n : k, transpose_b ? k : n, transpose_b ? k : n));
        check(cublasLtMatrixLayoutCreate(std::out_ptr(output), fp32 ? CUDA_R_32F : CUDA_R_16BF, m, n, n));
        const cublasLtOrder_t order = CUBLASLT_ORDER_ROW;
        for (const auto layout : {a.get(), b.get(), output.get()}) check(cublasLtMatrixLayoutSetAttribute(layout, CUBLASLT_MATRIX_LAYOUT_ORDER, &order, sizeof(order)));
        std::unique_ptr<std::remove_pointer_t<cublasLtMatmulPreference_t>, decltype(&cublasLtMatmulPreferenceDestroy)> preference{nullptr, cublasLtMatmulPreferenceDestroy};
        check(cublasLtMatmulPreferenceCreate(std::out_ptr(preference)));
        const std::size_t workspace = 256uz << 20;
        check(cublasLtMatmulPreferenceSetAttribute(preference.get(), CUBLASLT_MATMUL_PREF_MAX_WORKSPACE_BYTES, &workspace, sizeof(workspace)));
        cublasLtMatmulHeuristicResult_t result{};
        int count{};
        check(cublasLtMatmulAlgoGetHeuristic(handle, operation.get(), a.get(), b.get(), output.get(), output.get(), preference.get(), 1, &result, &count));
        if (!count) throw std::runtime_error{"No cuBLASLt algorithm for the network matrix"};
        algorithm = result.algo;
    }
    Device::Device() {
        check(cudaStreamCreateWithFlags(std::out_ptr(stream), cudaStreamNonBlocking));
        check(cudnnCreate(std::out_ptr(dnn)));
        check(cudnnSetStream(dnn.get(), stream.get()));
        check(cublasLtCreate(std::out_ptr(blas)));
    }
    void Device::convolution(const int mode, const int batch, const int input, const int output, const int kernel, void* x, void* weight, void* y) {
        if (mode == 2) {
            const auto input_count = std::size_t(batch) * 90 * input, derivative_count = std::size_t(batch) * 90 * output;
            if (input_fp32.bytes < input_count * 4) input_fp32 = Buffer{input_count * 4};
            if (derivative_fp32.bytes < derivative_count * 4) derivative_fp32 = Buffer{derivative_count * 4};
            kernels::expand_precision(stream.get(), static_cast<float*>(input_fp32.data), x, input_count);
            kernels::expand_precision(stream.get(), static_cast<float*>(derivative_fp32.data), y, derivative_count);
            x = input_fp32.data;
            y = derivative_fp32.data;
        }
        const std::array shape{mode, batch, input, output, kernel};
        auto plan = std::ranges::find_if(convolutions, [&](const auto& value) { return value->shape == shape; });
        if (plan == convolutions.end()) {
            convolutions.push_back(std::make_unique<ConvPlan>(shape, dnn.get()));
            plan = std::prev(convolutions.end());
        }
        std::unordered_map<std::int64_t, void*> tensors{{1, x}, {2, weight}, {3, y}};
        check((*plan)->graph.execute(dnn.get(), tensors, workspace.data));
    }
    void Device::matrix(const std::array<int, 6> shape, void* a, void* b, void* output) {
        auto plan = std::ranges::find_if(matrices, [&](const auto& value) { return value->shape == shape; });
        if (plan == matrices.end()) {
            matrices.push_back(std::make_unique<MatrixPlan>(shape, blas.get()));
            plan = std::prev(matrices.end());
        }
        constexpr float alpha = 1, beta = 0;
        check(cublasLtMatmul(blas.get(), (*plan)->operation.get(), &alpha, a, (*plan)->a.get(), b, (*plan)->b.get(), &beta, output, (*plan)->output.get(), output, (*plan)->output.get(), &(*plan)->algorithm, workspace.data, workspace.bytes, stream.get()));
    }
    void Device::synchronize() {
        check(cudaStreamSynchronize(stream.get()));
    }
} // namespace chess::ai
