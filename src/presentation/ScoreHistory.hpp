#pragma once
#include "game/Game.hpp"
#include <deque>
#include <optional>
#include <tuple>

namespace presentation {
struct ScoreRecord {
    int round, hand, attempt;
    bj::Outcome outcome;
    bj::ScoreBreakdown score;
    int roundScore, goal;
};

// Read-only presentation log. Accept settled snapshots only, never intermediate rewards.
class ScoreHistory {
public:
    static constexpr std::size_t capacity = 8;
    void reset() { records_.clear(); last_.reset(); }
    void observe(const bj::Snapshot& state, bool busy) {
        if (busy || state.outcome == bj::Outcome::None ||
            (state.phase != bj::Phase::RoundOver && state.phase != bj::Phase::RunLost)) return;
        const Key key{state.seed,state.round,state.handsLeft,state.revivalsUsed};
        if (last_ && *last_ == key) return;
        last_ = key;
        records_.push_front({state.round,state.maxHands-state.handsLeft,state.revivalsUsed+1,
                             state.outcome,state.score,state.roundScore,state.goal});
        if(records_.size()>capacity) records_.pop_back();
    }
    const std::deque<ScoreRecord>& records() const { return records_; }
private:
    using Key = std::tuple<std::uint32_t,int,int,int>;
    std::optional<Key> last_;
    std::deque<ScoreRecord> records_;
};
} // namespace presentation
