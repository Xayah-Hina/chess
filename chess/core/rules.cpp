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

    bool in_check(const Position& position, const Color color) {
        int king{};
        for (int square = 0; square < 90; ++square)
            if (position.board[square].kind == Kind::general && position.board[square].color == color) king = square;
        for (int square = 0; square < 90; ++square)
            if (position.board[square].kind != Kind::none && position.board[square].color != color && attacks(position, {square, king})) return true;
        return false;
    }

    std::vector<Move> legal_moves(const Position& position) {
        std::vector<Move> moves;
        moves.reserve(64);
        Position next = position;
        for (int from = 0; from < 90; ++from) {
            if (position.board[from].kind == Kind::none || position.board[from].color != position.turn) continue;
            for (int to = 0; to < 90; ++to) {
                const auto target = position.board[to];
                if (target.kind != Kind::none && (target.color == position.turn || target.kind == Kind::general)) continue;
                if (!attacks(position, {from, to})) continue;
                const auto undo = make_move(next, {from, to});
                if (!in_check(next, position.turn)) moves.push_back({from, to});
                unmake_move(next, undo);
            }
        }
        return moves;
    }
} // namespace chess
