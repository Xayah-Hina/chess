module chess.ai.checkpoint;
import std;
namespace chess::ai {
    Archive::Archive(const std::filesystem::path& path, const bool read, const std::uint32_t kind) : reading{read} {
        file.exceptions(std::ios::badbit | std::ios::failbit);
        file.open(path, std::ios::binary | (reading ? std::ios::in : std::ios::out | std::ios::trunc));
        std::uint64_t magic   = 0x3149414951514843;
        std::uint32_t version = kind == 2 ? 5 : kind == 5 ? 4 : kind == 7 ? 3 : 1, type = kind;
        pod(magic);
        pod(version);
        pod(type);
        if (magic != 0x3149414951514843 || version != (kind == 2 ? 5 : kind == 5 ? 4 : kind == 7 ? 3 : 1) || type != kind) throw std::runtime_error{"Unsupported AI artifact format"};
    }
    void serialize(Archive& archive, Sample& sample) {
        archive.pod(sample.observation);
        archive.sequence(sample.policy);
        archive.pod(sample.version);
        archive.pod(sample.result);
    }
    void serialize(Archive& archive, Game& game) {
        archive.pod(game.position);
        archive.sequence(game.history);
        archive.pod(game.decision);
        if (archive.reading) game.moves = game.decision.outcome == Outcome::ongoing ? legal_moves(game.position) : std::vector<Move>{};
    }
    void serialize(Archive& archive, SelfPlay& selfplay) {
        std::uint64_t count = selfplay.actors.size();
        archive.pod(count);
        if (archive.reading) selfplay.actors.resize(count);
        for (auto& actor : selfplay.actors) {
            serialize(archive, actor.game);
            archive.sequence(actor.pending);
        }
        std::ostringstream output;
        output << selfplay.random;
        std::string random = output.str();
        archive.sequence(random);
        if (archive.reading) std::istringstream{random} >> selfplay.random;
        archive.pod(selfplay.games);
        archive.pod(selfplay.plies);
        archive.pod(selfplay.leaves);
        archive.pod(selfplay.outcomes);
        std::vector<Weights> models;
        std::vector<const DeviceWeights*> identities;
        std::vector<std::uint64_t> seeds(selfplay.actors.size());
        std::vector<int> indices(selfplay.actors.size(), -1);
        if (!archive.reading) {
            for (std::size_t index = 0; index < selfplay.actors.size(); ++index) {
                const auto& actor = selfplay.actors[index];
                seeds[index]      = actor.seed;
                if (!actor.model) continue;
                const auto found = std::ranges::find(identities, actor.model.get());
                indices[index]   = int(found - identities.begin());
                if (found == identities.end()) {
                    identities.push_back(actor.model.get());
                    models.push_back(Network::snapshot(*actor.model));
                }
            }
        }
        archive.sequence(seeds);
        archive.sequence(indices);
        archive.sequence(models);
        if (archive.reading) {
            std::vector<std::shared_ptr<const DeviceWeights>> shared;
            for (auto& model : models) shared.push_back(Network::upload(model));
            for (std::size_t index = 0; index < selfplay.actors.size(); ++index) {
                auto& actor = selfplay.actors[index];
                actor.seed  = seeds[index];
                actor.model = indices[index] >= 0 ? shared[indices[index]] : nullptr;
            }
        }
    }
    void serialize(Archive& archive, Replay& replay) {
        auto samples = archive.reading ? std::vector<Sample>{} : replay.download();
        archive.sequence(samples);
        auto generated = replay.generated;
        auto capacity  = replay.capacity;
        archive.pod(generated);
        archive.pod(capacity);
        if (archive.reading) {
            replay = Replay{capacity};
            for (std::size_t index = 0; index < samples.size(); index += 4096) {
                const auto first = std::make_move_iterator(samples.begin() + std::ptrdiff_t(index));
                const auto last  = std::make_move_iterator(samples.begin() + std::ptrdiff_t(std::min(index + 4096, samples.size())));
                replay.append(std::vector<Sample>{first, last});
            }
            replay.generated = generated;
        }
    }
    void serialize(Archive& archive, Weights& weights) {
        archive.sequence(weights.parameters);
        archive.sequence(weights.running);
        archive.pod(weights.version);
    }
    void serialize(Archive& archive, Optimizer& optimizer) {
        archive.sequence(optimizer.first);
        archive.sequence(optimizer.second);
        archive.pod(optimizer.steps);
    }
    void save_weights(const std::filesystem::path& path, Weights weights) {
        Archive archive{path, false, 1};
        serialize(archive, weights);
    }
    Weights load_weights(const std::filesystem::path& path) {
        Archive archive{path, true, 1};
        Weights weights;
        serialize(archive, weights);
        return weights;
    }
} // namespace chess::ai
