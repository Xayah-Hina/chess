export module chess.ai.archive;
import std;

export namespace chess::ai {
    struct Archive final {
        std::fstream file;
        bool reading;
        Archive(const std::filesystem::path& path, bool read, std::uint32_t kind);
        template <class T>
        void pod(T& value) {
            if (reading) file.read(reinterpret_cast<char*>(&value), sizeof(T));
            else file.write(reinterpret_cast<const char*>(&value), sizeof(T));
        }
        template <class T>
        void sequence(T& values) {
            std::uint64_t count = values.size();
            pod(count);
            if (reading) values.resize(count);
            if constexpr (std::ranges::contiguous_range<T> && std::is_trivially_copyable_v<std::ranges::range_value_t<T>>) {
                if (reading) file.read(reinterpret_cast<char*>(values.data()), std::streamsize(count * sizeof(std::ranges::range_value_t<T>)));
                else file.write(reinterpret_cast<const char*>(values.data()), std::streamsize(count * sizeof(std::ranges::range_value_t<T>)));
            } else
                for (auto& value : values)
                    if constexpr (std::is_trivially_copyable_v<std::ranges::range_value_t<T>>) pod(value);
                    else if constexpr (std::ranges::range<std::ranges::range_value_t<T>>) sequence(value);
                    else serialize(*this, value);
        }
    };
}
