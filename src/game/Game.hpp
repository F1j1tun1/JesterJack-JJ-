#pragma once

#include <cstddef>
#include <cstdint>
#include <random>
#include <string>
#include <vector>

namespace bj {

enum class Suit { Clubs, Diamonds, Hearts, Spades };

struct Card {
    int rank;
    Suit suit;
};

struct HandValue {
    int total = 0;
    bool soft = false;
    bool blackjack = false;
    bool bust = false;
};

HandValue evaluateHand(const std::vector<Card>& hand);

enum class Outcome { None, Win, Loss, Push };
enum class Phase { Ready, PlayerTurn, RoundOver, Shop, RunLost };
enum class Relic {
    Precision, LongHand, SecondChance, Streak, RoyalPurse, SoftTouch,
    NarrowEscape, RedRibbon, BlackVelvet, EvenOdds, CourtFavor, HumbleCrown,
    BlackjackBonus, CopperBell, SeventeenCharm, ScarletMask, MidnightMask,
    RoyalCourt, SoftCrown, JesterCrown, FiveCardParade, FinalAct, Underdog, Encore,
    Rewind, Redraw, ForkedPath, Lifeline, VelvetDividend, LuckySeven, TwinSigil,
    MixedCompany, Crescendo, AceHigh, SilverLining, LoadedDice, Alchemist,
    Phoenix, RoyalDecree, GrandFinale
};

inline constexpr std::size_t maxEquipmentSlots = 8;
bool isActiveRelic(Relic relic);

enum class Rarity { Common, Rare, Legendary };
const char* rarityName(Rarity rarity);
int rarityWeight(Rarity rarity);

// Standard keeps the five-hand command-line/default game; the menu offers the other three.
enum class StartDeck { Standard, ExtraHand, SecondLife, Rich };

struct RelicInfo {
    Relic id;
    const char* name;
    const char* description;
    int cost;
    Rarity rarity;
};

const std::vector<RelicInfo>& relicCatalog();

struct AppliedEffect {
    Relic relic;
    int points = 0;
    int coins = 0;
    float multiplierBonus = 0.0f;
};

struct ScoreBreakdown {
    int base = 0;
    int bonus = 0;
    float multiplier = 1.0f;
    int total = 0;
    int coins = 0;
    std::vector<AppliedEffect> appliedEffects;
};

struct Snapshot {
    std::vector<Card> player;
    std::vector<Card> dealer;
    Phase phase = Phase::Ready;
    Outcome outcome = Outcome::None;
    int round = 1;
    int handsLeft = 5;
    int roundScore = 0;
    int goal = 30;
    StartDeck startDeck = StartDeck::Standard;
    int maxHands = 5;
    int revivesLeft = 0;
    int revivalsUsed = 0;
    int wins = 0;
    int totalScore = 0;
    int coins = 0;
    int streak = 0;
    int dealerStand = 17;
    std::uint32_t seed = 42;
    bool usedSecondChance = false;
    std::vector<Relic> owned;
    std::vector<Relic> offers;
    // Charges belong to equipped relics; no parallel equipment inventory exists.
    std::vector<Relic> usedHandAbilities;
    std::vector<Relic> usedRoundAbilities;
    std::vector<Card> cardChoices;
    bool pendingRescue = false;
    bool alteredThisHand = false;
    bool rescuedThisHand = false;
    ScoreBreakdown score;
    std::string message;
};

struct AbilityStatus {
    int remaining = 0;
    int maximum = 0;
    bool usable = false;
    const char* reason = "Passive effect";
};

// Safe to query both the authoritative state and a paced presentation snapshot.
AbilityStatus abilityStatus(const Snapshot& state, Relic relic);

class Game {
public:
    explicit Game(std::uint32_t seed = 42, StartDeck deck = StartDeck::Standard);

    const Snapshot& state() const;
    void restart(std::uint32_t seed);
    void restart(std::uint32_t seed, StartDeck deck);
    bool revive();
    bool deal();
    bool hit();
    bool stand();
    bool advance();
    bool buy(Relic relic);
    bool sell(Relic relic);
    bool replace(Relic equipped, Relic offered);
    bool activate(Relic relic);
    bool chooseCard(std::size_t index);
    bool leaveShop();
    bool hasRelic(Relic relic) const;
    bool boughtThisShop(Relic relic) const;

private:
    Snapshot state_;
    std::mt19937 cardRandom_;
    std::mt19937 shopRandom_;
    std::vector<Card> deck_;
    std::vector<Relic> previousOffers_;
    std::vector<Relic> boughtThisShop_;
    bool rescuedThisHand_ = false;

    void shuffleDeck();
    Card draw();
    void finishRound();
    void resolvePlayerCard();
    void openShop();
    void resetRoundAttempt();
};

} // namespace bj
