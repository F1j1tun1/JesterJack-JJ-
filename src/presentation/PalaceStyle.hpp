#pragma once

#include <imgui.h>

#include <algorithm>

// Shared decoration for the palace's existing layouts. These helpers never
// reserve space or move a cursor: typography, card geometry and input stay owned
// by the screen. All ornaments respect the caller's fade / disabled alpha.
namespace palace {

inline ImU32 ink(float r, float g, float b, float alpha = 1.0f) {
    return ImGui::ColorConvertFloat4ToU32({r, g, b,
        std::clamp(alpha * ImGui::GetStyle().Alpha, 0.0f, 1.0f)});
}

inline void diamond(ImDrawList* draw, ImVec2 center, float radius, ImU32 color) {
    draw->AddQuadFilled({center.x, center.y - radius}, {center.x + radius, center.y},
                        {center.x, center.y + radius}, {center.x - radius, center.y}, color);
}

inline void applyStyle() {
    auto& style = ImGui::GetStyle();
    // Preserve the established fonts, padding, spacing, scrollbar widths and
    // minimum sizes so the same eight-slot layout fits at 720p.
    style.WindowRounding = 7.0f;
    style.ChildRounding = 5.0f;
    style.FrameRounding = 4.0f;
    style.PopupRounding = 6.0f;
    style.ScrollbarRounding = 7.0f;
    style.GrabRounding = 3.0f;
    style.TabRounding = 4.0f;
    style.WindowBorderSize = 1.0f;
    style.ChildBorderSize = 1.0f;
    style.PopupBorderSize = 1.0f;
    style.FrameBorderSize = 1.0f;
    style.DisabledAlpha = .48f;

    auto* c = style.Colors;
    const ImVec4 parchment{.97f, .92f, .82f, 1.0f};
    const ImVec4 gold{.83f, .66f, .37f, 1.0f};
    c[ImGuiCol_Text] = parchment;
    c[ImGuiCol_TextDisabled] = {.62f, .60f, .57f, 1.0f};
    c[ImGuiCol_WindowBg] = {.052f, .073f, .073f, .985f};
    c[ImGuiCol_ChildBg] = {.074f, .084f, .085f, .97f};
    c[ImGuiCol_PopupBg] = {.075f, .058f, .083f, .985f};
    c[ImGuiCol_Border] = {.44f, .35f, .23f, .72f};
    c[ImGuiCol_BorderShadow] = {0, 0, 0, .18f};
    c[ImGuiCol_FrameBg] = {.16f, .12f, .17f, 1};
    c[ImGuiCol_FrameBgHovered] = {.23f, .18f, .24f, 1};
    c[ImGuiCol_FrameBgActive] = {.30f, .22f, .28f, 1};
    c[ImGuiCol_TitleBg] = {.12f, .075f, .13f, 1};
    c[ImGuiCol_TitleBgActive] = {.20f, .115f, .19f, 1};
    c[ImGuiCol_TitleBgCollapsed] = {.10f, .065f, .11f, .95f};
    c[ImGuiCol_MenuBarBg] = {.10f, .085f, .11f, 1};
    c[ImGuiCol_ScrollbarBg] = {.045f, .038f, .055f, .75f};
    c[ImGuiCol_ScrollbarGrab] = {.34f, .28f, .23f, 1};
    c[ImGuiCol_ScrollbarGrabHovered] = {.49f, .39f, .26f, 1};
    c[ImGuiCol_ScrollbarGrabActive] = gold;
    c[ImGuiCol_CheckMark] = {.92f, .75f, .43f, 1};
    c[ImGuiCol_SliderGrab] = gold;
    c[ImGuiCol_SliderGrabActive] = parchment;
    c[ImGuiCol_Button] = {.115f, .205f, .18f, 1};
    c[ImGuiCol_ButtonHovered] = {.18f, .29f, .25f, 1};
    c[ImGuiCol_ButtonActive] = {.075f, .155f, .13f, 1};
    c[ImGuiCol_Header] = {.235f, .17f, .235f, .94f};
    c[ImGuiCol_HeaderHovered] = {.31f, .23f, .29f, .96f};
    c[ImGuiCol_HeaderActive] = {.36f, .27f, .29f, 1};
    c[ImGuiCol_Separator] = {.49f, .39f, .25f, .62f};
    c[ImGuiCol_SeparatorHovered] = {.74f, .57f, .32f, .92f};
    c[ImGuiCol_SeparatorActive] = gold;
    c[ImGuiCol_ResizeGrip] = {.50f, .37f, .21f, .18f};
    c[ImGuiCol_ResizeGripHovered] = {.68f, .51f, .29f, .6f};
    c[ImGuiCol_ResizeGripActive] = gold;
    c[ImGuiCol_Tab] = {.14f, .11f, .17f, 1};
    c[ImGuiCol_TabHovered] = {.28f, .20f, .28f, 1};
    c[ImGuiCol_TabSelected] = {.23f, .15f, .23f, 1};
    c[ImGuiCol_TabSelectedOverline] = gold;
    c[ImGuiCol_TabDimmed] = {.09f, .075f, .11f, 1};
    c[ImGuiCol_TabDimmedSelected] = {.16f, .12f, .18f, 1};
    c[ImGuiCol_TabDimmedSelectedOverline] = {.46f, .35f, .23f, 1};
    c[ImGuiCol_PlotLines] = {.75f, .68f, .53f, 1};
    c[ImGuiCol_PlotLinesHovered] = parchment;
    c[ImGuiCol_PlotHistogram] = gold;
    c[ImGuiCol_PlotHistogramHovered] = {.96f, .80f, .46f, 1};
    c[ImGuiCol_TableHeaderBg] = {.16f, .11f, .16f, 1};
    c[ImGuiCol_TableBorderStrong] = {.40f, .30f, .21f, .9f};
    c[ImGuiCol_TableBorderLight] = {.28f, .23f, .21f, .6f};
    c[ImGuiCol_TableRowBg] = {0, 0, 0, 0};
    c[ImGuiCol_TableRowBgAlt] = {.38f, .26f, .32f, .055f};
    c[ImGuiCol_TextSelectedBg] = {.36f, .27f, .18f, .7f};
    c[ImGuiCol_DragDropTarget] = {.96f, .79f, .42f, .95f};
    c[ImGuiCol_NavCursor] = {.97f, .80f, .47f, 1};
    c[ImGuiCol_NavWindowingHighlight] = {.93f, .80f, .55f, .8f};
    c[ImGuiCol_NavWindowingDimBg] = {.025f, .015f, .035f, .6f};
    c[ImGuiCol_ModalWindowDimBg] = {.025f, .015f, .035f, .72f};
}

inline void panelFrame(ImDrawList* draw, ImVec2 pos, ImVec2 size, bool prominent = false) {
    if (!draw || size.x < 48 || size.y < 32) return;
    const ImVec2 end{pos.x + size.x, pos.y + size.y};
    const float reach = prominent ? 26.0f : 16.0f;
    const auto trim = ink(.80f, .63f, .35f, prominent ? .68f : .40f);
    draw->AddRect({pos.x + 3, pos.y + 3}, {end.x - 3, end.y - 3},
                  ink(.69f, .53f, .31f, prominent ? .23f : .12f), 5, 0, 1);
    for (int corner = 0; corner < 4; ++corner) {
        const float sx = corner % 2 == 0 ? 1.0f : -1.0f;
        const float sy = corner < 2 ? 1.0f : -1.0f;
        const ImVec2 p{sx > 0 ? pos.x + 6 : end.x - 6,
                       sy > 0 ? pos.y + 6 : end.y - 6};
        draw->AddLine({p.x + sx * 4, p.y}, {p.x + sx * reach, p.y}, trim, 1);
        draw->AddLine({p.x, p.y + sy * 4}, {p.x, p.y + sy * reach}, trim, 1);
        diamond(draw, {p.x + sx * 1.5f, p.y + sy * 1.5f}, prominent ? 2.7f : 2.0f,
                corner % 2 ? ink(.32f, .51f, .48f, .66f) : ink(.63f, .25f, .36f, .66f));
    }
    if (prominent && size.x >= 250 && size.y >= 100) {
        const float middle = pos.x + size.x * .5f;
        draw->AddLine({middle - 26, pos.y + 6}, {middle - 7, pos.y + 6}, trim);
        draw->AddLine({middle + 7, pos.y + 6}, {middle + 26, pos.y + 6}, trim);
        diamond(draw, {middle, pos.y + 6}, 3, ink(.88f, .69f, .38f, .8f));
    }
}

// Call immediately after the corresponding item so keyboard focus can share its
// visible gold outline. Pointer inspection is never used to activate a button.
inline void buttonFrame(ImDrawList* draw, ImVec2 min, ImVec2 max,
                        bool hovered, bool active, bool disabled) {
    if (!draw || max.x - min.x < 28 || max.y - min.y < 18) return;
    const bool focused = !disabled && ImGui::GetIO().NavVisible && ImGui::IsItemFocused();
    const float strength = disabled ? .23f : active ? .86f : hovered || focused ? .76f : .30f;
    const auto edge = ink(.91f, .74f, .44f, strength);
    draw->AddRect({min.x + 1, min.y + 1}, {max.x - 1, max.y - 1}, edge, 4, 0,
                  focused ? 1.6f : 1.0f);
    draw->AddLine({min.x + 9, min.y + 3}, {max.x - 9, min.y + 3},
                  ink(.98f, .87f, .64f, disabled ? .10f : active ? .09f : .20f));
    if (max.x - min.x >= 96 && max.y - min.y >= 32) {
        const float y = max.y - 6;
        diamond(draw, {min.x + 7, y}, 1.6f, ink(.64f, .27f, .36f, strength));
        diamond(draw, {max.x - 7, y}, 1.6f, ink(.34f, .54f, .48f, strength));
    }
    if (focused) draw->AddRect({min.x + 4, min.y + 4}, {max.x - 4, max.y - 4},
                              ink(1.0f, .88f, .60f, .42f), 2, 0, 1);
}

inline void sectionRule(ImDrawList* draw, ImVec2 pos, float width) {
    if (!draw || width < 30) return;
    const float middle = pos.x + width * .5f;
    const auto line = ink(.73f, .56f, .32f, .44f);
    draw->AddLine(pos, {middle - 7, pos.y}, line);
    draw->AddLine({middle + 7, pos.y}, {pos.x + width, pos.y}, line);
    diamond(draw, {middle, pos.y}, 2.7f, ink(.84f, .66f, .37f, .72f));
}

// Inner face for the sixteen new curiosities. The screen draws the outer rim
// and shadow; these simple silhouettes remain distinct at thumbnail size.
inline void relicSigil(ImDrawList* draw, ImVec2 center, float radius, int id, ImU32 accent) {
    if (!draw || radius <= 0) return;
    const auto p = [&](float x, float y) { return ImVec2{center.x + x * radius, center.y + y * radius}; };
    const float stroke = std::max(1.15f, radius * .055f);
    ImVec4 tint = ImGui::ColorConvertU32ToFloat4(accent);
    tint.w *= ImGui::GetStyle().Alpha;
    const ImU32 gold = ImGui::ColorConvertFloat4ToU32(tint);
    const ImU32 face = ink(.075f, .055f, .09f, 1);
    const ImU32 scarlet = ink(.59f, .17f, .27f, .94f);
    const ImU32 jade = ink(.24f, .49f, .46f, .94f);
    draw->AddCircleFilled(center, radius * .82f, face, 48);
    for (int segment = 0; segment < 12; ++segment) {
        const float a = segment * .52359878f;
        draw->PathArcTo(center, radius * .85f, a + .08f, a + .33f, 4);
        draw->PathStroke(segment % 2 ? ink(.34f, .20f, .36f, .92f) : scarlet, 0, radius * .105f);
    }
    draw->AddCircle(center, radius * .72f, gold, 48, std::max(.8f, radius * .018f));
    const auto line = [&](float ax, float ay, float bx, float by) { draw->AddLine(p(ax, ay), p(bx, by), gold, stroke); };
    const auto card = [&](float x, float y, float width, float height) {
        draw->AddRectFilled(p(x, y), p(x + width, y + height), face, radius * .04f);
        draw->AddRect(p(x, y), p(x + width, y + height), gold, radius * .04f, 0, stroke * .72f);
    };
    const auto heart = [&](float x, float y, float size, ImU32 color) {
        draw->AddCircleFilled(p(x - size * .24f, y - size * .14f), size * radius * .31f, color, 16);
        draw->AddCircleFilled(p(x + size * .24f, y - size * .14f), size * radius * .31f, color, 16);
        draw->AddTriangleFilled(p(x - size * .52f, y - size * .02f), p(x + size * .52f, y - size * .02f), p(x, y + size * .60f), color);
    };
    const auto star = [&](float x, float y, float size) {
        diamond(draw, p(x, y), radius * size, gold);
        draw->AddLine(p(x - size * 1.45f, y), p(x + size * 1.45f, y), gold, std::max(.8f, stroke * .45f));
    };
    const auto crown = [&](float y, float size) {
        draw->PathLineTo(p(-size, y - size * .24f));
        draw->PathLineTo(p(-size * .48f, y + size * .12f));
        draw->PathLineTo(p(0, y - size * .62f));
        draw->PathLineTo(p(size * .48f, y + size * .12f));
        draw->PathLineTo(p(size, y - size * .24f));
        draw->PathLineTo(p(size * .75f, y + size * .57f));
        draw->PathLineTo(p(-size * .75f, y + size * .57f));
        draw->PathFillConcave(gold);
        draw->AddLine(p(-size * .68f, y + size * .34f), p(size * .68f, y + size * .34f), face, stroke * .6f);
    };
    switch (id) {
    case 24: { // Rewind: a backwards arrow on a broken clock ring.
        draw->PathArcTo(p(.03f, .03f), radius * .42f, -.65f, 4.05f, 26);
        draw->PathStroke(gold, 0, stroke * 1.25f);
        draw->AddTriangleFilled(p(-.53f, -.20f), p(-.15f, -.24f), p(-.37f, -.51f), gold);
        line(.03f, .03f, .03f, -.21f);line(.03f, .03f, .22f, .15f);
        break;
    }
    case 25: // Redraw: two cards and an exchange arrow.
        card(-.42f, -.47f, .48f, .63f);card(-.04f, -.16f, .48f, .63f);
        diamond(draw, p(.20f, .15f), radius * .10f, scarlet);
        line(-.49f, .34f, -.12f, .34f);
        draw->AddTriangleFilled(p(-.06f, .34f), p(-.24f, .20f), p(-.24f, .48f), gold);
        break;
    case 26: // Forked Path: a deliberate split into two face-up cards.
        line(0, .49f, 0, .12f);line(0, .12f, -.30f, -.15f);line(0, .12f, .30f, -.15f);
        card(-.48f, -.51f, .34f, .44f);card(.14f, -.51f, .34f, .44f);
        diamond(draw, p(-.31f, -.30f), radius * .07f, scarlet);
        diamond(draw, p(.31f, -.30f), radius * .07f, jade);
        break;
    case 27: // Lifeline: heart protected by a shield.
        draw->PathLineTo(p(-.43f, -.40f));draw->PathLineTo(p(0, -.53f));draw->PathLineTo(p(.43f, -.40f));
        draw->PathLineTo(p(.35f, .22f));draw->PathLineTo(p(0, .54f));draw->PathLineTo(p(-.35f, .22f));
        draw->PathStroke(gold, ImDrawFlags_Closed, stroke);
        heart(0, -.05f, .48f, scarlet);
        break;
    case 28: // Velvet Dividend: coins spilling from a purse.
        draw->AddCircleFilled(p(-.18f, .18f), radius * .28f, gold, 24);
        draw->AddCircle(p(-.18f, .18f), radius * .17f, face, 24, stroke * .6f);
        draw->AddCircleFilled(p(.23f, -.15f), radius * .24f, gold, 24);
        draw->AddCircle(p(.23f, -.15f), radius * .14f, face, 24, stroke * .6f);
        star(-.30f, -.40f, .10f);
        break;
    case 29: // Lucky Seven.
        line(-.34f, -.43f, .36f, -.43f);line(.36f, -.43f, -.12f, .46f);
        line(-.10f, .01f, .30f, .01f);star(-.42f, .30f, .11f);
        break;
    case 30: // Twin Sigil.
        diamond(draw, p(-.20f, -.06f), radius * .34f, gold);
        diamond(draw, p(.22f, .09f), radius * .34f, gold);
        diamond(draw, p(-.20f, -.06f), radius * .20f, face);
        diamond(draw, p(.22f, .09f), radius * .20f, face);
        break;
    case 31: // Mixed Company: four different court marks.
        heart(-.25f, -.25f, .30f, scarlet);
        diamond(draw, p(.28f, -.22f), radius * .18f, gold);
        draw->AddCircleFilled(p(-.26f, .26f), radius * .16f, jade, 20);
        draw->AddTriangleFilled(p(.27f, .08f), p(.08f, .44f), p(.46f, .44f), gold);
        break;
    case 32: // Crescendo.
        for (int i = 0; i < 4; ++i)
            draw->AddRectFilled(p(-.44f + i * .24f, .20f - i * .20f), p(-.30f + i * .24f, .43f), gold, radius * .025f);
        star(-.30f, -.38f, .10f);
        break;
    case 33: // Ace High.
        line(-.33f, .44f, 0, -.28f);line(0, -.28f, .33f, .44f);line(-.21f, .16f, .21f, .16f);
        star(0, -.49f, .12f);
        break;
    case 34: // Silver Lining.
        draw->AddCircleFilled(p(-.26f, .13f), radius * .23f, gold, 24);
        draw->AddCircleFilled(p(.01f, .02f), radius * .30f, gold, 24);
        draw->AddCircleFilled(p(.30f, .16f), radius * .20f, gold, 24);
        draw->AddRectFilled(p(-.29f, .17f), p(.32f, .34f), gold);
        line(-.42f, -.34f, -.30f, -.22f);line(0, -.55f, 0, -.38f);line(.42f, -.34f, .30f, -.22f);
        break;
    case 35: // Loaded Dice.
        draw->AddRect(p(-.43f, -.43f), p(.43f, .43f), gold, radius * .11f, 0, stroke);
        for (const ImVec2 dot : {ImVec2{-.22f,-.22f},ImVec2{.22f,-.22f},ImVec2{0,0},ImVec2{-.22f,.22f},ImVec2{.22f,.22f}})
            draw->AddCircleFilled(p(dot.x, dot.y), radius * .065f, gold, 12);
        break;
    case 36: // Alchemist.
        draw->PathLineTo(p(-.14f, -.47f));draw->PathLineTo(p(.14f, -.47f));draw->PathLineTo(p(.14f, -.12f));
        draw->PathLineTo(p(.43f, .39f));draw->PathLineTo(p(-.43f, .39f));draw->PathLineTo(p(-.14f, -.12f));
        draw->PathStroke(gold, ImDrawFlags_Closed, stroke);
        draw->AddTriangleFilled(p(-.29f, .28f), p(.29f, .28f), p(0, -.06f), jade);
        draw->AddCircleFilled(p(.28f, -.28f), radius * .07f, gold, 12);
        break;
    case 37: // Phoenix: rising wings, tail and ember.
        draw->AddTriangleFilled(p(-.50f, -.30f), p(-.12f, .30f), p(-.07f, -.04f), gold);
        draw->AddTriangleFilled(p(.50f, -.30f), p(.12f, .30f), p(.07f, -.04f), gold);
        draw->AddTriangleFilled(p(0, .48f), p(-.13f, .05f), p(.13f, .05f), scarlet);
        draw->AddCircleFilled(p(0, -.18f), radius * .13f, gold, 16);
        star(0, -.47f, .11f);
        break;
    case 38: // Royal Decree: sealed parchment under a crown.
        card(-.31f, -.04f, .62f, .47f);line(-.17f, .08f, .17f, .08f);line(-.17f, .21f, .06f, .21f);
        crown(-.29f, .34f);diamond(draw, p(.20f, .38f), radius * .12f, scarlet);
        break;
    default: // Grand Finale: a radiant final crown.
        crown(.04f, .48f);star(-.44f, -.39f, .11f);star(.44f, -.39f, .11f);star(0, -.53f, .10f);
        break;
    }
}
} // namespace palace
