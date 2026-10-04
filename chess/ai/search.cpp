module chess.ai.search;
import std;
namespace chess::ai {
    Search::Search(const int count, const int threads) : busy(count), jobs(count) {
        for (int worker = 0; worker < std::min(count, threads); ++worker)
            workers.emplace_back([this](const std::stop_token stop) {
                for (;;) {
                    int index{};
                    {
                        std::unique_lock lock{mutex};
                        work_condition.wait(lock, [&] { return stop.stop_requested() || !pending.empty(); });
                        if (stop.stop_requested()) return;
                        index = pending.front();
                        pending.pop_front();
                        jobs[index].stage = Stage::working;
                    }
                    auto& job = jobs[index];
                    auto& tree = *job.tree;
                    Stage next = Stage::queued;
                    try {
                        Computation computation{tree.config.deadline, stop};
                        if (tree.nodes[0].expanded && tree.simulation >= tree.config.simulations) {
                            job.result = tree.finish();
                            if (job.commit) {
                                const auto chosen = std::ranges::find(tree.nodes[0].edges, job.result.move, &Tree::Edge::move);
                                if (chosen->child < 0) tree.game.play(job.result.move);
                                else tree.play_cached(job.result.move, tree.nodes[chosen->child].decision);
                            }
                            next = Stage::complete;
                        } else {
                            tree.prepare();
                            if (tree.request) next = Stage::inference;
                        }
                    } catch (...) {
                        const std::lock_guard lock{mutex};
                        if (!stop.stop_requested()) error = std::current_exception();
                        condition.notify_one();
                        return;
                    }
                    {
                        const std::lock_guard lock{mutex};
                        job.simulations = tree.simulation;
                        job.stage = next;
                        if (next == Stage::queued) pending.push_back(index);
                    }
                    if (next == Stage::queued) work_condition.notify_one();
                    else condition.notify_one();
                }
            });
    }
    Search::~Search() {
        {
            const std::lock_guard lock{mutex};
            for (auto& worker : workers) worker.request_stop();
        }
        work_condition.notify_all();
        for (auto& worker : workers) worker.join();
    }
    void Search::submit(const int actor, const Game& game, const SearchConfig config, const std::uint64_t seed, std::shared_ptr<const DeviceWeights> model, const bool commit) {
        auto& job = jobs[actor];
        job.tree.emplace(game, config, seed);
        job.model = std::move(model);
        job.commit = commit;
        job.simulations = 0;
        busy[actor] = true;
        {
            const std::lock_guard lock{mutex};
            job.stage = Stage::queued;
            pending.push_back(actor);
        }
        work_condition.notify_one();
    }
    std::vector<SearchCompleted> Search::advance(Network& network) {
        std::vector<SearchCompleted> completed;
        std::vector<int> ready;
        {
            std::unique_lock lock{mutex};
            condition.wait_for(lock, std::chrono::milliseconds{20}, [&] { return error || std::ranges::any_of(jobs, [](const Job& job) { return job.stage == Stage::inference || job.stage == Stage::complete; }); });
            if (error) std::rethrow_exception(error);
            if (std::ranges::any_of(jobs, [](const Job& job) { return job.stage == Stage::inference; })) {
                const auto target = std::min<std::ptrdiff_t>(64, std::ranges::count_if(jobs, [](const Job& job) { return job.stage != Stage::idle && job.stage != Stage::complete; }));
                condition.wait_until(lock, std::chrono::steady_clock::now() + std::chrono::microseconds{250}, [&] { return error || std::ranges::count_if(jobs, [](const Job& job) { return job.stage == Stage::inference; }) >= std::max<std::ptrdiff_t>(1, target); });
                if (error) std::rethrow_exception(error);
            }
            for (int index = 0; index < int(jobs.size()); ++index) {
                auto& job = jobs[index];
                if (job.stage == Stage::complete) {
                    completed.push_back({index, job.model->version, std::move(job.result), std::move(job.tree->game)});
                    job.stage = Stage::idle;
                    busy[index] = false;
                } else if (job.stage == Stage::inference) {
                    ready.push_back(index);
                    job.stage = Stage::working;
                }
            }
        }
        while (!ready.empty()) {
            const auto model = jobs[ready.front()].model;
            std::vector<Observation> observations;
            std::vector<int> indices;
            for (const int index : ready)
                if (jobs[index].model == model) {
                    observations.push_back(*jobs[index].tree->request);
                    indices.push_back(index);
                }
            const auto predictions = network.infer(observations, model);
            for (std::size_t sample = 0; sample < indices.size(); ++sample) jobs[indices[sample]].tree->accept(predictions[sample]);
            {
                const std::lock_guard lock{mutex};
                for (const int index : indices) {
                    jobs[index].stage = Stage::queued;
                    pending.push_back(index);
                }
            }
            work_condition.notify_all();
            std::erase_if(ready, [&](const int index) { return jobs[index].model == model; });
        }
        {
            const std::lock_guard lock{mutex};
            activity = {};
            for (const auto& job : jobs) activity.simulations += job.simulations;
        }
        return completed;
    }
    std::vector<SearchResult> search(Network& network, const std::span<Game* const> games, const SearchConfig config, std::mt19937_64& random, std::function<void(const SearchActivity&)> progress) {
        Search tasks{int(games.size()), config.threads};
        const auto model = network.freeze();
        for (int actor = 0; actor < int(games.size()); ++actor) tasks.submit(actor, *games[actor], config, random(), model);
        std::vector<SearchResult> result(games.size());
        std::size_t completed{};
        auto next_progress = std::chrono::steady_clock::now();
        while (completed < games.size()) {
            if (config.cancellation.stop_requested() || std::chrono::steady_clock::now() >= config.deadline) throw Interrupted{};
            for (auto& entry : tasks.advance(network)) {
                result[entry.actor] = std::move(entry.result);
                ++completed;
            }
            if (progress && std::chrono::steady_clock::now() >= next_progress) {
                auto activity = tasks.activity;
                activity.completed = completed;
                activity.total = games.size();
                progress(activity);
                next_progress = std::chrono::steady_clock::now() + std::chrono::seconds{1};
            }
        }
        if (progress) {
            auto activity = tasks.activity;
            activity.completed = games.size();
            activity.total = games.size();
            progress(activity);
        }
        return result;
    }
} // namespace chess::ai
