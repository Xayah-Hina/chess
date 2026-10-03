export module tools.trainer.editor.workspace;
export import tools.trainer.session;
import tools.editor.platform.window;
import std;
export namespace tools::trainer::editor {
    struct Workspace final {
        tools::editor::WindowPlatform& window;
        Session session;
        Snapshot state;
        chess::benchmark::Config config;
        std::array<char, 4096> directory{};
        explicit Workspace(tools::editor::WindowPlatform& window);
        void receive();
        void draw(bool closing);

    private:
        std::filesystem::path loaded;
        void draw_training();
        void draw_benchmark(const BenchmarkResult& result);
        void draw_probes(const BenchmarkResult& result);
        void draw_matches(const BenchmarkResult& result);
    };
} // namespace tools::trainer::editor
