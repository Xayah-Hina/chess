import tools.trainer.headless;
import std;
int main(const int argc, char** argv) {
    try {
        const std::vector<std::string_view> arguments{argv + 1, argv + argc};
        return tools::trainer::run(arguments);
    } catch (const std::exception& error) {
        std::println(std::cerr, "Trainer: {}", error.what());
        return 1;
    }
}
