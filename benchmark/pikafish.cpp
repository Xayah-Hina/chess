module;
#include <Windows.h>
module chess.benchmark.pikafish;
import std;
namespace chess::benchmark {
    Pikafish::Pikafish(const std::filesystem::path& executable, const std::filesystem::path& model) {
        SECURITY_ATTRIBUTES security{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
        std::unique_ptr<void, decltype(&CloseHandle)> child_input{nullptr, CloseHandle}, child_output{nullptr, CloseHandle};
        if (!CreatePipe(std::out_ptr(child_input), std::out_ptr(input), &security, 0) || !CreatePipe(std::out_ptr(output), std::out_ptr(child_output), &security, 0)) throw std::system_error{int(GetLastError()), std::system_category(), "Pikafish pipes"};
        if (!SetHandleInformation(input.get(), HANDLE_FLAG_INHERIT, 0) || !SetHandleInformation(output.get(), HANDLE_FLAG_INHERIT, 0)) throw std::system_error{int(GetLastError()), std::system_category(), "Pikafish pipe inheritance"};
        STARTUPINFOW startup{};
        startup.cb         = sizeof(startup);
        startup.dwFlags    = STARTF_USESTDHANDLES;
        startup.hStdInput  = child_input.get();
        startup.hStdOutput = startup.hStdError = child_output.get();
        PROCESS_INFORMATION information{};
        const auto path      = std::filesystem::absolute(executable);
        std::wstring command = L"\"" + path.native() + L"\"";
        if (!CreateProcessW(path.c_str(), command.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, path.parent_path().c_str(), &startup, &information)) throw std::system_error{int(GetLastError()), std::system_category(), "Start Pikafish"};
        process.reset(information.hProcess);
        CloseHandle(information.hThread);
        child_input.reset();
        child_output.reset();
        send("uci");
        for (std::string text = line(false); text != "uciok"; text = line(false)) {
            if (text.starts_with("id name ")) identity = text.substr(8);
            if (text.starts_with("option ")) options += text + "\n";
        }
        send("setoption name Threads value 1");
        send("setoption name Hash value 16");
        send("setoption name Ponder value false");
        send("setoption name MultiPV value 1");
        send("setoption name Rule value AsianRule");
        send("setoption name EvalFile value " + std::filesystem::absolute(model).string());
        send("ucinewgame");
        send("isready");
        while (line(false) != "readyok") {
        }
    }
    Pikafish::~Pikafish() {
        const std::string quit{"quit\n"};
        DWORD bytes{};
        WriteFile(input.get(), quit.data(), DWORD(quit.size()), &bytes, nullptr);
        input.reset();
        WaitForSingleObject(process.get(), INFINITE);
    }
    Move Pikafish::choose(const Game& game, const std::size_t opening_length, const Color color, const int budget) {
        if (!reconstructed) {
            // Replay earlier engine searches to rebuild its per-game hash and
            // search history when an unfinished benchmark resumes.
            for (std::size_t index = opening_length; index < game.history.size(); ++index)
                if (game.history[index].before.turn == color) calculate(std::span{game.history}.first(index), budget);
            reconstructed = true;
        }
        return calculate(game.history, budget);
    }
    void Pikafish::send(std::string text) {
        text += '\n';
        DWORD bytes{};
        if (!WriteFile(input.get(), text.data(), DWORD(text.size()), &bytes, nullptr)) throw std::system_error{int(GetLastError()), std::system_category(), "Write Pikafish command"};
    }
    std::string Pikafish::line(const bool cancellable) {
        for (;;) {
            if (cancellable && (Computation::cancellation.stop_requested() || std::chrono::steady_clock::now() >= Computation::deadline)) {
                send("stop");
                while (!line(false).starts_with("bestmove ")) {
                }
                throw Interrupted{};
            }
            const auto newline = buffered.find('\n');
            if (newline != std::string::npos) {
                std::string text = buffered.substr(0, newline);
                buffered.erase(0, newline + 1);
                if (!text.empty() && text.back() == '\r') text.pop_back();
                return text;
            }
            std::array<char, 4096> buffer{};
            DWORD bytes{}, available{};
            if (!PeekNamedPipe(output.get(), nullptr, 0, nullptr, &available, nullptr)) throw std::system_error{int(GetLastError()), std::system_category(), "Read Pikafish pipe"};
            if (!available) {
                std::this_thread::sleep_for(std::chrono::milliseconds{1});
                continue;
            }
            if (!ReadFile(output.get(), buffer.data(), std::min(DWORD(buffer.size()), available), &bytes, nullptr) || !bytes) throw std::runtime_error{"Pikafish stopped before completing its UCI command"};
            buffered.append(buffer.data(), bytes);
        }
    }
    Move Pikafish::calculate(const std::span<const Step> history, const int budget) {
        std::string position{"position startpos moves"};
        for (const auto& step : history) position += " " + coordinate(step.undo.move);
        send(position);
        send(std::format("go nodes {}", budget));
        nodes = 0;
        depth = 0;
        for (;;) {
            const auto text = line();
            if (text.starts_with("bestmove ")) {
                const auto move = text.substr(9, 4);
                if (move == "0000" || move == "(non") throw std::runtime_error{"Pikafish returned no move while core considers the game ongoing"};
                return coordinate(move);
            }
            if (text.starts_with("info ")) {
                std::istringstream input{text};
                std::string token;
                while (input >> token) {
                    if (token == "nodes") input >> nodes;
                    else if (token == "depth") input >> depth;
                }
            }
        }
    }
} // namespace chess::benchmark
