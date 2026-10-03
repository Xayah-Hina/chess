module;
#include <Windows.h>
module chess.benchmark.artifact;
import std;
namespace chess::benchmark {
    void publish(const std::filesystem::path& temporary, const std::filesystem::path& destination) {
        if (!MoveFileExW(temporary.c_str(), destination.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) throw std::system_error{int(GetLastError()), std::system_category(), std::format("Publish artifact {}", destination.string())};
    }
} // namespace chess::benchmark
