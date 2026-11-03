#include "game/Game.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

// A bounded, reproducible public-API simulation, not an optimal-play solver.
// Compile with BJ_BALANCE_ACTIVE=1 for the expanded catalog's active API.
// Compile against the historical core without that definition for the baseline.
#ifndef BJ_BALANCE_ACTIVE
#define BJ_BALANCE_ACTIVE 0
#endif

namespace {
bool traceCommands = false;
bool traceNextShop = false;
bool traceTargetReached = false;

struct Result {
    std::uint32_t seed;
    int cleared = 0;
    int hands = 0;
    int wins = 0;
    int maxOwned = 0;
    int totalScore = 0;
    int abilityUses = 0;
    bool capped = false;
};

const bj::RelicInfo& info(bj::Relic relic) {
    const auto& catalog = bj::relicCatalog();
    const auto it = std::find_if(catalog.begin(), catalog.end(), [=](const auto& item) { return item.id == relic; });
    if (it == catalog.end()) throw std::logic_error("Missing catalog entry");
    return *it;
}

double utility(bj::Relic relic) {
    // Fixed ex-ante preferences, unchanged between versions for legacy items.
    // Units are heuristic expected benefit, not estimated or asserted EVs.
    static const std::map<std::string, double> ranks = {
        {"Precision", 5.0}, {"Long Hand", 3.4}, {"Second Chance", 12.0},
        {"Streak", 4.4}, {"Royal Purse", 1.1}, {"Soft Touch", 1.2},
        {"Narrow Escape", 6.5}, {"Red Ribbon", 1.4}, {"Black Velvet", 1.4},
        {"Even Odds", 2.4}, {"Court Favor", 3.5}, {"Humble Crown", 3.0},
        {"Blackjack Bonus", 1.8}, {"Copper Bell", 2.8}, {"Seventeen Charm", 1.2},
        {"Scarlet Mask", 3.0}, {"Midnight Mask", 3.0}, {"Royal Court", 4.0},
        {"Soft Crown", 3.8}, {"Jester Crown", 3.2}, {"Five Card Parade", 2.2},
        {"Final Act", 7.5}, {"Underdog", 2.5}, {"Encore", 3.0},
        {"Rewind", 12.0}, {"Redraw", 10.0}, {"Forked Path", 13.0}, {"Lifeline", 11.0},
        {"Velvet Dividend", 5.0}, {"Lucky Seven", 3.0}, {"Twin Sigil", 3.0},
        {"Mixed Company", 4.0}, {"Crescendo", 7.0}, {"Ace High", 6.0},
        {"Silver Lining", 7.0}, {"Loaded Dice", 7.0}, {"Alchemist", 8.0},
        {"Phoenix", 9.0}, {"Royal Decree", 15.0}, {"Grand Finale", 12.0}
    };
    const auto& item = info(relic);
    const auto found = ranks.find(item.name);
    if (found != ranks.end()) return found->second;
    // New items receive a documented neutral rarity prior unless specified above.
    return item.rarity == bj::Rarity::Legendary ? 10.0 : item.rarity == bj::Rarity::Rare ? 6.0 : 3.5;
}

bool shouldHit(const bj::Snapshot& state) {
    const auto value = bj::evaluateHand(state.player);
    const int shown = state.dealer.front().rank == 1 ? 11 : std::min(state.dealer.front().rank, 10);
    if (value.soft) return value.total <= 17 || (value.total == 18 && shown >= 9);
    if (value.total <= 11) return true;
    if (value.total == 12) return shown < 4 || shown > 6;
    if (value.total <= 16) return shown < 2 || shown > 6;
    return false;
}

void shop(bj::Game& game, int capacity) {
    // Buy every affordable improvement, best utility first. When full, replace
    // the weakest only for >=15% higher utility and after checking affordability.
    for (int guard = 0; guard < 16; ++guard) {
        const auto& state = game.state();
        if (traceCommands && state.owned.size() >= 8) return;
        const auto weakest = std::min_element(state.owned.begin(), state.owned.end(),
            [](auto a, auto b) {
                return utility(a) != utility(b) ? utility(a) < utility(b) : static_cast<int>(a) < static_cast<int>(b);
            });
        const bool full = static_cast<int>(state.owned.size()) >= capacity;
        const int refund = full && weakest != state.owned.end() ? info(*weakest).cost / 2 : 0;
        const bj::RelicInfo* best = nullptr;
        for (auto offered : state.offers) {
            const auto& candidate = info(offered);
            if (game.hasRelic(offered) || game.boughtThisShop(offered)) continue;
            if (candidate.cost > state.coins + refund) continue;
            if (full && utility(offered) < utility(*weakest) * 1.15) continue;
            if (!best || utility(offered) > utility(best->id)) best = &candidate;
        }
        if (!best) return;
        const auto purchase = best->id;
        if (full) {
            const auto sold = *weakest;
#if BJ_BALANCE_ACTIVE
            if (!game.replace(sold, purchase)) throw std::logic_error("Expected replacement rejected");
            if (traceCommands) std::cout << "replace " << static_cast<int>(sold) << ' ' << static_cast<int>(purchase) << '\n';
            continue;
#else
            if (!game.sell(sold)) throw std::logic_error("Expected sale rejected");
            if (traceCommands) std::cout << "sell " << static_cast<int>(sold) << '\n';
#endif
        }
        if (!game.buy(purchase)) throw std::logic_error("Expected purchase rejected");
        if (traceCommands) std::cout << "buy " << static_cast<int>(purchase) << '\n';
    }
    throw std::logic_error("Shop guard exceeded");
}

// Implemented through public game commands only; no state editing or future-card
// inspection. Choice can inspect only cards explicitly revealed by that ability.
int useAbility(bj::Game& game, bool enabled) {
#if BJ_BALANCE_ACTIVE
    const auto& state = game.state();
    if (!state.cardChoices.empty()) {
        std::size_t best = 0;
        int bestValue = -1000;
        for (std::size_t index = 0; index < state.cardChoices.size(); ++index) {
            auto hand = state.player;
            hand.push_back(state.cardChoices[index]);
            const auto value = bj::evaluateHand(hand);
            const int rank = value.bust ? -value.total : value.total;
            if (rank > bestValue) { bestValue = rank; best = index; }
        }
        if (!game.chooseCard(best)) throw std::logic_error("Expected choice rejected");
        if (traceCommands) std::cout << "choose " << best << '\n';
        return 2;
    }
    if (state.pendingRescue) {
        if (enabled) {
            for (auto relic : {bj::Relic::Lifeline, bj::Relic::Rewind, bj::Relic::Redraw}) {
                if (bj::abilityStatus(state, relic).usable) {
                    if (!game.activate(relic)) throw std::logic_error("Expected rescue rejected");
                    if (traceCommands) std::cout << "activate " << static_cast<int>(relic) << '\n';
                    return 1;
                }
            }
        }
        if (!game.stand()) throw std::logic_error("Expected bust acceptance rejected");
        if (traceCommands) std::cout << "stand\n";
        return 2;
    }
    if (enabled && shouldHit(state)) {
        if (bj::abilityStatus(state, bj::Relic::ForkedPath).usable) {
            if (!game.activate(bj::Relic::ForkedPath)) throw std::logic_error("Expected fork rejected");
            if (traceCommands) std::cout << "activate " << static_cast<int>(bj::Relic::ForkedPath) << '\n';
            return 1;
        }
        const auto value = bj::evaluateHand(state.player);
        if (!value.soft && value.total >= 12 && value.total <= 16 && state.player.back().rank >= 6
            && bj::abilityStatus(state, bj::Relic::Redraw).usable) {
            if (!game.activate(bj::Relic::Redraw)) throw std::logic_error("Expected redraw rejected");
            if (traceCommands) std::cout << "activate " << static_cast<int>(bj::Relic::Redraw) << '\n';
            return 1;
        }
    }
#else
    static_cast<void>(game);
    static_cast<void>(enabled);
#endif
    return 0;
}

Result simulate(std::uint32_t seed, bj::StartDeck deck, int capacity, int limit, bool purchaseItems, bool activeEnabled) {
    bj::Game game(seed, deck);
    Result result{seed};
    for (int guard = 0; guard < 20000; ++guard) {
        const auto& state = game.state();
        result.wins = state.wins;
        result.totalScore = state.totalScore;
        result.maxOwned = std::max(result.maxOwned, static_cast<int>(state.owned.size()));
        if (traceCommands && result.maxOwned >= 8 && !traceNextShop) {
            traceTargetReached = true;
            return result;
        }
        if (state.phase == bj::Phase::Ready) {
            if (!game.deal()) throw std::logic_error("Expected deal rejected");
            if (traceCommands) std::cout << "deal\n";
            ++result.hands;
        } else if (state.phase == bj::Phase::PlayerTurn) {
            const int action = useAbility(game, activeEnabled);
            if (action != 0) { if (action == 1) ++result.abilityUses; continue; }
            const bool hit = shouldHit(state);
            if (!(hit ? game.hit() : game.stand())) throw std::logic_error("Expected action rejected");
            if (traceCommands) std::cout << (hit ? "hit\n" : "stand\n");
        } else if (state.phase == bj::Phase::RoundOver) {
            if (!game.advance()) throw std::logic_error("Expected advance rejected");
            if (traceCommands) std::cout << "advance\n";
        } else if (state.phase == bj::Phase::Shop) {
            result.cleared = state.round;
            if (traceCommands && traceNextShop && state.owned.size() >= 8) {
                traceTargetReached = true;
                std::cerr << "Trace reached shop after round " << state.round
                          << "; equipped " << state.owned.size() << "; coins " << state.coins << "; offers";
                for (auto offered : state.offers) std::cerr << ' ' << static_cast<int>(offered);
                std::cerr << '\n';
                return result;
            }
            if (result.cleared >= limit) { result.capped = true; return result; }
            if (purchaseItems) shop(game, capacity);
            if (!game.leaveShop()) throw std::logic_error("Expected shop leave rejected");
            if (traceCommands) std::cout << "leave\n";
        } else if (state.phase == bj::Phase::RunLost) {
            if (!game.revive()) return result;
            if (traceCommands) std::cout << "revive\n";
        }
    }
    throw std::logic_error("Run guard exceeded");
}

void summarize(const std::vector<Result>& results, const std::string& name) {
    std::vector<int> cleared;
    double total = 0, hands = 0, wins = 0, active = 0, equipment = 0;
    for (const auto& r : results) {
        cleared.push_back(r.cleared); total += r.cleared; hands += r.hands;
        wins += r.wins; active += r.abilityUses; equipment += r.maxOwned;
    }
    std::sort(cleared.begin(), cleared.end());
    const auto percentile = [&](double p) { return cleared[static_cast<std::size_t>(std::floor(p * (cleared.size() - 1)))]; };
    std::cout << name << ',' << results.size() << ',' << std::fixed << std::setprecision(3)
              << total / results.size() << ',' << percentile(.25) << ',' << percentile(.5) << ',' << percentile(.75) << ',' << percentile(.9);
    for (int round : {3, 5, 8, 12, 20, 30}) {
        const auto reached = std::count_if(results.begin(), results.end(), [=](const Result& r) { return r.cleared >= round - 1; });
        std::cout << ',' << 100.0 * reached / results.size();
    }
    const auto capped = std::count_if(results.begin(), results.end(), [](const Result& r) { return r.capped; });
    std::cout << ',' << 100.0 * capped / results.size() << ',' << hands / results.size() << ','
              << 100.0 * wins / hands << ',' << equipment / results.size() << ',' << active / results.size() << '\n';
}
}

int main(int argc, char** argv) {
    try {
        if (argc > 1 && std::string(argv[1]) == "--trace") {
            const auto seed = argc > 2 ? static_cast<std::uint32_t>(std::stoul(argv[2])) : 26u;
            const std::string deckName = argc > 3 ? argv[3] : "Life";
            if (deckName != "Life" && deckName != "Hands" && deckName != "Coins")
                throw std::invalid_argument("Trace deck must be Life, Hands or Coins");
            const auto deck = deckName == "Hands" ? bj::StartDeck::ExtraHand : deckName == "Coins" ? bj::StartDeck::Rich : bj::StartDeck::SecondLife;
            if (argc > 4 && std::string(argv[4]) != "next-shop")
                throw std::invalid_argument("Optional trace target must be next-shop");
            traceNextShop = argc > 4;
            traceCommands = true;
            const auto result = simulate(seed, deck, 8, 30, true, true);
            if (!traceTargetReached || result.maxOwned < 8)
                throw std::runtime_error(traceNextShop ? "Trace seed did not reach a new shop with eight equipped items"
                                                      : "Trace seed did not acquire eight equipment items");
            return 0;
        }
        const int runs = argc > 1 ? std::stoi(argv[1]) : 5000;
        const int capacity = argc > 2 ? std::stoi(argv[2]) : 8;
        const int limit = argc > 3 ? std::stoi(argv[3]) : 30;
        const bool purchaseItems = !(argc > 4 && std::string(argv[4]) == "no-shop");
        const bool activeEnabled = !(argc > 4 && std::string(argv[4]) == "no-active");
        if (runs < 1 || runs > 100000 || capacity < 1 || capacity > 8 || limit < 1 || limit > 100)
            throw std::invalid_argument("Usage: balance_sim [runs 1..100000] [capacity 1..8] [round-cap 1..100] [shop|no-shop|no-active] [raw.csv]");
        std::ofstream raw;
        if (argc > 5) {
            raw.open(argv[5]);
            if (!raw) throw std::runtime_error("Cannot open raw output");
            raw << "deck,seed,rounds_cleared,hands,wins,max_owned,total_score,ability_uses,capped\n";
        }
        std::cout << "deck,runs,mean_cleared,p25_cleared,median_cleared,p75_cleared,p90_cleared,reached3_pct,reached5_pct,reached8_pct,reached12_pct,reached20_pct,reached30_pct,cleared_cap_pct,mean_hands,hand_win_pct,mean_max_owned,mean_ability_uses\n";
        for (const auto& entry : std::array<std::pair<bj::StartDeck, const char*>, 3>{{
            {bj::StartDeck::ExtraHand, "Hands"}, {bj::StartDeck::SecondLife, "Life"}, {bj::StartDeck::Rich, "Coins"}}}) {
            std::vector<Result> results;
            results.reserve(static_cast<std::size_t>(runs));
            for (int seed = 1; seed <= runs; ++seed) {
                const auto result = simulate(static_cast<std::uint32_t>(seed), entry.first, capacity, limit, purchaseItems, activeEnabled);
                results.push_back(result);
                if (raw) raw << entry.second << ',' << result.seed << ',' << result.cleared << ',' << result.hands << ','
                    << result.wins << ',' << result.maxOwned << ',' << result.totalScore << ',' << result.abilityUses << ',' << result.capped << '\n';
            }
            summarize(results, entry.second);
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Balance simulation failed: " << error.what() << '\n';
        return 1;
    }
}
