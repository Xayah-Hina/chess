module;
#include <cuda_runtime.h>
export module chess.ai.replay;
export import chess.ai.encoding;
export import chess.ai.device;
import std;

export namespace chess::ai {
    struct Replay final {
        Buffer records, policies;
        std::uint64_t generated{};
        std::size_t capacity, size{}, cursor{}, policy_capacity, policy_begin{}, policy_end{};
        explicit Replay(std::size_t capacity = 500000);
        void append(std::vector<Sample> completed);
        std::vector<Sample> download() const;

    private:
        std::vector<std::uint32_t> lengths;
        HostBuffer staging;
        std::unique_ptr<std::remove_pointer_t<cudaStream_t>, decltype(&cudaStreamDestroy)> stream{nullptr, cudaStreamDestroy};
    };
} // namespace chess::ai
