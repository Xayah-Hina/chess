module;
#include <nlohmann/json.hpp>
module tools.trainer.session;
import chess.benchmark.artifact;
import std;
namespace tools::trainer {
    namespace {
        BenchmarkResult summarize(const chess::benchmark::Evaluation& evaluation, const BenchmarkRequest request, const std::uint64_t version) {
            BenchmarkResult result;
            result.request      = request;
            result.version      = evaluation.network ? evaluation.network->version : evaluation.probes[0].positions ? evaluation.probes[0].version : version;
            result.complete     = evaluation.complete;
            result.probe_index  = evaluation.probe_index;
            result.probe_target = evaluation.deep ? 3 : 2;
            result.seconds      = evaluation.seconds + evaluation.running_seconds;
            result.activity     = evaluation.activity;
            result.stage        = evaluation.stage;
            result.probes       = evaluation.probes;
            result.directory    = evaluation.config.run;
            for (const auto& job : evaluation.arena.jobs) {
                std::string name;
                switch (job.opponent) {
                case chess::benchmark::Opponent::random: name = "随机着法"; break;
                case chess::benchmark::Opponent::material: name = std::format("子力评估（深度 {}）", job.depth); break;
                case chess::benchmark::Opponent::checkpoint: name = job.reference == evaluation.arena.initial ? "初始随机模型" : "参考模型"; break;
                case chess::benchmark::Opponent::pikafish: name = std::format("PF-N{}", job.nodes); break;
                }
                result.matches.push_back({std::move(name), chess::benchmark::statistics(job), job.pairs, int(std::ranges::count(job.matches, true, &chess::benchmark::Match::complete)), job.nodes, job.raw_policy ? 0 : evaluation.config.training.simulations, job.reported});
                for (const auto& match : job.matches) result.matches.back().plies += match.game.history.size() - match.opening_length;
            }
            return result;
        }
    } // namespace
    Session::Session(std::function<void()> callback) : notify{std::move(callback)}, worker{[this] { run(); }} {}
    Session::~Session() {
        submit({Action::close});
        worker.join();
    }
    void Session::submit(Request request) {
        {
            const std::lock_guard lock{mutex};
            if (state.closed) return;
            if (request.action == Action::pause || request.action == Action::close || (request.action == Action::remove_benchmark && state.active && state.active->request.id == request.id)) cancellation.request_stop();
            if (request.action == Action::pause || request.action == Action::close) state.phase = request.action == Action::close ? Phase::closing : Phase::pausing;
            requests.push_back(std::move(request));
            ++state.revision;
        }
        condition.notify_one();
        if (notify) notify();
    }
    Snapshot Session::drain() {
        const std::lock_guard lock{mutex};
        return state;
    }
    void Session::open(chess::benchmark::Config settings) {
        if (training) {
            training->selfplay.stop();
            save();
        }
        training.reset();
        evaluation.reset();
        active.reset();
        latest.reset();
        queue.clear();
        history.clear();
        events.clear();
        latest_weights.clear();
        training_seconds = benchmark_seconds = next_record = prior_seconds = 0;
        sampled_steps = prior_plies = prior_steps = 0;
        next_id                                   = 1;
        enabled                                   = false;
        hold_jobs                                 = true;
        opening                                   = true;
        config                                    = std::move(settings);
        config.run                                = std::filesystem::absolute(config.run);
        const bool resume                         = std::filesystem::exists(config.run / "checkpoint.bin");
        if (resume) config = chess::benchmark::load_config(config.run / "config.json");
        publish(Phase::loading, resume ? "正在恢复训练状态" : "正在创建训练任务");
        training = std::make_unique<chess::ai::Training>(config.training, config.arena_batch);
        std::filesystem::create_directories(config.run);
        if (resume) {
            chess::ai::Archive archive{config.run / "checkpoint.bin", true, 2};
            training->persist(archive);
            archive.pod(training_seconds);
            archive.pod(benchmark_seconds);
            archive.pod(next_id);
            archive.sequence(queue);
            bool evaluating{};
            archive.pod(evaluating);
            if (evaluating) {
                BenchmarkRequest request;
                archive.pod(request);
                active        = request;
                auto settings = config;
                settings.run  = config.run / "benchmark" / std::format("{:06}-{}", request.id, request.deep ? "deep" : "short");
                evaluation    = std::make_unique<chess::benchmark::Evaluation>(settings, request.deep, true);
            }
            bool reported{};
            archive.pod(reported);
            if (reported) {
                BenchmarkRequest request;
                std::uint64_t version{};
                archive.pod(request);
                archive.pod(version);
                auto settings = config;
                settings.run  = config.run / "benchmark" / std::format("{:06}-{}", request.id, request.deep ? "deep" : "short");
                const chess::benchmark::Evaluation completed{settings, request.deep, true};
                latest = summarize(completed, request, version);
            }
            archive.sequence(history);
            if (!history.empty()) sampled_steps = history.back().steps;
        } else {
            chess::ai::save_weights(config.run / "weights-initial.bin", training->network.snapshot());
            chess::benchmark::save_config(config, config.run / "config.json");
        }
        next_save     = training_seconds + benchmark_seconds + config.save_seconds;
        prior_seconds = training_seconds;
        prior_steps   = training->network.steps;
        prior_plies   = training->selfplay.plies;
        save();
        opening = false;
        publish(Phase::paused, "训练任务已就绪");
    }
    void Session::publish(const Phase phase, std::string event) {
        if (!event.empty()) {
            events.push_back(std::move(event));
            if (events.size() > 12) events.erase(events.begin());
        }
        Snapshot frame;
        bool rates_updated{};
        frame.phase             = phase;
        frame.loaded            = bool(training);
        frame.training          = enabled;
        frame.config            = config;
        frame.training_seconds  = training_seconds;
        frame.benchmark_seconds = benchmark_seconds;
        frame.saved             = saved;
        frame.queue             = queue;
        frame.latest            = latest;
        frame.events            = events;
        if (training) {
            frame.version         = training->network.version;
            frame.steps           = training->network.steps;
            frame.games           = training->selfplay.games;
            frame.plies           = training->selfplay.plies;
            frame.outcomes        = training->selfplay.outcomes;
            frame.replay          = training->replay.size;
            frame.generated       = training->replay.generated;
            frame.presented       = training->progress.presented;
            frame.minimum_samples = std::uint64_t(std::ceil(config.training.batch / double(config.training.presentations)));
            for (const auto& actor : training->selfplay.actors) frame.pending_samples += actor.pending.size();
            frame.metrics = training->progress.metrics;
            frame.search  = training->selfplay.activity;
            if (frame.steps && frame.steps != sampled_steps) {
                history.push_back({frame.steps, frame.metrics});
                if (history.size() > 300) history.erase(history.begin());
                sampled_steps = frame.steps;
            }
            const double span = training_seconds - prior_seconds;
            if (span >= 1) {
                rates_updated          = true;
                frame.plies_per_second = (frame.plies - prior_plies) / span;
                frame.steps_per_second = (frame.steps - prior_steps) / span;
                prior_seconds          = training_seconds;
                prior_plies            = frame.plies;
                prior_steps            = frame.steps;
            }
        }
        frame.history = history;
        if (evaluation) {
            frame.active = summarize(*evaluation, *active, training->network.version);
            frame.benchmark_seconds += evaluation->running_seconds;
            if (phase == Phase::benchmark) frame.search = evaluation->activity;
        }

        {
            const std::lock_guard lock{mutex};
            frame.revision = state.revision + 1;
            if (enabled && phase != Phase::benchmark && !rates_updated) {
                frame.plies_per_second = state.plies_per_second;
                frame.steps_per_second = state.steps_per_second;
            }
            state = frame;
        }
        if (notify) notify();
        const double total = frame.training_seconds + frame.benchmark_seconds;
        if (training && phase != Phase::failed && (total >= next_record || phase == Phase::paused || phase == Phase::saving || phase == Phase::closing)) {
            nlohmann::json queued = nlohmann::json::array();
            for (const auto& job : queue) queued.push_back({{"id", job.id}, {"profile", job.deep ? "deep" : "short"}});
            const nlohmann::json output{{"state", phases[int(phase)]}, {"training", enabled}, {"version", frame.version}, {"steps", frame.steps}, {"games", frame.games}, {"plies", frame.plies}, {"replay", frame.replay}, {"generated", frame.generated}, {"presented", frame.presented}, {"pending_samples", frame.pending_samples}, {"rule_tasks", frame.search.adjudicating}, {"longest_rule_seconds", frame.search.longest_seconds}, {"training_seconds", training_seconds}, {"benchmark_seconds", frame.benchmark_seconds}, {"updated_at", std::format("{:%FT%TZ}", std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now()))}, {"metrics", frame.steps ? nlohmann::json(frame.metrics) : nlohmann::json(nullptr)}, {"queued_benchmarks", queued}, {"active_benchmark", active ? nlohmann::json(active->id) : nlohmann::json(nullptr)}, {"checkpoint", (config.run / "checkpoint.bin").string()}, {"latest_weights", latest_weights.string()}};
            const auto temporary = config.run / "status.tmp";
            std::ofstream file{temporary};
            file.exceptions(std::ios::badbit | std::ios::failbit);
            file << output.dump(2);
            file.close();
            chess::benchmark::publish(temporary, config.run / "status.json");
            const auto destination = config.run / "training.csv";
            const bool header      = !std::filesystem::exists(destination);
            std::ofstream metrics{destination, std::ios::app};
            metrics.exceptions(std::ios::badbit | std::ios::failbit);
            if (header) metrics << "training_seconds,steps,games,plies,replay,policy_loss,value_loss,gradient_norm,update_norm\n";
            metrics << std::format("{:.3f},{},{},{},{},", training_seconds, frame.steps, frame.games, frame.plies, frame.replay);
            if (frame.steps) metrics << std::format("{:.6f},{:.6f},{:.6f},{:.6f}", frame.metrics[0], frame.metrics[1], frame.metrics[2], frame.metrics[3]);
            else metrics << ",,,";
            metrics << '\n';
            next_record = total + 10;
        }
    }
    void Session::save() {
        if (!training) return;
        publish(closing ? Phase::closing : Phase::saving, "正在保存完整训练状态");
        if (evaluation) {
            evaluation->save();
            evaluation->report(hold_jobs ? "paused" : "running");
        }
        const auto temporary = config.run / "checkpoint.tmp";
        {
            chess::ai::Archive archive{temporary, false, 2};
            training->persist(archive);
            archive.pod(training_seconds);
            archive.pod(benchmark_seconds);
            archive.pod(next_id);
            archive.sequence(queue);
            bool evaluating = bool(evaluation);
            archive.pod(evaluating);
            if (evaluating) archive.pod(*active);
            bool reported = latest.has_value();
            archive.pod(reported);
            if (reported) {
                archive.pod(latest->request);
                archive.pod(latest->version);
            }
            archive.sequence(history);
        }
        chess::benchmark::publish(temporary, config.run / "checkpoint.bin");
        const auto weights = config.run / std::format("weights-{:010}.bin", training->network.version);
        auto staging       = weights;
        staging += ".tmp";
        chess::ai::save_weights(staging, training->network.snapshot());
        chess::benchmark::publish(staging, weights);
        latest_weights = weights;
        saved          = std::chrono::system_clock::now();
        next_save      = training_seconds + benchmark_seconds + config.save_seconds;
    }
    void Session::run() {
        auto next_publish = std::chrono::steady_clock::now();
        for (;;) {
            std::deque<Request> commands;
            std::stop_token token;
            {
                std::unique_lock lock{mutex};
                condition.wait(lock, [this] { return !requests.empty() || enabled || (!hold_jobs && (evaluation || !queue.empty())); });
                commands.swap(requests);
                cancellation = std::stop_source{};
                token        = cancellation.get_token();
            }
            try {
                while (!commands.empty()) {
                    auto request = std::move(commands.front());
                    commands.pop_front();
                    switch (request.action) {
                    case Action::open: open(std::move(request.config)); break;
                    case Action::start:
                        if (!training || std::filesystem::absolute(request.config.run) != config.run) open(std::move(request.config));
                        enabled   = true;
                        hold_jobs = false;
                        publish(Phase::selfplay, "训练已开始");
                        break;
                    case Action::pause:
                        training->selfplay.stop();
                        enabled   = false;
                        hold_jobs = true;
                        save();
                        publish(Phase::paused, "已暂停并保存");
                        break;
                    case Action::save:
                        save();
                        publish(evaluation && !hold_jobs ? Phase::benchmark : enabled ? Phase::selfplay : Phase::paused, "训练状态已保存");
                        break;
                    case Action::short_benchmark:
                    case Action::deep_benchmark:
                        if (!training) open(std::move(request.config));
                        queue.push_back({next_id++, request.action == Action::deep_benchmark});
                        hold_jobs = false;
                        publish(Phase::benchmark, "手动评估已加入队列");
                        break;
                    case Action::resume_benchmark:
                        hold_jobs = false;
                        publish(Phase::benchmark, "评估已继续");
                        break;
                    case Action::remove_benchmark:
                        if (active && active->id == request.id) {
                            evaluation->save();
                            evaluation->report("cancelled");
                            evaluation.reset();
                            active.reset();
                        } else std::erase_if(queue, [&](const BenchmarkRequest& entry) { return entry.id == request.id; });
                        save();
                        publish(enabled ? Phase::selfplay : Phase::paused, "评估任务已移除");
                        break;
                    case Action::close:
                        if (training) training->selfplay.stop();
                        closing   = true;
                        enabled   = false;
                        hold_jobs = true;
                        save();
                        publish(Phase::closing, "已保存，可以关闭");
                        {
                            const std::lock_guard lock{mutex};
                            state.closed = true;
                            ++state.revision;
                        }
                        if (notify) notify();
                        return;
                    }
                }
                if (token.stop_requested()) continue;
                if (!hold_jobs && !evaluation && !queue.empty()) {
                    training->selfplay.stop();
                    active = queue.front();
                    queue.pop_front();
                    auto settings    = config;
                    settings.run     = config.run / "benchmark" / std::format("{:06}-{}", active->id, active->deep ? "deep" : "short");
                    evaluation       = std::make_unique<chess::benchmark::Evaluation>(settings, active->deep);
                    const auto model = evaluation->config.run / std::format("weights-{:010}.bin", training->network.version);
                    chess::ai::save_weights(model, training->network.snapshot());
                    evaluation->begin(model, {}, config.run / "weights-initial.bin");
                    save();
                    publish(Phase::benchmark, std::format("正在评估模型 v{}", training->network.version));
                }
                if (evaluation && !hold_jobs) {
                    const auto start = std::chrono::steady_clock::now();
                    try {
                        chess::Computation computation{std::chrono::steady_clock::time_point::max(), token};
                        evaluation->heartbeat = [this] {
                            publish(Phase::benchmark);
                            evaluation->report("running");
                        };
                        evaluation->advance();
                    } catch (...) {
                        benchmark_seconds += std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
                        throw;
                    }
                    benchmark_seconds += std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
                    publish(Phase::benchmark);
                    if (evaluation->complete) {
                        evaluation->save();
                        evaluation->report("complete");
                        latest = drain().active;
                        evaluation.reset();
                        active.reset();
                        save();
                        publish(enabled ? Phase::selfplay : Phase::paused, "评估已完成");
                    }
                } else if (enabled) {
                    const auto start = std::chrono::steady_clock::now();
                    try {
                        chess::Computation computation{std::chrono::steady_clock::time_point::max(), token};
                        training->advance(chess::Computation::deadline);
                    } catch (...) {
                        training_seconds += std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
                        throw;
                    }
                    training_seconds += std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
                    if (std::chrono::steady_clock::now() >= next_publish) {
                        const auto phase = !training->network.steps ? Phase::collecting : training->progress.training_phase ? Phase::updating : Phase::selfplay;
                        publish(phase);
                        next_publish = std::chrono::steady_clock::now() + std::chrono::milliseconds{250};
                    }
                }
                if (training && training_seconds + benchmark_seconds >= next_save) {
                    save();
                    publish(evaluation ? Phase::benchmark : enabled ? Phase::selfplay : Phase::paused, "自动保存完成");
                }
            } catch (const chess::Interrupted&) {
            } catch (const std::exception& error) {
                if (training) training->selfplay.stop();
                enabled   = false;
                hold_jobs = true;
                if (opening) {
                    evaluation.reset();
                    active.reset();
                    training.reset();
                }
                opening = false;
                publish(Phase::failed);
                {
                    const std::lock_guard lock{mutex};
                    state.error = error.what();
                    ++state.revision;
                    requests.insert(requests.begin(), std::make_move_iterator(commands.begin()), std::make_move_iterator(commands.end()));
                    if (closing) state.closed = true;
                }
                if (notify) notify();
                if (closing) return;
            }
        }
    }
} // namespace tools::trainer
