module chess.ai.selfplay;
import std;
namespace chess::ai {
    SelfPlay::SelfPlay(const int count, const std::uint64_t seed) : actors(count), random{seed} {}
    std::size_t SelfPlay::advance(Network& network, Replay& replay, const SearchConfig config) {
        if (config.cancellation.stop_requested()) throw Interrupted{};
        if (!searches) searches.emplace(int(actors.size()), config.threads);
        if (!evaluator) evaluator.emplace(int(actors.size()), 0);
        for (int index = 0; index < int(actors.size()); ++index) {
            if (searches->busy[index]) continue;
            auto& actor = actors[index];
            if (!actor.model) {
                if (!snapshot || snapshot->version != network.version) snapshot = network.freeze();
                actor.model = snapshot;
                actor.seed  = random();
            }
            searches->submit(index, actor.game, config, actor.seed, actor.model, true);
        }
        std::size_t produced{};
        for (auto& choice : searches->advance(*evaluator)) {
            auto& actor            = actors[choice.actor];
            const auto observation = observe(actor.game);
            actor.game             = std::move(choice.game);
            actor.pending.push_back({observation, std::move(choice.result.policy), choice.version});
            actor.model.reset();
            ++plies;
            leaves += choice.result.leaves;
            if (actor.game.decision.outcome == Outcome::ongoing) continue;
            const auto outcome = actor.game.decision.outcome;
            ++games;
            ++outcomes[outcome == Outcome::red_win ? 0 : outcome == Outcome::draw ? 1 : 2];
            const int winner = outcome == Outcome::red_win ? 0 : 1;
            for (std::size_t sample = 0; sample < actor.pending.size(); ++sample) actor.pending[sample].result = std::uint8_t(outcome == Outcome::draw ? 1 : int(actor.game.history[sample].before.turn) == winner ? 0 : 2);
            produced += actor.pending.size();
            replay.append(std::move(actor.pending));
            actor = Actor{};
        }
        activity = searches->activity;
        return produced;
    }
    void SelfPlay::stop() {
        searches.reset();
        activity = {};
    }
} // namespace chess::ai
