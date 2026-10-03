module;
#include <Windows.h>
#include <bcrypt.h>
#include <cuda_runtime.h>
#include <cudnn.h>
#include <nlohmann/json.hpp>
module chess.benchmark.arena;
import chess.benchmark.artifact;
import std;
namespace chess::benchmark {
    std::string fingerprint(const std::filesystem::path& path) {
        const auto status = [](const NTSTATUS result) {
            if (result < 0) throw std::runtime_error{std::format("SHA256 status {}", result)};
        };
        std::unique_ptr<std::remove_pointer_t<BCRYPT_HASH_HANDLE>, decltype(&BCryptDestroyHash)> hash{nullptr, BCryptDestroyHash};
        status(BCryptCreateHash(BCRYPT_SHA256_ALG_HANDLE, std::out_ptr(hash), nullptr, 0, nullptr, 0, 0));
        std::ifstream file{path, std::ios::binary};
        file.exceptions(std::ios::badbit);
        if (!file.is_open()) throw std::runtime_error{std::format("Open artifact: {}", path.string())};
        std::array<char, 65536> buffer{};
        while (file.read(buffer.data(), buffer.size()) || file.gcount()) status(BCryptHashData(hash.get(), reinterpret_cast<PUCHAR>(buffer.data()), ULONG(file.gcount()), 0));
        std::array<unsigned char, 32> digest{};
        status(BCryptFinishHash(hash.get(), digest.data(), ULONG(digest.size()), 0));
        std::string result;
        for (const auto byte : digest) result += std::format("{:02x}", byte);
        return result;
    }

    namespace {
        nlohmann::json configuration(const Config& config) {
            cudaDeviceProp gpu{};
            ai::check(cudaGetDeviceProperties(&gpu, 0));
            int driver{};
            ai::check(cudaDriverGetVersion(&driver));
            return {{"seed", config.training.seed}, {"cpu_threads", config.training.threads}, {"simulations", config.training.simulations}, {"root_candidates", config.training.candidates}, {"network", "ResNet-128x6/input128/policy4500/valueWDL"}, {"exploration", false}, {"rules", "CCA-2020/core"}, {"precision", "BF16/FP32; FP32 convolution weight gradients"}, {"engine_rules", "Pikafish internal judging; final result adjudicated by core"}, {"engine_threads", 1}, {"engine_hash_mb", 16}, {"ponder", false}, {"material_values", {0, 0, 2, 2, 4, 9, 4, 1}}, {"gpu", gpu.name}, {"cuda_driver", driver}, {"cuda", CUDART_VERSION}, {"cudnn", cudnnGetVersion()}, {"source_revision", CHESS_SOURCE_REVISION}, {"source_sha256", CHESS_SOURCE_HASH}};
        }
    } // namespace
    Statistics statistics(const ArenaJob& job) {
        Statistics result;
        for (std::size_t pair = 0; pair < job.matches.size() / 2; ++pair) {
            if (!job.matches[pair * 2].complete || !job.matches[pair * 2 + 1].complete) continue;
            int sum{};
            ++result.pairs;
            for (int color = 0; color < 2; ++color) {
                const auto& match  = job.matches[pair * 2 + color];
                const auto outcome = match.game.decision.outcome;
                const int category = outcome == Outcome::draw ? 1 : (outcome == Outcome::red_win) == match.candidate_red ? 0 : 2;
                ++result.wdl[category];
                ++(match.candidate_red ? result.red : result.black)[category];
                sum += category == 0 ? 2 : category == 1 ? 1 : 0;
                result.plies += match.game.history.size() - match.opening_length;
            }
            ++result.frequencies[sum];
        }
        if (!result.pairs) {
            result.score = result.lower = result.upper = std::numeric_limits<double>::quiet_NaN();
            return result;
        }
        result.score = double(result.wdl[0] * 2 + result.wdl[1]) / (result.pairs * 4);
        double unconstrained{};
        for (const auto count : result.frequencies)
            if (count) unconstrained += count * std::log(double(count) / result.pairs);
        const auto likelihood = [&](const double mean) {
            double low = -1 / (1 - mean), high = 1 / mean;
            const auto derivative = [&](const double lambda) {
                double value{};
                for (int index = 0; index < 5; ++index)
                    if (result.frequencies[index]) value += double(result.frequencies[index]) * (index / 4.0 - mean) / (1 + lambda * (index / 4.0 - mean));
                return value;
            };
            double lambda{};
            if (!result.frequencies[4] && derivative(low) <= 0) lambda = low;
            else if (!result.frequencies[0] && derivative(high) >= 0) lambda = high;
            else {
                for (int iteration = 0; iteration < 80; ++iteration) {
                    const double middle = (low + high) / 2;
                    if (derivative(middle) > 0) low = middle;
                    else high = middle;
                }
                lambda = (low + high) / 2;
            }
            double value{};
            for (int index = 0; index < 5; ++index)
                if (result.frequencies[index]) value += result.frequencies[index] * std::log(double(result.frequencies[index]) / result.pairs / (1 + lambda * (index / 4.0 - mean)));
            return 2 * (unconstrained - value);
        };
        double low = 0, high = result.score;
        for (int iteration = 0; iteration < 64; ++iteration) {
            const double middle = (low + high) / 2;
            if (likelihood(middle) > 3.841458820694124) low = middle;
            else high = middle;
        }
        result.lower = high;
        low          = result.score;
        high         = 1;
        for (int iteration = 0; iteration < 64; ++iteration) {
            const double middle = (low + high) / 2;
            if (likelihood(middle) > 3.841458820694124) high = middle;
            else low = middle;
        }
        result.upper = low;
        return result;
    }
    Arena::Arena(const Config& settings, const bool preparation) : config{settings}, random{settings.training.seed ^ 0xb9a7e31c4d52ULL} {
        if (!preparation) {
            ai::Archive archive{config.suite, true, 6};
            persist_suite(archive);
            suite_hash  = fingerprint(config.suite);
            suite_ready = true;
            return;
        }
        for (auto* suite : {&openings, &holdout}) {
            for (int pair = 0; pair < config.opening_pairs; ++pair) {
                Game game;
                std::vector<Move> prefix;
                const int count = std::uniform_int_distribution<int>{4, 10}(random);
                for (int ply = 0; ply < count; ++ply) {
                    const auto move = baseline(game, 0, random);
                    prefix.push_back(move);
                    game.play(move);
                }
                suite->push_back(std::move(prefix));
            }
        }
    }
    void Arena::prepare() {
        std::filesystem::create_directories(config.suite.parent_path());
        auto checkpoint = config.suite;
        checkpoint += ".prepare";
        if (std::filesystem::exists(checkpoint)) {
            ai::Archive archive{checkpoint, true, 7};
            persist(archive);
        }
        double next_save{};
        const auto start = std::chrono::steady_clock::now();
        try {
            while (!suite_ready) {
                mine();
                const double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
                if (elapsed >= next_save) {
                    auto temporary = checkpoint;
                    temporary += ".tmp";
                    {
                        ai::Archive archive{temporary, false, 7};
                        persist(archive);
                    }
                    publish(temporary, checkpoint);
                    std::println("prepare elapsed={:.1f}s short={}/{} holdout={}/{}", elapsed, diagnostics.size(), config.diagnostic_positions, holdout_diagnostics.size(), config.diagnostic_positions);
                    std::cout.flush();
                    next_save = elapsed + 10;
                }
            }
        } catch (const Interrupted&) {
        }
        auto temporary = checkpoint;
        temporary += ".tmp";
        {
            ai::Archive archive{temporary, false, 7};
            persist(archive);
        }
        publish(temporary, checkpoint);
        if (suite_ready) {
            temporary = config.suite;
            temporary += ".tmp";
            {
                ai::Archive archive{temporary, false, 6};
                persist_suite(archive);
            }
            publish(temporary, config.suite);
            std::println("prepared {}: short={} holdout={} opening_pairs={}", config.suite.string(), diagnostics.size(), holdout_diagnostics.size(), openings.size());
        } else std::println("preparation saved; repeat benchmark-prepare to continue");
    }
    void Arena::persist_suite(ai::Archive& archive) {
        archive.sequence(openings);
        archive.sequence(holdout);
        for (auto* suite : {&diagnostics, &holdout_diagnostics}) {
            std::uint64_t count = suite->size();
            archive.pod(count);
            if (archive.reading) suite->resize(count);
            for (auto& diagnostic : *suite) {
                ai::serialize(archive, diagnostic.game);
                archive.sequence(diagnostic.correct);
                archive.pod(diagnostic.category);
            }
        }
    }
    void Arena::enqueue(const std::filesystem::path& model, const std::filesystem::path& previous) {
        std::filesystem::create_directories(config.run);
        const int pairs    = config.opening_pairs;
        const auto add_job = [&](const std::string& name, const Opponent opponent, const int depth, const int nodes, const std::filesystem::path& reference, const bool raw) {
            ArenaJob job;
            job.name           = std::format("{}-deep-{}-{}-holdout", model.stem().string(), name, raw ? "policy" : "search");
            job.candidate      = model.string();
            job.reference      = reference.string();
            job.candidate_hash = fingerprint(model);
            if (!reference.empty()) job.reference_hash = fingerprint(reference);
            job.opponent   = opponent;
            job.depth      = depth;
            job.nodes      = nodes;
            job.pairs      = pairs;
            job.raw_policy = raw;
            job.holdout    = true;
            for (int pair = 0; pair < pairs; ++pair)
                for (int color = 0; color < 2; ++color) {
                    Match match;
                    match.candidate_red = color == 0;
                    match.random.seed(config.training.seed ^ (std::uint64_t(pair) << 16) ^ std::uint64_t(depth * 13 + nodes + color));
                    match.opening_length = holdout[pair].size();
                    for (const auto move : holdout[pair]) match.game.play(move);
                    job.matches.push_back(std::move(match));
                }
            jobs.push_back(std::move(job));
        };
        add_job("random", Opponent::random, 0, 0, {}, false);
        add_job("material-1", Opponent::material, 1, 0, {}, false);
        if (!previous.empty() && model != previous) add_job("previous", Opponent::checkpoint, 0, 0, previous, false);
        if (!initial.empty() && model != initial && previous != initial) add_job("initial", Opponent::checkpoint, 0, 0, initial, false);
        add_job("material-3", Opponent::material, 3, 0, {}, false);
        add_job("random", Opponent::random, 0, 0, {}, true);
        if (!previous.empty() && model != previous) add_job("previous", Opponent::checkpoint, 0, 0, previous, true);
        const std::array levels{500, 2000, 10000, 50000};
        for (const int nodes : levels) add_job(std::format("PF-N{}", nodes), Opponent::pikafish, 0, nodes, {}, false);
        report();
    }
    Advance Arena::advance(ai::Network& model, std::function<void(std::string_view, const ai::SearchActivity&)> progress) {
        const auto start = std::chrono::steady_clock::now();
        ArenaJob* charged{};
        const auto work = [&] {
            auto position = std::ranges::find_if(jobs, [](const ArenaJob& job) { return !job.reported; });
            if (position == jobs.end()) return false;
            auto& job = *position;
            charged   = &job;
            if (job.opponent == Opponent::checkpoint) {
                if (!reference) reference = std::make_unique<ai::Network>(config.arena_batch, config.training.seed);
                if (loaded_reference != job.reference) {
                    reference->restore(ai::load_weights(job.reference));
                    loaded_reference = job.reference;
                }
            }
            std::vector<std::size_t> active, own, other;
            std::vector<Game*> own_games, other_games;
            for (std::size_t offset = 0; offset < job.matches.size() && int(active.size()) < config.arena_batch; ++offset) {
                const auto index = (job.cursor + offset) % job.matches.size();
                auto& match      = job.matches[index];
                if (match.complete) continue;
                active.push_back(index);
                if (match.pending) continue;
                if ((match.game.position.turn == Color::red) == match.candidate_red) {
                    own.push_back(index);
                    own_games.push_back(&match.game);
                } else {
                    other.push_back(index);
                    other_games.push_back(&match.game);
                }
            }
            if (!active.empty()) job.cursor = (active.back() + 1) % job.matches.size();
            ai::SearchConfig search_config{job.raw_policy ? 0 : config.training.simulations, config.training.candidates, config.training.threads, false, Computation::deadline};
            if (!own.empty()) {
                const auto prior = random;
                std::vector<ai::SearchResult> moves;
                try {
                    moves = ai::search(model, own_games, search_config, random, [&](const ai::SearchActivity& activity) {
                        if (progress) progress("Candidate search", activity);
                    });
                } catch (const Interrupted&) {
                    random = prior;
                    throw;
                }
                for (std::size_t index = 0; index < own.size(); ++index) {
                    auto& match        = job.matches[own[index]];
                    match.pending_move = moves[index].move;
                    match.pending      = true;
                }
            }
            if (job.opponent == Opponent::checkpoint && !other.empty()) {
                const auto prior = random;
                std::vector<ai::SearchResult> moves;
                try {
                    moves = ai::search(*reference, other_games, search_config, random, [&](const ai::SearchActivity& activity) {
                        if (progress) progress("Reference search", activity);
                    });
                } catch (const Interrupted&) {
                    random = prior;
                    throw;
                }
                for (std::size_t index = 0; index < other.size(); ++index) {
                    auto& match        = job.matches[other[index]];
                    match.pending_move = moves[index].move;
                    match.pending      = true;
                }
            } else {
                const auto deadline     = Computation::deadline;
                const auto cancellation = Computation::cancellation;
                std::atomic_size_t cursor{}, finished{}, moves_done{};
                std::exception_ptr failure;
                std::mutex failure_mutex;
                std::vector<std::jthread> workers;
                for (int worker = 0; worker < std::min(config.training.threads, int(other.size())); ++worker)
                    workers.emplace_back([&] {
                        Computation computation{deadline, cancellation};
                        for (auto offset = cursor.fetch_add(1); offset < other.size(); offset = cursor.fetch_add(1)) {
                            auto& match      = job.matches[other[offset]];
                            const auto prior = match.random;
                            try {
                                if (cancellation.stop_requested() || std::chrono::steady_clock::now() >= deadline) throw Interrupted{};
                                if (job.opponent == Opponent::pikafish) {
                                    if (!match.engine) match.engine = std::make_unique<Pikafish>(config.engine, config.engine_model);
                                    match.pending_move = match.engine->choose(match.game, match.opening_length, match.candidate_red ? Color::black : Color::red, job.nodes);
                                    match.engine_nodes += match.engine->nodes;
                                } else match.pending_move = baseline(match.game, job.depth, match.random);
                                match.pending = true;
                                ++moves_done;
                            } catch (...) {
                                match.random = prior;
                                match.engine.reset();
                                std::lock_guard lock{failure_mutex};
                                if (!failure) failure = std::current_exception();
                                break;
                            }
                        }
                        ++finished;
                    });
                auto next_progress = std::chrono::steady_clock::now();
                while (finished.load() < workers.size()) {
                    if (progress && std::chrono::steady_clock::now() >= next_progress) {
                        ai::SearchActivity activity;
                        activity.completed = moves_done.load();
                        activity.total     = other.size();
                        progress("Opponent moves", activity);
                        next_progress = std::chrono::steady_clock::now() + std::chrono::seconds{1};
                    }
                    std::this_thread::sleep_for(std::chrono::milliseconds{2});
                }
                workers.clear();
                if (job.opponent == Opponent::pikafish) {
                    for (const auto index : other)
                        if (job.matches[index].engine) {
                            engine_identity = job.matches[index].engine->identity;
                            engine_options  = job.matches[index].engine->options;
                            break;
                        }
                    if (engine_hash.empty()) {
                        engine_hash = fingerprint(config.engine);
                        model_hash  = fingerprint(config.engine_model);
                    }
                }
                if (failure) std::rethrow_exception(failure);
            }
            for (const auto index : active) {
                auto& match = job.matches[index];
                if (match.pending) {
                    auto operation         = match.game.play_async(match.pending_move);
                    const auto prior_slice = TaskContext::until;
                    const auto prior_nodes = TaskContext::rule_nodes;
                    const auto rule_start  = std::chrono::steady_clock::now();
                    auto next_progress     = rule_start;
                    try {
                        for (;;) {
                            TaskContext::until = std::chrono::steady_clock::now() + std::chrono::milliseconds{2};
                            if (operation.resume()) break;
                            if (progress && std::chrono::steady_clock::now() >= next_progress) {
                                ai::SearchActivity activity;
                                activity.adjudicating    = 1;
                                activity.rule_nodes      = TaskContext::rule_nodes - prior_nodes;
                                activity.longest_seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - rule_start).count();
                                progress("Move adjudication", activity);
                                next_progress = std::chrono::steady_clock::now() + std::chrono::seconds{1};
                            }
                        }
                    } catch (...) {
                        TaskContext::until = prior_slice;
                        throw;
                    }
                    TaskContext::until = prior_slice;
                    match.pending      = false;
                }
                match.complete = match.game.decision.outcome != Outcome::ongoing;
                if (match.complete) match.engine.reset();
            }
            return true;
        };
        bool progressed = true, interrupted{};
        try {
            progressed = work();
        } catch (const Interrupted&) {
            interrupted = true;
            if (charged)
                for (auto& match : charged->matches) {
                    match.complete = match.game.decision.outcome != Outcome::ongoing;
                    if (match.complete) match.engine.reset();
                }
        }
        const double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        if (charged) charged->seconds += elapsed;
        seconds += elapsed;
        if (charged) write_job(*charged);
        return interrupted ? Advance::interrupted : progressed ? Advance::progress : Advance::idle;
    }
    void Arena::persist(ai::Archive& archive) {
        persist_suite(archive);
        archive.pod(seconds);
        archive.pod(suite_ready);
        archive.pod(diagnostic_holdout);
        archive.sequence(suite_hash);
        archive.sequence(initial);
        ai::serialize(archive, miner);
        std::string generator;
        if (!archive.reading) {
            std::ostringstream output;
            output << random;
            generator = output.str();
        }
        archive.sequence(generator);
        if (archive.reading) std::istringstream{generator} >> random;
        archive.sequence(engine_identity);
        archive.sequence(engine_options);
        archive.sequence(engine_hash);
        archive.sequence(model_hash);
        std::uint64_t count;
        count = jobs.size();
        archive.pod(count);
        if (archive.reading) jobs.resize(count);
        for (auto& job : jobs) {
            archive.sequence(job.name);
            archive.sequence(job.candidate);
            archive.sequence(job.reference);
            archive.sequence(job.candidate_hash);
            archive.sequence(job.reference_hash);
            archive.pod(job.opponent);
            archive.pod(job.depth);
            archive.pod(job.nodes);
            archive.pod(job.pairs);
            archive.pod(job.raw_policy);
            archive.pod(job.holdout);
            archive.pod(job.reported);
            archive.pod(job.cursor);
            archive.pod(job.seconds);
            count = job.matches.size();
            archive.pod(count);
            if (archive.reading) job.matches.resize(count);
            for (auto& match : job.matches) {
                ai::serialize(archive, match.game);
                archive.pod(match.candidate_red);
                archive.pod(match.complete);
                archive.pod(match.opening_length);
                archive.pod(match.engine_nodes);
                archive.pod(match.pending_move);
                archive.pod(match.pending);
                generator.clear();
                if (!archive.reading) {
                    std::ostringstream output;
                    output << match.random;
                    generator = output.str();
                }
                archive.sequence(generator);
                if (archive.reading) std::istringstream{generator} >> match.random;
            }
        }
        if (archive.reading) {
            loaded_reference.clear();
        }
    }
    void Arena::report() {
        const auto destination = config.run / "summary.csv";
        auto temporary         = destination;
        temporary += ".tmp";
        std::ofstream output{temporary};
        output.exceptions(std::ios::badbit | std::ios::failbit);
        output << "job,status,pairs,wins,draws,losses,score,score_low95,score_high95,relative_elo,elo_low95,elo_high95,mean_plies,seconds\n";
        for (const auto& job : jobs) {
            const auto result = statistics(job);
            const double elo  = result.score == 0 ? -std::numeric_limits<double>::infinity() : result.score == 1 ? std::numeric_limits<double>::infinity() : 400 * std::log10(result.score / (1 - result.score));
            const double low = 400 * std::log10(result.lower / (1 - result.lower)), high = 400 * std::log10(result.upper / (1 - result.upper));
            output << std::format("{},{},{},{},{},{},{:.6f},{:.6f},{:.6f},{:.3f},{:.3f},{:.3f},{:.3f},{:.3f}\n", job.name, job.reported ? "complete" : "pending", result.pairs, result.wdl[0], result.wdl[1], result.wdl[2], result.score, result.lower, result.upper, result.pairs ? elo : std::numeric_limits<double>::quiet_NaN(), result.pairs ? low : std::numeric_limits<double>::quiet_NaN(), result.pairs ? high : std::numeric_limits<double>::quiet_NaN(), result.pairs ? double(result.plies) / (result.pairs * 2) : 0, job.seconds);
        }
        output.close();
        publish(temporary, destination);
    }
    void Arena::mine() {
        const auto before      = miner;
        const auto prior       = random;
        const auto short_count = diagnostics.size(), holdout_count = holdout_diagnostics.size();
        try {
            for (int ply = 0; ply < 16; ++ply) {
                if (Computation::cancellation.stop_requested()) throw Interrupted{};
                miner.play(baseline(miner, 0, random));
                if (miner.decision.outcome == Outcome::ongoing) continue;
                auto& suite = int(diagnostics.size()) < config.diagnostic_positions ? diagnostics : holdout_diagnostics;
                for (int age = 0; age < 3 && !miner.history.empty(); ++age) {
                    miner.unplay();
                    if (std::ranges::any_of(diagnostics, [&](const Diagnostic& entry) { return same_position(entry.game.position, miner.position); }) || std::ranges::any_of(holdout_diagnostics, [&](const Diagnostic& entry) { return same_position(entry.game.position, miner.position); })) continue;
                    int category{};
                    auto correct = winning_moves(miner, 1);
                    if (correct.empty()) {
                        category = 1;
                        correct  = winning_moves(miner, 3);
                    }
                    if (correct.empty()) {
                        category = 2;
                        correct  = safe_moves(miner);
                    }
                    const std::array quotas{(config.diagnostic_positions + 2) / 3, (config.diagnostic_positions + 1) / 3, config.diagnostic_positions / 3};
                    if (!correct.empty() && (category < 2 || correct.size() != miner.moves.size()) && std::ranges::count(suite, category, &Diagnostic::category) < quotas[category]) suite.push_back({miner, std::move(correct), category});
                    if (int(suite.size()) >= config.diagnostic_positions) break;
                }
                miner = Game{};
                if (int(holdout_diagnostics.size()) >= config.diagnostic_positions) {
                    suite_ready = true;
                    return;
                }
            }
        } catch (const Interrupted&) {
            miner  = before;
            random = prior;
            diagnostics.resize(short_count);
            holdout_diagnostics.resize(holdout_count);
            throw;
        }
    }
    void Arena::write_job(ArenaJob& job) {
        const auto result    = statistics(job);
        const bool complete  = std::ranges::all_of(job.matches, &Match::complete);
        nlohmann::json games = nlohmann::json::array();
        for (std::size_t index = 0; index < job.matches.size(); ++index) {
            const auto& match = job.matches[index];
            std::vector<std::string> moves;
            for (const auto& step : match.game.history) moves.push_back(coordinate(step.undo.move));
            games.push_back({{"id", index}, {"candidate_red", match.candidate_red}, {"complete", match.complete}, {"outcome", int(match.game.decision.outcome)}, {"reason", describe(match.game.decision.reason)}, {"moves", moves}, {"engine_nodes", match.engine_nodes}});
        }
        nlohmann::json output{{"job", job.name}, {"status", complete ? "complete" : "pending"}, {"candidate_sha256", job.candidate_hash}, {"reference_sha256", job.reference_hash}, {"suite_sha256", suite_hash}, {"configuration", configuration(config)}, {"diagnostic_holdout", diagnostic_holdout}, {"candidate_hash_scope", "artifact"}, {"opponent", int(job.opponent)}, {"depth", job.depth}, {"requested_nodes_per_move", job.nodes}, {"raw_policy", job.raw_policy}, {"holdout", job.holdout}, {"engine", engine_identity}, {"engine_options", engine_options}, {"engine_sha256", engine_hash}, {"engine_model_sha256", model_hash}, {"pairs", result.pairs}, {"wdl", result.wdl}, {"red_wdl", result.red}, {"black_wdl", result.black}, {"pentanomial", result.frequencies}, {"score", result.score}, {"score_ci95", {result.lower, result.upper}}, {"ci_method", "paired multinomial profile likelihood; asymptotic 95%"}, {"seconds", job.seconds}, {"games", games}};
        const double elo       = 400 * std::log10(result.score / (1 - result.score));
        output["relative_elo"] = result.pairs ? nlohmann::json(elo) : nlohmann::json(nullptr);
        output["elo_ci95"]     = {400 * std::log10(result.lower / (1 - result.lower)), 400 * std::log10(result.upper / (1 - result.upper))};
        output["elo_boundary"] = !result.pairs ? "no_completed_pairs" : result.score == 0 ? "all_losses; unbounded_below" : result.score == 1 ? "all_wins; unbounded_above" : "finite";
        output["mean_plies"]   = result.pairs ? double(result.plies) / (result.pairs * 2) : 0;
        const auto destination = config.run / (job.name + ".json");
        auto temporary         = destination;
        temporary += ".tmp";
        std::ofstream file{temporary};
        file.exceptions(std::ios::badbit | std::ios::failbit);
        file << output.dump(2);
        file.close();
        publish(temporary, destination);
        if (complete && !job.reported) {
            job.reported = true;
            std::println("benchmark {}: W/D/L={}/{}/{} score={:.1f}% CI95=[{:.1f},{:.1f}] pairs={}", job.name, result.wdl[0], result.wdl[1], result.wdl[2], result.score * 100, result.lower * 100, result.upper * 100, result.pairs);
        }
        report();
    }
} // namespace chess::benchmark
