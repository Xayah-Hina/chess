module chess.ai.training;
import std;
namespace chess::ai {
    Training::Training(Config settings, const int capacity) : config{std::move(settings)}, network{std::max({config.batch, config.actors, capacity}), config.seed}, selfplay{config.actors, config.seed ^ 0xd783a521ULL}, replay{config.replay_capacity}, random{config.seed ^ 0x8763438ULL} {
        progress.metrics.fill(std::numeric_limits<float>::quiet_NaN());
    }
    void Training::advance(const std::chrono::steady_clock::time_point deadline) {
        Computation computation{deadline};
        if (Computation::cancellation.stop_requested()) throw Interrupted{};
        if (progress.training_phase) {
            if (double(progress.presented + config.batch) <= replay.generated * config.presentations && replay.size) {
                const auto phase = std::chrono::steady_clock::now();
                progress.metrics = network.train(replay, config.batch, random, config.learning_rate, config.weight_decay);
                progress.training_seconds += std::chrono::duration<double>(std::chrono::steady_clock::now() - phase).count();
                progress.presented += config.batch;
            } else {
                progress.training_phase   = false;
                progress.generation_round = 0;
            }
        } else {
            const auto phase  = std::chrono::steady_clock::now();
            const auto rounds = selfplay.plies / config.actors;
            try {
                selfplay.advance(network, replay, {config.simulations, config.candidates, config.threads, true, deadline});
            } catch (const Interrupted&) {
                progress.selfplay_seconds += std::chrono::duration<double>(std::chrono::steady_clock::now() - phase).count();
                throw;
            }
            progress.selfplay_seconds += std::chrono::duration<double>(std::chrono::steady_clock::now() - phase).count();
            progress.generation_round += int(selfplay.plies / config.actors - rounds);
            if (progress.generation_round >= config.generation_plies) progress.training_phase = true;
        }
    }
    void Training::persist(Archive& archive) {
        Weights weights;
        Optimizer optimizer;
        if (!archive.reading) {
            weights   = network.snapshot();
            optimizer = network.optimizer();
        }
        serialize(archive, weights);
        serialize(archive, optimizer);
        if (archive.reading) network.restore(weights, &optimizer);
        serialize(archive, selfplay);
        serialize(archive, replay);
        archive.pod(progress.selfplay_seconds);
        archive.pod(progress.training_seconds);
        archive.pod(progress.presented);
        archive.pod(progress.generation_round);
        archive.pod(progress.training_phase);
        archive.pod(progress.metrics);
        std::string generator;
        if (!archive.reading) {
            std::ostringstream output;
            output << random;
            generator = output.str();
        }
        archive.sequence(generator);
        if (archive.reading) std::istringstream{generator} >> random;
    }
} // namespace chess::ai
