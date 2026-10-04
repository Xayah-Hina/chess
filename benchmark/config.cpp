module;
#include <nlohmann/json.hpp>
module chess.benchmark.config;
import std;
namespace chess::benchmark {
    Config load_config(const std::filesystem::path& path) {
        std::ifstream file{path};
        file.exceptions(std::ios::badbit | std::ios::failbit);
        const auto json = nlohmann::json::parse(file);
        Config config;
        config.training.seed             = json.at("seed");
        config.training.actors           = json.at("actors");
        config.training.threads          = json.at("threads");
        config.training.batch            = json.at("batch");
        config.training.simulations      = json.at("simulations");
        config.training.candidates       = json.at("candidates");
        config.training.generation_plies = json.at("generation_plies");
        config.training.replay_capacity  = json.at("replay_capacity");
        config.training.learning_rate    = json.at("learning_rate");
        config.training.weight_decay     = json.at("weight_decay");
        config.training.presentations    = json.at("presentations");

        config.opening_pairs        = json.at("opening_pairs");
        config.arena_batch          = json.at("arena_batch");
        config.diagnostic_positions = json.at("diagnostic_positions");
        config.save_seconds         = json.at("save_seconds");
        config.suite                = json.at("suite").get<std::string>();
        config.run                  = json.at("run").get<std::string>();
        config.engine               = json.at("engine").get<std::string>();
        config.engine_model         = json.at("engine_model").get<std::string>();
        return config;
    }
    void save_config(const Config& config, const std::filesystem::path& path) {
        std::ofstream configuration{path};
        configuration.exceptions(std::ios::badbit | std::ios::failbit);
        const nlohmann::json settings{{"seed", config.training.seed}, {"actors", config.training.actors}, {"threads", config.training.threads}, {"batch", config.training.batch}, {"simulations", config.training.simulations}, {"candidates", config.training.candidates}, {"generation_plies", config.training.generation_plies}, {"replay_capacity", config.training.replay_capacity}, {"learning_rate", config.training.learning_rate}, {"weight_decay", config.training.weight_decay}, {"presentations", config.training.presentations}, {"opening_pairs", config.opening_pairs}, {"arena_batch", config.arena_batch}, {"diagnostic_positions", config.diagnostic_positions}, {"save_seconds", config.save_seconds}, {"suite", config.suite.string()}, {"run", config.run.string()}, {"engine", config.engine.string()}, {"engine_model", config.engine_model.string()}, {"network", "MiniZero-ResNet-128x6/input128/policyFC4500/valueWDL"}, {"rules", "WXF-2018"}, {"search_reference", "MiniZero/394b2e483d00cb658d5a24ccca297f864c3280c7"}, {"source_revision", CHESS_SOURCE_REVISION}, {"source_sha256", CHESS_SOURCE_HASH}};
        configuration << settings.dump(2);
        configuration.close();
    }
} // namespace chess::benchmark
