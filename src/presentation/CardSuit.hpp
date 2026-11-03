#pragma once

#include <imgui.h>

#include "game/Game.hpp"

namespace presentation {

// A single filled outline keeps the icon uniform even when its color fades.
inline void drawCardSuit(ImDrawList* draw, bj::Suit suit, ImVec2 center,
                         float size, ImU32 color) {
    if (!draw || size <= 0.0f || (color & IM_COL32_A_MASK) == 0) return;
    const auto point = [&](float x, float y) {
        return ImVec2(center.x + x * size, center.y + y * size);
    };

    ImVec2 outline[64];
    int count = 0;
    const auto line = [&](ImVec2 end) { outline[count++] = end; };
    const auto curve = [&](ImVec2 control1, ImVec2 control2, ImVec2 end,
                           int segments, bool closes = false) {
        const ImVec2 start = outline[count - 1];
        // A filled polygon closes itself; do not duplicate its first vertex.
        for (int step = 1; step <= segments - (closes ? 1 : 0); ++step) {
            const float t = static_cast<float>(step) / static_cast<float>(segments);
            const float u = 1.0f - t;
            const float a = u * u * u;
            const float b = 3.0f * u * u * t;
            const float c = 3.0f * u * t * t;
            const float d = t * t * t;
            line({a * start.x + b * control1.x + c * control2.x + d * end.x,
                  a * start.y + b * control1.y + c * control2.y + d * end.y});
        }
    };
    switch (suit) {
        case bj::Suit::Diamonds:
            draw->AddQuadFilled(point(0, -.49f), point(.38f, 0),
                                point(0, .49f), point(-.38f, 0), color);
            return;

        case bj::Suit::Hearts:
            line(point(0, .48f));
            curve(point(-.14f, .31f), point(-.47f, .05f), point(-.47f, -.15f), 8);
            curve(point(-.47f, -.47f), point(-.12f, -.55f), point(0, -.24f), 8);
            curve(point(.12f, -.55f), point(.47f, -.47f), point(.47f, -.15f), 8);
            curve(point(.47f, .05f), point(.14f, .31f), point(0, .48f), 8, true);
            break;

        case bj::Suit::Clubs:
            line(point(0, -.48f));
            curve(point(.23f, -.48f), point(.31f, -.22f), point(.13f, -.09f), 8);
            curve(point(.44f, -.21f), point(.59f, .12f), point(.36f, .27f), 8);
            curve(point(.24f, .34f), point(.11f, .27f), point(.07f, .17f), 6);
            curve(point(.075f, .31f), point(.13f, .43f), point(.18f, .49f), 4);
            line(point(-.18f, .49f));
            curve(point(-.13f, .43f), point(-.075f, .31f), point(-.07f, .17f), 4);
            curve(point(-.11f, .27f), point(-.24f, .34f), point(-.36f, .27f), 6);
            curve(point(-.59f, .12f), point(-.44f, -.21f), point(-.13f, -.09f), 8);
            curve(point(-.31f, -.22f), point(-.23f, -.48f), point(0, -.48f), 8, true);
            break;

        case bj::Suit::Spades:
            line(point(0, -.49f));
            curve(point(.13f, -.29f), point(.44f, -.06f), point(.44f, .11f), 8);
            curve(point(.44f, .34f), point(.16f, .42f), point(.07f, .19f), 8);
            curve(point(.075f, .34f), point(.13f, .43f), point(.18f, .49f), 4);
            line(point(-.18f, .49f));
            curve(point(-.13f, .43f), point(-.075f, .34f), point(-.07f, .19f), 4);
            curve(point(-.16f, .42f), point(-.44f, .34f), point(-.44f, .11f), 8);
            curve(point(-.44f, -.06f), point(-.13f, -.29f), point(0, -.49f), 8, true);
            break;
    }
    draw->AddConcavePolyFilled(outline, count, color);
}

} // namespace presentation
