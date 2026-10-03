module chess.position;
import std;

namespace chess {
    Color opposite(const Color color) {
        return color == Color::red ? Color::black : Color::red;
    }

    Position initial_position() {
        Position position;
        constexpr std::array back{Kind::rook, Kind::horse, Kind::elephant, Kind::advisor, Kind::general, Kind::advisor, Kind::elephant, Kind::horse, Kind::rook};
        std::uint8_t id{1};
        for (const Color color : {Color::red, Color::black}) {
            const int home    = color == Color::red ? 0 : 9;
            const int cannon  = color == Color::red ? 2 : 7;
            const int soldier = color == Color::red ? 3 : 6;
            for (int file = 0; file < 9; ++file) position.board[home * 9 + file] = {back[file], color, id++};
            for (const int file : {1, 7}) position.board[cannon * 9 + file] = {Kind::cannon, color, id++};
            for (const int file : {0, 2, 4, 6, 8}) position.board[soldier * 9 + file] = {Kind::soldier, color, id++};
        }
        return position;
    }

    Undo make_move(Position& position, const Move move) {
        const Undo undo{move, position.board[move.to], position.turn, position.just_crossed};
        const auto piece          = position.board[move.from];
        position.just_crossed     = piece.kind == Kind::soldier && (piece.color == Color::red ? move.from / 9 == 4 && move.to / 9 == 5 : move.from / 9 == 5 && move.to / 9 == 4) ? piece.id : 0;
        position.board[move.to]   = position.board[move.from];
        position.board[move.from] = {};
        position.turn             = opposite(position.turn);
        return undo;
    }

    void unmake_move(Position& position, const Undo undo) {
        position.board[undo.move.from] = position.board[undo.move.to];
        position.board[undo.move.to]   = undo.captured;
        position.turn                  = undo.turn;
        position.just_crossed          = undo.just_crossed;
    }

    bool same_position(const Position& first, const Position& second) {
        if (first.turn != second.turn) return false;
        for (int square = 0; square < 90; ++square) {
            const auto& a = first.board[square];
            const auto& b = second.board[square];
            if (a.kind != b.kind || (a.kind != Kind::none && a.color != b.color)) return false;
        }
        return true;
    }

    std::string coordinate(const Move move) {
        return std::string{char('a' + move.from % 9), char('0' + move.from / 9), char('a' + move.to % 9), char('0' + move.to / 9)};
    }

    Move coordinate(const std::string_view text) {
        return {(text[1] - '0') * 9 + text[0] - 'a', (text[3] - '0') * 9 + text[2] - 'a'};
    }
} // namespace chess
