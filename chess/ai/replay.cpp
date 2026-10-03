module;
#include "kernels.h"
module chess.ai.replay;
import std;
namespace chess::ai {
    Replay::Replay(const std::size_t count) : records{count * sizeof(ReplayRecord)}, policies{count * 32 * sizeof(PolicyEntry)}, capacity{count}, policy_capacity{count * 32}, lengths(count) {
        check(cudaStreamCreateWithFlags(std::out_ptr(stream), cudaStreamNonBlocking));
    }
    void Replay::append(std::vector<Sample> completed) {
        generated += completed.size();
        const auto samples = std::span{completed}.last(std::min(completed.size(), capacity));
        const auto removed = size + samples.size() > capacity ? size + samples.size() - capacity : 0;
        for (std::size_t index = 0; index < removed; ++index) policy_begin += lengths[(cursor + capacity - size + index) % capacity];
        size -= removed;
        std::size_t entries{};
        for (const auto& sample : samples) entries += sample.policy.size();
        const auto used = policy_end - policy_begin;
        if (used + entries > policy_capacity) {
            const auto count = std::max(used + entries, policy_capacity * 2);
            Buffer expanded{count * sizeof(PolicyEntry)};
            const auto first = std::min(used, policy_capacity - policy_begin % policy_capacity);
            if (first) check(cudaMemcpyAsync(expanded.data, static_cast<PolicyEntry*>(policies.data) + policy_begin % policy_capacity, first * sizeof(PolicyEntry), cudaMemcpyDeviceToDevice, stream.get()));
            if (used > first) check(cudaMemcpyAsync(static_cast<PolicyEntry*>(expanded.data) + first, policies.data, (used - first) * sizeof(PolicyEntry), cudaMemcpyDeviceToDevice, stream.get()));
            kernels::rebase_replay(stream.get(), records.data, capacity, cursor, size, policy_begin);
            check(cudaStreamSynchronize(stream.get()));
            policies        = std::move(expanded);
            policy_capacity = count;
            policy_begin    = 0;
            policy_end      = used;
        }
        const auto bytes = samples.size() * sizeof(ReplayRecord) + entries * sizeof(PolicyEntry);
        if (staging.bytes < bytes) staging = HostBuffer{bytes};
        auto* packed = static_cast<ReplayRecord*>(staging.data);
        auto* policy = reinterpret_cast<PolicyEntry*>(packed + samples.size());
        std::size_t written{};
        for (std::size_t index = 0; index < samples.size(); ++index) {
            const auto& sample = samples[index];
            packed[index]      = {sample.observation, policy_end + written, sample.version, std::uint32_t(sample.policy.size()), sample.result};
            std::ranges::copy(sample.policy, policy + written);
            written += sample.policy.size();
            lengths[(cursor + index) % capacity] = std::uint32_t(sample.policy.size());
        }
        const auto first_records = std::min(samples.size(), capacity - cursor);
        if (first_records) check(cudaMemcpyAsync(static_cast<ReplayRecord*>(records.data) + cursor, packed, first_records * sizeof(ReplayRecord), cudaMemcpyHostToDevice, stream.get()));
        if (samples.size() > first_records) check(cudaMemcpyAsync(records.data, packed + first_records, (samples.size() - first_records) * sizeof(ReplayRecord), cudaMemcpyHostToDevice, stream.get()));
        const auto first_policy = std::min(entries, policy_capacity - policy_end % policy_capacity);
        if (first_policy) check(cudaMemcpyAsync(static_cast<PolicyEntry*>(policies.data) + policy_end % policy_capacity, policy, first_policy * sizeof(PolicyEntry), cudaMemcpyHostToDevice, stream.get()));
        if (entries > first_policy) check(cudaMemcpyAsync(policies.data, policy + first_policy, (entries - first_policy) * sizeof(PolicyEntry), cudaMemcpyHostToDevice, stream.get()));
        check(cudaStreamSynchronize(stream.get()));
        cursor = (cursor + samples.size()) % capacity;
        size += samples.size();
        policy_end += entries;
    }
    std::vector<Sample> Replay::download() const {
        std::vector<ReplayRecord> packed(size);
        const auto used = policy_end - policy_begin;
        std::vector<PolicyEntry> policy(used);
        const auto oldest        = (cursor + capacity - size) % capacity;
        const auto first_records = std::min(size, capacity - oldest);
        if (first_records) check(cudaMemcpy(packed.data(), static_cast<ReplayRecord*>(records.data) + oldest, first_records * sizeof(ReplayRecord), cudaMemcpyDeviceToHost));
        if (size > first_records) check(cudaMemcpy(packed.data() + first_records, records.data, (size - first_records) * sizeof(ReplayRecord), cudaMemcpyDeviceToHost));
        const auto first_policy = std::min(used, policy_capacity - policy_begin % policy_capacity);
        if (first_policy) check(cudaMemcpy(policy.data(), static_cast<PolicyEntry*>(policies.data) + policy_begin % policy_capacity, first_policy * sizeof(PolicyEntry), cudaMemcpyDeviceToHost));
        if (used > first_policy) check(cudaMemcpy(policy.data() + first_policy, policies.data, (used - first_policy) * sizeof(PolicyEntry), cudaMemcpyDeviceToHost));
        std::vector<Sample> result(size);
        for (std::size_t index = 0; index < size; ++index) {
            const auto& record = packed[index];
            const auto first   = policy.begin() + std::ptrdiff_t(record.offset - policy_begin);
            result[index]      = {record.observation, std::vector<PolicyEntry>{first, first + record.count}, record.version, record.result};
        }
        return result;
    }
} // namespace chess::ai
