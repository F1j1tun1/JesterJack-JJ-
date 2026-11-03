#include "presentation/Presentation.hpp"
#include "presentation/ScoreHistory.hpp"
#include "presentation/CollectionOrder.hpp"
#include <algorithm>

#include <cmath>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>

namespace {
int failures = 0;
int checks = 0;

void check(bool condition, const char* message) {
    ++checks;
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

std::vector<bj::Card> cards(std::initializer_list<int> ranks) {
    std::vector<bj::Card> result;
    for (const int rank : ranks) result.push_back({rank, bj::Suit::Hearts});
    return result;
}

std::string fingerprint(const bj::Snapshot& snapshot) {
    std::ostringstream out;
    out << static_cast<int>(snapshot.phase) << ':' << static_cast<int>(snapshot.outcome)
        << ':' << snapshot.round << ':' << snapshot.handsLeft << ':' << snapshot.roundScore
        << ':' << snapshot.goal << ':' << static_cast<int>(snapshot.startDeck) << ':' << snapshot.maxHands
        << ':' << snapshot.revivesLeft << ':' << snapshot.revivalsUsed << ':' << snapshot.wins << ':' << snapshot.totalScore
        << ':' << snapshot.coins << ':' << snapshot.streak << ':' << snapshot.dealerStand
        << ':' << snapshot.seed << ':' << snapshot.usedSecondChance << ':' << snapshot.message
        << ':' << snapshot.pendingRescue << ':' << snapshot.alteredThisHand << ':' << snapshot.rescuedThisHand;
    for (const auto& hand : {snapshot.player, snapshot.dealer, snapshot.cardChoices}) {
        out << '|';
        for (const auto& card : hand) out << card.rank << ',' << static_cast<int>(card.suit) << ';';
    }
    for (const auto& relics : {snapshot.owned, snapshot.offers, snapshot.usedHandAbilities, snapshot.usedRoundAbilities}) {
        out << '|';
        for (const auto relic : relics) out << static_cast<int>(relic) << ',';
    }
    out << '|' << snapshot.score.base << ':' << snapshot.score.bonus << ':'
        << snapshot.score.multiplier << ':' << snapshot.score.total << ':' << snapshot.score.coins;
    for (const auto& effect : snapshot.score.appliedEffects)
        out << '|' << static_cast<int>(effect.relic) << ':' << effect.points << ':'
            << effect.coins << ':' << effect.multiplierBonus;
    return out.str();
}

void settle(presentation::Presentation& timeline, float speed = 0.0f) {
    int frames = 0;
    while (timeline.busy() && frames++ < 10000) timeline.update(0.01f, speed);
    check(!timeline.busy(), "Every timeline eventually settles");
}

bj::Snapshot playing() {
    bj::Snapshot snapshot;
    snapshot.phase = bj::Phase::PlayerTurn;
    snapshot.player = cards({10, 7});
    snapshot.dealer = cards({10, 6});
    snapshot.handsLeft = 4;
    return snapshot;
}

bj::Snapshot winFrom(const bj::Snapshot& before) {
    auto after = before;
    after.phase = bj::Phase::RoundOver;
    after.outcome = bj::Outcome::Win;
    after.score.base = bj::evaluateHand(after.player).total;
    after.score.total = after.score.base;
    after.score.coins = 3;
    after.coins += 3;
    after.roundScore += after.score.total;
    after.totalScore += after.score.total;
    ++after.wins;
    ++after.streak;
    after.message = "Final reward";
    return after;
}

void initialDeal() {
    bj::Snapshot before;
    const auto after = playing();
    presentation::Presentation timeline;
    timeline.reset(before);
    check(!timeline.busy() && timeline.playerArrival() == 1.0f && timeline.dealerFlip() == 1.0f,
          "Reset has settled rendering defaults");
    timeline.begin(before, after, presentation::Action::Deal);
    check(timeline.busy() && timeline.visible().player.size() == 1 && timeline.visible().dealer.empty(),
          "Initial deal starts with only the first player card");
    check(timeline.playerArrival() == 0.0f && timeline.dealingToPlayer(), "New player card starts at the deck");
    timeline.update(0.275f);
    check(std::abs(timeline.playerArrival() - 0.5f) < 0.001f && timeline.dealerReach() > 0.99f,
          "Half-flight gives normalized arrival and a reaching pose");
    timeline.update(0.275f);
    check(timeline.visible().player.size() == 1 && timeline.visible().dealer.size() == 1
          && timeline.dealerArrival() == 0.0f, "Dealer receives the second dealt card");
    timeline.update(0.55f);
    check(timeline.visible().player.size() == 2 && timeline.visible().dealer.size() == 1,
          "Player receives the third dealt card");
    timeline.update(0.55f);
    check(timeline.visible().dealer.size() == 2 && !timeline.dealerRevealed(), "Hole card is dealt last and remains hidden");
    timeline.update(0.55f);
    check(!timeline.busy() && fingerprint(timeline.visible()) == fingerprint(after),
          "Ordinary deal ends at the exact authoritative player-turn snapshot");
    check(!timeline.dealerRevealed(), "Ordinary player turn keeps the hole card hidden");
}

void naturalRevealAndRewards() {
    bj::Snapshot before;
    auto after = playing();
    after.player = cards({1, 13});
    after.dealer = cards({10, 8});
    after.owned = {bj::Relic::BlackjackBonus, bj::Relic::RoyalPurse};
    after = winFrom(after);
    after.score.appliedEffects = {{bj::Relic::BlackjackBonus, 5, 0, 0.0f},
                                 {bj::Relic::RoyalPurse, 0, 1, 0.0f}};
    after.score.bonus = 5;
    after.score.total = 26;
    after.score.coins = 4;
    after.coins = 4;
    after.roundScore = after.totalScore = 26;
    presentation::Presentation timeline;
    timeline.begin(before, after, presentation::Action::Deal);
    timeline.update(2.2f);
    check(timeline.dealerFlipping() && !timeline.dealerRevealed(), "A natural waits for all four cards before revealing");
    check(timeline.visible().phase == bj::Phase::PlayerTurn && timeline.visible().outcome == bj::Outcome::None
          && timeline.visible().score.total == 0 && timeline.visible().coins == 0
          && timeline.visible().totalScore == 0, "A natural cannot leak its payout while the hole card flips");
    timeline.update(0.2f);
    check(std::abs(timeline.dealerFlip() - 0.5f) < 0.001f, "Hole-card flip exposes smooth progress");
    timeline.update(0.2f);
    check(timeline.dealerRevealed() && timeline.rewardStep() == 0 && timeline.rewardVisibleEffects() == 0,
          "Settled cards start reward playback at the base");
    check(fingerprint(timeline.visible()) == fingerprint(after), "Reward playback uses the authoritative final snapshot");
    timeline.update(0.4f);
    check(timeline.rewardStep() == 1 && timeline.rewardVisibleEffects() == 1, "First applied effect follows the base");
    timeline.update(0.5f);
    check(timeline.rewardStep() == 2 && timeline.rewardVisibleEffects() == 2, "Second effect is displayed separately");
    timeline.update(0.5f);
    check(timeline.rewardStep() == 3 && timeline.celebration() > 0.99f, "Total follows every applied effect");
    timeline.update(0.6f);
    check(!timeline.busy() && timeline.rewardStep() == 3 && timeline.rewardVisibleEffects() == 2,
          "Final reward summary persists when input becomes available");
    timeline.update(5.0f);
    check(timeline.celebration() == 0.0f && timeline.legendPulse() == 0.0f,
          "Celebration decays without changing the retained reward summary");
}

void dealerTurn() {
    auto before = playing();
    before.dealer = cards({2, 3});
    auto after = before;
    after.dealer = cards({2, 3, 6, 6});
    after.phase = bj::Phase::RoundOver;
    after.outcome = bj::Outcome::Push;
    presentation::Presentation timeline;
    timeline.begin(before, after, presentation::Action::Stand);
    check(timeline.dealerFlipping() && timeline.visible().dealer.size() == 2, "Stand flips before dealing any extra card");
    timeline.update(0.4f);
    check(timeline.dealerRevealed() && timeline.visible().dealer.size() == 3
          && timeline.dealerArrival() == 0.0f, "First dealer draw starts after the completed flip");
    check(timeline.visible().outcome == bj::Outcome::None && timeline.visible().phase == bj::Phase::PlayerTurn,
          "Dealer draws cannot reveal the final push early");
    timeline.update(0.55f);
    check(timeline.visible().dealer.size() == 4 && timeline.dealerArrival() == 0.0f,
          "Dealer draws arrive one at a time");
    timeline.update(0.55f);
    check(!timeline.busy() && fingerprint(timeline.visible()) == fingerprint(after),
          "A settled push has no invented reward animation");

    before = playing();
    before.player = cards({10, 9});
    before.dealer = cards({10, 7});
    after = winFrom(before);
    timeline.begin(before, after, presentation::Action::Stand);
    timeline.update(0.4f);
    check(timeline.rewardStep() == 0 && timeline.visible().dealer.size() == 2,
          "Standing against an already-settled dealer still flips and pays out without an extra draw");
    settle(timeline);
    check(fingerprint(timeline.visible()) == fingerprint(after), "No-extra-draw stand keeps the final snapshot exact");
}

void hitRescueAndSkip() {
    auto before = playing();
    auto after = before;
    after.player.push_back({3, bj::Suit::Spades});
    presentation::Presentation timeline;
    timeline.begin(before, after, presentation::Action::Hit);
    check(timeline.visible().player.size() == 3 && timeline.playerArrival() == 0.0f
          && !timeline.dealerFlipping(), "Hit animates the new player card without revealing the dealer");
    timeline.accelerate();
    check(!timeline.busy() && fingerprint(timeline.visible()) == fingerprint(after), "Acceleration settles one-card hits immediately");

    before.owned = {bj::Relic::SecondChance};
    after = before;
    after.usedSecondChance = true;
    timeline.begin(before, after, presentation::Action::Hit);
    check(timeline.busy() && timeline.legendPulse() == 1.0f && timeline.visible().player.size() == 2,
          "A discarded bust card triggers a legendary pulse without inventing a retained card");
    settle(timeline);
    check(fingerprint(timeline.visible()) == fingerprint(after), "Rescue settles to the rules snapshot");

    before = playing();
    after = winFrom(before);
    after.dealer.push_back({10, bj::Suit::Clubs});
    timeline.begin(before, after, presentation::Action::Stand);
    for (int presses = 0; timeline.busy() && presses < 20; ++presses) timeline.accelerate();
    check(!timeline.busy() && fingerprint(timeline.visible()) == fingerprint(after)
          && timeline.rewardStep() == 1, "Repeated acceleration traverses flip, draws and reward without losing the result");
    timeline.accelerate();
    check(fingerprint(timeline.visible()) == fingerprint(after), "Acceleration on an idle timeline is harmless");
}

void timingResetAndDeterminism() {
    bj::Snapshot before;
    const auto after = playing();
    presentation::Presentation normal, fast;
    normal.begin(before, after, presentation::Action::Deal);
    fast.begin(before, after, presentation::Action::Deal);
    normal.update(0.28f, 0.0f);
    fast.update(0.28f, 1.0f);
    check(normal.busy() && !fast.busy(), "Fast pacing settles the same deal sooner");
    settle(normal);
    check(fingerprint(normal.visible()) == fingerprint(fast.visible()), "Pacing cannot change the final game data");

    normal.begin(before, after, presentation::Action::Deal);
    const auto initial = fingerprint(normal.visible());
    normal.update(-1.0f);
    normal.update(std::numeric_limits<float>::quiet_NaN());
    normal.update(std::numeric_limits<float>::infinity());
    check(fingerprint(normal.visible()) == initial && normal.playerArrival() == 0.0f,
          "Invalid frame times cannot advance or corrupt the timeline");
    normal.reset(before);
    normal.update(100.0f);
    check(!normal.busy() && fingerprint(normal.visible()) == fingerprint(before), "Reset cancels all stale card and reward stages");

    normal.begin(before, after, presentation::Action::Deal);
    fast.begin(before, after, presentation::Action::Deal);
    normal.update(100.0f);
    settle(fast);
    check(fingerprint(normal.visible()) == fingerprint(fast.visible()), "Large and small frame steps settle to the same snapshot");

    bj::Game game(42);
    const auto gameBefore = game.state();
    game.deal();
    const auto authoritative = fingerprint(game.state());
    normal.begin(gameBefore, game.state(), presentation::Action::Deal);
    settle(normal, 0.0f);
    check(fingerprint(game.state()) == authoritative && fingerprint(normal.visible()) == authoritative,
          "Presentation playback never mutates or recalculates the authoritative game");

    auto purchased = before;
    purchased.owned = {bj::Relic::SecondChance};
    normal.begin(before, purchased, presentation::Action::Other);
    check(!normal.busy() && normal.legendPulse() == 1.0f,
          "A legendary purchase can pulse without starting a card animation");
}

void legendaryEffects() {
    bj::Snapshot before;
    before.owned = {bj::Relic::JesterCrown};
    auto after = playing();
    after.player = cards({1, 13});
    after.owned = before.owned;
    after = winFrom(after);
    after.score.appliedEffects = {{bj::Relic::JesterCrown, 0, 0, 1.0f}};
    after.score.multiplier = 2.0f;
    after.score.total = after.roundScore = after.totalScore = 42;
    presentation::Presentation timeline;
    timeline.begin(before, after, presentation::Action::Deal);
    for (int skips = 0; timeline.busy() && timeline.rewardStep() < 0 && skips < 20; ++skips)
        timeline.accelerate();
    check(timeline.rewardStep() == 0 && timeline.legendPulse() == 0.0f,
          "An already-owned legendary waits for its actual reward effect before pulsing");
    timeline.accelerate();
    check(timeline.rewardStep() == 1 && timeline.legendPulse() == 1.0f,
          "Jester Crown's authoritative applied effect triggers a legendary pulse");
    settle(timeline);

    before = bj::Snapshot{};
    after = before;
    after.owned = {bj::Relic::FiveCardParade};
    timeline.begin(before, after, presentation::Action::Other);
    check(!timeline.busy() && timeline.legendPulse() == 1.0f,
          "A different catalogue legendary pulses on acquisition");

    before = after;
    before.score.appliedEffects = {{bj::Relic::JesterCrown, 0, 0, 1.0f}};
    after = before;
    after.owned.push_back(bj::Relic::RoyalPurse);
    timeline.begin(before, after, presentation::Action::Other);
    check(timeline.legendPulse() == 0.0f,
          "A common purchase cannot replay an old legendary activation");
}

void activeCardAlterations() {
    auto before = playing();
    before.player = cards({10, 4, 9});
    before.owned = {bj::Relic::Rewind, bj::Relic::Redraw, bj::Relic::ForkedPath, bj::Relic::Lifeline};
    before.pendingRescue = true;
    auto after = before;
    after.player.pop_back();
    after.pendingRescue = false;
    after.alteredThisHand = after.rescuedThisHand = true;
    after.usedHandAbilities = {bj::Relic::Rewind};
    after.message = "Last hit undone";
    presentation::Presentation timeline;
    presentation::ScoreHistory history;
    timeline.begin(before, after, presentation::Action::Ability);
    check(timeline.busy() && timeline.visible().player.size() == 2 && !timeline.dealerRevealed(),
          "Rewind removes the actual last card while keeping the dealer hidden");
    check(!timeline.visible().pendingRescue && timeline.visible().usedHandAbilities == after.usedHandAbilities
          && timeline.visible().rescuedThisHand && timeline.visible().alteredThisHand,
          "Rescue state and spent charges update as soon as the ability starts");
    history.observe(timeline.visible(), timeline.busy());
    settle(timeline);
    history.observe(timeline.visible(), timeline.busy());
    check(fingerprint(timeline.visible()) == fingerprint(after) && history.records().empty(),
          "A rewind changes no settled score and creates no journal entry");

    before = after;
    before.player.push_back({5, bj::Suit::Hearts});
    after = before;
    after.player.back() = {6, bj::Suit::Spades};
    after.usedHandAbilities.push_back(bj::Relic::Redraw);
    timeline.begin(before, after, presentation::Action::Ability);
    check(timeline.busy() && timeline.visible().player.size() == 3
          && timeline.visible().player.back().rank == 6 && timeline.visible().player.back().suit == bj::Suit::Spades
          && timeline.playerArrival() == 0 && !timeline.dealerRevealed(),
          "Redraw animates the replacement card even when hand size is unchanged");
    check(timeline.visible().usedHandAbilities == after.usedHandAbilities,
          "Redraw charge cannot appear available while its replacement card arrives");
    settle(timeline);
    check(fingerprint(timeline.visible()) == fingerprint(after), "Redraw settles to exactly the authoritative hand");
    before = after;
    after.player.back().suit = bj::Suit::Diamonds;
    timeline.begin(before, after, presentation::Action::Ability);
    check(timeline.busy() && timeline.playerArrival() == 0
          && timeline.visible().player.back().suit == bj::Suit::Diamonds,
          "A same-rank redraw still notices the changed suit");
    settle(timeline);

    before = playing();
    before.player = cards({10, 3});
    before.owned = {bj::Relic::ForkedPath};
    after = before;
    after.cardChoices = cards({4, 8});
    after.usedHandAbilities = {bj::Relic::ForkedPath};
    timeline.begin(before, after, presentation::Action::Ability);
    check(!timeline.busy() && !timeline.dealerRevealed() && fingerprint(timeline.visible()) == fingerprint(after),
          "Opening a choice exposes its candidates immediately without inventing a draw or dealer reveal");
    history.observe(timeline.visible(), timeline.busy());
    before = after;
    after.cardChoices.clear();
    after.player.push_back({4, bj::Suit::Hearts});
    after.alteredThisHand = true;
    timeline.begin(before, after, presentation::Action::Ability);
    check(timeline.busy() && timeline.playerArrival() == 0 && timeline.visible().cardChoices.empty()
          && !timeline.dealerRevealed(), "The chosen card arrives once and closes the choice immediately");
    settle(timeline);
    history.observe(timeline.visible(), timeline.busy());
    check(history.records().empty(), "Unsettled choices never enter the score journal");

    before = after;
    after = winFrom(before);
    timeline.begin(before, after, presentation::Action::Stand);
    while (timeline.busy()) {
        history.observe(timeline.visible(), true);
        timeline.accelerate();
    }
    history.observe(timeline.visible(), false);
    history.observe(timeline.visible(), false);
    check(history.records().size() == 1 && history.records().front().score.total == after.score.total,
          "An altered hand enters the journal exactly once at its real settlement");

    before = playing();
    before.player = cards({10, 3, 9});
    before.pendingRescue = true;
    before.owned = {bj::Relic::Lifeline};
    after = before;
    after.player.pop_back();
    after.pendingRescue = false;
    after.alteredThisHand = after.rescuedThisHand = true;
    after.usedRoundAbilities = {bj::Relic::Lifeline};
    timeline.begin(before, after, presentation::Action::Ability);
    check(timeline.visible().usedRoundAbilities == after.usedRoundAbilities
          && timeline.visible().outcome == bj::Outcome::None && !timeline.dealerRevealed(),
          "Lifeline spends its round charge without settling or revealing the dealer");
    settle(timeline);
    check(fingerprint(timeline.visible()) == fingerprint(after), "Lifeline keeps all final snapshot metadata exact");

    before = playing();
    before.player = cards({10, 5, 9});
    before.pendingRescue = true;
    after = before;
    after.player.back() = {6, bj::Suit::Clubs};
    after.pendingRescue = false;
    after.alteredThisHand = after.rescuedThisHand = true;
    after.usedHandAbilities = {bj::Relic::Redraw};
    after = winFrom(after);
    timeline.begin(before, after, presentation::Action::Ability);
    check(timeline.visible().player.back().rank == 6 && timeline.visible().score.total == 0
          && timeline.visible().coins == before.coins && !timeline.dealerRevealed(),
          "A redraw reaching 21 cannot leak payout or dealer cards before replacement arrival");
    timeline.accelerate();
    check(timeline.dealerFlipping(), "A settled ability reveals the hole card only after its replacement arrives");
    settle(timeline);
    check(fingerprint(timeline.visible()) == fingerprint(after), "A winning redraw settles to its sole authoritative payout");
}

void collectionDisplayIdentity() {
    const auto& catalog=bj::relicCatalog();
    std::vector<bj::Relic> original;
    for(const auto& item:catalog) original.push_back(item.id);
    const auto& display=presentation::collectionOrder();
    check(display.size()==catalog.size(),"Collection displays every relic exactly once");
    check(display.front()==bj::Relic::RoyalPurse,"All collection opens with Common Royal Purse");
    std::size_t position=0;
    for(const auto rarity:{bj::Rarity::Common,bj::Rarity::Rare,bj::Rarity::Legendary}) {
        for(const auto& item:catalog) if(item.rarity==rarity) {
            check(position<display.size() && display[position++]==item.id,
                  "Rarity groups preserve real relic IDs and stable catalog order");
        }
    }
    const auto precision=std::find(display.begin(),display.end(),bj::Relic::Precision);
    const auto chance=std::find(display.begin(),display.end(),bj::Relic::SecondChance);
    check(precision<chance,"Rare Precision appears before Legendary Second Chance");
    check(std::count(display.begin(),display.end(),bj::Relic::Rewind)==1 &&
          std::count(display.begin(),display.end(),bj::Relic::GrandFinale)==1,
          "New Rare and Legendary relics retain distinct selectable identities");
    std::vector<bj::Relic> unchanged;
    for(const auto& item:bj::relicCatalog()) unchanged.push_back(item.id);
    check(unchanged==original && unchanged.front()==bj::Relic::Precision,
          "Gallery sorting does not mutate the game catalog");
}


using presentation::Cue;

std::vector<Cue> drainCues(presentation::Presentation& timeline, float speed = 0.0f) {
    auto cues = timeline.takeCues();
    int frames = 0;
    while (timeline.busy() && frames++ < 10000) {
        timeline.update(0.01f, speed);
        for (const auto cue : timeline.takeCues()) cues.push_back(cue);
    }
    return cues;
}

int countOf(const std::vector<Cue>& cues, Cue cue) {
    return static_cast<int>(std::count(cues.begin(), cues.end(), cue));
}

void soundCues() {
    // Opening deal: one stake, then every one of the four cards is a "dealt" card.
    bj::Snapshot empty;
    presentation::Presentation timeline;
    timeline.begin(empty, playing(), presentation::Action::Deal);
    auto cues = timeline.takeCues();
    check(cues.size() == 2 && cues[0] == Cue::BetPlaced && cues[1] == Cue::DealCard,
          "A deal raises the bet and the first dealt card together");
    check(timeline.takeCues().empty(), "Cues are delivered once");
    timeline.update(0.55f);
    cues = drainCues(timeline);
    cues.insert(cues.begin(), Cue::DealCard); // The first card was taken above.
    check(countOf(cues, Cue::DealCard) == 4 && countOf(cues, Cue::PlayerCardRoll) == 0 && countOf(cues, Cue::DealerCardRoll) == 0,
          "All four opening cards use the dealing sound, never the roll sounds");
    check(countOf(cues, Cue::HandWon) + countOf(cues, Cue::HandLost) + countOf(cues, Cue::RunLost) == 0,
          "An ordinary deal announces no result");

    // Hits are the player's own card roll; dealer draws are the bot's.
    auto before = playing();
    auto after = before;
    after.player.push_back({3, bj::Suit::Spades});
    timeline.begin(before, after, presentation::Action::Hit);
    cues = drainCues(timeline);
    check(cues.size() == 1 && cues[0] == Cue::PlayerCardRoll, "A hit is exactly one player card roll");

    before = playing();
    before.dealer = cards({2, 3});
    after = before;
    after.dealer = cards({2, 3, 6, 6});
    after.phase = bj::Phase::RoundOver;
    after.outcome = bj::Outcome::Push;
    timeline.begin(before, after, presentation::Action::Stand);
    cues = drainCues(timeline);
    check(cues.size() == 2 && countOf(cues, Cue::DealerCardRoll) == 2,
          "Each dealer draw is one dealer card sound and a push adds nothing");

    // Wins: tada minor below the goal, tada major once the goal is met, chips for both.
    before = playing();
    before.player = cards({10, 9});
    before.dealer = cards({10, 7});
    after = winFrom(before);
    timeline.begin(before, after, presentation::Action::Stand);
    cues = drainCues(timeline);
    check(cues.size() == 2 && cues[0] == Cue::HandWon && cues[1] == Cue::ChipsWon,
          "A simple win plays the minor fanfare, then the coins");
    after.goal = 15;
    timeline.begin(before, after, presentation::Action::Stand);
    cues = drainCues(timeline);
    check(cues.size() == 2 && cues[0] == Cue::RoundCleared && cues[1] == Cue::ChipsWon,
          "Reaching the round goal plays the major fanfare instead");
    after.goal = 30;
    timeline.begin(before, after, presentation::Action::Stand);
    const auto accelerated = [&] {
        std::vector<Cue> all = timeline.takeCues();
        while (timeline.busy()) {
            timeline.accelerate();
            for (const auto cue : timeline.takeCues()) all.push_back(cue);
        }
        return all;
    }();
    check(accelerated.size() == 2 && accelerated[0] == Cue::HandWon && accelerated[1] == Cue::ChipsWon,
          "Skipping the animation still raises each cue once");
    timeline.begin(before, after, presentation::Action::Stand);
    cues = drainCues(timeline, 1.0f);
    check(cues.size() == 2 && cues[0] == Cue::HandWon, "Cues do not depend on animation speed");

    // Losses: the dealer sweeps the stake; running out of hands ends the run.
    before = playing();
    before.player = cards({10, 6});
    after = before;
    after.player.push_back({10, bj::Suit::Spades});
    after.phase = bj::Phase::RoundOver;
    after.outcome = bj::Outcome::Loss;
    timeline.begin(before, after, presentation::Action::Hit);
    cues = drainCues(timeline);
    check(cues.size() == 2 && cues[0] == Cue::PlayerCardRoll && cues[1] == Cue::HandLost,
          "A bust rolls the card, then the dealer takes the stake");
    after.phase = bj::Phase::RunLost;
    timeline.begin(before, after, presentation::Action::Hit);
    cues = drainCues(timeline);
    check(cues.size() == 2 && cues.back() == Cue::RunLost && countOf(cues, Cue::HandLost) == 0,
          "The last lost hand plays the game-over sound, not a hand-lost sound");
    before = playing();
    before.player = cards({10, 9});
    before.dealer = cards({10, 7});
    after = winFrom(before);
    after.phase = bj::Phase::RunLost;
    timeline.begin(before, after, presentation::Action::Stand);
    cues = drainCues(timeline);
    check(cues.size() == 3 && cues[0] == Cue::HandWon && cues[1] == Cue::ChipsWon && cues[2] == Cue::RunLost,
          "A win that still misses the goal on the last hand is celebrated, then the run ends");

    // Shop and other instant commands are silent; resets drop stale cues.
    before = playing();
    after = before;
    timeline.begin(before, after, presentation::Action::Other);
    check(timeline.takeCues().empty(), "Instant commands raise no timeline cues");
    timeline.begin(before, winFrom(before), presentation::Action::Stand);
    timeline.reset(before);
    check(timeline.takeCues().empty(), "Reset discards cues from a cancelled timeline");
}

} // namespace

int main() {
    collectionDisplayIdentity();
    initialDeal();
    naturalRevealAndRewards();
    dealerTurn();
    hitRescueAndSkip();
    timingResetAndDeterminism();
    legendaryEffects();
    activeCardAlterations();
    soundCues();
    std::cout << checks << " checks, " << failures << " failures\n";
    return failures == 0 ? 0 : 1;
}
