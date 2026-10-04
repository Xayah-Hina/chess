module chess.ai.tree;
import std;
namespace chess::ai {
    Tree::Tree(const Game& initial, const SearchConfig settings, const std::uint64_t seed) : game{initial}, config{settings}, random{seed}, sample_size{settings.candidates} {
        nodes[0].decision = game.decision;
        budget = sample_size > 1 ? std::max(1, int(std::floor(config.simulations / (std::log2(sample_size) * sample_size)))) : config.simulations;
    }
    std::vector<float> Tree::completed(const Node& node) const {
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
    void Tree::play_cached(const Move move, const Decision& decision) {
        const auto before = game.position;
        const auto undo = make_move(game.position, move);
        game.history.push_back({before, undo, game.decision});
        game.decision = decision;
        game.moves = decision.outcome == Outcome::ongoing ? legal_moves(game.position) : std::vector<Move>{};
    }
    void Tree::prepare() {
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
    void Tree::accept(const Prediction& prediction) {
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
    void Tree::backup(float value) {
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
    SearchResult Tree::finish() const {
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
}
