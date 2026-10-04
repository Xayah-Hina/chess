module chess.rules;
import std;

namespace chess {
    bool attacks(const Position& position, const Move move) {
        const auto piece = position.board[move.from];
        const int x = move.from % 9, y = move.from / 9;
        const int tx = move.to % 9, ty = move.to / 9;
        const int dx = tx - x, dy = ty - y;
        const int ax = std::abs(dx), ay = std::abs(dy);
        const int forward = piece.color == Color::red ? 1 : -1;
        const bool palace = tx >= 3 && tx <= 5 && (piece.color == Color::red ? ty <= 2 : ty >= 7);
        switch (piece.kind) {
        case Kind::none: return false;
        case Kind::general:
            if (ax + ay == 1 && palace) return true;
            if (dx != 0 || position.board[move.to].kind != Kind::general) return false;
            break;
        case Kind::advisor: return ax == 1 && ay == 1 && palace;
        case Kind::elephant: return ax == 2 && ay == 2 && (piece.color == Color::red ? ty <= 4 : ty >= 5) && position.board[(y + dy / 2) * 9 + x + dx / 2].kind == Kind::none;
        case Kind::horse:
            if (ax == 2 && ay == 1) return position.board[y * 9 + x + dx / 2].kind == Kind::none;
            if (ax == 1 && ay == 2) return position.board[(y + dy / 2) * 9 + x].kind == Kind::none;
            return false;
        case Kind::soldier: return (dx == 0 && dy == forward) || (dy == 0 && ax == 1 && (piece.color == Color::red ? y >= 5 : y <= 4));
        case Kind::rook:
        case Kind::cannon:
            if ((dx == 0) == (dy == 0)) return false;
            break;
        }
        const int step = dx == 0 ? (dy > 0 ? 9 : -9) : (dx > 0 ? 1 : -1);
        int screens{};
        for (int square = move.from + step; square != move.to; square += step)
            if (position.board[square].kind != Kind::none) ++screens;
        return screens == (piece.kind == Kind::cannon && position.board[move.to].kind != Kind::none ? 1 : 0);
    }

    namespace {
        bool checked(const Position& position, const Color color, const int king) {
            const int x = king % 9, y = king / 9;
            for (const auto direction : std::array{std::array{1, 0}, std::array{-1, 0}, std::array{0, 1}, std::array{0, -1}}) {
                bool screen{};
                for (int tx = x + direction[0], ty = y + direction[1]; tx >= 0 && tx < 9 && ty >= 0 && ty < 10; tx += direction[0], ty += direction[1]) {
                    const auto piece = position.board[ty * 9 + tx];
                    if (piece.kind == Kind::none) continue;
                    if (piece.color != color && (screen ? piece.kind == Kind::cannon : piece.kind == Kind::rook || piece.kind == Kind::general && attacks(position, {ty * 9 + tx, king}))) return true;
                    if (screen) break;
                    screen = true;
                }
            }
            for (const auto offset : std::array{std::array{1, 2}, std::array{2, 1}, std::array{1, 1}, std::array{2, 2}, std::array{1, 0}, std::array{0, 1}})
                for (const int sx : {-1, 1})
                    for (const int sy : {-1, 1}) {
                        const int tx = x + sx * offset[0], ty = y + sy * offset[1];
                        if (tx < 0 || tx >= 9 || ty < 0 || ty >= 10) continue;
                        const auto piece = position.board[ty * 9 + tx];
                        if (piece.kind != Kind::none && piece.color != color && attacks(position, {ty * 9 + tx, king})) return true;
                    }
            return false;
        }
    } // namespace

    bool in_check(const Position& position, const Color color) {
        int king{};
        for (int square = 0; square < 90; ++square)
            if (position.board[square].kind == Kind::general && position.board[square].color == color) king = square;
        return checked(position, color, king);
    }

    std::vector<Move> legal_moves(const Position& position) {
        std::vector<Move> moves;
        moves.reserve(64);
        Position next = position;
        int king{};
        for (int square = 0; square < 90; ++square)
            if (position.board[square].kind == Kind::general && position.board[square].color == position.turn) king = square;
        for (int from = 0; from < 90; ++from) {
            const auto piece = position.board[from];
            if (piece.kind == Kind::none || piece.color != position.turn) continue;
            const int x = from % 9, y = from / 9;
            std::array<int, 36> destinations;
            int count{};
            if (piece.kind == Kind::rook || piece.kind == Kind::cannon) {
                for (const auto direction : std::array{std::array{1, 0}, std::array{-1, 0}, std::array{0, 1}, std::array{0, -1}}) {
                    bool screen{};
                    for (int tx = x + direction[0], ty = y + direction[1]; tx >= 0 && tx < 9 && ty >= 0 && ty < 10; tx += direction[0], ty += direction[1]) {
                        const int to        = ty * 9 + tx;
                        const bool occupied = position.board[to].kind != Kind::none;
                        if (piece.kind == Kind::rook || !occupied && !screen || occupied && screen) destinations[count++] = to;
                        if (!occupied) continue;
                        if (piece.kind == Kind::rook || screen) break;
                        screen = true;
                    }
                }
            } else {
                for (const auto offset : std::array{std::array{1, 0}, std::array{-1, 0}, std::array{0, 1}, std::array{0, -1}, std::array{1, 1}, std::array{-1, 1}, std::array{1, -1}, std::array{-1, -1}, std::array{2, 2}, std::array{-2, 2}, std::array{2, -2}, std::array{-2, -2}, std::array{1, 2}, std::array{-1, 2}, std::array{1, -2}, std::array{-1, -2}, std::array{2, 1}, std::array{-2, 1}, std::array{2, -1}, std::array{-2, -1}}) {
                    const int tx = x + offset[0], ty = y + offset[1];
                    if (tx >= 0 && tx < 9 && ty >= 0 && ty < 10 && attacks(position, {from, ty * 9 + tx})) destinations[count++] = ty * 9 + tx;
                }
            }
            std::ranges::sort(std::span{destinations}.first(count));
            for (const int to : std::span{destinations}.first(count)) {
                const auto target = position.board[to];
                if (target.kind != Kind::none && (target.color == position.turn || target.kind == Kind::general)) continue;
                const auto undo = make_move(next, {from, to});
                if (!checked(next, position.turn, piece.kind == Kind::general ? to : king)) moves.push_back({from, to});
                unmake_move(next, undo);
            }
        }
        return moves;
    }
} // namespace chess
