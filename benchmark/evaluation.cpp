module;
#include <nlohmann/json.hpp>
module chess.benchmark.evaluation;
import chess.benchmark.artifact;
import std;
namespace chess::benchmark {
    Evaluation::Evaluation(Config settings, const bool thorough, const bool resume) : config{std::move(settings)}, arena{config}, deep{thorough} {
        std::filesystem::create_directories(config.run);
        if (resume) {
            ai::Archive archive{config.run / "arena.bin", true, 5};
            persist(archive);
        }
    }
    void Evaluation::begin(const std::filesystem::path& candidate, const std::filesystem::path& comparison, const std::filesystem::path& initial, const std::string& opponent) {
        model = candidate.string();
        if (deep) {
            arena.diagnostics.swap(arena.holdout_diagnostics);
            arena.diagnostic_holdout = true;
            arena.initial            = initial.string();
            arena.enqueue(candidate, comparison);
            if (!opponent.empty())
                std::erase_if(arena.jobs, [&](const ArenaJob& job) {
                    if (opponent == "random") return job.opponent != Opponent::random;
                    if (opponent == "material-1") return job.opponent != Opponent::material || job.depth != 1;
                    if (opponent == "material-3") return job.opponent != Opponent::material || job.depth != 3;
                    if (opponent == "checkpoint") return job.opponent != Opponent::checkpoint;
                    return job.opponent != Opponent::pikafish || job.nodes != std::stoi(opponent.substr(4));
                });
        }
        save_config(config, config.run / "config.json");
        save();
    }
    void Evaluation::advance() {
        if (!network) {
            network = std::make_unique<ai::Network>(config.arena_batch, config.training.seed);
            network->restore(ai::load_weights(model));
        }
        const auto start    = std::chrono::steady_clock::now();
        const auto progress = [&](const std::string_view phase, const ai::SearchActivity& current) {
            stage           = phase;
            activity        = current;
            const auto now  = std::chrono::steady_clock::now();
            running_seconds = std::chrono::duration<double>(now - start).count();
            if (heartbeat && now >= next_heartbeat) {
                heartbeat();
                next_heartbeat = now + std::chrono::seconds{1};
            }
        };
        try {
            const int phases = deep ? 3 : 2;
            if (probe_index < phases) {
                auto& probe = probes[probe_index];
                std::vector<Diagnostic> subset;
                if (!deep && probe_index == 1) {
                    for (int category = 0; category < 3; ++category) {
                        int count{};
                        for (const auto& entry : arena.diagnostics)
                            if (entry.category == category && count++ < (category == 2 ? 6 : 5)) subset.push_back(entry);
                    }
                }
                const std::span<const Diagnostic> bank = subset.empty() ? std::span<const Diagnostic>{arena.diagnostics} : std::span<const Diagnostic>{subset};
                const std::array simulations{0, 32, 128};
                const int searches = deep ? simulations[probe_index] : probe_index == 0 ? 0 : config.training.simulations;
                const auto cursor  = std::size_t(probe.positions);
                const auto count   = std::min(std::size_t(config.arena_batch), bank.size() - cursor);
                const ai::SearchConfig search{searches, config.training.candidates, config.training.threads, false, Computation::deadline};
                const auto phase = std::chrono::steady_clock::now();
                try {
                    stage = std::format("Tactics: {} simulations", searches);
                    measure(*network, bank.subspan(cursor, count), search, probe, [&](const ai::SearchActivity& current) { progress(stage, current); });
                } catch (...) {
                    probe.seconds += std::chrono::duration<double>(std::chrono::steady_clock::now() - phase).count();
                    throw;
                }
                probe.seconds += std::chrono::duration<double>(std::chrono::steady_clock::now() - phase).count();
                if (probe.positions == bank.size()) {
                    probe.elapsed = seconds + std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
                    write_probe(probe, config, arena.suite_hash, deep, config.run / std::format("probe-{}.json", searches), fingerprint(model));
                    ++probe_index;
                }
            } else if (!deep || arena.advance(*network, progress) == Advance::idle) complete = true;
        } catch (...) {
            seconds += std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
            running_seconds = 0;
            throw;
        }
        seconds += std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        running_seconds = 0;
    }
    void Evaluation::persist(ai::Archive& archive) {
        arena.persist(archive);
        archive.pod(deep);
        archive.pod(complete);
        archive.pod(probes);
        archive.pod(probe_index);
        archive.pod(seconds);
        archive.sequence(model);
    }
    void Evaluation::save() {
        const auto temporary = config.run / "arena.tmp";
        {
            ai::Archive archive{temporary, false, 5};
            persist(archive);
        }
        publish(temporary, config.run / "arena.bin");
    }
    void Evaluation::report(const std::string_view state) {
        nlohmann::json jobs = nlohmann::json::array();
        bool running_job{};
        for (const auto& job : arena.jobs) {
            const auto result = statistics(job);
            std::uint64_t plies{};
            for (const auto& match : job.matches) plies += match.game.history.size() - match.opening_length;
            jobs.push_back({{"name", job.name}, {"state", job.reported ? "complete" : probe_index < (deep ? 3 : 2) || running_job ? "queued" : state}, {"completed_pairs", result.pairs}, {"target_pairs", job.pairs}, {"completed_games", std::ranges::count(job.matches, true, &Match::complete)}, {"plies", plies}});
            running_job |= !job.reported;
        }
        const nlohmann::json output{{"state", state}, {"profile", deep ? "deep" : "short"}, {"seconds", seconds + running_seconds}, {"stage", stage}, {"updated_at", std::format("{:%FT%TZ}", std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now()))}, {"search_completed", activity.completed}, {"search_total", activity.total}, {"simulations", activity.simulations}, {"rule_nodes", activity.rule_nodes}, {"rule_tasks", activity.adjudicating}, {"longest_rule_seconds", activity.longest_seconds}, {"model", model}, {"probe_index", probe_index}, {"probe_positions", probe_index < (deep ? 3 : 2) ? probes[probe_index].positions : 0}, {"suite_sha256", arena.suite_hash}, {"jobs", jobs}};
        const auto temporary = config.run / "status.tmp";
        std::ofstream file{temporary};
        file.exceptions(std::ios::badbit | std::ios::failbit);
        file << output.dump(2);
        file.close();
        publish(temporary, config.run / "status.json");
        if (deep) arena.report();
    }
} // namespace chess::benchmark
