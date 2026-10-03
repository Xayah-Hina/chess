module;
#include <nlohmann/json.hpp>
module chess.benchmark.probe;
import chess.benchmark.artifact;
import std;
namespace chess::benchmark {
    void measure(ai::Network& network, const std::span<const Diagnostic> diagnostics, const ai::SearchConfig search, Probe& probe, std::function<void(const ai::SearchActivity&)> progress) {
        if (Computation::cancellation.stop_requested()) throw Interrupted{};
        std::vector<ai::Observation> observations;
        std::vector<Game> copies;
        std::vector<Game*> games;
        for (const auto& diagnostic : diagnostics) {
            observations.push_back(ai::observe(diagnostic.game));
            if (search.simulations) copies.push_back(diagnostic.game);
        }
        for (auto& game : copies) games.push_back(&game);
        const auto predictions = network.infer(observations);
        std::vector<ai::SearchResult> choices;
        if (search.simulations) {
            std::mt19937_64 random{0xa73168e29ULL};
            choices = ai::search(network, games, search, random, std::move(progress));
        }
        Probe result;
        for (std::size_t index = 0; index < diagnostics.size(); ++index) {
            const auto& diagnostic = diagnostics[index];
            const auto& prediction = predictions[index];
            const auto turn        = diagnostic.game.position.turn;
            const auto best        = std::ranges::max_element(diagnostic.game.moves, {}, [&](const Move move) { return prediction.logits[ai::action(move, turn)]; });
            const auto selected    = search.simulations ? choices[index].move : *best;
            double mass{};
            if (search.simulations) {
                for (const auto move : diagnostic.correct)
                    for (const auto& entry : choices[index].policy)
                        if (entry.action == ai::action(move, turn)) mass += entry.probability;
            } else {
                const float maximum = prediction.logits[ai::action(*best, turn)];
                double sum{};
                for (const auto move : diagnostic.game.moves) sum += std::exp(prediction.logits[ai::action(move, turn)] - maximum);
                for (const auto move : diagnostic.correct) mass += std::exp(prediction.logits[ai::action(move, turn)] - maximum) / sum;
            }
            ++result.positions;
            ++result.total[diagnostic.category];
            result.correct[diagnostic.category] += std::ranges::find(diagnostic.correct, selected) != diagnostic.correct.end();
            result.mass += mass;
            // Only categories 0 and 1 have a proven WDL label. Safe moves do not imply a winning position.
            if (diagnostic.category < 2) {
                ++result.value_positions;
                result.brier += std::pow(prediction.wdl[0] - 1, 2) + std::pow(prediction.wdl[1], 2) + std::pow(prediction.wdl[2], 2);
                const int bin = std::min(9, int(prediction.wdl[0] * 10));
                ++result.calibration_count[bin];
                result.calibration_prediction[bin] += prediction.wdl[0];
            }
        }
        if (Computation::cancellation.stop_requested() || std::chrono::steady_clock::now() >= Computation::deadline) throw Interrupted{};
        probe.version     = network.version;
        probe.simulations = search.simulations;
        probe.positions += result.positions;
        probe.value_positions += result.value_positions;
        probe.mass += result.mass;
        probe.brier += result.brier;
        for (int category = 0; category < 3; ++category) {
            probe.correct[category] += result.correct[category];
            probe.total[category] += result.total[category];
        }
        for (int bin = 0; bin < 10; ++bin) {
            probe.calibration_count[bin] += result.calibration_count[bin];
            probe.calibration_prediction[bin] += result.calibration_prediction[bin];
        }
    }
    void write_probe(const Probe& probe, const Config& config, const std::string_view suite_hash, const bool holdout, const std::filesystem::path& destination, const std::string_view candidate_hash) {
        nlohmann::json calibration = nlohmann::json::array();
        for (int bin = 0; bin < 10; ++bin) calibration.push_back({{"count", probe.calibration_count[bin]}, {"prediction_sum", probe.calibration_prediction[bin]}, {"observed_win_sum", probe.calibration_count[bin]}});
        const nlohmann::json output{{"version", probe.version}, {"candidate_sha256", candidate_hash}, {"candidate_identity", "frozen artifact"}, {"simulations", probe.simulations}, {"positions", probe.positions}, {"elapsed", probe.elapsed}, {"seconds", probe.seconds}, {"correct", probe.correct}, {"total", probe.total}, {"correct_action_mass", probe.positions ? nlohmann::json(probe.mass / probe.positions) : nlohmann::json(nullptr)}, {"value_brier", probe.value_positions ? nlohmann::json(probe.brier / probe.value_positions) : nlohmann::json(nullptr)}, {"value_positions", probe.value_positions}, {"value_label_scope", "core-proven wins within one or three plies; no WDL label for safe-move positions"}, {"calibration", calibration}, {"categories", {"one_ply_forced_win", "three_ply_forced_win_without_one_ply_win", "avoid_opponent_one_ply_win"}}, {"holdout", holdout}, {"suite_sha256", suite_hash}, {"root_candidates", config.training.candidates}, {"cpu_threads", config.training.threads}, {"exploration", false},
            {"source_revision", CHESS_SOURCE_REVISION}, {"source_sha256", CHESS_SOURCE_HASH}};
        auto temporary = destination;
        temporary += ".tmp";
        std::ofstream file{temporary};
        file.exceptions(std::ios::badbit | std::ios::failbit);
        file << output.dump(2);
        file.close();
        publish(temporary, destination);
    }
} // namespace chess::benchmark
