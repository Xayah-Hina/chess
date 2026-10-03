module tools.trainer.headless;
import tools.trainer.session;
import chess.ai.headless;
import std;
namespace tools::trainer {
    int run(const std::span<const std::string_view> arguments) {
        chess::benchmark::Config config;
        if (arguments.empty() || arguments.front() == "help") {
            std::println("Headless tool for Trainer automation and verification.\nsession --run DIR [settings]: opens paused; accepts stdin commands\nCommands: start, pause, save, short, deep, continue-benchmark, remove ID, status, quit\nbenchmark --profile short|deep --model FILE --run DIR\nbenchmark-prepare --suite FILE\nthink --model FILE [--moves \"a0a1 a9a8\"]\nSettings: --actors {} --batch {} --threads {} --simulations {} --candidates {}\n--generation-plies {} --seed {} --replay {} --learning-rate {}\n--weight-decay {} --presentations {} --save-seconds {}\n--suite FILE --pairs {} --arena-batch {} --diagnostic-positions {}\n--engine FILE --engine-model FILE --initial FILE --against FILE\n--opponent random|material-1|material-3|checkpoint|PF-N500|PF-N2000|PF-N10000|PF-N50000", config.training.actors, config.training.batch, config.training.threads, config.training.simulations, config.training.candidates, config.training.generation_plies, config.training.seed, config.training.replay_capacity,
                config.training.learning_rate, config.training.weight_decay, config.training.presentations, config.save_seconds, config.opening_pairs, config.arena_batch, config.diagnostic_positions);
            return 0;
        }
        const auto command = arguments.front();
        if (command == "think") return chess::ai::run(arguments);
        std::filesystem::path model, comparison, initial;
        std::string opponent, profile{"deep"};
        for (std::size_t index = 1; index < arguments.size(); ++index) {
            const auto option = arguments[index];
            const std::string value{arguments[++index]};
            if (option == "--run") config.run = value;
            else if (option == "--suite") config.suite = value;
            else if (option == "--actors") config.training.actors = std::stoi(value);
            else if (option == "--batch") config.training.batch = std::stoi(value);
            else if (option == "--threads") config.training.threads = std::stoi(value);
            else if (option == "--simulations") config.training.simulations = std::stoi(value);
            else if (option == "--candidates") config.training.candidates = std::stoi(value);
            else if (option == "--generation-plies") config.training.generation_plies = std::stoi(value);
            else if (option == "--seed") config.training.seed = std::stoull(value);
            else if (option == "--replay") config.training.replay_capacity = std::stoull(value);
            else if (option == "--learning-rate") config.training.learning_rate = std::stof(value);
            else if (option == "--weight-decay") config.training.weight_decay = std::stof(value);
            else if (option == "--presentations") config.training.presentations = std::stof(value);
            else if (option == "--save-seconds") config.save_seconds = std::stod(value);
            else if (option == "--pairs") config.opening_pairs = std::stoi(value);
            else if (option == "--arena-batch") config.arena_batch = std::stoi(value);
            else if (option == "--diagnostic-positions") config.diagnostic_positions = std::stoi(value);
            else if (option == "--engine") config.engine = value;
            else if (option == "--engine-model") config.engine_model = value;
            else if (option == "--model") model = value;
            else if (option == "--initial") initial = value;
            else if (option == "--against") comparison = value;
            else if (option == "--opponent") opponent = value;
            else if (option == "--profile") profile = value;
            else throw std::runtime_error{std::format("Unknown option: {}", option)};
        }
        config.run          = std::filesystem::absolute(config.run);
        config.suite        = std::filesystem::absolute(config.suite);
        config.engine       = std::filesystem::absolute(config.engine);
        config.engine_model = std::filesystem::absolute(config.engine_model);
        if (command == "benchmark-prepare") {
            chess::benchmark::Arena arena{config, true};
            arena.prepare();
            return 0;
        }
        if (command == "benchmark") {
            if (profile != "short" && profile != "deep") throw std::runtime_error{"Benchmark profile must be short or deep"};
            chess::benchmark::Evaluation evaluation{config, profile == "deep"};
            model = std::filesystem::absolute(model);
            if (!comparison.empty()) comparison = std::filesystem::absolute(comparison);
            initial = initial.empty() ? model.parent_path() / "weights-initial.bin" : std::filesystem::absolute(initial);
            evaluation.begin(model, comparison, initial, opponent);
            const auto log = [&] {
                evaluation.report("running");
                std::println("benchmark profile={} probes={}/{} positions={} seconds={:.1f} stage=\"{}\" searches={}/{} simulations={} rule_tasks={} rule_nodes={} longest_rule={:.1f}s", evaluation.deep ? "deep" : "short", evaluation.probe_index, evaluation.deep ? 3 : 2, evaluation.probe_index < 3 ? evaluation.probes[evaluation.probe_index].positions : 0, evaluation.seconds + evaluation.running_seconds, evaluation.stage, evaluation.activity.completed, evaluation.activity.total, evaluation.activity.simulations, evaluation.activity.adjudicating, evaluation.activity.rule_nodes, evaluation.activity.longest_seconds);
                std::cout.flush();
            };
            evaluation.heartbeat = log;
            double next_log{};
            while (!evaluation.complete) {
                evaluation.advance();
                if (evaluation.seconds >= next_log) {
                    log();
                    next_log = evaluation.seconds + 5;
                }
            }
            evaluation.save();
            evaluation.report("complete");
            std::println("benchmark complete: {}", config.run.string());
            return 0;
        }
        if (command != "session") throw std::runtime_error{std::format("Unknown command: {}", command)};
        Session session;
        session.submit({Action::open, config});
        for (std::string line; std::getline(std::cin, line);) {
            std::istringstream text{line};
            std::string action;
            text >> action;
            if (action == "start") session.submit({Action::start, config});
            else if (action == "pause") session.submit({Action::pause});
            else if (action == "save") session.submit({Action::save});
            else if (action == "short") session.submit({Action::short_benchmark});
            else if (action == "deep") session.submit({Action::deep_benchmark});
            else if (action == "continue-benchmark") session.submit({Action::resume_benchmark});
            else if (action == "remove") {
                std::uint64_t id{};
                text >> id;
                session.submit({Action::remove_benchmark, {}, id});
            } else if (action == "quit") break;
            else if (action == "status") {
                const auto state = session.drain();
                std::println("state={} steps={} games={} plies={} replay={} pending={} benchmark={} queued={} rule_tasks={} longest_rule={:.1f}s", phases[int(state.phase)], state.steps, state.games, state.plies, state.replay, state.pending_samples, state.active ? std::to_string(state.active->request.id) : "none", state.queue.size(), state.search.adjudicating, state.search.longest_seconds);
                if (state.active) {
                    const auto& benchmark = *state.active;
                    std::uint64_t completed_games{}, plies{};
                    for (const auto& match : benchmark.matches) {
                        completed_games += match.completed_games;
                        plies += match.plies;
                    }
                    std::println("matches games={} plies={}", completed_games, plies);
                    std::println("benchmark seconds={:.1f} probes={}/{} stage=\"{}\" searches={}/{} simulations={} rule_nodes={}", benchmark.seconds, benchmark.probe_index, benchmark.probe_target, benchmark.stage, benchmark.activity.completed, benchmark.activity.total, benchmark.activity.simulations, benchmark.activity.rule_nodes);
                }
                if (!state.error.empty()) throw std::runtime_error{state.error};
                std::cout.flush();
            } else if (!action.empty()) throw std::runtime_error{std::format("Unknown session action: {}", action)};
        }
        session.submit({Action::close});
        for (;;) {
            const auto state = session.drain();
            if (!state.error.empty()) throw std::runtime_error{state.error};
            if (state.closed) {
                std::println("saved: {} version={} steps={} games={}", (state.config.run / "checkpoint.bin").string(), state.version, state.steps, state.games);
                return 0;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds{50});
        }
    }
} // namespace tools::trainer
