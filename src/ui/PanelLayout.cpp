#include "ui/PanelLayout.h"

#include <cmath>

namespace ssc::ui
{
namespace
{
    /** Panel and flyout with the pair's left edge at x and the panel's top at y. */
    BandPanelDock place (const BandPanelDockRequest& r, int x, int y, bool atTop, bool flyoutLeft)
    {
        BandPanelDock dock;
        dock.atTop = atTop;
        dock.flyoutLeft = flyoutLeft && r.flyoutSize.x > 0;
        const int panelX = dock.flyoutLeft ? x + r.flyoutSize.x + r.gap : x;
        dock.panel = { panelX, y, r.panelSize.x, r.panelSize.y };
        if (r.flyoutSize.x > 0)
        {
            // Bottom-docked: aligned at the panel's bottom; top-docked: at its top.
            const int flyY = atTop ? y : dock.panel.getBottom() - r.flyoutSize.y;
            const int flyX = dock.flyoutLeft ? x : dock.panel.getRight() + r.gap;
            dock.flyout = { flyX, flyY, r.flyoutSize.x, r.flyoutSize.y };
        }
        return dock;
    }

    bool covers (const BandPanelDock& dock, juce::Point<float> node, float clearance)
    {
        auto hit = [&] (juce::Rectangle<int> rect) { return ! rect.isEmpty() && rect.toFloat().expanded (clearance).contains (node); };
        return hit (dock.panel) || hit (dock.flyout);
    }

    /** How far the node lies inside the pair (0 when outside). */
    float depth (const BandPanelDock& dock, juce::Point<float> node, float clearance)
    {
        float worst = 0.0f;
        for (const auto& rect : { dock.panel, dock.flyout })
        {
            if (rect.isEmpty())
                continue;
            const auto r = rect.toFloat().expanded (clearance);
            if (! r.contains (node))
                continue;
            worst = juce::jmax (worst, juce::jmin (node.x - r.getX(), r.getRight() - node.x, node.y - r.getY(), r.getBottom() - node.y));
        }
        return worst;
    }
} // namespace

BandPanelDock dockBandPanel (const BandPanelDockRequest& r)
{
    const bool hasFlyout = r.flyoutSize.x > 0;
    const int pairWidth = r.panelSize.x + (hasFlyout ? r.gap + r.flyoutSize.x : 0);
    const int left = r.area.getX() + r.margin;
    const int right = r.area.getRight() - r.margin;
    auto clampX = [&] (int x) { return juce::jmax (left, juce::jmin (x, right - pairWidth)); };

    // Node in editor coordinates.
    const auto node = r.node + r.area.getPosition().toFloat();
    const int nodeX = juce::roundToInt (node.x);

    // Bottom: panel and flyout share their bottom edge above the inset. Top: below the top inset.
    const int bottomY = r.area.getBottom() - r.bottomInset - r.panelSize.y;
    const int topY = r.area.getY() + r.topInset;

    // In order of preference: the panel centred under the node at the bottom (the flyout to the
    // right, then to the left), the same at the top, then the pair beside the node.
    std::vector<BandPanelDock> candidates;
    for (const bool atTop : { false, true })
        for (const bool flyoutLeft : { false, true })
        {
            if (flyoutLeft && ! hasFlyout)
                continue;
            const int panelLeft = nodeX - r.panelSize.x / 2;
            const int pairX = flyoutLeft ? panelLeft - r.flyoutSize.x - r.gap : panelLeft;
            candidates.push_back (place (r, clampX (pairX), atTop ? topY : bottomY, atTop, flyoutLeft));
        }
    const int besideRight = static_cast<int> (std::ceil (node.x + r.nodeClearance)) + 1;
    const int besideLeft = static_cast<int> (std::floor (node.x - r.nodeClearance)) - 1 - pairWidth;
    for (const bool atTop : { false, true })
        for (const int x : { besideRight, besideLeft })
            if (x >= left && x + pairWidth <= right)
                candidates.push_back (place (r, x, atTop ? topY : bottomY, atTop, x < nodeX));

    for (const auto& candidate : candidates)
        if (! covers (candidate, node, r.nodeClearance))
            return candidate;

    // Nothing keeps the node visible (a tall panel in a short display): cover it least.
    auto least = candidates.front();
    for (const auto& candidate : candidates)
        if (depth (candidate, node, r.nodeClearance) < depth (least, node, r.nodeClearance))
            least = candidate;
    least.coversNode = true;
    return least;
}
} // namespace ssc::ui
