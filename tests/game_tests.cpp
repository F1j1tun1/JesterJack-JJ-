#include "game/Game.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

int failures = 0;
int checks = 0;

void check(bool condition, const std::string& message) {
    ++checks;
    if (!condition) {
        ++failures;
        if (failures <= 25) {
            std::cerr << "FAIL: " << message << '\n';
        }
    }
}

std::vector<bj::Card> hand(std::initializer_list<int> ranks) {
    std::vector<bj::Card> cards;
    for (int rank : ranks) {
        cards.push_back({rank, bj::Suit::Clubs});
    }
    return cards;
}

bool sameCards(const std::vector<bj::Card>& a, const std::vector<bj::Card>& b) {
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](const bj::Card& x, const bj::Card& y) {
        return x.rank == y.rank && x.suit == y.suit;
    });
}

bool sameEffects(const std::vector<bj::AppliedEffect>& a, const std::vector<bj::AppliedEffect>& b) {
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](const bj::AppliedEffect& x, const bj::AppliedEffect& y) {
        return x.relic == y.relic && x.points == y.points && x.coins == y.coins && x.multiplierBonus == y.multiplierBonus;
    });
}

bool sameState(const bj::Snapshot& a, const bj::Snapshot& b) {
    return sameCards(a.player, b.player) && sameCards(a.dealer, b.dealer)
        && a.phase == b.phase && a.outcome == b.outcome && a.round == b.round
        && a.handsLeft == b.handsLeft && a.roundScore == b.roundScore && a.goal == b.goal
        && a.startDeck == b.startDeck && a.maxHands == b.maxHands && a.revivesLeft == b.revivesLeft
        && a.revivalsUsed == b.revivalsUsed
        && a.wins == b.wins && a.totalScore == b.totalScore
        && a.coins == b.coins && a.streak == b.streak && a.dealerStand == b.dealerStand
        && a.seed == b.seed && a.usedSecondChance == b.usedSecondChance && a.owned == b.owned
        && a.offers == b.offers && a.usedHandAbilities == b.usedHandAbilities
        && a.usedRoundAbilities == b.usedRoundAbilities && sameCards(a.cardChoices, b.cardChoices)
        && a.pendingRescue == b.pendingRescue && a.alteredThisHand == b.alteredThisHand
        && a.rescuedThisHand == b.rescuedThisHand && a.score.base == b.score.base && a.score.bonus == b.score.bonus
        && a.score.multiplier == b.score.multiplier && a.score.total == b.score.total
        && a.score.coins == b.score.coins && sameEffects(a.score.appliedEffects, b.score.appliedEffects) && a.message == b.message;
}

bool contains(const std::vector<bj::Relic>& relics, bj::Relic relic) {
    return std::find(relics.begin(), relics.end(), relic) != relics.end();
}

bj::Rarity rarityOf(bj::Relic relic) {
    const auto& catalog = bj::relicCatalog();
    return std::find_if(catalog.begin(), catalog.end(), [relic](const bj::RelicInfo& item) { return item.id == relic; })->rarity;
}

void checkBlockedActions(bj::Game& game) {
    const auto original = game.state();
    const auto phase = original.phase;
    if (phase != bj::Phase::Ready) {
        check(!game.deal(), "deal is restricted to Ready");
    }
    if (phase != bj::Phase::PlayerTurn) {
        check(!game.hit(), "hit is restricted to PlayerTurn");
        check(!game.stand(), "stand is restricted to PlayerTurn");
    }
    if (phase != bj::Phase::RoundOver) {
        check(!game.advance(), "advance is restricted to RoundOver");
    }
    if (phase != bj::Phase::Shop) {
        check(!game.buy(bj::Relic::Precision), "buy is restricted to Shop");
        check(!game.sell(bj::Relic::Precision), "sell is restricted to Shop");
        check(!game.leaveShop(), "leaveShop is restricted to Shop");
    }
    if (phase != bj::Phase::RunLost || original.revivesLeft == 0) {
        check(!game.revive(), "revive requires a failed round and an unused Second Life");
    }
    check(!game.buy(static_cast<bj::Relic>(99)), "unknown upgrade cannot be bought");
    check(!game.sell(static_cast<bj::Relic>(99)), "unknown upgrade cannot be sold");
    check(!game.activate(static_cast<bj::Relic>(99)), "unknown ability cannot activate");
    check(!game.activate(bj::Relic::Precision), "passive cannot be activated");
    check(!game.chooseCard(99), "invalid card selection is harmless");
    check(!game.replace(static_cast<bj::Relic>(99), bj::Relic::Precision), "invalid replacement is harmless");
    check(sameState(original, game.state()), "blocked commands preserve every snapshot field");
}

void testHands() {
    auto value = bj::evaluateHand({});
    check(value.total == 0 && !value.soft && !value.blackjack && !value.bust, "empty hand");
    value = bj::evaluateHand(hand({1, 13}));
    check(value.total == 21 && value.soft && value.blackjack && !value.bust, "ace + king is natural blackjack");
    value = bj::evaluateHand(hand({1, 6}));
    check(value.total == 17 && value.soft, "soft 17");
    value = bj::evaluateHand(hand({1, 6, 10}));
    check(value.total == 17 && !value.soft, "ace drops from 11 to 1");
    value = bj::evaluateHand(hand({1, 1, 9}));
    check(value.total == 21 && value.soft && !value.blackjack, "three-card 21 is not natural");
    value = bj::evaluateHand(hand({1, 1, 1, 9}));
    check(value.total == 12 && !value.soft && !value.bust, "multiple aces drop together");
    value = bj::evaluateHand(hand({10, 11, 12}));
    check(value.total == 30 && value.bust && !value.soft, "face cards count as ten");
    value = bj::evaluateHand(hand({1, 1, 1, 1, 7}));
    check(value.total == 21 && value.soft, "four aces can retain one soft ace");
    for (int rank : {0, 14}) {
        bool rejected = false;
        try {
            bj::evaluateHand(hand({rank}));
        } catch (const std::invalid_argument&) {
            rejected = true;
        }
        check(rejected, "invalid rank is rejected");
    }
}

void testExpandedCatalogueContract() {
    const auto& catalog = bj::relicCatalog();
    check(catalog.size() == 40, "expanded catalogue contains exactly forty relics");
    const std::array<bj::Rarity, 3> rarities{bj::Rarity::Common, bj::Rarity::Rare, bj::Rarity::Legendary};
    const std::array<int, 3> counts{14, 15, 11};
    const std::array<int, 3> weights{50, 36, 14};
    const std::array<int, 3> costs{3, 6, 10};
    for (std::size_t tier = 0; tier < rarities.size(); ++tier) {
        check(bj::rarityWeight(rarities[tier]) == weights[tier], "three-tier shop uses the agreed rarity weights");
        check(std::count_if(catalog.begin(), catalog.end(), [&](const bj::RelicInfo& item) {
            return item.rarity == rarities[tier];
        }) == counts[tier], "each rarity has the agreed number of relics");
        check(std::all_of(catalog.begin(), catalog.end(), [&](const bj::RelicInfo& item) {
            return item.rarity != rarities[tier] || item.cost == costs[tier];
        }), "rarity determines purchase price");
    }
}

// Independent comparison includes the priority of a natural over an ordinary 21.
bj::Outcome expectedOutcome(const bj::Snapshot& state) {
    const auto player = bj::evaluateHand(state.player);
    const auto dealer = bj::evaluateHand(state.dealer);
    if (player.bust) return bj::Outcome::Loss;
    if (dealer.blackjack) return player.blackjack ? bj::Outcome::Push : bj::Outcome::Loss;
    if (player.blackjack || dealer.bust) return bj::Outcome::Win;
    if (player.total == dealer.total) return bj::Outcome::Push;
    return player.total > dealer.total ? bj::Outcome::Win : bj::Outcome::Loss;
}

int resultRank(const bj::Snapshot& state) {
    const int outcome = state.outcome == bj::Outcome::Win ? 2 : (state.outcome == bj::Outcome::Push ? 1 : 0);
    return outcome * 10000 + state.score.coins * 100 + state.score.total;
}

// Explore legal continuations on copies to exercise later shops and upgrades.
// This is only a test search policy, not the strategy shipped in the game.
bj::Game finishBest(bj::Game game) {
    if (game.state().phase == bj::Phase::RoundOver || game.state().phase == bj::Phase::RunLost) return game;
    bj::Game best = game;
    int bestRank = -1;
    for (int action = 0; action < 52; ++action) {
        if (game.state().phase == bj::Phase::RoundOver || game.state().phase == bj::Phase::RunLost) {
            if (resultRank(game.state()) > bestRank) best = game;
            break;
        }
        auto standing = game;
        standing.stand();
        if (resultRank(standing.state()) > bestRank) {
            best = standing;
            bestRank = resultRank(standing.state());
        }
        if (game.state().pendingRescue) break;
        game.hit();
    }
    return best;
}

void replayStep(bj::Game& game) {
    switch (game.state().phase) {
    case bj::Phase::Ready: game.deal(); break;
    case bj::Phase::PlayerTurn:
        if (bj::evaluateHand(game.state().player).total < 17) game.hit();
        else game.stand();
        break;
    case bj::Phase::RoundOver: game.advance(); break;
    case bj::Phase::Shop:
        for (auto relic : game.state().offers) game.buy(relic);
        game.leaveShop();
        break;
    case bj::Phase::RunLost: game.restart(game.state().seed + 1); break;
    }
}

void testDeterminismAndBlockedRandomness() {
    bj::Game first(112358);
    bj::Game second(112358);
    const auto initial = first.state();
    for (int action = 0; action < 1000; ++action) {
        checkBlockedActions(first);
        check(sameState(first.state(), second.state()), "blocked commands preserve observable state");
        replayStep(first);
        replayStep(second);
        check(sameState(first.state(), second.state()), "blocked commands preserve future card and shop randomness");
    }
    first.restart(112358);
    second.restart(112358);
    check(sameState(initial, first.state()), "restart clears the complete run state");
    first.deal();
    second.deal();
    check(sameState(first.state(), second.state()), "restart resets both random streams");
    bj::Game other(112359);
    other.deal();
    check(!sameCards(first.state().player, other.state().player)
        || !sameCards(first.state().dealer, other.state().dealer), "different seeds change the opening deal");
}

void testNaturalBaseAndActualBust() {
    bool naturalWin = false;
    bool naturalPush = false;
    bool actualBust = false;
    for (std::uint32_t seed = 0; seed < 10000 && !(naturalWin && naturalPush && actualBust); ++seed) {
        bj::Game game(seed);
        game.deal();
        const auto result = game.state();
        const auto player = bj::evaluateHand(result.player);
        if (!naturalWin && player.blackjack && result.outcome == bj::Outcome::Win) {
            naturalWin = true;
            check(result.score.base == 21 && result.score.bonus == 0 && result.score.total == 21
                && result.roundScore == 21 && result.totalScore == 21, "an unupgraded natural scores exactly twenty-one, without an automatic bonus");
            check(result.score.coins == 4 && result.coins == 4 && result.handsLeft == 4,
                "a natural pays normal win income and consumes one hand");
            check(result.phase == bj::Phase::RoundOver && game.advance()
                && game.state().phase == bj::Phase::Ready && game.state().roundScore == 21,
                "a first-hand natural alone does not clear the thirty-point round goal");
        }
        if (!naturalPush && player.blackjack && result.outcome == bj::Outcome::Push) {
            naturalPush = true;
            check(result.score.base == 0 && result.score.bonus == 0 && result.score.total == 0
                && result.score.coins == 0 && result.roundScore == 0, "two natural blackjacks push and award no points or coins");
        }
        if (!actualBust && result.phase == bj::Phase::PlayerTurn) {
            auto probe = game;
            for (int action = 0; action < 52 && probe.state().phase == bj::Phase::PlayerTurn; ++action) {
                probe.hit();
                const auto value = bj::evaluateHand(probe.state().player);
                if (value.bust) {
                    actualBust = true;
                    check(value.total > 21 && probe.state().outcome == bj::Outcome::Loss
                        && probe.state().score.base == 0 && probe.state().score.bonus == 0
                        && probe.state().score.total == 0 && probe.state().score.coins == 0,
                        "an actual hit bust retains its real above-twenty-one total and awards nothing");
                    check(probe.state().phase == bj::Phase::RoundOver && probe.state().handsLeft == 4
                        && probe.advance() && probe.state().phase == bj::Phase::Ready,
                        "an early bust ends only that hand and retains the other four attempts");
                }
            }
        }
    }
    check(naturalWin && naturalPush && actualBust, "bounded legal seed search covers a natural win, natural push and actual bust");
}

void testBlackjackBonusCounterfactual() {
    int naturalWins = 0;
    int ordinaryTwentyOnes = 0;
    int losses = 0;
    int pushes = 0;
    int comparedHands = 0;
    for (std::uint32_t seed = 0; seed < 5000
        && (naturalWins == 0 || ordinaryTwentyOnes == 0 || losses == 0 || pushes == 0); ++seed) {
        bj::Game plain(seed);
        bool offered = false;
        for (int attempt = 0; attempt < 30 && plain.state().phase == bj::Phase::Ready; ++attempt) {
            plain.deal();
            plain = finishBest(plain);
            if (plain.state().phase == bj::Phase::RunLost) break;
            plain.advance();
            if (plain.state().phase != bj::Phase::Shop) continue;
            offered = contains(plain.state().offers, bj::Relic::BlackjackBonus) && plain.state().coins >= 3;
            if (offered) break;
            plain.leaveShop();
        }
        if (!offered) continue;
        auto upgraded = plain;
        check(upgraded.buy(bj::Relic::BlackjackBonus) && upgraded.state().owned.size() == 1,
            "the counterfactual run acquires Blackjack Bonus through its real shop offer");
        plain.leaveShop();
        upgraded.leaveShop();
        int accumulatedBonus = 0;
        for (int attempt = 0; attempt < 5; ++attempt) {
            check(plain.deal() && upgraded.deal(), "both comparison runs deal through the public command API");
            for (int action = 0; action < 52 && plain.state().phase == bj::Phase::PlayerTurn
                && upgraded.state().phase == bj::Phase::PlayerTurn; ++action) {
                if (bj::evaluateHand(plain.state().player).total < 17) {
                    plain.hit();
                    upgraded.hit();
                } else {
                    plain.stand();
                    upgraded.stand();
                }
            }
            const auto baseline = plain.state();
            const auto boosted = upgraded.state();
            const auto player = bj::evaluateHand(baseline.player);
            const bool eligible = baseline.outcome == bj::Outcome::Win && player.blackjack;
            const int extraPoints = eligible ? 5 : 0;
            accumulatedBonus += extraPoints;
            ++comparedHands;
            check(sameCards(baseline.player, boosted.player) && sameCards(baseline.dealer, boosted.dealer)
                && baseline.outcome == boosted.outcome, "buying the natural bonus never changes dealt cards or the winner");
            check(boosted.score.base == baseline.score.base && boosted.score.bonus == baseline.score.bonus + extraPoints
                && boosted.score.total == baseline.score.total + extraPoints,
                "Blackjack Bonus adds five points only to a winning two-card natural");
            check(boosted.score.coins == baseline.score.coins && boosted.coins == baseline.coins - 3,
                "Blackjack Bonus costs three coins and never adds extra win income");
            check(boosted.roundScore == baseline.roundScore + accumulatedBonus
                && boosted.totalScore == baseline.totalScore + accumulatedBonus,
                "only the earned natural bonus changes accumulated round and run points");
            naturalWins += eligible ? 1 : 0;
            ordinaryTwentyOnes += baseline.outcome == bj::Outcome::Win && player.total == 21 && !player.blackjack ? 1 : 0;
            losses += baseline.outcome == bj::Outcome::Loss ? 1 : 0;
            pushes += baseline.outcome == bj::Outcome::Push ? 1 : 0;
            checkBlockedActions(upgraded);
            if (baseline.phase == bj::Phase::RunLost || boosted.phase == bj::Phase::RunLost
                || baseline.roundScore >= baseline.goal || boosted.roundScore >= boosted.goal) break;
            check(plain.advance() && upgraded.advance() && plain.state().phase == bj::Phase::Ready
                && upgraded.state().phase == bj::Phase::Ready, "comparison hands continue normally below the goal");
        }
    }
    check(naturalWins > 0 && ordinaryTwentyOnes > 0 && losses > 0 && pushes > 0,
        "bounded shop-and-play search tests the bonus on naturals, ordinary winning twenty-ones, losses and pushes");
    std::cout << "Blackjack Bonus comparison: " << comparedHands << " hands; natural wins " << naturalWins
              << ", ordinary winning twenty-ones " << ordinaryTwentyOnes << ", losses " << losses << ", pushes " << pushes << '\n';
}

void testStartingDecks() {
    const std::array<bj::StartDeck, 4> decks{
        bj::StartDeck::Standard, bj::StartDeck::ExtraHand, bj::StartDeck::SecondLife, bj::StartDeck::Rich
    };
    for (auto deck : decks) {
        bj::Game game(12345, deck);
        const auto initial = game.state();
        const int hands = deck == bj::StartDeck::ExtraHand ? 6 : 5;
        const int coins = deck == bj::StartDeck::Rich ? 10 : 0;
        const int lives = deck == bj::StartDeck::SecondLife ? 1 : 0;
        check(initial.startDeck == deck && initial.maxHands == hands && initial.handsLeft == hands
            && initial.coins == coins && initial.revivesLeft == lives && initial.revivalsUsed == 0,
            "each starting deck applies exactly its advertised starting benefit");
        checkBlockedActions(game);
        bj::Game standard(12345);
        game.deal();
        standard.deal();
        check(sameCards(game.state().player, standard.state().player) && sameCards(game.state().dealer, standard.state().dealer),
            "starting deck choice never changes the shuffled opening cards");
        game.restart(12345);
        check(sameState(initial, game.state()), "restart(seed) retains the selected deck and resets all of its one-run benefits");
        const auto beforeInvalid = game.state();
        bool rejected = false;
        try {
            game.restart(1, static_cast<bj::StartDeck>(99));
        } catch (const std::invalid_argument&) {
            rejected = true;
        }
        check(rejected && sameState(beforeInvalid, game.state()), "an invalid deck is rejected before mutating a run");

        bool cleared = false;
        for (std::uint32_t seed = 0; seed < 100 && !cleared; ++seed) {
            bj::Game next(seed, deck);
            for (int attempt = 0; attempt < hands; ++attempt) {
                next.deal();
                next = finishBest(next);
                if (next.state().phase == bj::Phase::RunLost) break;
                next.advance();
                if (next.state().phase != bj::Phase::Shop) continue;
                const auto shop = next.state();
                check(next.leaveShop(), "a selected starting deck can advance through a real cleared round");
                check(next.state().round == 2 && next.state().maxHands == hands && next.state().handsLeft == hands
                    && next.state().roundScore == 0 && next.state().coins == shop.coins
                    && next.state().revivesLeft == lives && next.state().revivalsUsed == 0,
                    "new rounds keep the extra hand or unused life and do not grant rich starting coins again");
                cleared = true;
                break;
            }
        }
        check(cleared, "bounded legal play reaches a second round for every deck");

        bj::Game first(54321, deck);
        bj::Game second(54321, deck);
        for (int action = 0; action < 350; ++action) {
            checkBlockedActions(first);
            if (first.state().phase == bj::Phase::RunLost && first.state().revivesLeft > 0) {
                check(first.revive() && second.revive(), "paired second-life runs accept the same revive command");
            } else {
                replayStep(first);
                replayStep(second);
            }
            check(sameState(first.state(), second.state()), "same deck, seed and actions stay deterministic across future shops, restarts and revives");
        }
    }
    bj::Game switched(7);
    switched.restart(8, bj::StartDeck::Rich);
    check(switched.state().seed == 8 && switched.state().startDeck == bj::StartDeck::Rich
        && switched.state().coins == 10, "explicit restart chooses a new seed and starting deck");
}

bool forceDefeat(bj::Game& game) {
    for (int action = 0; action < 1500 && game.state().phase != bj::Phase::RunLost; ++action) {
        switch (game.state().phase) {
        case bj::Phase::Ready: game.deal(); break;
        case bj::Phase::PlayerTurn: game.hit(); break;
        case bj::Phase::RoundOver: game.advance(); break;
        case bj::Phase::Shop: game.leaveShop(); break;
        case bj::Phase::RunLost: break;
        }
    }
    return game.state().phase == bj::Phase::RunLost;
}

void testSecondLifePreservesBuild() {
    bool witnessed = false;
    for (std::uint32_t seed = 0; seed < 1500 && !witnessed; ++seed) {
        bj::Game game(seed, bj::StartDeck::SecondLife);
        for (int attempt = 0; attempt < 60 && game.state().phase == bj::Phase::Ready; ++attempt) {
            game.deal();
            game = finishBest(game);
            if (game.state().phase == bj::Phase::RunLost) break;
            game.advance();
            if (game.state().phase != bj::Phase::Shop) continue;
            if (game.buy(bj::Relic::SecondChance)) {
                game.leaveShop();
                break;
            }
            game.leaveShop();
        }
        if (!game.hasRelic(bj::Relic::SecondChance) || !forceDefeat(game) || !game.state().usedSecondChance) continue;
        witnessed = true;
        const auto failed = game.state();
        auto twin = game;
        check(failed.revivesLeft == 1 && failed.handsLeft == 0 && failed.roundScore < failed.goal,
            "the unused second life remains available on the real failed-round screen");
        check(game.revive() && twin.revive(), "a failed Second Life run can revive once");
        const auto revived = game.state();
        check(revived.phase == bj::Phase::Ready && revived.round == failed.round && revived.goal == failed.goal
            && revived.roundScore == 0 && revived.handsLeft == failed.maxHands && revived.maxHands == failed.maxHands,
            "revive retries exactly the failed round with zero round points and a fresh hand allowance");
        check(revived.coins == failed.coins && revived.owned == failed.owned && revived.totalScore == failed.totalScore
            && revived.wins == failed.wins && revived.seed == failed.seed && revived.startDeck == failed.startDeck,
            "revive keeps earned money, purchased relics, total points, wins, seed and starting deck");
        check(revived.revivesLeft == 0 && revived.revivalsUsed == 1 && revived.streak == 0
            && !revived.usedSecondChance && revived.player.empty() && revived.dealer.empty()
            && revived.score.appliedEffects.empty() && revived.score.total == 0 && revived.outcome == bj::Outcome::None,
            "revive consumes the one life, clears the failed hand and refreshes Second Chance for the new attempt");
        checkBlockedActions(game);
        game.deal();
        twin.deal();
        check(sameState(game.state(), twin.state()), "blocked revive attempts do not change the fresh retry deck or future RNG");
        bj::Game original(seed, bj::StartDeck::SecondLife);
        original.deal();
        check(!sameCards(game.state().player, original.state().player) || !sameCards(game.state().dealer, original.state().dealer),
            "the retry continues the random stream instead of replaying the seed's original cards");
        check(forceDefeat(game) && forceDefeat(twin) && sameState(game.state(), twin.state()),
            "identical revived runs remain deterministic until their next failure");
        const auto exhausted = game.state();
        check(!game.revive() && sameState(exhausted, game.state()), "a second failure cannot spend another life");
        game.restart(seed);
        const bj::Game fresh(seed, bj::StartDeck::SecondLife);
        check(sameState(game.state(), fresh.state()), "a new run restores exactly one second life and removes the old build");
    }
    check(witnessed, "bounded public-API play exercises revive with a real purchased relic and spent shield");
}

void testUnbiasedOpeningDistribution() {
    constexpr std::uint32_t samples = 40000;
    int naturals = 0;
    int dealerNaturals = 0;
    std::array<int, 13> ranks{};
    std::array<int, 4> suits{};
    for (std::uint32_t seed = 0; seed < samples; ++seed) {
        bj::Game game(seed);
        game.deal();
        naturals += bj::evaluateHand(game.state().player).blackjack ? 1 : 0;
        dealerNaturals += bj::evaluateHand(game.state().dealer).blackjack ? 1 : 0;
        bool seen[52] = {};
        for (const auto* cards : {&game.state().player, &game.state().dealer}) {
            for (const auto& card : *cards) {
                const int suit = static_cast<int>(card.suit);
                const int id = suit * 13 + card.rank - 1;
                check(id >= 0 && id < 52 && !seen[id], "opening deals use unique standard-deck cards");
                if (id >= 0 && id < 52) {
                    seen[id] = true;
                    ++ranks[static_cast<std::size_t>(card.rank - 1)];
                    ++suits[static_cast<std::size_t>(suit)];
                }
            }
        }
    }
    // Standard-deck P(natural) = 2 * 4/52 * 16/51, approximately 4.83%.
    const double frequency = static_cast<double>(naturals) / samples;
    const double dealerFrequency = static_cast<double>(dealerNaturals) / samples;
    check(frequency > 0.042 && frequency < 0.055, "player naturals are near the standard 4.83 percent");
    check(dealerFrequency > 0.042 && dealerFrequency < 0.055, "dealer naturals are near the standard 4.83 percent");
    for (int count : ranks) check(count > 11500 && count < 13100, "no preferred rank in opening deals");
    for (int count : suits) check(count > 39000 && count < 41000, "no preferred suit in opening deals");
    std::cout << "Opening naturals: player " << naturals << '/' << samples << " ("
              << std::fixed << std::setprecision(3) << frequency * 100.0 << "%), dealer "
              << dealerNaturals << '/' << samples << " (" << dealerFrequency * 100.0 << "%)\n";
}

void testRarityDistribution() {
    constexpr std::array<bj::Rarity, 3> rarities{bj::Rarity::Common, bj::Rarity::Rare, bj::Rarity::Legendary};
    constexpr std::array<int, 3> weights{50, 36, 14};
    constexpr std::array<const char*, 3> names{"Common", "Rare", "Legendary"};
    constexpr std::array<int, 3> tierSizes{14, 15, 11};
    const std::array<const char*, 13> legacyNames{
        "Precision", "Long Hand", "Second Chance", "Streak", "Royal Purse", "Soft Touch",
        "Narrow Escape", "Red Ribbon", "Black Velvet", "Even Odds", "Court Favor", "Humble Crown", "Blackjack Bonus"
    };
    for (std::size_t tier = 0; tier < rarities.size(); ++tier) {
        check(bj::rarityWeight(rarities[tier]) == weights[tier] && std::string(bj::rarityName(rarities[tier])) == names[tier],
            "rarity metadata exposes three documented names and weights");
    }
    check(bj::rarityWeight(static_cast<bj::Rarity>(99)) == 0
        && std::string(bj::rarityName(static_cast<bj::Rarity>(99))) == "Unknown", "unknown rarity metadata is harmless");
    const auto& catalog = bj::relicCatalog();
    for (std::size_t id = 0; id < legacyNames.size(); ++id) {
        const auto item = std::find_if(catalog.begin(), catalog.end(), [id](const bj::RelicInfo& info) {
            return static_cast<std::size_t>(info.id) == id;
        });
        check(item != catalog.end() && std::string(item->name) == legacyNames[id], "all thirteen existing relic identifiers retain their meaning");
    }

    int shops = 0;
    std::array<int, 3> firstCounts{};
    std::array<int, 40> itemCounts{};
    std::array<std::array<int, 3>, 3> secondCounts{};
    for (std::uint32_t seed = 0; seed < 40000; ++seed) {
        bj::Game game(seed);
        for (int attempt = 0; attempt < 5 && game.state().phase == bj::Phase::Ready; ++attempt) {
            game.deal();
            game = finishBest(game);
            if (game.state().phase == bj::Phase::RunLost) break;
            game.advance();
            if (game.state().phase != bj::Phase::Shop) continue;
            const auto& offers = game.state().offers;
            check(offers.size() == 2 && offers[0] != offers[1], "weighted first shops contain two distinct choices");
            if (offers.size() != 2) break;
            const auto first = static_cast<std::size_t>(rarityOf(offers[0]));
            const auto second = static_cast<std::size_t>(rarityOf(offers[1]));
            ++firstCounts[first];
            ++itemCounts[static_cast<std::size_t>(offers[0])];
            ++secondCounts[first][second];
            ++shops;
        }
    }
    check(shops > 25000, "distribution test reaches enough first shops through legal play");
    for (std::size_t tier = 0; tier < rarities.size(); ++tier) {
        const double probability = static_cast<double>(weights[tier]) / 100.0;
        const double tolerance = 6.0 * std::sqrt(shops * probability * (1.0 - probability)) + 10.0;
        check(std::abs(firstCounts[tier] - shops * probability) < tolerance,
            std::string(names[tier]) + " first-slot frequency follows its tier weight, independent of tier size");
        // Every tier contains at least eleven items. Even eight equipped items
        // and the first offer leave a candidate in every tier for slot two.
        for (std::size_t second = 0; second < rarities.size(); ++second) {
            const double conditional = static_cast<double>(weights[second]) / 100.0;
            const double conditionalTolerance = 6.0 * std::sqrt(firstCounts[tier] * conditional * (1.0 - conditional)) + 10.0;
            check(std::abs(secondCounts[tier][second] - firstCounts[tier] * conditional) < conditionalTolerance,
                "slot two keeps 50/36/14 tier weights while excluding its first selected item");
        }
    }
    for (const auto& item : catalog) {
        const auto tier = static_cast<std::size_t>(item.rarity);
        const double probability = static_cast<double>(weights[tier]) / (100.0 * tierSizes[tier]);
        const double tolerance = 6.0 * std::sqrt(shops * probability * (1.0 - probability)) + 10.0;
        check(std::abs(itemCounts[static_cast<std::size_t>(item.id)] - shops * probability) < tolerance,
            std::string(item.name) + " is uniform among fresh items inside its selected rarity");
    }
    std::cout << "First-shop rarity samples: " << shops;
    for (std::size_t tier = 0; tier < rarities.size(); ++tier) {
        std::cout << "; " << names[tier] << ' ' << firstCounts[tier] << " ("
                  << std::fixed << std::setprecision(3) << 100.0 * firstCounts[tier] / shops << "%)";
    }
    std::cout << '\n';
}
struct Coverage {
    int wins = 0, pushes = 0, losses = 0, soft17 = 0, longRuns = 0;
    int clearedRounds = 0, failedRounds = 0, lastHandClears = 0, earlyClears = 0;
    int lossContinues = 0, pushContinues = 0, winContinues = 0, lastHandWinFails = 0;
    int cappedGoals = 0, shieldResets = 0, maxRound = 1;
    int streakBoosts = 0, streakCaps = 0;
    int naturalPrecisionStacks = 0, naturalStreakStacks = 0;
    int additiveStacks = 0, legendaryWins = 0, ongoingRuns = 0;
    int purchases = 0, sales = 0, reoffers = 0, fullBuilds = 0, cardStreams = 0;
    int shields = 0, spentShields = 0, persistentShields = 0;
    std::array<int, 40> active{};
    std::array<int, 40> inactive{};
};

bool red(const bj::Card& card) {
    return card.suit == bj::Suit::Diamonds || card.suit == bj::Suit::Hearts;
}

void checkRewards(const bj::Snapshot& before, const bj::Snapshot& result, Coverage& coverage) {
    const auto player = bj::evaluateHand(result.player);
    const auto dealer = bj::evaluateHand(result.dealer);
    if (result.outcome != bj::Outcome::Win) {
        check(result.score.base == 0 && result.score.bonus == 0 && result.score.total == 0
            && result.score.coins == 0 && result.score.multiplier == 1.0f && result.score.appliedEffects.empty(),
            "non-wins award no points, income, multiplier or applied winning effects");
        check(result.coins == before.coins && result.totalScore == before.totalScore && result.roundScore == before.roundScore
            && result.wins == before.wins, "non-wins leave accumulated rewards unchanged");
        check(result.streak == (result.outcome == bj::Outcome::Loss ? 0 : before.streak), "loss breaks streak; push preserves it");
        return;
    }
    int expectedCoins = 4;
    int expectedBonus = 0;
    float expectedMultiplier = 1.0f;
    float productMultiplier = 1.0f;
    int multiplierEffects = 0;
    std::vector<bj::AppliedEffect> expectedEffects;
    const auto faces = std::count_if(result.player.begin(), result.player.end(), [](const bj::Card& card) { return card.rank > 10; });
    for (const auto& item : bj::relicCatalog()) {
        const auto relic = item.id;
        if (!contains(before.owned, relic)) continue;
        bool active = false;
        int points = 0;
        int coins = 0;
        float multiplier = 0.0f;
        switch (relic) {
        case bj::Relic::Precision: active = player.total == 21; coins = 1; multiplier = .5f; break;
        case bj::Relic::LongHand: active = result.player.size() >= 4; coins = 1; multiplier = .6f; break;
        case bj::Relic::SecondChance:
            active = !before.usedSecondChance && result.usedSecondChance; multiplier = 1.0f; break;
        case bj::Relic::Streak:
            active = before.streak > 0;
            coins = before.streak % 3 == 2 ? 1 : 0;
            multiplier = .1f * static_cast<float>(std::min(before.streak, 4));
            break;
        case bj::Relic::RoyalPurse: active = player.blackjack; coins = 1; break;
        case bj::Relic::SoftTouch: active = player.soft; coins = 1; break;
        case bj::Relic::NarrowEscape: active = dealer.bust; coins = 1; multiplier = .4f; break;
        case bj::Relic::RedRibbon: active = red(result.player[0]) && red(result.player[1]); coins = 1; break;
        case bj::Relic::BlackVelvet: active = !red(result.player[0]) && !red(result.player[1]); coins = 1; break;
        case bj::Relic::EvenOdds: active = player.total % 2 == 0; coins = 1; break;
        case bj::Relic::CourtFavor: active = faces > 0; points = 3; break;
        case bj::Relic::HumbleCrown: active = player.total <= 18; points = 5; break;
        case bj::Relic::BlackjackBonus: active = player.blackjack; points = 5; break;
        case bj::Relic::CopperBell: active = result.player.size() >= 3; points = 3; break;
        case bj::Relic::SeventeenCharm: active = player.total == 17; coins = 1; break;
        case bj::Relic::ScarletMask:
            active = std::all_of(result.player.begin(), result.player.end(), red); multiplier = .5f; break;
        case bj::Relic::MidnightMask:
            active = std::none_of(result.player.begin(), result.player.end(), red); multiplier = .5f; break;
        case bj::Relic::RoyalCourt: active = faces >= 2; multiplier = .75f; break;
        case bj::Relic::SoftCrown: active = player.soft && player.total >= 19; multiplier = .75f; break;
        case bj::Relic::JesterCrown: active = player.blackjack; multiplier = 1.0f; break;
        case bj::Relic::FiveCardParade: active = result.player.size() >= 5; multiplier = 1.5f; break;
        case bj::Relic::FinalAct: active = result.handsLeft == 0; multiplier = 1.0f; break;
        case bj::Relic::Underdog: active = player.total <= 16 && dealer.bust; multiplier = 1.5f; break;
        case bj::Relic::Encore: active = before.streak >= 3; multiplier = 1.0f; break;
        case bj::Relic::Rewind: case bj::Relic::Redraw: case bj::Relic::ForkedPath: case bj::Relic::Lifeline: break;
        case bj::Relic::VelvetDividend: active = true; coins = 2; break;
        case bj::Relic::LuckySeven:
            active = std::any_of(result.player.begin(), result.player.end(), [](const bj::Card& c) { return c.rank == 7; }); points = 7; break;
        case bj::Relic::TwinSigil: active = result.player[0].rank == result.player[1].rank; points = 10; break;
        case bj::Relic::MixedCompany:
            active = std::any_of(result.player.begin(), result.player.end(), red) && !std::all_of(result.player.begin(), result.player.end(), red); points = 5; break;
        case bj::Relic::Crescendo: active = result.player.size() >= 3; multiplier = .75f; break;
        case bj::Relic::AceHigh:
            active = std::any_of(result.player.begin(), result.player.end(), [](const bj::Card& c) { return c.rank == 1; }); multiplier = .75f; coins = 1; break;
        case bj::Relic::SilverLining: active = true; multiplier = .35f; break;
        case bj::Relic::LoadedDice: active = player.total >= 18; points = 8; break;
        case bj::Relic::Alchemist: active = result.alteredThisHand; multiplier = .8f; coins = 2; break;
        case bj::Relic::Phoenix: active = result.rescuedThisHand; multiplier = 2.0f; coins = 2; break;
        case bj::Relic::RoyalDecree: active = true; multiplier = .8f; break;
        case bj::Relic::GrandFinale: active = result.handsLeft <= 1; multiplier = 1.5f; break;
        }
        const auto index = static_cast<std::size_t>(relic);
        ++(active ? coverage.active[index] : coverage.inactive[index]);
        if (active) {
            expectedCoins += coins;
            expectedBonus += points;
            expectedMultiplier += multiplier;
            expectedEffects.push_back({relic, points, coins, multiplier});
            if (multiplier > 0) {
                ++multiplierEffects;
                productMultiplier *= 1.0f + multiplier;
            }
            coverage.legendaryWins += item.rarity == bj::Rarity::Legendary ? 1 : 0;
        }
    }
    coverage.streakBoosts += contains(before.owned, bj::Relic::Streak) && before.streak > 0 ? 1 : 0;
    coverage.streakCaps += contains(before.owned, bj::Relic::Streak) && before.streak > 4 ? 1 : 0;
    const bool naturalBonus = player.blackjack && contains(before.owned, bj::Relic::BlackjackBonus);
    coverage.naturalPrecisionStacks += naturalBonus && contains(before.owned, bj::Relic::Precision) ? 1 : 0;
    coverage.naturalStreakStacks += naturalBonus && contains(before.owned, bj::Relic::Streak) && before.streak > 0 ? 1 : 0;
    const int expectedTotal = static_cast<int>(std::lround(static_cast<float>(player.total + expectedBonus) * expectedMultiplier));
    check(result.score.base == player.total && result.score.bonus == expectedBonus
        && result.score.total == expectedTotal && std::abs(result.score.multiplier - expectedMultiplier) < .00001f,
        "winning points add flat relic bonuses before applying one plus the sum of multiplier bonuses");
    check(sameEffects(result.score.appliedEffects, expectedEffects), "applied effects list exactly the owned relics whose conditions actually triggered");
    check(result.score.coins == expectedCoins && result.coins == before.coins + expectedCoins,
        "win income is four base coins plus the actual relic income");
    check(result.totalScore == before.totalScore + result.score.total && result.roundScore == before.roundScore + result.score.total
        && result.wins == before.wins + 1, "winning points accumulate once in the round and complete run");
    check(result.streak == before.streak + 1, "win increments streak exactly once");
    if (multiplierEffects >= 2) {
        ++coverage.additiveStacks;
        check(std::abs(result.score.multiplier - productMultiplier) > .00001f,
            "multiple relic multipliers add their extra factors instead of multiplying factors together");
    }
}
void probeShield(bj::Game game, Coverage& coverage) {
    if (!game.hasRelic(bj::Relic::SecondChance) || game.state().phase != bj::Phase::PlayerTurn) return;
    for (int action = 0; game.state().phase == bj::Phase::PlayerTurn && action < 52; ++action) {
        const auto before = game.state();
        game.hit();
        if (!before.usedSecondChance && game.state().usedSecondChance) {
            ++coverage.shields;
            check(sameCards(before.player, game.state().player), "shield discards exactly the new bust card");
            check(game.state().phase == bj::Phase::PlayerTurn && game.state().coins == before.coins
                && game.state().totalScore == before.totalScore, "shield rescue does not award a win");
        }
        if (before.usedSecondChance && (game.state().phase == bj::Phase::RoundOver || game.state().phase == bj::Phase::RunLost)
            && bj::evaluateHand(game.state().player).bust) {
            ++coverage.spentShields;
            check(game.state().outcome == bj::Outcome::Loss, "spent shield cannot rescue another bust");
        }
    }
}

void testSeededRunsAndShop() {
    Coverage coverage;
    const auto& catalog = bj::relicCatalog();
    check(catalog.size() == 40, "catalogue has forty choices");
    for (const auto& item : catalog) {
        check((item.cost == 3 || item.cost == 6 || item.cost == 10)
            && item.name[0] != '\0' && item.description[0] != '\0', "tier prices and descriptive text");
        check(std::count_if(catalog.begin(), catalog.end(), [&](const bj::RelicInfo& other) { return item.id == other.id; }) == 1,
            "unique catalogue identifiers");
    }
    for (std::uint32_t seed = 0; seed < 3000; ++seed) {
        bj::Game game(seed);
        check(game.state().phase == bj::Phase::Ready && game.state().round == 1
            && game.state().handsLeft == 5 && game.state().roundScore == 0 && game.state().goal == 30
            && game.state().coins == 0 && game.state().wins == 0 && game.state().totalScore == 0
            && game.state().dealerStand == 17 && game.state().owned.empty() && game.state().offers.empty(), "new run defaults");
        std::vector<bj::Relic> previousOffers;
        std::vector<bj::Relic> sold;
        bool ended = false;
        for (int dealtHand = 0; dealtHand < 300; ++dealtHand) {
            checkBlockedActions(game);
            const auto beforeRound = game.state();
            check(game.deal(), "deal succeeds in Ready");
            check(game.state().handsLeft == beforeRound.handsLeft - 1, "every dealt hand spends one of the five attempts");
            check(game.state().player.size() == 2 && game.state().dealer.size() == 2, "opening deal gives two cards to each side");
            check(game.state().usedSecondChance == beforeRound.usedSecondChance, "deal does not recharge shield");
            probeShield(game, coverage);
            game = finishBest(game);
            const auto result = game.state();
            check((result.phase == bj::Phase::RoundOver || result.phase == bj::Phase::RunLost)
                && result.outcome == expectedOutcome(result), "hand resolves by blackjack comparison");
            const auto player = bj::evaluateHand(result.player);
            const auto dealer = bj::evaluateHand(result.dealer);
            bool seen[52] = {};
            for (const auto* cards : {&result.player, &result.dealer}) {
                for (const auto& card : *cards) {
                    const int id = static_cast<int>(card.suit) * 13 + card.rank - 1;
                    check(id >= 0 && id < 52 && !seen[id], "complete round uses unique standard-deck cards");
                    if (id >= 0 && id < 52) seen[id] = true;
                }
            }
            if (!player.bust && !player.blackjack && !dealer.blackjack) {
                check(dealer.total >= 17, "dealer draws to hard or soft 17");
                if (result.dealer.size() > 2) {
                    auto previous = result.dealer;
                    previous.pop_back();
                    check(bj::evaluateHand(previous).total < 17, "dealer stops as soon as threshold is reached");
                }
                coverage.soft17 += dealer.total == 17 && dealer.soft ? 1 : 0;
            }
            checkRewards(beforeRound, result, coverage);
            coverage.wins += result.outcome == bj::Outcome::Win ? 1 : 0;
            coverage.pushes += result.outcome == bj::Outcome::Push ? 1 : 0;
            coverage.losses += result.outcome == bj::Outcome::Loss ? 1 : 0;
            coverage.longRuns += result.round > 5 ? 1 : 0;
            coverage.maxRound = std::max(coverage.maxRound, result.round);
            coverage.cappedGoals += result.round >= 8 && result.goal == 86 ? 1 : 0;
            checkBlockedActions(game);
            if (result.roundScore < result.goal && result.handsLeft == 0) {
                ++coverage.failedRounds;
                coverage.lastHandWinFails += result.outcome == bj::Outcome::Win ? 1 : 0;
                check(game.state().phase == bj::Phase::RunLost, "unmet goal after all five hands ends the run immediately");
                checkBlockedActions(game);
                game.restart(seed);
                const bj::Game fresh(seed);
                check(sameState(fresh.state(), game.state()), "restart after defeat clears full run state");
                ended = true;
                break;
            }
            check(result.phase == bj::Phase::RoundOver, "a surviving hand or final-hand goal clear keeps its result screen");
            check(game.advance(), "advance succeeds after a surviving hand");
            check(game.state().coins == result.coins && game.state().totalScore == result.totalScore
                && game.state().roundScore == result.roundScore && game.state().handsLeft == result.handsLeft,
                "advance never repeats rewards or spends another hand");
            if (result.roundScore < result.goal) {
                coverage.lossContinues += result.outcome == bj::Outcome::Loss ? 1 : 0;
                coverage.pushContinues += result.outcome == bj::Outcome::Push ? 1 : 0;
                coverage.winContinues += result.outcome == bj::Outcome::Win ? 1 : 0;
                coverage.persistentShields += result.usedSecondChance ? 1 : 0;
                check(game.state().phase == bj::Phase::Ready && game.state().round == result.round
                    && game.state().offers.empty() && game.state().usedSecondChance == result.usedSecondChance,
                    "a hand below the target continues the same round without opening a shop or recharging a shield");
                continue;
            }
            ++coverage.clearedRounds;
            coverage.lastHandClears += result.handsLeft == 0 ? 1 : 0;
            coverage.earlyClears += result.handsLeft > 0 ? 1 : 0;
            check(game.state().phase == bj::Phase::Shop, "only a cleared round opens the shop, including a last-hand clear");
            const auto offers = game.state().offers;
            check(offers.size() == 2 && offers[0] != offers[1], "shop offers exactly two distinct upgrades");
            std::vector<bj::Relic> earlierSlots;
            for (auto relic : offers) {
                check(!game.hasRelic(relic), "shops do not offer an active upgrade");
                if (contains(previousOffers, relic)) {
                    check(std::none_of(catalog.begin(), catalog.end(), [&](const bj::RelicInfo& item) {
                        return item.rarity == rarityOf(relic) && !game.hasRelic(item.id)
                            && !contains(previousOffers, item.id) && !contains(earlierSlots, item.id);
                    }), "an earlier offer repeats only when its selected rarity has no fresh eligible item");
                }
                earlierSlots.push_back(relic);
                coverage.reoffers += contains(sold, relic) ? 1 : 0;
            }
            previousOffers = offers;
            checkBlockedActions(game);
            auto noShopping = game;
            for (const auto& item : catalog) {
                const auto before = game.state();
                const bool expected = contains(offers, item.id) && !game.hasRelic(item.id)
                    && before.owned.size() < bj::maxEquipmentSlots && before.coins >= item.cost;
                check(game.buy(item.id) == expected, "buy enforces offers, inventory, funds and ownership");
                if (expected) {
                    ++coverage.purchases;
                    check(game.boughtThisShop(item.id), "shop purchase query records a successful buy");
                    check(game.state().coins == before.coins - item.cost
                        && game.state().owned.size() == before.owned.size() + 1 && game.hasRelic(item.id), "purchase pays listed price for one upgrade");
                    const auto bought = game.state();
                    check(!game.buy(item.id) && sameState(bought, game.state()), "duplicate purchase is harmless");
                } else {
                    check(sameState(before, game.state()), "blocked purchase preserves complete state");
                }
                check(game.state().offers == offers, "transactions do not change current shop offers");
            }
            if (game.state().owned.size() == bj::maxEquipmentSlots) {
                ++coverage.fullBuilds;
                const auto full = game.state();
                for (auto relic : offers) check(!game.buy(relic) && sameState(full, game.state()), "eight active upgrades is a hard limit");
            }
            if (seed % 5 == 0 && !game.state().owned.empty()) {
                const auto relic = game.state().owned.front();
                const auto& item = *std::find_if(catalog.begin(), catalog.end(), [relic](const bj::RelicInfo& info) { return info.id == relic; });
                const auto beforeSale = game.state();
                check(game.sell(relic), "owned upgrade sells in shop");
                ++coverage.sales;
                sold.push_back(relic);
                check(!game.hasRelic(relic) && game.state().coins == beforeSale.coins + item.cost / 2
                    && game.state().owned.size() + 1 == beforeSale.owned.size(), "sale refunds exactly half purchase price");
                const auto afterSale = game.state();
                check(!game.sell(relic) && sameState(afterSale, game.state()), "cannot sell one upgrade twice");
                check(!game.buy(relic) && sameState(afterSale, game.state()), "cannot rebuy a sold upgrade in same shop");
                check(game.boughtThisShop(relic) == contains(offers, relic), "selling retains this visit's purchase marker");
                check(game.state().offers == offers && game.state().usedSecondChance == beforeSale.usedSecondChance,
                    "selling never rerolls offers or recharges shield");
            }
            auto shopping = game;
            shopping.leaveShop();
            noShopping.leaveShop();
            shopping.deal();
            noShopping.deal();
            for (int action = 0; action < 3; ++action) {
                check(sameCards(shopping.state().player, noShopping.state().player)
                    && sameCards(shopping.state().dealer, noShopping.state().dealer), "shop choices do not change next deal or hit cards");
                ++coverage.cardStreams;
                if (shopping.state().phase != bj::Phase::PlayerTurn || noShopping.state().phase != bj::Phase::PlayerTurn) break;
                shopping.hit();
                noShopping.hit();
                if (shopping.state().usedSecondChance != noShopping.state().usedSecondChance) break;
            }
            const auto beforeLeave = game.state();
            check(game.leaveShop(), "shop exit starts next unlimited round");
            const auto afterLeave = game.state();
            for (const auto& item : catalog) check(!game.boughtThisShop(item.id), "shop exit clears purchase markers");
            check(afterLeave.phase == bj::Phase::Ready && afterLeave.round == result.round + 1
                && afterLeave.handsLeft == 5 && afterLeave.roundScore == 0
                && afterLeave.dealerStand == 17 && afterLeave.player.empty() && afterLeave.dealer.empty()
                && afterLeave.outcome == bj::Outcome::None && afterLeave.score.total == 0
                && afterLeave.score.coins == 0 && afterLeave.offers.empty(), "next round resets all five hands, progress and display");
            const std::array<int, 8> goals{30, 38, 46, 54, 62, 70, 78, 86};
            check(afterLeave.goal == goals[static_cast<std::size_t>(std::min(afterLeave.round, 8) - 1)],
                "round goals rise by eight and remain capped at eighty-six");
            check(afterLeave.coins == beforeLeave.coins && afterLeave.totalScore == beforeLeave.totalScore
                && afterLeave.wins == beforeLeave.wins && afterLeave.owned == beforeLeave.owned
                && afterLeave.streak == beforeLeave.streak && !afterLeave.usedSecondChance,
                "next round keeps run rewards, upgrades and streak but recharges the once-per-round shield");
            coverage.shieldResets += beforeLeave.usedSecondChance ? 1 : 0;
        }
        coverage.ongoingRuns += ended ? 0 : 1;
    }
    check(coverage.wins > 0 && coverage.pushes > 0 && coverage.losses > 0 && coverage.failedRounds > 0
        && coverage.failedRounds + coverage.ongoingRuns == 3000,
        "seeded simulation covers all outcomes and records failures or surviving endless runs at its bounded horizon");
    check(coverage.lossContinues > 0 && coverage.pushContinues > 0 && coverage.winContinues > 0,
        "wins below target, pushes and individual losses all continue the same round");
    check(coverage.lastHandClears > 0 && coverage.earlyClears > 0 && coverage.lastHandWinFails > 0,
        "simulation clears goals early and on the final hand, and fails an insufficient final-hand win");
    check(coverage.longRuns > 0, "runs continue beyond five rounds and old three-stage structure");
    check(coverage.soft17 > 0, "soft-17 dealer stands exercised");
    check(coverage.cappedGoals > 0, "longer runs exercise the eighty-six-point target cap");
    check(coverage.streakBoosts > 0 && coverage.streakCaps > 0, "Rare Streak adds its capped multiplier after earlier wins");
    check(coverage.additiveStacks > 0 && coverage.legendaryWins > 0, "simulation exercises additive multiplier stacks and triggered Legendary effects");
    check(coverage.naturalPrecisionStacks > 0 && coverage.naturalStreakStacks > 0,
        "Blackjack Bonus stacks with Precision and is included before a Streak multiplier");
    check(coverage.purchases > 0 && coverage.sales > 0 && coverage.reoffers > 0 && coverage.fullBuilds > 0,
        "simulation buys, sells, reoffers sold upgrades and reaches eight active upgrades");
    check(coverage.cardStreams > 0, "transactions compared against no-shopping runs");
    check(coverage.shields > 0 && coverage.spentShields > 0 && coverage.persistentShields > 0 && coverage.shieldResets > 0,
        "shield rescues one bust, stays spent within a round and recharges at the next round");
    for (const auto& item : catalog) {
        const auto index = static_cast<std::size_t>(item.id);
        if (bj::isActiveRelic(item.id) || item.id == bj::Relic::Alchemist) continue; // Exercised by the active-command tests.
        const bool unconditional = item.id == bj::Relic::VelvetDividend || item.id == bj::Relic::SilverLining || item.id == bj::Relic::RoyalDecree;
        check(coverage.active[index] > 0 && (unconditional || coverage.inactive[index] > 0), std::string(item.name) + " reward condition covered");
    }
    std::cout << "Seeded runs: 3000; wins " << coverage.wins << ", pushes " << coverage.pushes
              << ", lost hands " << coverage.losses << "; cleared rounds " << coverage.clearedRounds
              << ", final-hand clears " << coverage.lastHandClears << ", maximum round " << coverage.maxRound
              << "; purchases " << coverage.purchases << ", sales " << coverage.sales
              << ", sold reoffers " << coverage.reoffers << ", full builds " << coverage.fullBuilds
              << "; shield saves " << coverage.shields << ", recharges " << coverage.shieldResets
              << "; additive stacks " << coverage.additiveStacks << ", horizon survivors " << coverage.ongoingRuns << '\n';
}

// Acquire through real seeded shops; never mutate Game's const snapshot to arrange tests.
bj::Game findAbilityHand(bj::Relic relic, const std::function<bool(const bj::Snapshot&)>& matches) {
    for (std::uint32_t seed = 0; seed < 2000; ++seed) {
        bj::Game game(seed, bj::StartDeck::Rich);
        for (int step = 0; step < 160; ++step) {
            if (game.state().phase == bj::Phase::Ready) game.deal();
            else if (game.state().phase == bj::Phase::PlayerTurn) {
                if (game.hasRelic(relic)) {
                    auto candidate = game;
                    for (int hit = 0; hit < 20 && candidate.state().phase == bj::Phase::PlayerTurn; ++hit) {
                        if (matches(candidate.state())) return candidate;
                        if (!candidate.hit()) break;
                    }
                }
                game = finishBest(game);
            } else if (game.state().phase == bj::Phase::RoundOver) game.advance();
            else if (game.state().phase == bj::Phase::Shop) {
                game.buy(relic);
                game.leaveShop();
            } else break;
        }
    }
    throw std::runtime_error("Ability test could not acquire a matching legal hand");
}

void testBoundedActiveAbilities() {
    const auto pending = [](const bj::Snapshot& state) { return state.pendingRescue && state.handsLeft > 0; };
    for (auto relic : {bj::Relic::Rewind, bj::Relic::Redraw, bj::Relic::Lifeline}) {
        auto game = findAbilityHand(relic, pending);
        const auto before = game.state();
        check(before.outcome == bj::Outcome::None && before.score.total == 0 && before.dealer.size() == 2,
            "rescue window precedes settlement and does not draw/reveal dealer cards");
        check(bj::abilityStatus(before, relic).usable && bj::abilityStatus(before, relic).remaining == 1,
            "an owned usable rescue exposes exactly one charge");
        check(!game.hit() && !game.chooseCard(0) && sameState(before, game.state()),
            "a pending bust blocks extra hits and nonexistent choices without mutation");
        auto decline = game;
        check(decline.stand() && decline.state().outcome == bj::Outcome::Loss && !decline.state().pendingRescue,
            "Stand explicitly accepts a pending bust loss");
        const auto settled = decline.state();
        check(!decline.activate(relic) && !decline.stand() && sameState(settled, decline.state()),
            "settled loss cannot be reopened or paid twice");
        auto deterministic = game;
        check(game.activate(relic) && deterministic.activate(relic) && sameState(game.state(), deterministic.state()),
            "active rescue is deterministic on identical seeded states");
        const auto after = game.state();
        check(bj::abilityStatus(after, relic).remaining == 0 && !game.activate(relic) && sameState(after, game.state()),
            "activation consumes its charge once and repeated clicks are harmless");
        check(sameCards(before.dealer, after.dealer) || after.outcome != bj::Outcome::None,
            "rescue keeps dealer cards hidden until a real settlement");
        if (relic != bj::Relic::Redraw) {
            auto expected = before.player;
            expected.pop_back();
            check(sameCards(after.player, expected) && after.phase == bj::Phase::PlayerTurn
                && !after.pendingRescue && after.rescuedThisHand && after.alteredThisHand,
                "discard rescue removes exactly the last hit and resumes the same hand");
            check(after.coins == before.coins && after.totalScore == before.totalScore
                && after.roundScore == before.roundScore && after.handsLeft == before.handsLeft,
                "rescue neither awards rewards nor refunds a hand");
        } else {
            check(after.player.size() == before.player.size()
                && !sameCards({after.player.back()}, {before.player.back()}),
                "Redraw replaces exactly one card with a fresh deck card");
        }
        if (relic == bj::Relic::Lifeline) {
            check(after.usedHandAbilities.empty() && contains(after.usedRoundAbilities, relic),
                "Lifeline uses its independent once-per-round budget");
            auto continuation = finishBest(game);
            if (continuation.state().phase == bj::Phase::RoundOver) {
                continuation.advance();
                if (continuation.state().phase == bj::Phase::Ready) {
                    continuation.deal();
                    check(bj::abilityStatus(continuation.state(), relic).remaining == 0,
                        "dealing again in the same round does not recharge Lifeline");
                } else if (continuation.state().phase == bj::Phase::Shop) {
                    continuation.leaveShop();
                    check(bj::abilityStatus(continuation.state(), relic).remaining == 1,
                        "entering the next round recharges Lifeline");
                }
            }
        }
        game.restart(before.seed);
        check(game.state().owned.empty() && game.state().usedHandAbilities.empty()
            && game.state().usedRoundAbilities.empty() && game.state().cardChoices.empty()
            && !game.state().pendingRescue && !game.state().alteredThisHand && !game.state().rescuedThisHand,
            "restart clears equipment and all ability, choice and rescue state");
    }
    for (auto relic : {bj::Relic::Rewind, bj::Relic::Redraw, bj::Relic::ForkedPath}) {
        auto game = findAbilityHand(relic, [=](const bj::Snapshot& state) {
            return !state.pendingRescue && state.handsLeft > 0
                && (relic == bj::Relic::ForkedPath || state.player.size() > 2);
        });
        check(game.activate(relic), "per-hand charge is usable before recharge test");
        if (!game.state().cardChoices.empty()) game.chooseCard(0);
        game = finishBest(game);
        check(game.advance(), "ability hand can advance without replaying activation");
        if (game.state().phase == bj::Phase::Shop) game.leaveShop();
        check(game.deal() && bj::abilityStatus(game.state(), relic).remaining == 1
            && game.state().usedHandAbilities.empty() && !game.state().alteredThisHand,
            "dealing a new hand clears per-hand usage and synergy flags exactly once");
    }
    auto rewind = findAbilityHand(bj::Relic::Rewind, [](const bj::Snapshot& state) {
        return state.player.size() == 2 && !state.pendingRescue;
    });
    const auto opening = rewind.state();
    check(!rewind.activate(bj::Relic::Rewind) && sameState(opening, rewind.state()),
        "Rewind cannot remove an opening card");
    auto redraw = findAbilityHand(bj::Relic::Redraw, [](const bj::Snapshot& state) {
        return state.player.size() == 2 && !state.pendingRescue;
    });
    check(!redraw.activate(bj::Relic::Redraw), "Redraw cannot manufacture a natural from an opening pair");

    auto fork = findAbilityHand(bj::Relic::ForkedPath, [](const bj::Snapshot& state) { return !state.pendingRescue; });
    const auto beforeFork = fork.state();
    check(fork.activate(bj::Relic::ForkedPath), "Forked Path activates on a live nonbusting hand");
    const auto choosing = fork.state();
    check(choosing.cardChoices.size() == 2 && sameCards(choosing.player, beforeFork.player)
        && sameCards(choosing.dealer, beforeFork.dealer) && bj::abilityStatus(choosing, bj::Relic::ForkedPath).remaining == 0,
        "choice reveals exactly two fresh cards and consumes its charge before selection");
    check(!fork.hit() && !fork.stand() && !fork.activate(bj::Relic::ForkedPath)
        && !fork.chooseCard(2) && sameState(choosing, fork.state()),
        "a pending choice blocks all competing moves and rejects out-of-range selection atomically");
    for (std::size_t index = 0; index < 2; ++index) {
        auto choice = fork;
        check(choice.chooseCard(index), "either offered choice is selectable");
        const auto& selected = choice.state();
        check(selected.cardChoices.empty() && selected.player.size() == beforeFork.player.size() + 1
            && sameCards({selected.player.back()}, {choosing.cardChoices[index]}),
            "selection adds exactly the chosen card and clears both options");
        const auto settledChoice = selected;
        check(!choice.chooseCard(index) && !choice.activate(bj::Relic::ForkedPath) && sameState(settledChoice, choice.state()),
            "a selected choice cannot be replayed or replenished in the same hand");
    }
    fork.restart(beforeFork.seed);
    check(fork.state().cardChoices.empty(), "restart during a choice discards the pending UI decision");
    bj::Game bare(1);
    for (auto relic : {bj::Relic::Rewind, bj::Relic::Redraw, bj::Relic::ForkedPath, bj::Relic::Lifeline}) {
        check(!bj::abilityStatus(bare.state(), relic).usable && bj::abilityStatus(bare.state(), relic).remaining == 0
            && !bare.activate(relic), "unowned abilities cannot be used");
    }
}

void testActiveSynergySettlement() {
    bool covered = false;
    for (std::uint32_t seed = 0; seed < 1500 && !covered; ++seed) {
        bj::Game game(seed, bj::StartDeck::Rich);
        for (int step = 0; step < 220 && !covered; ++step) {
            if (game.state().phase == bj::Phase::Ready) game.deal();
            else if (game.state().phase == bj::Phase::PlayerTurn) {
                if (game.hasRelic(bj::Relic::Rewind) && game.hasRelic(bj::Relic::Alchemist)) {
                    auto attempt = game;
                    for (int hit = 0; hit < 20 && attempt.state().phase == bj::Phase::PlayerTurn; ++hit) {
                        if (bj::abilityStatus(attempt.state(), bj::Relic::Rewind).usable) {
                            auto activated = attempt;
                            activated.activate(bj::Relic::Rewind);
                            const auto beforeSettlement = activated.state();
                            activated = finishBest(activated);
                            if (activated.state().outcome == bj::Outcome::Win) {
                                const auto& effects = activated.state().score.appliedEffects;
                                auto effect = std::find_if(effects.begin(), effects.end(), [](const bj::AppliedEffect& e) {
                                    return e.relic == bj::Relic::Alchemist;
                                });
                                check(effect != effects.end() && effect->coins == 2 && std::abs(effect->multiplierBonus - .8f) < .00001f,
                                    "Alchemist grants its actual active-use synergy on a real winning hand");
                                check(activated.state().coins == beforeSettlement.coins + activated.state().score.coins
                                    && activated.state().totalScore == beforeSettlement.totalScore + activated.state().score.total,
                                    "active synergy rewards settle exactly once");
                                const auto paid = activated.state();
                                check(!activated.activate(bj::Relic::Rewind) && !activated.chooseCard(0) && sameState(paid, activated.state()),
                                    "post-win ability clicks cannot duplicate synergy payouts");
                                covered = true;
                                break;
                            }
                        }
                        if (!attempt.hit()) break;
                    }
                }
                game = finishBest(game);
            } else if (game.state().phase == bj::Phase::RoundOver) game.advance();
            else if (game.state().phase == bj::Phase::Shop) {
                game.buy(bj::Relic::Rewind);
                game.buy(bj::Relic::Alchemist);
                game.leaveShop();
            } else break;
        }
    }
    check(covered, "bounded legal-shop search reaches and verifies active synergy payout");
}

void testEquipmentExchange() {
    std::array<bool, 9> seen{};
    bool exchanged = false;
    bool rejectedInsufficient = false;
    for (std::uint32_t seed = 0; seed < 300 && (!exchanged || !rejectedInsufficient); ++seed) {
        bj::Game game(seed, bj::StartDeck::Rich);
        for (int step = 0; step < 800; ++step) {
            seen[game.state().owned.size()] = true;
            if (game.state().phase == bj::Phase::Ready) game.deal();
            else if (game.state().phase == bj::Phase::PlayerTurn) game = finishBest(game);
            else if (game.state().phase == bj::Phase::RoundOver) game.advance();
            else if (game.state().phase == bj::Phase::RunLost) break;
            else {
                const auto offers = game.state().offers;
                if (!game.state().owned.empty()) {
                    for (auto offered : offers) {
                        if (game.hasRelic(offered)) continue;
                        const auto equipped = game.state().owned.front();
                        const auto& catalog = bj::relicCatalog();
                        const int oldCost = std::find_if(catalog.begin(), catalog.end(), [&](const bj::RelicInfo& i) { return i.id == equipped; })->cost;
                        const int newCost = std::find_if(catalog.begin(), catalog.end(), [&](const bj::RelicInfo& i) { return i.id == offered; })->cost;
                        const auto before = game.state();
                        if (before.coins + oldCost / 2 < newCost) {
                            check(!game.replace(equipped, offered) && sameState(before, game.state()),
                                "insufficient-funds replacement leaves ownership, coins and charges unchanged");
                            rejectedInsufficient = true;
                        } else if (before.owned.size() == bj::maxEquipmentSlots) {
                            check(!game.buy(offered) && sameState(before, game.state()), "a ninth purchase cannot create a ninth equipped chip");
                            check(game.replace(equipped, offered), "full equipment can atomically trade a slot for an offered upgrade");
                            check(game.state().owned.size() == 8 && game.state().owned.front() == offered
                                && !game.hasRelic(equipped) && game.state().coins == before.coins + oldCost / 2 - newCost,
                                "replacement keeps slot order and counts with exact sale credit and purchase debit");
                            const auto after = game.state();
                            check(!game.replace(offered, offered) && sameState(after, game.state()), "duplicate replacement cannot pay a second refund");
                            check(game.sell(offered) && game.state().owned.size() == 7, "sale removes exactly one equipped slot");
                            check(!game.buy(offered), "selling a newly exchanged relic does not allow same-shop rebuy");
                            game.restart(seed);
                            check(game.state().owned.empty(), "new run has exactly zero equipped chips");
                            exchanged = true;
                            break;
                        }
                    }
                }
                if (game.state().phase != bj::Phase::Shop) break;
                for (auto offered : offers) {
                    game.buy(offered);
                    seen[game.state().owned.size()] = true;
                }
                game.leaveShop();
            }
        }
    }
    check(exchanged && rejectedInsufficient, "legal play covers full-capacity and rejected exchanges");
    for (std::size_t count = 0; count < seen.size(); ++count)
        check(seen[count], "legal acquisition visits each equipment count from zero through eight");
}

} // namespace

int main() {
    try {
        testHands();
        testExpandedCatalogueContract();
        testDeterminismAndBlockedRandomness();
        testBoundedActiveAbilities();
        testEquipmentExchange();
        testActiveSynergySettlement();
        testNaturalBaseAndActualBust();
        testBlackjackBonusCounterfactual();
        testStartingDecks();
        testSecondLifePreservesBuild();
        testUnbiasedOpeningDistribution();
        testRarityDistribution();
        testSeededRunsAndShop();
    } catch (const std::exception& error) {
        std::cerr << "Unexpected exception: " << error.what() << '\n';
        return 1;
    }
    std::cout << checks << " checks, " << failures << " failures\n";
    return failures == 0 ? 0 : 1;
}
