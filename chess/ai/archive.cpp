module chess.ai.archive;
import std;

namespace chess::ai {
    Archive::Archive(const std::filesystem::path& path, const bool read, const std::uint32_t kind) : reading{read} {
        file.exceptions(std::ios::badbit | std::ios::failbit);
        file.open(path, std::ios::binary | (reading ? std::ios::in : std::ios::out | std::ios::trunc));
        std::uint64_t magic = 0x3149414951514843;
        std::uint32_t version = kind == 2 ? 6 : kind == 5 ? 5 : kind == 7 ? 4 : 2, type = kind;
        pod(magic);
        pod(version);
        pod(type);
        if (magic != 0x3149414951514843 || version != (kind == 2 ? 6 : kind == 5 ? 5 : kind == 7 ? 4 : 2) || type != kind) throw std::runtime_error{"Unsupported AI artifact format"};
    }
}
