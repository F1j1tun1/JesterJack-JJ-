#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <numeric>
#include <random>
#include <vector>

namespace audio {

// Endless shuffled order: every track is played once per cycle, a new cycle is
// reshuffled, and the last track of a cycle is never repeated straight away.
class ShufflePlaylist {
public:
    ShufflePlaylist(std::size_t count, std::uint32_t seed) : count_(count), random_(seed) {}

    std::size_t size() const { return count_; }

    std::size_t next() {
        if (count_ == 0) return 0;
        if (pending_.empty()) refill();
        last_ = pending_.back();
        pending_.pop_back();
        hasLast_ = true;
        return last_;
    }

private:
    std::size_t count_;
    std::mt19937 random_;
    std::vector<std::size_t> pending_; // Consumed from the back.
    std::size_t last_ = 0;
    bool hasLast_ = false;

    void refill() {
        pending_.resize(count_);
        std::iota(pending_.begin(), pending_.end(), std::size_t{0});
        std::shuffle(pending_.begin(), pending_.end(), random_);
        if (count_ > 1 && hasLast_ && pending_.back() == last_) std::swap(pending_.back(), pending_.front());
    }
};

} // namespace audio
