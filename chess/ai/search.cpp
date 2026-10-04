module chess.ai.search;
import std;
namespace chess::ai {
    Search::Tree::Tree(const Game& initial, const SearchConfig settings, const std::uint64_t seed) : game{initial}, config{settings}, random{seed}, sample_size{settings.candidates} {
        nodes[0].decision = game.decision;
        budget = sample_size > 1 ? std::max(1, int(std::floor(config.simulations / (std::log2(sample_size) * sample_size)))) : config.simulations;
    }
    std::vector<float> Search::Tree::completed(const Node& node) const {
        int maximum{};
        float weighted{}, mass{};
        for (const auto& edge : node.edges) {
            maximum = std::max(maximum, edge.visits);
            if (edge.visits) {
                weighted += edge.prior * edge.sum / edge.visits;
                mass += edge.prior;
            }
        }
        const float mixed = mass ? (node.raw + config.simulations * weighted / mass) / (config.simulations + 1) : node.raw;
        std::vector<float> values;
        for (const auto& edge : node.edges) values.push_back((50.0F + maximum) * (edge.visits ? edge.sum / edge.visits : mixed));
        return values;
    }
    void Search::Tree::play_cached(const Move move, const Decision& decision) {
        const auto before = game.position;
        const auto undo = make_move(game.position, move);
        game.history.push_back({before, undo, game.decision});
        game.decision = decision;
        game.moves = decision.outcome == Outcome::ongoing ? legal_moves(game.position) : std::vector<Move>{};
    }
    void Search::Tree::prepare() {
        request.reset();
        int index{};
        while (nodes[index].expanded && !nodes[index].edges.empty()) {
            if (Computation::cancellation.stop_requested() || config.cancellation.stop_requested() || std::chrono::steady_clock::now() >= config.deadline) throw Interrupted{};
            const auto& node = nodes[index];
            int selected{};
            if (!index) {
                selected = *std::ranges::min_element(candidates, [&](const int left, const int right) {
                    const auto& a = node.edges[left];
                    const auto& b = node.edges[right];
                    return a.visits != b.visits ? a.visits < b.visits : a.logit + a.gumbel > b.logit + b.gumbel;
                });
            } else {
                int visits{}, visited{};
                float sum{}, best = -std::numeric_limits<float>::infinity(), best_prior = best;
                for (const auto& edge : node.edges) {
                    visits += edge.visits;
                    if (!edge.visits) continue;
                    sum += edge.sum / edge.visits;
                    ++visited;
                }
                const float initial = (sum - 1) / (visited + 1);
                const float exploration = (1.25F + std::log((1.0F + visits + 19652) / 19652)) * std::sqrt(float(visits));
                for (int choice = 0; choice < int(node.edges.size()); ++choice) {
                    const auto& edge = node.edges[choice];
                    const float score = (edge.visits ? edge.sum / edge.visits : initial) + exploration * edge.prior / (edge.visits + 1);
                    if (score < best || score == best && edge.prior <= best_prior) continue;
                    best = score;
                    best_prior = edge.prior;
                    selected = choice;
                }
            }
            const auto move = nodes[index].edges[selected].move;
            path.emplace_back(index, selected);
            int child = nodes[index].edges[selected].child;
            if (child < 0) {
                game.play(move);
                child = int(nodes.size());
                nodes[index].edges[selected].child = child;
                nodes.emplace_back();
                nodes[child].decision = game.decision;
            } else play_cached(move, nodes[child].decision);
            index = child;
        }
        pending = index;
        if (game.decision.outcome == Outcome::ongoing) request = observe(game);
        else {
            nodes[index].expanded = true;
            nodes[index].raw = terminal_value(game);
            backup(nodes[index].raw);
        }
    }
    void Search::Tree::accept(const Prediction& prediction) {
        auto& node = nodes[pending];
        node.raw = prediction.wdl[0] - prediction.wdl[2];
        node.expanded = true;
        const float maximum = std::ranges::max(prediction.logits);
        float denominator{};
        for (const float logit : prediction.logits) denominator += std::exp(logit - maximum);
        for (const auto move : game.moves) {
            const float logit = prediction.logits[action(move, game.position.turn)];
            const float prior = std::exp(logit - maximum);
            float gumbel{};
            if (!pending && config.exploration) {
                const double uniform = (double(random() >> 11) + 0.5) / 9007199254740992.0;
                gumbel = float(-std::log(-std::log(uniform)));
            }
            node.edges.push_back({move, logit, prior, gumbel});
        }
        for (auto& edge : node.edges) edge.prior /= denominator;
        std::ranges::sort(node.edges, {}, [](const Edge& edge) { return -edge.prior; });
        ++leaves;
        if (pending) backup(node.raw);
        else {
            candidates.resize(node.edges.size());
            std::iota(candidates.begin(), candidates.end(), 0);
            std::ranges::sort(candidates, [&](const int left, const int right) { return node.edges[left].logit + node.edges[left].gumbel > node.edges[right].logit + node.edges[right].gumbel; });
            candidates.resize(std::min(candidates.size(), std::size_t(config.candidates)));
        }
    }
    void Search::Tree::backup(float value) {
        for (auto position = path.rbegin(); position != path.rend(); ++position) {
            value = -value;
            auto& edge = nodes[position->first].edges[position->second];
            edge.sum += value;
            ++edge.visits;
            game.unplay();
        }
        path.clear();
        ++simulation;
        const auto& root = nodes[0];
        if (sample_size <= 2 || !std::ranges::all_of(candidates, [&](const int candidate) { return root.edges[candidate].visits >= budget; })) return;
        const int next = int(std::floor(config.simulations / (std::log2(config.candidates) * sample_size / 2)));
        if (!next) return;
        const auto values = completed(root);
        std::ranges::sort(candidates, [&](const int left, const int right) {
            const auto& a = root.edges[left];
            const auto& b = root.edges[right];
            const float sa = a.visits ? a.logit + a.gumbel + values[left] : -std::numeric_limits<float>::infinity();
            const float sb = b.visits ? b.logit + b.gumbel + values[right] : -std::numeric_limits<float>::infinity();
            return sa > sb;
        });
        sample_size /= 2;
        candidates.resize(std::min(candidates.size(), std::size_t(sample_size)));
        budget = root.edges[candidates.front()].visits + next;
    }
    SearchResult Search::Tree::finish() const {
        const auto& root = nodes[0];
        const auto values = completed(root);
        int selected{};
        float best = -std::numeric_limits<float>::infinity();
        if (config.simulations) {
            for (const int candidate : candidates) {
                const auto& edge = root.edges[candidate];
                const float score = edge.visits ? edge.logit + edge.gumbel + values[candidate] : -std::numeric_limits<float>::infinity();
                if (score <= best) continue;
                best = score;
                selected = candidate;
            }
        } else {
            for (int index = 0; index < int(root.edges.size()); ++index)
                if (root.edges[index].logit > best) {
                    best = root.edges[index].logit;
                    selected = index;
                }
        }
        float maximum = -std::numeric_limits<float>::infinity(), denominator{};
        for (int index = 0; index < int(root.edges.size()); ++index) maximum = std::max(maximum, root.edges[index].logit + values[index]);
        for (int index = 0; index < int(root.edges.size()); ++index) denominator += std::exp(root.edges[index].logit + values[index] - maximum);
        SearchResult result{root.edges[selected].move, {}, root.raw, leaves};
        for (int index = 0; index < int(root.edges.size()); ++index) result.policy.push_back({std::uint16_t(action(root.edges[index].move, game.position.turn)), std::exp(root.edges[index].logit + values[index] - maximum) / denominator});
        return result;
    }
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
                                const auto chosen = std::ranges::find(tree.nodes[0].edges, job.result.move, &Edge::move);
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
