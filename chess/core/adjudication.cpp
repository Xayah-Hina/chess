module chess.adjudication;
import std;

namespace chess {
    namespace {
        struct Nature final {
            int status{1};
            std::uint32_t victims{};
        };
        bool legal_capture(Position position, const Move move) {
            const auto color = position.board[move.from].color;
            position.turn = color;
            make_move(position, move);
            return !in_check(position, color);
        }
        bool protected_piece(const Position& position, const Move capture) {
            constexpr std::array values{0, 0, 2, 2, 4, 9, 4, 2};
            const auto victim = position.board[capture.to];
            if (values[int(victim.kind)] > values[int(position.board[capture.from].kind)]) return false;
            Position after = position;
            after.turn = position.board[capture.from].color;
            make_move(after, capture);
            for (int square = 0; square < 90; ++square) {
                const auto defender = after.board[square];
                if (defender.kind == Kind::none || defender.color != victim.color) continue;
                const auto& geometry = defender.kind == Kind::rook || defender.kind == Kind::cannon ? position : after;
                if (attacks(geometry, {square, capture.to}) && legal_capture(after, {square, capture.to})) return true;
            }
            return false;
        }
        // WXF chase detection follows the defender's reply, rather than solving
        // a future mating tree. See Tan and Medina, arXiv:2412.17334, algorithms 2-8.
        Nature nature(const Position& before, const Position& position, const Position& response, const Move move, const Move reply) {
            if (in_check(position, position.turn)) return {4};
            if (before.board[move.to].kind != Kind::none || before.board[move.from].kind == Kind::soldier && move.from / 9 != move.to / 9) return {8};
            Position attacking = position;
            attacking.turn = opposite(position.turn);
            Nature result;
            for (const auto capture : legal_moves(attacking)) {
                const auto attacker = attacking.board[capture.from];
                const auto victim = attacking.board[capture.to];
                if (victim.kind == Kind::none || attacker.kind == Kind::general || attacker.kind == Kind::soldier) continue;
                if (victim.kind == Kind::soldier && (victim.color == Color::red ? capture.to / 9 < 5 : capture.to / 9 > 4)) continue;
                if (attacker.kind == victim.kind && attacks(position, {capture.to, capture.from}) && legal_capture(position, {capture.to, capture.from})) continue;
                if (protected_piece(position, capture)) continue;
                Move remaining = capture;
                if (remaining.to == reply.from) remaining.to = reply.to;
                if (response.board[remaining.from].id == attacker.id && response.board[remaining.to].id == victim.id && attacks(response, remaining) && legal_capture(response, remaining) && !protected_piece(response, remaining)) continue;
                result.victims |= std::uint32_t{1} << (victim.id - 1);
            }
            if (result.victims) result.status = 2;
            return result;
        }
    }

    thread_local std::chrono::steady_clock::time_point Computation::deadline = std::chrono::steady_clock::time_point::max();
    thread_local std::stop_token Computation::cancellation;
    Computation::Computation(const std::chrono::steady_clock::time_point limit, const std::stop_token token) : previous{deadline}, previous_cancellation{cancellation} {
        cancellation = token;
        deadline = limit;
    }
    Computation::~Computation() {
        deadline = previous;
        cancellation = previous_cancellation;
    }
    Decision adjudicate(const Position& position, const std::span<const Step> history, const std::span<const Move> moves) {
        if (Computation::cancellation.stop_requested() || std::chrono::steady_clock::now() >= Computation::deadline) throw Interrupted{};
        if (moves.empty()) return {position.turn == Color::red ? Outcome::black_win : Outcome::red_win, in_check(position, position.turn) ? Reason::checkmate : Reason::stalemate};
        int attacking{}, cannons{}, support{};
        for (const auto piece : position.board) {
            attacking += piece.kind == Kind::rook || piece.kind == Kind::horse || piece.kind == Kind::cannon || piece.kind == Kind::soldier;
            cannons += piece.kind == Kind::cannon;
            support += piece.kind == Kind::advisor || piece.kind == Kind::elephant;
        }
        if (!attacking || attacking == 1 && cannons == 1 && !support) return {Outcome::draw, Reason::dead_position};
        std::size_t capture{};
        for (std::size_t index = history.size(); index > 0; --index)
            if (history[index - 1].undo.captured.kind != Kind::none) {
                capture = index;
                break;
            }
        const auto at = [&](const std::size_t index) -> const Position& { return index == history.size() ? position : history[index].before; };
        std::array<int, 2> counts{}, checks{};
        for (std::size_t index = capture; index < history.size(); ++index) {
            const int side = int(history[index].before.turn);
            if (!in_check(at(index + 1), at(index + 1).turn) || checks[side]++ < 10) ++counts[side];
        }
        // WXF 2018, articles 3.2.B/D and 8: four occurrences of a position;
        // fifty moves per player without capture, counting at most ten checks.
        if (counts[0] >= 50 && counts[1] >= 50) return {Outcome::draw, Reason::move_limit};
        if (history.size() < 12) return {};
        std::optional<std::size_t> begin;
        int repeats{};
        for (auto index = history.size() - 2;; index -= 2) {
            const auto& first = history[index];
            const auto& second = history[index + 1];
            if (first.undo.captured.kind != Kind::none || second.undo.captured.kind != Kind::none) break;
            if (same_position(position, at(index)) && ++repeats == 3) {
                begin = index;
                break;
            }
            if (index < 2) break;
        }
        if (!begin) return {};
        std::array<int, 2> status{};
        std::array<std::uint32_t, 2> victims{~std::uint32_t{}, ~std::uint32_t{}};
        // The last move has no reply yet. Its earlier occurrences in this
        // repeated interval have replies, so both players can be classified.
        for (auto index = *begin; index + 1 < history.size(); ++index) {
            if (Computation::cancellation.stop_requested() || std::chrono::steady_clock::now() >= Computation::deadline) throw Interrupted{};
            const auto entry = nature(at(index), at(index + 1), at(index + 2), history[index].undo.move, history[index + 1].undo.move);
            const int side = int(at(index).turn);
            status[side] |= entry.status;
            victims[side] &= entry.victims;
        }
        std::array<int, 2> levels{};
        for (int side = 0; side < 2; ++side) levels[side] = status[side] == 4 ? 2 : status[side] == 2 && victims[side] ? 1 : 0;
        if (levels[0] == levels[1]) return {Outcome::draw, Reason::repetition};
        const auto winner = levels[0] > levels[1] ? Outcome::black_win : Outcome::red_win;
        return {winner, std::max(levels[0], levels[1]) == 2 ? Reason::perpetual_check : Reason::perpetual_chase};
    }
    std::string_view describe(const Reason reason) {
        switch (reason) {
        case Reason::none: return "";
        case Reason::checkmate: return "将死";
        case Reason::stalemate: return "困毙";
        case Reason::perpetual_check: return "长将";
        case Reason::perpetual_chase: return "长捉";
        case Reason::repetition: return "重复局面";
        case Reason::move_limit: return "自然限着";
        case Reason::dead_position: return "双方均无法获胜";
        }
        std::unreachable();
    }
} // namespace chess
