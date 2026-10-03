module chess.ai.search;
import std;
namespace chess::ai {
    Search::Tree::Tree(const Game& initial, const SearchConfig settings, const std::uint64_t seed) : game{initial}, config{settings}, random{seed} {
        const int considered = std::min({config.candidates, int(game.moves.size()), std::max(1, config.simulations)});
        if (considered == 1) {
            for (int index = 0; index < config.simulations; ++index) schedule.push_back(index);
        } else {
            const int rounds = int(std::ceil(std::log2(considered)));
            std::vector<int> visits(considered);
            int count = considered;
            while (int(schedule.size()) < config.simulations) {
                const int repeats = std::max(1, config.simulations / (rounds * count));
                for (int repeat = 0; repeat < repeats; ++repeat)
                    for (int index = 0; index < count; ++index) schedule.push_back(visits[index]++);
                count = std::max(2, count / 2);
            }
            schedule.resize(config.simulations);
        }
    }
    std::vector<float> Search::Tree::completed(const Node& node) const {
        int visits{}, maximum{};
        float weighted{}, mass{};
        for (const auto& edge : node.edges) {
            visits += edge.visits;
            maximum = std::max(maximum, edge.visits);
            if (edge.visits) {
                weighted += edge.prior * edge.sum / edge.visits;
                mass += edge.prior;
            }
        }
        const float mixed = visits ? (node.raw + visits * weighted / mass) / (visits + 1) : node.raw;
        std::vector<float> values;
        for (const auto& edge : node.edges) values.push_back(edge.visits ? edge.sum / edge.visits : mixed);
        const auto [lowest, highest] = std::ranges::minmax_element(values);
        const float minimum = *lowest, scale = 0.1F * (50 + maximum) / std::max(*highest - minimum, 1e-8F);
        for (auto& value : values) value = (value - minimum) * scale;
        return values;
    }
    Task<bool> Search::Tree::prepare() {
        request.reset();
        int index{};
        while (nodes[index].expanded) {
            const auto& node = nodes[index];
            if (node.terminal) {
                backup(node.raw);
                co_return true;
            }
            const auto q = completed(node);
            int selected{};
            float best = -std::numeric_limits<float>::infinity();
            if (index == 0) {
                for (int choice = 0; choice < int(node.edges.size()); ++choice) {
                    const auto& edge = node.edges[choice];
                    if (edge.visits != schedule[simulation]) continue;
                    const float score = edge.gumbel + edge.logit + q[choice];
                    if (score > best) {
                        best     = score;
                        selected = choice;
                    }
                }
            } else {
                float maximum = -std::numeric_limits<float>::infinity(), denominator{};
                int total{};
                for (int choice = 0; choice < int(node.edges.size()); ++choice) {
                    maximum = std::max(maximum, node.edges[choice].logit + q[choice]);
                    total += node.edges[choice].visits;
                }
                for (int choice = 0; choice < int(node.edges.size()); ++choice) denominator += std::exp(node.edges[choice].logit + q[choice] - maximum);
                for (int choice = 0; choice < int(node.edges.size()); ++choice) {
                    const auto& edge  = node.edges[choice];
                    const float score = std::exp(edge.logit + q[choice] - maximum) / denominator - float(edge.visits) / (total + 1);
                    if (score > best) {
                        best     = score;
                        selected = choice;
                    }
                }
            }
            const auto move = nodes[index].edges[selected].move;
            path.emplace_back(index, selected);
            co_await game.play_async(move);
            int child = nodes[index].edges[selected].child;
            if (child < 0) {
                child                              = int(nodes.size());
                nodes[index].edges[selected].child = child;
                nodes.emplace_back();
            }
            index = child;
        }
        pending = index;
        if (game.decision.outcome != Outcome::ongoing) {
            nodes[index].expanded = nodes[index].terminal = true;
            nodes[index].raw                              = terminal_value(game);
            backup(nodes[index].raw);
        } else request = observe(game);
        co_return true;
    }
    void Search::Tree::accept(const Prediction& prediction) {
        auto& node    = nodes[pending];
        node.raw      = prediction.wdl[0] - prediction.wdl[2];
        node.expanded = true;
        float maximum = -std::numeric_limits<float>::infinity(), denominator{};
        for (const auto move : game.moves) maximum = std::max(maximum, prediction.logits[action(move, game.position.turn)]);
        for (const auto move : game.moves) {
            const float logit = prediction.logits[action(move, game.position.turn)];
            const float prior = std::exp(logit - maximum);
            denominator += prior;
            float gumbel{};
            if (pending == 0 && config.exploration) {
                const double uniform = (double(random() >> 11) + 0.5) / 9007199254740992.0;
                gumbel               = float(-std::log(-std::log(uniform)));
            }
            node.edges.push_back({move, logit, prior, gumbel});
        }
        for (auto& edge : node.edges) edge.prior /= denominator;
        ++leaves;
        if (pending) backup(node.raw);
    }
    void Search::Tree::backup(float value) {
        for (auto position = path.rbegin(); position != path.rend(); ++position) {
            value      = -value;
            auto& edge = nodes[position->first].edges[position->second];
            edge.sum += value;
            ++edge.visits;
            game.unplay();
        }
        path.clear();
        ++simulation;
    }
    SearchResult Search::Tree::finish() const {
        const auto& root = nodes[0];
        const auto q     = completed(root);
        int selected{}, most{};
        float best = -std::numeric_limits<float>::infinity(), maximum = best, denominator{};
        for (const auto& edge : root.edges) most = std::max(most, edge.visits);
        for (int choice = 0; choice < int(root.edges.size()); ++choice) {
            const auto& edge  = root.edges[choice];
            maximum           = std::max(maximum, edge.logit + q[choice]);
            const float score = config.simulations ? edge.gumbel + edge.logit + q[choice] : edge.logit;
            if ((!config.simulations || edge.visits == most) && score > best) {
                selected = choice;
                best     = score;
            }
        }
        SearchResult result{root.edges[selected].move, {}, root.raw, leaves};
        for (int choice = 0; choice < int(root.edges.size()); ++choice) denominator += std::exp(root.edges[choice].logit + q[choice] - maximum);
        for (int choice = 0; choice < int(root.edges.size()); ++choice) result.policy.push_back({std::uint16_t(action(root.edges[choice].move, game.position.turn)), std::exp(root.edges[choice].logit + q[choice] - maximum) / denominator});
        return result;
    }
    Search::Search(const int count, const int threads) : busy(count), jobs(count) {
        for (int worker = 0; worker < std::min(count, threads); ++worker)
            workers.emplace_back([this](const std::stop_token stop) {
                for (;;) {
                    int index{};
                    {
                        std::unique_lock lock{mutex};
                        condition.wait(lock, [&] { return stop.stop_requested() || !pending.empty(); });
                        if (stop.stop_requested()) return;
                        index = pending.front();
                        pending.pop_front();
                        jobs[index].stage = Stage::working;
                    }
                    auto& job  = jobs[index];
                    auto& tree = *job.tree;
                    Stage next = Stage::queued;
                    bool adjudicating{};
                    const auto prior_nodes = TaskContext::rule_nodes;
                    try {
                        Computation computation{tree.config.deadline, stop};
                        TaskContext::until        = std::chrono::steady_clock::now() + std::chrono::milliseconds{2};
                        TaskContext::adjudicating = job.adjudicating;
                        if (!job.operation) {
                            if (tree.nodes[0].expanded && tree.simulation >= tree.config.simulations) {
                                job.result    = tree.finish();
                                job.finishing = true;
                                if (job.commit) job.operation.emplace(tree.game.play_async(job.result.move));
                                else next = Stage::complete;
                            } else job.operation.emplace(tree.prepare());
                        }
                        if (job.operation) {
                            if (job.operation->resume()) {
                                job.operation.reset();
                                if (job.finishing) next = Stage::complete;
                                else if (tree.request) next = Stage::inference;
                            } else adjudicating = TaskContext::adjudicating;
                        }
                    } catch (...) {
                        const std::lock_guard lock{mutex};
                        if (!stop.stop_requested()) error = std::current_exception();
                        condition.notify_all();
                        return;
                    }
                    {
                        const std::lock_guard lock{mutex};
                        if (adjudicating && !job.adjudicating) job.rule_started = std::chrono::steady_clock::now();
                        job.rule_nodes += TaskContext::rule_nodes - prior_nodes;
                        job.simulations  = tree.simulation;
                        job.adjudicating = adjudicating;
                        job.stage        = next;
                        if (next == Stage::queued) pending.push_back(index);
                    }
                    condition.notify_all();
                }
            });
    }
    Search::~Search() {
        for (auto& worker : workers) worker.request_stop();
        condition.notify_all();
        for (auto& worker : workers) worker.join();
    }
    void Search::submit(const int actor, const Game& game, const SearchConfig config, const std::uint64_t seed, std::shared_ptr<const DeviceWeights> model, const bool commit) {
        auto& job = jobs[actor];
        job.operation.reset();
        job.tree.emplace(game, config, seed);
        job.model     = std::move(model);
        job.commit    = commit;
        job.finishing = job.adjudicating = false;
        busy[actor]                      = true;
        job.rule_nodes                   = 0;
        job.simulations                  = 0;
        {
            const std::lock_guard lock{mutex};
            job.stage = Stage::queued;
            pending.push_back(actor);
        }
        condition.notify_one();
    }
    std::vector<SearchCompleted> Search::advance(Network& network) {
        std::vector<SearchCompleted> completed;
        std::vector<int> ready;
        {
            std::unique_lock lock{mutex};
            condition.wait_for(lock, std::chrono::milliseconds{20}, [&] { return error || std::ranges::any_of(jobs, [](const Job& job) { return job.stage == Stage::inference || job.stage == Stage::complete; }); });
            if (error) std::rethrow_exception(error);
            if (std::ranges::any_of(jobs, [](const Job& job) { return job.stage == Stage::inference; })) {
                const auto target = std::min<std::ptrdiff_t>(64, std::ranges::count_if(jobs, [](const Job& job) { return job.stage != Stage::idle && job.stage != Stage::complete && !job.adjudicating; }));
                condition.wait_until(lock, std::chrono::steady_clock::now() + std::chrono::microseconds{250}, [&] { return error || std::ranges::count_if(jobs, [](const Job& job) { return job.stage == Stage::inference; }) >= std::max<std::ptrdiff_t>(1, target) || target == 0 && std::ranges::any_of(jobs, [](const Job& job) { return job.stage == Stage::complete; }); });
                if (error) std::rethrow_exception(error);
            }
            for (int index = 0; index < int(jobs.size()); ++index) {
                auto& job = jobs[index];
                if (job.stage == Stage::complete) {
                    completed.push_back({index, job.model->version, std::move(job.result), std::move(job.tree->game)});
                    job.stage   = Stage::idle;
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
            condition.notify_all();
            std::erase_if(ready, [&](const int index) { return jobs[index].model == model; });
        }
        {
            const std::lock_guard lock{mutex};
            activity = {};
            for (const auto& job : jobs) {
                activity.rule_nodes += job.rule_nodes;
                activity.simulations += job.simulations;
                if (job.adjudicating) {
                    ++activity.adjudicating;
                    activity.longest_seconds = std::max(activity.longest_seconds, std::chrono::duration<double>(std::chrono::steady_clock::now() - job.rule_started).count());
                }
            }
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
                auto activity      = tasks.activity;
                activity.completed = completed;
                activity.total     = games.size();
                progress(activity);
                next_progress = std::chrono::steady_clock::now() + std::chrono::seconds{1};
            }
        }
        if (progress) {
            auto activity      = tasks.activity;
            activity.completed = games.size();
            activity.total     = games.size();
            progress(activity);
        }
        return result;
    }
} // namespace chess::ai
