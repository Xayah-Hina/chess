export module chess.ai.checkpoint;
export import chess.ai.selfplay;
import std;

export namespace chess::ai {
    void serialize(Archive& archive, Sample& sample);
    void serialize(Archive& archive, Game& game);
    void serialize(Archive& archive, SelfPlay& selfplay);
    void serialize(Archive& archive, Replay& replay);
    void serialize(Archive& archive, Optimizer& optimizer);
} // namespace chess::ai
