#include "Meters.h"
#include "UiUtils.h"

LevelMeter::LevelMeter (int numChannels, bool scale)
    : channels ((size_t) juce::jmax (1, numChannels)), showScale (scale)
{
    setInterceptsMouseClicks (true, false);
}

float LevelMeter::dbToFraction (float db)
{
    return juce::jlimit (0.0f, 1.0f, (db - minDb) / (maxDb - minDb));
}

void LevelMeter::setLevels (int channel, float peak, float rms)
{
    if (juce::isPositiveAndBelow (channel, (int) channels.size()))
    {
        auto& c = channels[(size_t) channel];
        c.newPeak = juce::jmax (c.newPeak, peak);
        c.newRms = juce::jmax (c.newRms, rms);
    }
}

void LevelMeter::tick()
{
    bool changed = false;
    for (auto& c : channels)
    {
        const float peak = juce::jmax (c.newPeak, c.peak * 0.80f);      // caída rápida
        const float rms = c.rms * 0.7f + c.newRms * 0.3f;               // integración ~100 ms
        if (c.newPeak >= c.hold)
        {
            c.hold = c.newPeak;
            c.holdTicks = 45;                                            // 1,5 s a 30 Hz
        }
        else if (--c.holdTicks <= 0)
            c.hold *= 0.9f;
        if (c.newPeak >= 1.0f)
            c.clipped = true;
        changed = changed || std::abs (peak - c.peak) > 1.0e-4f || std::abs (rms - c.rms) > 1.0e-4f;
        c.peak = peak;
        c.rms = rms;
        c.newPeak = 0.0f;
        c.newRms = 0.0f;
    }
    if (changed)
        repaint();
}

void LevelMeter::resetClip()
{
    for (auto& c : channels)
        c.clipped = false;
    repaint();
}

void LevelMeter::paint (juce::Graphics& g)
{
    auto area = getLocalBounds();
    if (showScale)
        area.removeFromRight (22);

    const int n = (int) channels.size();
    const int gap = 2;
    const int barW = juce::jmax (2, (area.getWidth() - gap * (n - 1)) / n);
    const int ledH = 6;

    for (int i = 0; i < n; ++i)
    {
        auto& c = channels[(size_t) i];
        auto bar = juce::Rectangle<int> (area.getX() + i * (barW + gap), area.getY(), barW, area.getHeight());
        auto led = bar.removeFromTop (ledH);
        bar.removeFromTop (2);

        // Clip
        g.setColour (c.clipped ? juce::Colours::red : juce::Colour (0xff3a1f1f));
        g.fillRoundedRectangle (led.toFloat(), 1.5f);

        // Fondo
        g.setColour (juce::Colours::black.withAlpha (0.55f));
        g.fillRect (bar);

        const float h = (float) bar.getHeight();
        auto fracToY = [&] (float frac) { return (float) bar.getBottom() - frac * h; };

        // Degradado verde / amarillo / rojo recortado por el RMS
        const float rmsDb = juce::Decibels::gainToDecibels (c.rms, minDb);
        const float rmsY = fracToY (dbToFraction (rmsDb));
        if (rmsY < (float) bar.getBottom())
        {
            juce::ColourGradient grad (juce::Colour (0xff43a047), 0.0f, (float) bar.getBottom(),
                                       juce::Colour (0xffe53935), 0.0f, fracToY (dbToFraction (maxDb)), false);
            grad.addColour ((double) dbToFraction (-12.0f), juce::Colour (0xffc0ca33));
            grad.addColour ((double) dbToFraction (-3.0f), juce::Colour (0xfffb8c00));
            g.setGradientFill (grad);
            g.fillRect (juce::Rectangle<float> ((float) bar.getX(), rmsY, (float) bar.getWidth(), (float) bar.getBottom() - rmsY));
        }

        // Pico (línea) y retención
        const float peakDb = juce::Decibels::gainToDecibels (c.peak, minDb);
        if (peakDb > minDb)
        {
            g.setColour (juce::Colours::white.withAlpha (0.85f));
            g.fillRect (juce::Rectangle<float> ((float) bar.getX(), fracToY (dbToFraction (peakDb)) - 1.0f, (float) bar.getWidth(), 2.0f));
        }
        const float holdDb = juce::Decibels::gainToDecibels (c.hold, minDb);
        if (holdDb > minDb)
        {
            g.setColour (holdDb >= 0.0f ? juce::Colours::red : juce::Colours::white);
            g.fillRect (juce::Rectangle<float> ((float) bar.getX(), fracToY (dbToFraction (holdDb)) - 0.5f, (float) bar.getWidth(), 1.0f));
        }
    }

    if (showScale)
    {
        auto scale = getLocalBounds().removeFromRight (22);
        scale.removeFromTop (ledH + 2);
        g.setFont (ui::font (9.0f));
        for (float db : { 0.0f, -6.0f, -12.0f, -24.0f, -48.0f })
        {
            const float y = (float) scale.getBottom() - dbToFraction (db) * (float) scale.getHeight();
            g.setColour (juce::Colours::grey);
            g.fillRect (juce::Rectangle<float> ((float) scale.getX(), y, 4.0f, 1.0f));
            g.drawText (juce::String ((int) db), scale.getX() + 5, (int) y - 6, 18, 12, juce::Justification::centredLeft);
        }
    }
}
