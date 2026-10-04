#include "ui/MeterView.h"

#include "ui/Theme.h"

namespace ssc::ui
{
MeterView::MeterView()
{
    setTooltip ("THD+N and aliasing of the processing, relative to the output");
    startTimerHz (10);
}

void MeterView::setThresholds (float thdWarningDb, float aliasingWarningDb)
{
    thdWarning = thdWarningDb;
    aliasingWarning = aliasingWarningDb;
    updateTooltip();
    repaint();
}

juce::String MeterView::formatDb (float db)
{
    if (db < plugin::kMeterFloorDb)
        return juce::String::fromUTF8 ("< \xe2\x88\x92") + juce::String (juce::roundToInt (-plugin::kMeterFloorDb)) + " dB";
    const auto text = juce::String (std::abs (db), 1) + " dB";
    return db < 0.0f ? juce::String::fromUTF8 ("\xe2\x88\x92") + text : text;
}

juce::String MeterView::formatPercent (float db)
{
    const double percent = 100.0 * std::pow (10.0, db / 20.0);
    if (percent < 0.001)
        return "< 0.001 %";
    return juce::String (percent, percent < 0.1 ? 3 : 2) + " %";
}

void MeterView::refresh()
{
    if (! readingsProvider)
        return;
    const auto next = readingsProvider();
    if (next.serial == readings.serial)
        return;
    readings = next;

    const int suggestion = plugin::suggestedOversampling (readings);
    if (suggestion != hint)
    {
        hint = suggestion;
        if (onOversamplingHint)
            onOversamplingHint (hint, readings.aliasingDb);
    }
    updateTooltip();
    repaint();
}

void MeterView::updateTooltip()
{
    juce::String text = "THD+N and aliasing of the processing, relative to the output (300 ms average).";
    if (readings.valid)
    {
        for (int b = 0; b < kNumBands; ++b)
        {
            const auto i = static_cast<std::size_t> (b);
            if (! readings.bandMeasured[i] || readings.bandThdDb[i] < plugin::kMeterFloorDb)
                continue;
            text << "\nBand " << (b + 1);
            if (bandDescription)
                text << " (" << bandDescription (b) << ")";
            text << ": THD+N " << formatDb (readings.bandThdDb[i]) << ", aliasing " << formatDb (readings.bandAliasingDb[i]);
        }
    }
    else
    {
        text << "\nNo signal.";
    }
    text << "\nWarning colour above " << formatDb (thdWarning) << " (THD+N) and " << formatDb (aliasingWarning)
         << " (aliasing): Analyzer menu.";
    if (hint > 0)
        text << "\nAliasing above " << formatDb (plugin::kOversamplingHintDb) << ": Oversampling " << hint << "x would reduce it.";
    setTooltip (text);
}

void MeterView::paint (juce::Graphics& g)
{
    auto area = getLocalBounds().reduced (2, 4);
    const int lineHeight = area.getHeight() / 2;
    const int labelWidth = juce::jmin (48, area.getWidth() / 3);

    auto line = [&] (juce::Rectangle<int> row, const juce::String& label, const juce::String& value, bool warn) {
        g.setFont (uiFont (11.0f));
        g.setColour (colours::textDim);
        g.drawText (label, row.removeFromLeft (labelWidth), juce::Justification::centredLeft);
        g.setFont (uiFont (12.0f));
        g.setColour (warn ? colours::warning : colours::text);
        g.drawText (value, row, juce::Justification::centredLeft, true);
    };

    const juce::String dash = juce::String::fromUTF8 ("\xe2\x80\x94");
    const bool valid = readings.valid;
    juce::String thd = valid ? formatDb (readings.thdDb) : dash;
    if (valid && readings.thdDb >= plugin::kMeterFloorDb)
        thd << "  " << formatPercent (readings.thdDb);
    line (area.removeFromTop (lineHeight), "THD+N", thd, valid && readings.thdDb > thdWarning);
    line (area, "Alias", valid ? formatDb (readings.aliasingDb) : dash, valid && readings.aliasingDb > aliasingWarning);
}
} // namespace ssc::ui
