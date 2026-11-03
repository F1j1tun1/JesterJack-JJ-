#pragma once

#include "game/Game.hpp"
#include <vector>

namespace presentation {
// Sort only the gallery. Return real IDs and preserve catalog order within each
// rarity so reward generation, saved commands and selection identity stay stable.
inline const std::vector<bj::Relic>& collectionOrder() {
    static const std::vector<bj::Relic> order=[] {
        std::vector<bj::Relic> result;
        result.reserve(bj::relicCatalog().size());
        for(const auto rarity:{bj::Rarity::Common,bj::Rarity::Rare,bj::Rarity::Legendary})
            for(const auto& item:bj::relicCatalog())
                if(item.rarity==rarity) result.push_back(item.id);
        return result;
    }();
    return order;
}
} // namespace presentation
