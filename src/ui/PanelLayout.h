#pragma once

#include <vector>

#include <juce_graphics/juce_graphics.h>

namespace ssc::ui
{
/** Where the band panel and its Advanced flyout go (research §15 d4). */
struct BandPanelDock
{
    juce::Rectangle<int> panel;
    juce::Rectangle<int> flyout; ///< empty while the flyout is closed
    bool flyoutLeft = false;     ///< the flyout opens to the left of the panel
    bool atTop = false;          ///< docked at the top of the display
    bool coversNode = false;     ///< no position keeps the node visible (very small displays)
};

struct BandPanelDockRequest
{
    juce::Rectangle<int> area;   ///< the display
    juce::Point<int> panelSize;  ///< band panel
    juce::Point<int> flyoutSize; ///< Advanced flyout; x = 0 when closed
    juce::Point<float> node;     ///< node of the focused band (display coordinates of `area`)
    int bottomInset = 22;        ///< frequency scale or the global Advanced drawer below the panel
    int topInset = 8;            ///< above a top-docked panel
    int margin = 8;              ///< to the sides of the display
    int gap = 4;                 ///< between panel and flyout
    float nodeClearance = 12.0f; ///< the node stays this far outside the panel
};

/**
    Docks the panel at the bottom of the display, centred under the node and kept `margin` inside;
    the flyout opens to the right of the panel (the pair moves left near the right edge). When that
    would cover the node, the flyout opens to the left instead; then the pair docks at the top
    (flyout right, then left); then it moves beside the node. When nothing keeps the node visible
    (a tall panel in a short display), it takes the position that covers the node least.
*/
BandPanelDock dockBandPanel (const BandPanelDockRequest& request);
} // namespace ssc::ui
