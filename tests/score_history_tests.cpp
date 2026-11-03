#include "presentation/ScoreHistory.hpp"
#include "presentation/Presentation.hpp"
#include <iostream>
#include <cstdlib>
void check(bool ok,const char* message) { if(!ok) {std::cerr<<message<<'\n';std::exit(1);} }
int main() {
    presentation::ScoreHistory history;
    bj::Game game(42,bj::StartDeck::Rich);
    history.observe(game.state(),false);
    check(history.records().empty(),"Ready state must not create a record");
    game.deal();
    if(game.state().phase==bj::Phase::PlayerTurn) game.stand();
    const auto settled=game.state();
    history.observe(settled,true);
    check(history.records().empty(),"Animation must finish before logging");
    history.observe(settled,false);
    history.observe(settled,false);
    check(history.records().size()==1,"Repeated rendered frames must not duplicate hands");
    check(history.records().front().score.total==settled.score.total &&
          history.records().front().score.base==settled.score.base &&
          history.records().front().score.coins==settled.score.coins,"Copy authoritative game breakdown");
    auto state=settled;
    state.phase=bj::Phase::Shop;
    history.observe(state,false);
    check(history.records().size()==1,"Shop retains score but must not log it again");
    state=settled; ++state.revivalsUsed;
    history.observe(state,false);
    check(history.records().size()==2 && history.records().front().attempt==2,"Revived hand has a new identity");
    for(int i=2;i<14;++i) {state.round=i;history.observe(state,false);}
    check(history.records().size()==8 && history.records().front().round==13 && history.records().back().round==6,
          "Bounded newest-first history evicts oldest entries");
    state.round=14;state.outcome=bj::Outcome::Push;state.score={};
    history.observe(state,false);
    check(history.records().front().score.total==0 && history.records().front().outcome==bj::Outcome::Push,
          "Push records zero without inventing a reward");
    state.round=15;state.phase=bj::Phase::RunLost;state.outcome=bj::Outcome::Loss;
    history.observe(state,false);
    check(history.records().front().outcome==bj::Outcome::Loss,"Final losing hand remains recorded");
    history.reset();history.observe(settled,false);
    check(history.records().size()==1,"Restart clears records and duplicate key even for same seed");
    auto bonus=settled;bonus.round=20;bonus.score.bonus=5;bonus.score.multiplier=1.75f;bonus.score.total=40;
    bonus.score.appliedEffects={{bj::Relic::BlackjackBonus,5,0,0},{bj::Relic::RoyalCourt,0,0,.75f}};
    history.observe(bonus,false);
    bonus.score.appliedEffects.clear();
    check(history.records().front().score.appliedEffects.size()==2 && history.records().front().score.total==40,
          "Relic attribution is an immutable copy, not recomputed from rounded multiplier");
    std::cout<<"Score history checks passed\n";
}
