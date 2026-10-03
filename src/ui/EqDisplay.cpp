#include "ui/EqDisplay.h"

#include <cmath>

#include "ui/Theme.h"

namespace ssc::ui
{
EqDisplay::EqDisplay (plugin::AnalyzerService& analyzerToUse)
    : analyzer (analyzerToUse),
      vblank (this, [this] { refreshFromAnalyzer(); })
{
    setOpaque (true);
}

void EqDisplay::refreshFromAnalyzer()
{
    if (analyzer.frames().update())
    {
        hasFrame = true;
        repaint();
    }
}

float EqDisplay::xForFrequency (float hz) const noexcept
{
    return static_cast<float> (getWidth()) * std::log (hz / kMinHz) / std::log (kMaxHz / kMinHz);
}

float EqDisplay::frequencyForX (float x) const noexcept
{
    return kMinHz * std::pow (kMaxHz / kMinHz, x / static_cast<float> (juce::jmax (1, getWidth())));
}

float EqDisplay::yForEqDb (float db) const noexcept
{
    const auto h = static_cast<float> (getHeight());
    return h * 0.5f - (db / kEqRangeDb) * h * 0.45f;
}

float EqDisplay::yForAnalyzerDb (float db) const noexcept
{
    const auto h = static_cast<float> (getHeight());
    return juce::jmap (db, kAnalyzerTopDb, kAnalyzerBottomDb, 0.0f, h);
}

void EqDisplay::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    g.setGradientFill (juce::ColourGradient (colours::displayTop, 0.0f, 0.0f, colours::displayBottom, 0.0f, bounds.getBottom(), false));
    g.fillRect (bounds);

    paintGrid (g);

    if (hasFrame)
    {
        const auto& frame = analyzer.frames().read();
        if (frame.numPoints > 1)
        {
            paintSpectrum (g, frame, false);
            paintSpectrum (g, frame, true);
        }
    }

    // 0 dB EQ line on top of the spectrum.
    g.setColour (colours::gridStrong.brighter (0.2f));
    g.drawHorizontalLine (juce::roundToInt (yForEqDb (0.0f)), 0.0f, bounds.getRight());
}

void EqDisplay::paintGrid (juce::Graphics& g) const
{
    const auto w = static_cast<float> (getWidth());
    const auto h = static_cast<float> (getHeight());
    g.setFont (uiFont (11.0f));

    static constexpr int lines[] = { 20, 30, 40, 50, 60, 80, 100, 200, 300, 400, 500, 600, 800,
                                     1000, 2000, 3000, 4000, 5000, 6000, 8000, 10000, 20000 };
    for (auto hz : lines)
    {
        const bool major = hz == 100 || hz == 1000 || hz == 10000;
        g.setColour (major ? colours::gridStrong : colours::grid);
        g.drawVerticalLine (juce::roundToInt (xForFrequency (static_cast<float> (hz))), 0.0f, h);
    }

    static constexpr std::pair<float, const char*> labels[] = { { 20.0f, "20" }, { 50.0f, "50" }, { 100.0f, "100" }, { 200.0f, "200" },
                                                                { 500.0f, "500" }, { 1000.0f, "1k" }, { 2000.0f, "2k" }, { 5000.0f, "5k" },
                                                                { 10000.0f, "10k" }, { 20000.0f, "20k" } };
    g.setColour (colours::textDim);
    for (const auto& [hz, text] : labels)
        g.drawText (text, juce::Rectangle<float> (xForFrequency (hz) - 20.0f, h - 16.0f, 40.0f, 14.0f), juce::Justification::centred);

    const int rangeDb = juce::roundToInt (kEqRangeDb);
    for (int db = -rangeDb; db <= rangeDb; db += 6)
    {
        const float y = yForEqDb (static_cast<float> (db));
        g.setColour (db == 0 ? colours::gridStrong : colours::grid);
        g.drawHorizontalLine (juce::roundToInt (y), 0.0f, w);
        if (db != 0)
        {
            g.setColour (colours::textDim);
            g.drawText ((db > 0 ? "+" : "") + juce::String (db), juce::Rectangle<float> (w - 30.0f, y - 7.0f, 26.0f, 14.0f),
                        juce::Justification::centredRight);
        }
    }
}

void EqDisplay::paintSpectrum (juce::Graphics& g, const plugin::SpectrumFrame& frame, bool post) const
{
    const auto h = static_cast<float> (getHeight());
    juce::Path line;
    bool started = false;

    for (int i = 0; i < frame.numPoints; ++i)
    {
        const auto idx = static_cast<std::size_t> (i);
        const float hz = frame.hz[idx];
        if (hz < kMinHz || hz > kMaxHz)
            continue;

        const float x = xForFrequency (hz);
        const float y = juce::jlimit (0.0f, h, yForAnalyzerDb (post ? frame.postDb[idx] : frame.preDb[idx]));
        if (! started)
        {
            line.startNewSubPath (x, y);
            started = true;
        }
        else
        {
            line.lineTo (x, y);
        }
    }

    if (! started)
        return;

    juce::Path fill (line);
    fill.lineTo (line.getCurrentPosition().x, h);
    fill.lineTo (line.getBounds().getX(), h);
    fill.closeSubPath();

    if (post)
    {
        g.setGradientFill (juce::ColourGradient (colours::spectrumPost.withAlpha (0.22f), 0.0f, 0.0f,
                                                 colours::spectrumPost.withAlpha (0.02f), 0.0f, h, false));
        g.fillPath (fill);
        g.setColour (colours::spectrumPost.withAlpha (0.85f));
        g.strokePath (line, juce::PathStrokeType (1.3f));
    }
    else
    {
        g.setColour (colours::spectrumPre.withAlpha (0.55f));
        g.fillPath (fill);
    }
}
} // namespace ssc::ui
