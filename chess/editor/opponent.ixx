export module chess.editor.opponent;
export import chess.game;
import std;

export namespace chess::editor {
    enum class OpponentPhase { loading, ready, thinking, failed };
    struct Opponent final {
        std::mutex mutex;
        OpponentPhase phase{OpponentPhase::loading};
        std::optional<Game> result;
        std::uint64_t version{};
        double seconds{};
        std::string error;

        Opponent();
        ~Opponent();
        void submit(std::filesystem::path model, std::optional<Game> game = {});

    private:
        struct Request final {
            std::filesystem::path model;
            std::optional<Game> game;
            std::uint64_t generation{};
        };
        std::condition_variable condition;
        std::optional<Request> request;
        std::stop_source cancellation;
        std::uint64_t generation{};
        std::jthread worker;
        void run(std::stop_token stop);
    };
} // namespace chess::editor
