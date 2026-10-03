module chess.adjudication;
import std;

namespace chess {
    namespace {
        struct Material final {
            int rooks{}, minors{}, weak{}, soldiers{};
        };

        struct Proof final {
            bool won{};
            std::uint32_t participants{};
        };

        struct Exchange final {
            int gain;
            std::uint32_t participants;
            Piece finish;
            int prior_gain;
        };

        struct Parent final {
            std::size_t index;
            std::uint32_t participant;
        };

        struct Node final {
            Position position;
            Proof proof;
            bool lost{};
            int remaining{};
            std::vector<Parent> parents;
        };

        struct Repetition final {
            bool prohibited{}, long_check{true}, urgent{true};
        };

        // These are the exchange relations in CCA 2020, 1.2 and 26.6–26.7,
        // used exclusively to distinguish a chase from an offer/exchange.
        int exchange_result(const Position& start, const Position& end, const Color attacker) {
            Material balance;
            for (int square = 0; square < 90; ++square) {
                for (const auto& [piece, direction] : std::array{std::pair{start.board[square], 1}, std::pair{end.board[square], -1}}) {
                    const int sign = piece.color == attacker ? -direction : direction;
                    switch (piece.kind) {
                    case Kind::rook: balance.rooks += sign; break;
                    case Kind::horse:
                    case Kind::cannon: balance.minors += sign; break;
                    case Kind::advisor:
                    case Kind::elephant: balance.weak += sign; break;
                    case Kind::soldier:
                        if (piece.color == Color::red ? square / 9 >= 5 : square / 9 <= 4) balance.soldiers += sign;
                        break;
                    default: break;
                    }
                }
            }
            if (balance.rooks == 0 && balance.soldiers != 0 && (balance.minors != 0 || balance.weak != 0)) return 0;
            const int strong = balance.rooks * 2 + balance.minors;
            if (strong != 0) return strong > 0 ? 1 : -1;
            if (balance.weak != 0) return balance.weak > 0 ? 1 : -1;
            return (balance.soldiers > 0) - (balance.soldiers < 0);
        }

        bool mate_in_one(Position position, const Color attacker) {
            position.turn = attacker;
            for (const auto move : legal_moves(position)) {
                const auto undo = make_move(position, move);
                const bool mate = in_check(position, position.turn) && legal_moves(position).empty();
                unmake_move(position, undo);
                if (mate) return true;
            }
            return false;
        }

        Exchange exchange(Position& position, const Position& start, const int square, const Color attacker, const std::uint32_t participants, const std::uint32_t involved, const int prior_gain = 0) {
            Exchange result{exchange_result(start, position, attacker), participants, position.board[square], prior_gain};
            const bool maximize = position.turn == attacker;
            for (const auto move : legal_moves(position)) {
                const auto victim = position.board[move.to];
                if (victim.kind == Kind::none || (move.to != square && !(involved & (1u << (victim.id - 1))))) continue;
                const auto piece                = position.board[move.from];
                const int before_gain           = exchange_result(start, position, attacker);
                const auto undo                 = make_move(position, move);
                std::uint32_t next_participants = participants;
                if (piece.color == attacker) next_participants |= 1u << (piece.id - 1);
                const auto next = exchange(position, start, move.to, attacker, next_participants, involved | (1u << (piece.id - 1)), before_gain);
                unmake_move(position, undo);
                if (maximize ? next.gain > result.gain : next.gain < result.gain) result = next;
                else if (!maximize && next.gain == result.gain) {
                    result.participants |= next.participants;
                    if (next.finish.color == attacker && next.prior_gain == 0 && (next.finish.kind == Kind::soldier || next.finish.kind == Kind::general)) {
                        result.finish     = next.finish;
                        result.prior_gain = 0;
                    }
                }
            }
            return result;
        }

        // Exact continuous-check reachability. A repeated state cannot prove a
        // finite forced mate/capture; there is no depth budget or evaluation.
        Proof forcing(const Position& start, const Color attacker, const std::uint8_t target) {
            std::vector<Node> graph{{start}};
            std::map<Position, std::size_t> indices{{start, 0}};
            std::deque<std::size_t> pending{0};
            const auto propagate = [&](const std::size_t index, const Proof proof) {
                if (graph[index].proof.won || graph[index].lost) return;
                graph[index].proof = proof;
                graph[index].lost  = !proof.won;
                std::vector<std::size_t> won{index};
                while (!won.empty()) {
                    const auto child = won.back();
                    won.pop_back();
                    for (const auto parent : graph[child].parents) {
                        auto& node = graph[parent.index];
                        if (node.proof.won || node.lost) continue;
                        const bool win = graph[child].proof.won;
                        if (win) node.proof.participants |= graph[child].proof.participants | parent.participant;
                        if ((node.position.turn == attacker) == win || --node.remaining == 0) {
                            node.proof.won = win;
                            node.lost      = !win;
                            won.push_back(parent.index);
                        }
                    }
                }
            };
            while (!pending.empty() && !graph[0].proof.won && !graph[0].lost) {
                const auto index = pending.front();
                pending.pop_front();
                Position position = graph[index].position;
                if (graph[index].proof.won || graph[index].lost) continue;
                const auto moves = legal_moves(position);
                if (moves.empty()) {
                    propagate(index, {!target && position.turn != attacker && in_check(position, position.turn)});
                    continue;
                }
                if (position.turn != attacker && !in_check(position, position.turn)) {
                    propagate(index, {});
                    continue;
                }
                graph[index].remaining = int(moves.size());
                const int prior_gain   = exchange_result(start, position, attacker);
                for (const auto move : moves) {
                    const auto piece                = position.board[move.from];
                    const auto captured             = position.board[move.to];
                    const bool attacking            = position.turn == attacker;
                    const auto undo                 = make_move(position, move);
                    const std::uint32_t participant = attacking ? 1u << (piece.id - 1) : 0;
                    if (attacking && target && captured.id == target) {
                        const auto traded = exchange(position, start, move.to, attacker, participant, participant | (1u << (target - 1)), prior_gain);
                        bool borrowed{};
                        if (piece.kind == Kind::general || piece.kind == Kind::soldier) {
                            int king{};
                            for (int square = 0; square < 90; ++square)
                                if (position.board[square].kind == Kind::general && position.board[square].color != attacker) king = square;
                            bool discovered_check{};
                            for (int square = 0; square < 90; ++square)
                                if (position.board[square].kind != Kind::none && position.board[square].color == attacker && position.board[square].id != piece.id && attacks(position, {square, king})) discovered_check = true;
                            const bool joint = std::ranges::any_of(moves, [&](const Move other) { return other.to == move.to && other.from != move.from; });
                            borrowed         = prior_gain == 0 && !discovered_check && !joint;
                        }
                        if (!borrowed && traded.gain > 0 && !mate_in_one(position, position.turn)) propagate(index, {true, traded.participants});
                        else --graph[index].remaining;
                    } else if (!attacking || in_check(position, position.turn)) {
                        const auto [entry, inserted] = indices.emplace(position, graph.size());
                        const auto child             = entry->second;
                        if (inserted) {
                            graph.push_back({position});
                            pending.push_back(child);
                        }
                        if (graph[child].proof.won || graph[child].lost) {
                            const bool win = graph[child].proof.won;
                            if (win) graph[index].proof.participants |= graph[child].proof.participants | participant;
                            if (attacking == win || --graph[index].remaining == 0) propagate(index, {win, graph[index].proof.participants});
                        } else graph[child].parents.push_back({index, participant});
                    } else --graph[index].remaining;
                    unmake_move(position, undo);
                    if (graph[index].proof.won || graph[index].lost) break;
                }
                if (graph[index].remaining == 0 && !graph[index].proof.won && !graph[index].lost) propagate(index, {});
            }
            return graph[0].proof;
        }

        bool defensive_point(const Position& position, const int square) {
            const Color defender = position.board[square].color;
            const Color attacker = opposite(defender);
            Position next        = position;
            next.turn            = attacker;
            for (const auto move : legal_moves(next)) {
                if (move.to == square) continue;
                const auto undo = make_move(next, move);
                Position threat = next;
                threat.turn     = attacker;
                if (!forcing(threat, attacker, 0).won) {
                    unmake_move(next, undo);
                    continue;
                }
                Position unsupported      = next;
                unsupported.board[square] = {};
                bool weak_defense{};
                for (const auto reply : legal_moves(unsupported)) {
                    const auto response = make_move(unsupported, reply);
                    weak_defense        = !forcing(unsupported, attacker, 0).won;
                    if (weak_defense) {
                        for (const auto continuation : legal_moves(unsupported)) {
                            const auto capture = make_move(unsupported, continuation);
                            const bool lost    = legal_moves(unsupported).empty();
                            unmake_move(unsupported, capture);
                            if (lost) {
                                weak_defense = false;
                                break;
                            }
                        }
                    }
                    unmake_move(unsupported, response);
                    if (weak_defense) break;
                }
                if (!weak_defense) {
                    for (const auto reply : legal_moves(next)) {
                        if (reply.from != square) continue;
                        const auto response = make_move(next, reply);
                        const bool holds    = !forcing(next, attacker, 0).won;
                        unmake_move(next, response);
                        if (holds) {
                            unmake_move(next, undo);
                            return true;
                        }
                    }
                }
                unmake_move(next, undo);
            }
            return false;
        }

        std::vector<Chase> threats(Position position, const Color attacker, const std::uint32_t involved) {
            position.turn = attacker;
            std::vector<Chase> result;
            const auto moves          = legal_moves(position);
            Position defending        = position;
            defending.turn            = opposite(attacker);
            const auto counterattacks = legal_moves(defending);
            for (int square = 0; square < 90; ++square) {
                const auto victim = position.board[square];
                if (victim.kind == Kind::none || victim.kind == Kind::general || victim.color == attacker) continue;
                if (victim.kind == Kind::soldier && (victim.id == position.just_crossed || (victim.color == Color::red ? square / 9 < 5 : square / 9 > 4))) continue;
                Chase chase{victim.id, victim.kind};
                for (const auto move : moves) {
                    if (move.to != square) continue;
                    const auto piece          = position.board[move.from];
                    const Position start      = position;
                    std::uint32_t related     = involved | (1u << (victim.id - 1)) | (1u << (piece.id - 1));
                    Position unpinned         = position;
                    unpinned.board[move.from] = {};
                    const bool pinned         = piece.kind != Kind::general && in_check(unpinned, attacker);
                    bool root{}, separate_trade{};
                    for (const auto counter : counterattacks) {
                        const auto protected_piece = defending.board[counter.to];
                        if (protected_piece.kind == Kind::none || protected_piece.id == piece.id) continue;
                        separate_trade |= (involved & (1u << (protected_piece.id - 1))) != 0;
                        const auto counter_undo = make_move(defending, counter);
                        const auto recaptures   = legal_moves(defending);
                        if (std::ranges::find(recaptures, Move{move.from, counter.to}) != recaptures.end()) {
                            root = true;
                            related |= (1u << (protected_piece.id - 1)) | (1u << (defending.board[counter.to].id - 1));
                        }
                        unmake_move(defending, counter_undo);
                    }
                    const auto undo            = make_move(position, move);
                    auto replies               = legal_moves(position);
                    const bool protected_piece = std::ranges::any_of(replies, [&](const Move reply) { return reply.to == square; });
                    const auto traded          = exchange(position, start, square, attacker, 1u << (piece.id - 1), related);
                    const int gain             = traded.gain;
                    const bool suicide         = mate_in_one(position, position.turn);
                    unmake_move(position, undo);
                    bool poisoned_offer{};
                    if (piece.kind == victim.kind || piece.kind == Kind::rook || victim.kind == Kind::rook) {
                        Position accepted      = start;
                        accepted.turn          = victim.color;
                        const auto invitations = legal_moves(accepted);
                        if (std::ranges::find(invitations, Move{square, move.from}) != invitations.end()) {
                            make_move(accepted, {square, move.from});
                            poisoned_offer = exchange(accepted, start, move.from, attacker, 1u << (piece.id - 1), related).gain > 0 || forcing(accepted, attacker, 0).won;
                        }
                    }
                    // Same-kind protected offers are exchanges. A pinned root
                    // is absent from legal replies and cannot protect a piece.
                    if (suicide || !poisoned_offer && (gain < 0 || gain == 0 && protected_piece && piece.kind == victim.kind)) continue;
                    if (gain == 0 && !root && !pinned && !separate_trade && !poisoned_offer) continue;
                    // 26.8: a final general/soldier capture after an equal
                    // exchange is idle unless it also attacks the same victim.
                    if (traded.prior_gain == 0 && traded.finish.color == attacker && (traded.finish.kind == Kind::general || traded.finish.kind == Kind::soldier)) {
                        const bool joint = std::ranges::any_of(moves, [&](const Move capture) { return capture.to == square && position.board[capture.from].id == traded.finish.id; });
                        if (!joint) continue;
                    }
                    chase.attackers |= traded.participants;
                    chase.unprotected |= !protected_piece;
                }
                const auto proof = forcing(position, attacker, victim.id);
                if (proof.won) chase.attackers |= proof.participants;
                if (chase.attackers) result.push_back(chase);
            }
            return result;
        }

        Repetition repetition(const std::span<const Nature> sequence, const std::span<const Step> history, const Color color) {
            Repetition result;
            for (std::size_t i = 0; i < sequence.size(); ++i) {
                if (history[i].before.turn != color) continue;
                const auto& nature = sequence[i];
                if (!nature.check && !nature.kill && nature.chases.empty()) return {};
                result.prohibited = true;
                result.long_check &= nature.check;
                if (nature.check || nature.kill) continue;
                bool valuable{};
                for (const auto& chase : nature.chases) {
                    valuable |= (chase.kind == Kind::rook || chase.unprotected) && std::popcount(chase.attackers) == 1;
                }
                result.urgent &= valuable;
            }
            return result;
        }
    } // namespace

    Nature classify(const Position& before, const Move move, const std::uint32_t involved) {
        Position after = before;
        make_move(after, move);
        Nature nature;
        nature.check = in_check(after, after.turn);
        if (nature.check) return nature;
        const auto moved = before.board[move.from];
        // 26.1.2: a general's response to check is idle, including discovered
        // chases and mating threats caused by that response.
        if (moved.kind == Kind::general && in_check(before, before.turn)) return nature;
        Position attacking = after;
        attacking.turn     = before.turn;
        const auto kill    = forcing(attacking, before.turn, 0);
        if (kill.won) {
            Position original = before;
            const auto old    = forcing(original, before.turn, 0);
            nature.kill       = !old.won || (kill.participants & (1u << (moved.id - 1))) != 0;
            if (nature.kill) return nature;
        }
        const auto previous = threats(before, before.turn, involved);
        const auto current  = threats(after, before.turn, involved);
        for (const auto& chase : current) {
            const auto old     = std::ranges::find(previous, chase.target, &Chase::target);
            const bool direct  = (chase.attackers & (1u << (moved.id - 1))) != 0;
            const bool created = old == previous.end() || (chase.attackers & ~old->attackers) != 0;
            if (!direct && !created) continue;
            std::uint32_t exempt{};
            for (const auto& piece : after.board)
                if (piece.kind == Kind::general || piece.kind == Kind::soldier) exempt |= 1u << (piece.id - 1);
            if ((chase.attackers & ~exempt) == 0) continue;
            // 26.2: a sole attacking piece occupying a defensive drawing
            // point does not chase advisors/elephants merely incidentally.
            int attacking_pieces{}, attacking_square{};
            for (int square = 0; square < 90; ++square) {
                const auto piece = after.board[square];
                if (piece.color == before.turn && (piece.kind == Kind::rook || piece.kind == Kind::horse || piece.kind == Kind::cannon || piece.kind == Kind::soldier)) {
                    ++attacking_pieces;
                    attacking_square = square;
                }
            }
            if (attacking_pieces == 1 && (chase.kind == Kind::advisor || chase.kind == Kind::elephant) && defensive_point(after, attacking_square)) continue;
            nature.chases.push_back(chase);
        }
        return nature;
    }

    Decision adjudicate(const Position& position, const std::span<const Step> history, Decision previous, const std::span<const Move> moves) {
        if (moves.empty()) return {position.turn == Color::red ? Outcome::black_win : Outcome::red_win, in_check(position, position.turn) ? Reason::checkmate : Reason::stalemate};
        const bool dead        = std::ranges::none_of(position.board, [](const Piece& piece) { return piece.kind == Kind::rook || piece.kind == Kind::horse || piece.kind == Kind::cannon || piece.kind == Kind::soldier; });
        const int cannons      = int(std::ranges::count(position.board, Kind::cannon, &Piece::kind));
        const bool lone_cannon = cannons == 1 && std::ranges::all_of(position.board, [](const Piece& piece) { return piece.kind == Kind::none || piece.kind == Kind::general || piece.kind == Kind::cannon; });
        if (dead || lone_cannon) return {Outcome::draw, Reason::dead_position};
        std::size_t since_capture{};
        for (std::size_t i = history.size(); i > 0; --i)
            if (history[i - 1].undo.captured.kind != Kind::none) {
                since_capture = i;
                break;
            }
        // 22.2 counts checks separately for either side acting as claimant.
        std::array<int, 2> turns{}, counts{}, checks{};
        for (std::size_t i = since_capture; i < history.size(); ++i) {
            const auto& step  = history[i];
            const auto& after = i + 1 == history.size() ? position : history[i + 1].before;
            const int side    = int(step.before.turn);
            ++turns[side];
            if (!in_check(after, after.turn) || checks[side]++ < 10) ++counts[side];
        }
        if ((counts[0] >= 60 && turns[1] >= 60) || (counts[1] >= 60 && turns[0] >= 60)) return {Outcome::draw, Reason::move_limit};
        if (previous.pending_draw) {
            const auto index     = history.size() - 1;
            const auto nature    = previous.require_idle ? classify(history[index].before, history[index].undo.move) : Nature{};
            const bool continues = previous.require_idle ? nature.check || nature.kill || !nature.chases.empty() : history[index].undo.move == history[index - previous.period].undo.move;
            if (continues) {
                if (history.size() - previous.started >= 4) return {Outcome::draw, Reason::repetition};
                return previous;
            }
            previous = {};
        }
        if (previous.change) {
            const auto& step = history.back();
            const int side   = int(step.before.turn);
            if (previous.change & (1u << side)) {
                const auto nature  = classify(step.before, step.undo.move);
                const bool idle    = !nature.check && !nature.kill && nature.chases.empty();
                const bool changed = previous.require_idle ? idle : step.undo.move != history[history.size() - 1 - previous.period].undo.move;
                if (changed) previous.change &= ~(1u << side);
                else if (history.size() - previous.started >= 3) return {side == 0 ? Outcome::black_win : Outcome::red_win, previous.reason};
            }
            if (previous.change) return previous;
        }
        std::size_t period{};
        for (std::size_t candidate = 2; candidate * 3 <= history.size() - since_capture; candidate += 2) {
            const auto start = history.size() - candidate * 3;
            if (!same_position(position, history[history.size() - candidate].before)) continue;
            bool repeated{true};
            // 23.2 includes the incoming first move, whose origin may be
            // outside the loop. Compare from the position after that move.
            for (std::size_t i = start + 1; i + candidate < history.size(); ++i)
                if (history[i].undo.move != history[i + candidate].undo.move || !same_position(history[i].before, history[i + candidate].before)) {
                    repeated = false;
                    break;
                }
            if (repeated) {
                period = candidate;
                break;
            }
        }
        if (!period && history.size() - since_capture < 18) return {};
        const auto sequence = history.last(period ? period : 18);
        std::vector<Nature> natures(sequence.size());
        std::uint32_t involved{};
        for (const auto& step : sequence) involved |= 1u << (step.before.board[step.undo.move.from].id - 1);
        std::array<bool, 2> eligible{true, true};
        for (const auto color : {Color::red, Color::black}) {
            if (!period) {
                std::set<int> points;
                std::map<std::pair<std::uint8_t, int>, int> visits;
                for (const auto& step : sequence)
                    if (step.before.turn == color) {
                        points.insert(step.undo.move.to);
                        ++visits[{step.before.board[step.undo.move.from].id, step.undo.move.to}];
                    }
                // 23.4 concerns alternating repeated attacking moves, not
                // nine unrelated moves that happen to visit different points.
                if (points.size() < 3 || std::ranges::any_of(visits, [](const auto& visit) { return visit.second < 2; })) {
                    eligible[int(color)] = false;
                    continue;
                }
            }
            for (std::size_t i = sequence.size(); i > 0; --i) {
                const auto& step = sequence[i - 1];
                if (step.before.turn != color) continue;
                natures[i - 1]     = classify(step.before, step.undo.move, involved);
                const auto& nature = natures[i - 1];
                if (!period && !nature.check && !nature.kill && nature.chases.empty()) {
                    eligible[int(color)] = false;
                    break;
                }
            }
        }
        if (!period && !eligible[0] && !eligible[1]) return {};
        const bool multiple_points = !period;
        const auto red             = eligible[0] ? repetition(natures, sequence, Color::red) : Repetition{};
        const auto black           = eligible[1] ? repetition(natures, sequence, Color::black) : Repetition{};
        if (red.prohibited && red.long_check && !(black.prohibited && black.long_check)) return {Outcome::black_win, Reason::perpetual_check};
        if (black.prohibited && black.long_check && !(red.prohibited && red.long_check)) return {Outcome::red_win, Reason::perpetual_check};
        if (!red.prohibited && !black.prohibited) {
            if (history.size() <= 50) return {Outcome::ongoing, Reason::repetition, 1, history.size(), period};
            return {Outcome::ongoing, Reason::repetition, 0, history.size(), period, false, true};
        }
        std::uint8_t change{};
        if (red.prohibited && !black.prohibited) change = 1;
        else if (black.prohibited && !red.prohibited) change = 2;
        else if (red.long_check && black.long_check) return {Outcome::ongoing, Reason::repetition, 0, history.size(), period, false, true};
        else if (red.urgent != black.urgent) change = red.urgent ? 1 : 2;
        if (!change) return {Outcome::ongoing, Reason::repetition, 0, history.size(), period ? period : 18, multiple_points, true};
        return {Outcome::ongoing, Reason::perpetual_attack, change, history.size(), period ? period : 18, multiple_points};
    }

    std::string_view describe(const Reason reason) {
        switch (reason) {
        case Reason::none: return "";
        case Reason::checkmate: return "将死";
        case Reason::stalemate: return "困毙";
        case Reason::perpetual_check: return "长将";
        case Reason::perpetual_attack: return "长杀 / 长捉";
        case Reason::repetition: return "重复局面";
        case Reason::move_limit: return "自然限着";
        case Reason::dead_position: return "双方无取胜可能";
        }
        std::unreachable();
    }
} // namespace chess
