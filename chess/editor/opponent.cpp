module chess.editor.opponent;
import chess.ai.checkpoint;
import chess.ai.config;
import chess.ai.search;
import std;

namespace chess::editor {
    Opponent::Opponent() : worker{[this](const std::stop_token stop) { run(stop); }} {}
    Opponent::~Opponent() {
        {
            const std::lock_guard lock{mutex};
            cancellation.request_stop();
            worker.request_stop();
        }
        condition.notify_one();
        worker.join();
    }
    void Opponent::submit(std::filesystem::path model, std::optional<Game> game) {
        {
            const std::lock_guard lock{mutex};
            cancellation.request_stop();
            cancellation = std::stop_source{};
            request = {std::move(model), std::move(game), ++generation};
            result.reset();
            error.clear();
            seconds = 0;
            phase = request->game ? OpponentPhase::thinking : OpponentPhase::loading;
        }
        condition.notify_one();
    }
    void Opponent::run(const std::stop_token stop) {
        std::optional<ai::Network> network;
        std::filesystem::path loaded;
        std::mt19937_64 random{ai::Config{}.seed};
        for (;;) {
            Request task;
            std::stop_token token;
            {
                std::unique_lock lock{mutex};
                condition.wait(lock, [&] { return stop.stop_requested() || request.has_value(); });
                if (stop.stop_requested()) return;
                task = std::move(*request);
                request.reset();
                token = cancellation.get_token();
            }
            try {
                Computation computation{std::chrono::steady_clock::time_point::max(), token};
                if (loaded != task.model) {
                    const auto weights = ai::load_weights(task.model);
                    if (!network) network.emplace(1, ai::Config{}.seed);
                    network->restore(weights);
                    loaded = task.model;
                }
                if (token.stop_requested()) throw Interrupted{};
                const auto start = std::chrono::steady_clock::now();
                if (task.game) {
                    const std::array games{&*task.game};
                    ai::SearchConfig settings;
                    settings.exploration = false;
                    settings.cancellation = token;
                    const auto choice = ai::search(*network, games, settings, random).front();
                    task.game->play(choice.move);
                }
                const std::lock_guard lock{mutex};
                if (task.generation != generation || token.stop_requested()) continue;
                result = std::move(task.game);
                version = network->version;
                seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
                phase = OpponentPhase::ready;
            } catch (const Interrupted&) {
            } catch (const std::exception& failure) {
                const std::lock_guard lock{mutex};
                if (task.generation != generation || token.stop_requested()) continue;
                error = std::format("AI opponent ({}): {}", task.model.string(), failure.what());
                phase = OpponentPhase::failed;
            }
        }
    }
} // namespace chess::editor
