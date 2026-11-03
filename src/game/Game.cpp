#include "Game.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace bj {
namespace {

constexpr int kWinCoins = 4;
constexpr int kHandsPerRound = 5;

int goalForRound(int round) {
    // Evaluate the multiplication only while the result remains below the cap.
    return round >= 8 ? 86 : 30 + 8 * (round - 1);
}

// Rejection sampling keeps seeded shuffles identical across standard libraries.
std::uint32_t boundedRandom(std::mt19937& random, std::uint32_t bound) {
    const auto threshold = (std::uint32_t{0} - bound) % bound;
    std::uint32_t value;
    do {
        value = static_cast<std::uint32_t>(random());
    } while (value < threshold);
    return value % bound;
}

bool isRed(const Card& card) {
    return card.suit == Suit::Diamonds || card.suit == Suit::Hearts;
}

const RelicInfo* relicInfo(Relic relic) {
    const auto& catalog = relicCatalog();
    const auto item = std::find_if(catalog.begin(), catalog.end(), [relic](const RelicInfo& info) {
        return info.id == relic;
    });
    return item == catalog.end() ? nullptr : &*item;
}

} // namespace

HandValue evaluateHand(const std::vector<Card>& hand) {
    HandValue value;
    int aces = 0;
    for (const auto& card : hand) {
        if (card.rank < 1 || card.rank > 13) {
            throw std::invalid_argument("Card rank must be between 1 and 13");
        }
        value.total += std::min(card.rank, 10);
        if (card.rank == 1) {
            ++aces;
        }
    }
    if (aces > 0 && value.total + 10 <= 21) {
        value.total += 10;
        value.soft = true;
    }
    value.blackjack = hand.size() == 2 && value.total == 21;
    value.bust = value.total > 21;
    return value;
}

const char* rarityName(Rarity rarity) {
    switch (rarity) {
    case Rarity::Common: return "Common";
    case Rarity::Rare: return "Rare";
    case Rarity::Legendary: return "Legendary";
    }
    return "Unknown";
}

int rarityWeight(Rarity rarity) {
    switch (rarity) {
    case Rarity::Common: return 50;
    case Rarity::Rare: return 36;
    case Rarity::Legendary: return 14;
    }
    return 0;
}

const std::vector<RelicInfo>& relicCatalog() {
    static const std::vector<RelicInfo> catalog = {
        {Relic::Precision, "Precision", "Win with 21: +0.5 multiplier and +1 coin.", 6, Rarity::Rare},
        {Relic::LongHand, "Long Hand", "Win with 4+ cards: +0.6 multiplier and +1 coin.", 6, Rarity::Rare},
        {Relic::SecondChance, "Second Chance", "Rescue one bust per round. Winning after that rescue adds +1.0 multiplier.", 10, Rarity::Legendary},
        {Relic::Streak, "Streak", "+0.1 multiplier per earlier consecutive win (max +0.4). Every third win earns +1 coin.", 6, Rarity::Rare},
        {Relic::RoyalPurse, "Royal Purse", "Win with a natural blackjack for +1 coin.", 3, Rarity::Common},
        {Relic::SoftTouch, "Soft Touch", "Win with a soft hand for +1 coin.", 3, Rarity::Common},
        {Relic::NarrowEscape, "Narrow Escape", "Win when the dealer busts: +0.4 multiplier and +1 coin.", 6, Rarity::Rare},
        {Relic::RedRibbon, "Red Ribbon", "Win with two red opening cards for +1 coin.", 3, Rarity::Common},
        {Relic::BlackVelvet, "Black Velvet", "Win with two black opening cards for +1 coin.", 3, Rarity::Common},
        {Relic::EvenOdds, "Even Odds", "Win with an even hand total for +1 coin.", 3, Rarity::Common},
        {Relic::CourtFavor, "Court Favor", "Win with at least one J, Q or K for +3 points.", 3, Rarity::Common},
        {Relic::HumbleCrown, "Humble Crown", "Win with 18 or less for +5 points.", 3, Rarity::Common},
        {Relic::BlackjackBonus, "Blackjack Bonus", "Win with a natural blackjack for +5 points.", 3, Rarity::Common},
        {Relic::CopperBell, "Copper Bell", "Win with 3+ cards for +3 points.", 3, Rarity::Common},
        {Relic::SeventeenCharm, "Seventeen Charm", "Win with exactly 17 for +1 coin.", 3, Rarity::Common},
        {Relic::ScarletMask, "Scarlet Mask", "Win with an entirely red hand: +0.5 multiplier.", 6, Rarity::Rare},
        {Relic::MidnightMask, "Midnight Mask", "Win with an entirely black hand: +0.5 multiplier.", 6, Rarity::Rare},
        {Relic::RoyalCourt, "Royal Court", "Win with 2+ face cards: +0.75 multiplier.", 6, Rarity::Rare},
        {Relic::SoftCrown, "Soft Crown", "Win with a soft 19, 20 or 21: +0.75 multiplier.", 6, Rarity::Rare},
        {Relic::JesterCrown, "Jester Crown", "Win with a natural blackjack: +1.0 multiplier.", 10, Rarity::Legendary},
        {Relic::FiveCardParade, "Five Card Parade", "Win with 5+ cards: +1.5 multiplier.", 10, Rarity::Legendary},
        {Relic::FinalAct, "Final Act", "Win on the round's last available hand: +1.0 multiplier.", 10, Rarity::Legendary},
        {Relic::Underdog, "Underdog", "Win with 16 or less when the dealer busts: +1.5 multiplier.", 10, Rarity::Legendary},
        {Relic::Encore, "Encore", "Win after 3+ earlier consecutive wins: +1.0 multiplier.", 10, Rarity::Legendary},
        {Relic::Rewind, "Rewind", "ACTIVE: discard your last hit, including a bust card. Once per hand; opening cards cannot be removed.", 6, Rarity::Rare},
        {Relic::Redraw, "Redraw", "ACTIVE: replace your last hit with the next card. Once per hand; the replacement can still bust.", 6, Rarity::Rare},
        {Relic::ForkedPath, "Forked Path", "ACTIVE: draw two cards and choose one as your next hit. The other is discarded. Once per hand.", 10, Rarity::Legendary},
        {Relic::Lifeline, "Lifeline", "ACTIVE: discard a bust card before the hand settles. Once per round; recharges next round.", 10, Rarity::Legendary},
        {Relic::VelvetDividend, "Velvet Dividend", "Every win earns +2 coins.", 3, Rarity::Common},
        {Relic::LuckySeven, "Lucky Seven", "Win with at least one seven: +7 points.", 3, Rarity::Common},
        {Relic::TwinSigil, "Twin Sigil", "Win with a matching opening pair: +10 points.", 3, Rarity::Common},
        {Relic::MixedCompany, "Mixed Company", "Win with both red and black cards: +5 points.", 3, Rarity::Common},
        {Relic::Crescendo, "Crescendo", "Win with 3+ cards: +0.75 multiplier.", 6, Rarity::Rare},
        {Relic::AceHigh, "Ace High", "Win with any ace: +0.75 multiplier and +1 coin.", 6, Rarity::Rare},
        {Relic::SilverLining, "Silver Lining", "Every win adds +0.35 multiplier.", 6, Rarity::Rare},
        {Relic::LoadedDice, "Loaded Dice", "Win with 18 or more: +8 points.", 6, Rarity::Rare},
        {Relic::Alchemist, "Alchemist", "Win after using an active ability: +0.8 multiplier and +2 coins.", 6, Rarity::Rare},
        {Relic::Phoenix, "Phoenix", "Win after escaping a bust with any rescue: +2.0 multiplier and +2 coins.", 10, Rarity::Legendary},
        {Relic::RoyalDecree, "Royal Decree", "Every win adds +0.8 multiplier.", 10, Rarity::Legendary},
        {Relic::GrandFinale, "Grand Finale", "Win with at most one hand remaining: +1.5 multiplier.", 10, Rarity::Legendary}
    };
    return catalog;
}

bool isActiveRelic(Relic relic) {
    return relic == Relic::Rewind || relic == Relic::Redraw
        || relic == Relic::ForkedPath || relic == Relic::Lifeline;
}

AbilityStatus abilityStatus(const Snapshot& state, Relic relic) {
    if (!isActiveRelic(relic)) return {};
    if (std::find(state.owned.begin(), state.owned.end(), relic) == state.owned.end())
        return {0, 1, false, "Not equipped"};
    const bool roundCharge = relic == Relic::Lifeline;
    const auto& used = roundCharge ? state.usedRoundAbilities : state.usedHandAbilities;
    if (std::find(used.begin(), used.end(), relic) != used.end())
        return {0, 1, false, roundCharge ? "Spent - recharges next round" : "Spent - recharges next hand"};
    if (state.phase != Phase::PlayerTurn)
        return {1, 1, false, "Available during your turn"};
    if (!state.cardChoices.empty()) return {1, 1, false, "Choose a card first"};
    if (relic == Relic::Lifeline)
        return {1, 1, state.pendingRescue, state.pendingRescue ? "Ready - once per round" : "Use after a bust"};
    if (relic == Relic::ForkedPath)
        return {1, 1, !state.pendingRescue, state.pendingRescue ? "Resolve the bust first" : "Ready - once per hand"};
    if (state.player.size() <= 2) return {1, 1, false, "Hit once before using"};
    return {1, 1, true, "Ready - once per hand"};
}

Game::Game(std::uint32_t seed, StartDeck deck) {
    restart(seed, deck);
}

const Snapshot& Game::state() const {
    return state_;
}

void Game::restart(std::uint32_t seed) {
    restart(seed, state_.startDeck);
}

void Game::restart(std::uint32_t seed, StartDeck deck) {
    if (deck != StartDeck::Standard && deck != StartDeck::ExtraHand
        && deck != StartDeck::SecondLife && deck != StartDeck::Rich) {
        throw std::invalid_argument("Unknown starting deck");
    }
    state_ = Snapshot{};
    state_.seed = seed;
    state_.startDeck = deck;
    state_.maxHands = deck == StartDeck::ExtraHand ? kHandsPerRound + 1 : kHandsPerRound;
    state_.handsLeft = state_.maxHands;
    state_.coins = deck == StartDeck::Rich ? 10 : 0;
    state_.revivesLeft = deck == StartDeck::SecondLife ? 1 : 0;
    state_.message = "Reach the round goal in " + std::to_string(state_.maxHands) + " hands. A win earns points and 4 coins.";
    cardRandom_.seed(seed);
    shopRandom_.seed(seed ^ 0x9e3779b9u);
    deck_.clear();
    previousOffers_.clear();
    boughtThisShop_.clear();
    rescuedThisHand_ = false;
}

bool Game::revive() {
    if (state_.phase != Phase::RunLost || state_.revivesLeft <= 0) {
        return false;
    }
    --state_.revivesLeft;
    ++state_.revivalsUsed;
    state_.streak = 0;
    resetRoundAttempt();
    state_.message = "Second life! Retry this round with fresh hands and no round points.";
    return true;
}

bool Game::hasRelic(Relic relic) const {
    return std::find(state_.owned.begin(), state_.owned.end(), relic) != state_.owned.end();
}

bool Game::boughtThisShop(Relic relic) const {
    return std::find(boughtThisShop_.begin(), boughtThisShop_.end(), relic) != boughtThisShop_.end();
}

void Game::shuffleDeck() {
    deck_.clear();
    deck_.reserve(52);
    for (int suit = 0; suit < 4; ++suit) {
        for (int rank = 1; rank <= 13; ++rank) {
            deck_.push_back({rank, static_cast<Suit>(suit)});
        }
    }
    for (std::size_t i = deck_.size() - 1; i > 0; --i) {
        const auto j = boundedRandom(cardRandom_, static_cast<std::uint32_t>(i + 1));
        std::swap(deck_[i], deck_[j]);
    }
}

Card Game::draw() {
    if (deck_.empty()) {
        throw std::logic_error("The round deck is empty");
    }
    Card card = deck_.back();
    deck_.pop_back();
    return card;
}

bool Game::deal() {
    if (state_.phase != Phase::Ready || state_.handsLeft <= 0) {
        return false;
    }
    shuffleDeck();
    state_.player.clear();
    state_.dealer.clear();
    state_.score = ScoreBreakdown{};
    rescuedThisHand_ = false;
    state_.usedHandAbilities.clear();
    state_.cardChoices.clear();
    state_.pendingRescue = false;
    state_.alteredThisHand = false;
    state_.rescuedThisHand = false;
    state_.outcome = Outcome::None;
    --state_.handsLeft;
    state_.phase = Phase::PlayerTurn;
    state_.player.push_back(draw());
    state_.dealer.push_back(draw());
    state_.player.push_back(draw());
    state_.dealer.push_back(draw());
    state_.message = "Hit to draw a card, or stand to let the dealer play.";
    if (evaluateHand(state_.player).blackjack || evaluateHand(state_.dealer).blackjack) {
        finishRound();
    }
    return true;
}

bool Game::hit() {
    if (state_.phase != Phase::PlayerTurn || state_.pendingRescue || !state_.cardChoices.empty()) {
        return false;
    }
    state_.player.push_back(draw());
    resolvePlayerCard();
    return true;
}

void Game::resolvePlayerCard() {
    state_.pendingRescue = false;
    const auto value = evaluateHand(state_.player);
    if (value.bust) {
        if (hasRelic(Relic::SecondChance) && !state_.usedSecondChance) {
            state_.player.pop_back();
            state_.usedSecondChance = true;
            rescuedThisHand_ = true;
            state_.rescuedThisHand = true;
            state_.message = "Second Chance saved you! The bust card was discarded.";
            return;
        }
        // Keep the hole card hidden and defer settlement while a real rescue is possible.
        state_.pendingRescue = true;
        if (abilityStatus(state_, Relic::Rewind).usable || abilityStatus(state_, Relic::Redraw).usable
            || abilityStatus(state_, Relic::Lifeline).usable) {
            state_.message = "Bust! Use an available rescue chip, or Stand to accept the loss.";
        } else {
            state_.pendingRescue = false;
            finishRound();
        }
    } else if (value.total == 21) {
        stand();
    } else {
        state_.message = "Draw again, or stand with your current hand.";
    }
}

bool Game::activate(Relic relic) {
    if (!abilityStatus(state_, relic).usable) return false;
    auto& used = relic == Relic::Lifeline ? state_.usedRoundAbilities : state_.usedHandAbilities;
    used.push_back(relic);
    state_.alteredThisHand = true;
    if (relic == Relic::ForkedPath) {
        state_.cardChoices = {draw(), draw()};
        state_.message = "Forked Path: choose one card to add to your hand. The other is discarded.";
        return true;
    }
    const bool wasBust = state_.pendingRescue;
    state_.pendingRescue = false;
    state_.player.pop_back();
    if (relic == Relic::Redraw) {
        state_.player.push_back(draw());
        if (wasBust && !evaluateHand(state_.player).bust) state_.rescuedThisHand = true;
        resolvePlayerCard();
    } else {
        state_.rescuedThisHand = state_.rescuedThisHand || wasBust;
        state_.message = relic == Relic::Rewind ? "Rewind discarded your last hit. Choose your next move."
            : "Lifeline saved this hand. Your round charge is spent.";
    }
    return true;
}

bool Game::chooseCard(std::size_t index) {
    if (state_.phase != Phase::PlayerTurn || index >= state_.cardChoices.size()) return false;
    const Card selected = state_.cardChoices[index];
    state_.cardChoices.clear();
    state_.player.push_back(selected);
    resolvePlayerCard();
    return true;
}

bool Game::stand() {
    if (state_.phase != Phase::PlayerTurn || !state_.cardChoices.empty()) {
        return false;
    }
    if (state_.pendingRescue) {
        state_.pendingRescue = false;
        finishRound();
        return true;
    }
    while (evaluateHand(state_.dealer).total < state_.dealerStand) {
        state_.dealer.push_back(draw());
    }
    finishRound();
    return true;
}

void Game::finishRound() {
    const auto player = evaluateHand(state_.player);
    const auto dealer = evaluateHand(state_.dealer);
    if (player.bust) {
        state_.outcome = Outcome::Loss;
    } else if (player.blackjack || dealer.blackjack) {
        state_.outcome = player.blackjack == dealer.blackjack
            ? Outcome::Push : (player.blackjack ? Outcome::Win : Outcome::Loss);
    } else if (dealer.bust || player.total > dealer.total) {
        state_.outcome = Outcome::Win;
    } else if (player.total == dealer.total) {
        state_.outcome = Outcome::Push;
    } else {
        state_.outcome = Outcome::Loss;
    }

    state_.score = ScoreBreakdown{};
    if (state_.outcome == Outcome::Win) {
        state_.score.base = player.total;
        state_.score.coins = kWinCoins;
        const auto apply = [&](Relic relic, bool condition, int points, int coins, float multiplier) {
            if (hasRelic(relic) && condition && (points != 0 || coins != 0 || multiplier != 0.0f)) {
                state_.score.appliedEffects.push_back({relic, points, coins, multiplier});
                state_.score.bonus += points;
                state_.score.coins += coins;
                state_.score.multiplier += multiplier;
            }
        };
        const bool firstRed = isRed(state_.player[0]);
        const bool secondRed = isRed(state_.player[1]);
        const int faces = static_cast<int>(std::count_if(state_.player.begin(), state_.player.end(), [](const Card& card) { return card.rank >= 11; }));
        apply(Relic::Precision, player.total == 21, 0, 1, .5f);
        apply(Relic::LongHand, state_.player.size() >= 4, 0, 1, .6f);
        apply(Relic::SecondChance, rescuedThisHand_, 0, 0, 1.0f);
        apply(Relic::Streak, true, 0, state_.streak % 3 == 2 ? 1 : 0, .1f * static_cast<float>(std::min(state_.streak, 4)));
        apply(Relic::RoyalPurse, player.blackjack, 0, 1, 0);
        apply(Relic::SoftTouch, player.soft, 0, 1, 0);
        apply(Relic::NarrowEscape, dealer.bust, 0, 1, .4f);
        apply(Relic::RedRibbon, firstRed && secondRed, 0, 1, 0);
        apply(Relic::BlackVelvet, !firstRed && !secondRed, 0, 1, 0);
        apply(Relic::EvenOdds, player.total % 2 == 0, 0, 1, 0);
        apply(Relic::CourtFavor, faces > 0, 3, 0, 0);
        apply(Relic::HumbleCrown, player.total <= 18, 5, 0, 0);
        apply(Relic::BlackjackBonus, player.blackjack, 5, 0, 0);
        apply(Relic::CopperBell, state_.player.size() >= 3, 3, 0, 0);
        apply(Relic::SeventeenCharm, player.total == 17, 0, 1, 0);
        apply(Relic::ScarletMask, std::all_of(state_.player.begin(), state_.player.end(), isRed), 0, 0, .5f);
        apply(Relic::MidnightMask, std::none_of(state_.player.begin(), state_.player.end(), isRed), 0, 0, .5f);
        apply(Relic::RoyalCourt, faces >= 2, 0, 0, .75f);
        apply(Relic::SoftCrown, player.soft && player.total >= 19, 0, 0, .75f);
        apply(Relic::JesterCrown, player.blackjack, 0, 0, 1.0f);
        apply(Relic::FiveCardParade, state_.player.size() >= 5, 0, 0, 1.5f);
        apply(Relic::FinalAct, state_.handsLeft == 0, 0, 0, 1.0f);
        apply(Relic::Underdog, player.total <= 16 && dealer.bust, 0, 0, 1.5f);
        apply(Relic::Encore, state_.streak >= 3, 0, 0, 1.0f);
        apply(Relic::VelvetDividend, true, 0, 2, 0);
        apply(Relic::LuckySeven, std::any_of(state_.player.begin(), state_.player.end(), [](const Card& c) { return c.rank == 7; }), 7, 0, 0);
        apply(Relic::TwinSigil, state_.player[0].rank == state_.player[1].rank, 10, 0, 0);
        const bool anyRed = std::any_of(state_.player.begin(), state_.player.end(), isRed);
        const bool anyBlack = std::any_of(state_.player.begin(), state_.player.end(), [](const Card& c) { return !isRed(c); });
        apply(Relic::MixedCompany, anyRed && anyBlack, 5, 0, 0);
        apply(Relic::Crescendo, state_.player.size() >= 3, 0, 0, .75f);
        apply(Relic::AceHigh, std::any_of(state_.player.begin(), state_.player.end(), [](const Card& c) { return c.rank == 1; }), 0, 1, .75f);
        apply(Relic::SilverLining, true, 0, 0, .35f);
        apply(Relic::LoadedDice, player.total >= 18, 8, 0, 0);
        apply(Relic::Alchemist, state_.alteredThisHand, 0, 2, .8f);
        apply(Relic::Phoenix, state_.rescuedThisHand, 0, 2, 2.0f);
        apply(Relic::RoyalDecree, true, 0, 0, .8f);
        apply(Relic::GrandFinale, state_.handsLeft <= 1, 0, 0, 1.5f);
        state_.score.total = static_cast<int>(std::lround(
            static_cast<float>(state_.score.base + state_.score.bonus) * state_.score.multiplier));
        state_.roundScore += state_.score.total;
        state_.totalScore += state_.score.total;
        state_.coins += state_.score.coins;
        ++state_.wins;
        ++state_.streak;
        state_.message = player.blackjack ? "Blackjack! Points and coins earned." : "You win! Coins and points earned.";
    } else if (state_.outcome == Outcome::Loss) {
        state_.streak = 0;
        state_.message = player.bust ? "Bust! This hand earns no points." : "Dealer wins this hand. No points earned.";
    } else {
        state_.message = "Push. No reward; your streak stays alive.";
    }
    state_.phase = state_.handsLeft == 0 && state_.roundScore < state_.goal ? Phase::RunLost : Phase::RoundOver;
    if (state_.phase == Phase::RunLost) {
        state_.message = "Out of hands before the goal. Start again and try another build.";
    }
}

bool Game::advance() {
    if (state_.phase != Phase::RoundOver) {
        return false;
    }
    if (state_.roundScore >= state_.goal) {
        openShop();
    } else if (state_.handsLeft == 0) {
        state_.phase = Phase::RunLost;
        state_.message = "Out of hands before the goal. Start again and try another build.";
    } else {
        state_.phase = Phase::Ready;
        state_.message = "Keep playing. Reach the round goal before your hands run out.";
    }
    return true;
}

bool Game::buy(Relic relic) {
    if (state_.phase != Phase::Shop || hasRelic(relic) || state_.owned.size() >= maxEquipmentSlots
        || std::find(state_.offers.begin(), state_.offers.end(), relic) == state_.offers.end()
        || boughtThisShop(relic)) {
        return false;
    }
    const auto* item = relicInfo(relic);
    if (!item || state_.coins < item->cost) {
        return false;
    }
    state_.coins -= item->cost;
    state_.owned.push_back(relic);
    boughtThisShop_.push_back(relic);
    state_.message = std::string(item->name) + " purchased. Your build is growing.";
    return true;
}

bool Game::sell(Relic relic) {
    if (state_.phase != Phase::Shop || !hasRelic(relic)) {
        return false;
    }
    const auto* item = relicInfo(relic);
    if (!item) {
        return false;
    }
    state_.owned.erase(std::find(state_.owned.begin(), state_.owned.end(), relic));
    state_.coins += item->cost / 2;
    state_.message = std::string(item->name) + " sold for " + std::to_string(item->cost / 2) + " coins.";
    return true;
}

bool Game::replace(Relic equipped, Relic offered) {
    if (state_.phase != Phase::Shop || !hasRelic(equipped) || hasRelic(offered)
        || boughtThisShop(offered)
        || std::find(state_.offers.begin(), state_.offers.end(), offered) == state_.offers.end()) return false;
    const auto* oldItem = relicInfo(equipped);
    const auto* newItem = relicInfo(offered);
    if (!oldItem || !newItem || state_.coins + oldItem->cost / 2 < newItem->cost) return false;
    // Validate the whole exchange before changing ownership or money.
    state_.coins += oldItem->cost / 2 - newItem->cost;
    *std::find(state_.owned.begin(), state_.owned.end(), equipped) = offered;
    boughtThisShop_.push_back(offered);
    state_.message = std::string(oldItem->name) + " replaced with " + newItem->name + ".";
    return true;
}

void Game::openShop() {
    constexpr std::array<Rarity, 3> rarities{
        Rarity::Common, Rarity::Rare, Rarity::Legendary
    };
    state_.offers.clear();
    for (int slot = 0; slot < 2; ++slot) {
        std::array<std::vector<Relic>, 3> eligible;
        std::array<std::vector<Relic>, 3> fresh;
        for (const auto& item : relicCatalog()) {
            if (hasRelic(item.id) || std::find(state_.offers.begin(), state_.offers.end(), item.id) != state_.offers.end()) {
                continue;
            }
            const auto tier = static_cast<std::size_t>(item.rarity);
            eligible[tier].push_back(item.id);
            if (std::find(previousOffers_.begin(), previousOffers_.end(), item.id) == previousOffers_.end()) {
                fresh[tier].push_back(item.id);
            }
        }
        int totalWeight = 0;
        for (std::size_t tier = 0; tier < rarities.size(); ++tier) {
            if (!eligible[tier].empty()) {
                totalWeight += rarityWeight(rarities[tier]);
            }
        }
        if (totalWeight == 0) {
            break;
        }
        int roll = static_cast<int>(boundedRandom(shopRandom_, static_cast<std::uint32_t>(totalWeight)));
        for (std::size_t tier = 0; tier < rarities.size(); ++tier) {
            if (eligible[tier].empty()) {
                continue;
            }
            const int weight = rarityWeight(rarities[tier]);
            if (roll < weight) {
                // Prefer a fresh item inside the chosen rarity. An earlier offer may
                // repeat when necessary, so anti-repeat never removes an available tier.
                const auto& pool = fresh[tier].empty() ? eligible[tier] : fresh[tier];
                const auto index = boundedRandom(shopRandom_, static_cast<std::uint32_t>(pool.size()));
                state_.offers.push_back(pool[index]);
                break;
            }
            roll -= weight;
        }
    }
    previousOffers_ = state_.offers;
    boughtThisShop_.clear();
    state_.phase = Phase::Shop;
    state_.message = "Round cleared! Choose an upgrade, sell an owned upgrade, or continue.";
}

bool Game::leaveShop() {
    if (state_.phase != Phase::Shop) {
        return false;
    }
    if (state_.round < std::numeric_limits<int>::max()) {
        ++state_.round;
    }
    state_.goal = goalForRound(state_.round);
    resetRoundAttempt();
    state_.message = "New round. Reach the next score goal in " + std::to_string(state_.maxHands) + " hands.";
    return true;
}

void Game::resetRoundAttempt() {
    state_.handsLeft = state_.maxHands;
    state_.roundScore = 0;
    state_.usedSecondChance = false;
    state_.usedHandAbilities.clear();
    state_.usedRoundAbilities.clear();
    state_.cardChoices.clear();
    state_.pendingRescue = false;
    state_.alteredThisHand = false;
    state_.rescuedThisHand = false;
    state_.player.clear();
    state_.dealer.clear();
    state_.score = ScoreBreakdown{};
    state_.outcome = Outcome::None;
    state_.offers.clear();
    boughtThisShop_.clear();
    deck_.clear();
    rescuedThisHand_ = false;
    state_.phase = Phase::Ready;
}

} // namespace bj
