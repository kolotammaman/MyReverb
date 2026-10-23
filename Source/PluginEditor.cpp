/*
  ==============================================================================
    MyReverb  —  PluginEditor.cpp
  ==============================================================================
*/
#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
    constexpr float kPi = juce::MathConstants<float>::pi;

    // design-space layout (Content::kW x Content::kH)
    constexpr int kMargin   = 24;
    constexpr int kHeroY    = 70;
    constexpr int kHeroH    = 198;
    constexpr int kPanelY   = 284;
    constexpr int kPanelH   = 272;
    constexpr int kPanelGap = 12;
    constexpr int kPanelTop = 40;      // title strip inside a panel

    struct Section { const char* title; juce::Colour hue; };
    const Section kSections[4] = { { "SPACE",   Theme::space },
                                   { "TONE",    Theme::tone },
                                   { "TEXTURE", Theme::texture },
                                   { "OUTPUT",  Theme::output } };

    float dbOf (float lin) { return 20.0f * std::log10 (lin + 1.0e-9f); }
}

//==============================================================================
// Knob
//==============================================================================
Knob::Knob (juce::AudioProcessorValueTreeState& apvts, const juce::String& paramId,
            const juce::String& n, juce::Colour a, float bipolarCentre)
    : name (n.toUpperCase()), accent (a), attachment (apvts, paramId, slider)
{
    slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    slider.setRotaryParameters (kPi * 1.25f, kPi * 2.75f, true);
    slider.setMouseDragSensitivity (220);
    slider.setColour (juce::Slider::rotarySliderFillColourId, accent);
    slider.getProperties().set ("centre", bipolarCentre);

    if (auto* p = apvts.getParameter (paramId))
        slider.setDoubleClickReturnValue (true, p->convertFrom0to1 (p->getDefaultValue()));

    slider.addMouseListener (this, false);
    slider.onValueChange = [this] { repaint (0, slider.getBottom(), getWidth(), getHeight() - slider.getBottom()); };
    addAndMakeVisible (slider);
}

void Knob::resized()
{
    const int side = juce::jmin (getWidth(), getHeight() - 34);
    slider.setBounds ((getWidth() - side) / 2, 0, side, side);
}

void Knob::paint (juce::Graphics& g)
{
    const bool active = slider.isMouseOverOrDragging();
    const int  y      = slider.getBottom() - 4;

    g.setFont (Theme::font (10.0f, true, 0.16f));
    g.setColour (active ? Theme::textBright : Theme::textMid);
    g.drawText (name, 0, y, getWidth(), 14, juce::Justification::centred, false);

    g.setFont (Theme::font (12.0f, false, 0.02f));
    g.setColour (active ? accent : Theme::textDim.brighter (0.25f));
    g.drawText (slider.getTextFromValue (slider.getValue()), 0, y + 15, getWidth(), 16,
                juce::Justification::centred, false);
}

//==============================================================================
// Backdrop
//==============================================================================
Backdrop::Backdrop()
{
    setInterceptsMouseClicks (false, false);
    setOpaque (true);

    // film grain tile — breaks up gradient banding and gives the ink some tooth
    grain = juce::Image (juce::Image::ARGB, 192, 192, true);
    juce::Random r (42);
    for (int y = 0; y < grain.getHeight(); ++y)
        for (int x = 0; x < grain.getWidth(); ++x)
        {
            const float v = r.nextFloat();
            grain.setPixelAt (x, y, (v > 0.5f ? juce::Colours::white : juce::Colours::black)
                                        .withAlpha (std::abs (v - 0.5f) * 0.045f));
        }
}

void Backdrop::paint (juce::Graphics& g)
{
    const auto b = getLocalBounds().toFloat();

    // ink
    g.setGradientFill (juce::ColourGradient (Theme::bgTop, 0.0f, 0.0f, Theme::bgBottom, 0.0f, b.getHeight(), false));
    g.fillAll();

    // aurora glows
    auto glow = [&] (juce::Colour c, float cx, float cy, float rad, float alpha)
    {
        g.setGradientFill (juce::ColourGradient (c.withAlpha (alpha), cx, cy, c.withAlpha (0.0f), cx + rad, cy, true));
        g.fillEllipse (cx - rad, cy - rad, rad * 2.0f, rad * 2.0f);
    };
    glow (Theme::space,   170.0f,  40.0f, 460.0f, 0.10f);
    glow (Theme::tone,    800.0f, 560.0f, 420.0f, 0.06f);
    glow (Theme::texture, 560.0f, 250.0f, 320.0f, 0.03f);

    g.setTiledImageFill (grain, 0, 0, 1.0f);
    g.fillAll();

    // ---- header: ripple mark + wordmark
    {
        const float cx = (float) kMargin + 10.0f, cy = 37.0f;
        for (int k = 0; k < 3; ++k)
        {
            const float rr = 4.0f + 4.5f * (float) k;
            juce::Path arc;
            arc.addCentredArc (cx, cy, rr, rr, 0.0f, kPi * 0.15f, kPi * 0.85f, true);
            g.setColour (Theme::space.withAlpha (0.95f - 0.28f * (float) k));
            g.strokePath (arc, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
        g.setColour (Theme::space);
        g.fillEllipse (cx - 2.0f, cy - 6.0f, 4.0f, 4.0f);

        g.setColour (Theme::textBright);
        g.setFont (Theme::font (21.0f, true, 0.40f));
        g.drawText ("MYREVERB", kMargin + 30, 16, 260, 26, juce::Justification::centredLeft, false);
        g.setColour (Theme::textDim);
        g.setFont (Theme::font (9.0f, true, 0.34f));
        g.drawText (juce::String::fromUTF8 ("ALGORITHMIC SPACE  \xc2\xb7  V2"), kMargin + 31, 41, 260, 14, juce::Justification::centredLeft, false);
    }

    // ---- hero card
    {
        const auto h = hero.toFloat();
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xFF0E1020), h.getX(), h.getY(),
                                                 juce::Colour (0xFF07080F), h.getX(), h.getBottom(), false));
        g.fillRoundedRectangle (h, 14.0f);
        g.setColour (Theme::panelEdge);
        g.drawRoundedRectangle (h.reduced (0.5f), 14.0f, 1.0f);
    }

    // ---- section panels
    for (int i = 0; i < 4; ++i)
    {
        const auto p = panels[(size_t) i].toFloat();
        g.setColour (Theme::panel);
        g.fillRoundedRectangle (p, 12.0f);
        g.setColour (Theme::panelEdge);
        g.drawRoundedRectangle (p.reduced (0.5f), 12.0f, 1.0f);

        // hue hairline along the top edge
        juce::ColourGradient line (kSections[i].hue.withAlpha (0.0f), p.getX() + 12.0f, 0.0f,
                                   kSections[i].hue.withAlpha (0.0f), p.getRight() - 12.0f, 0.0f, false);
        line.addColour (0.3, kSections[i].hue.withAlpha (0.45f));
        g.setGradientFill (line);
        g.fillRect (p.getX() + 12.0f, p.getY(), p.getWidth() - 24.0f, 1.0f);

        // title
        const float dx = p.getX() + 18.0f, dy = p.getY() + 21.0f;
        g.setColour (kSections[i].hue.withAlpha (0.25f));
        g.fillEllipse (dx - 5.0f, dy - 5.0f, 10.0f, 10.0f);
        g.setColour (kSections[i].hue);
        g.fillEllipse (dx - 2.5f, dy - 2.5f, 5.0f, 5.0f);
        g.setColour (Theme::textMid);
        g.setFont (Theme::font (10.0f, true, 0.32f));
        g.drawText (kSections[i].title, (int) dx + 10, (int) dy - 7, 160, 14, juce::Justification::centredLeft, false);
    }
}

//==============================================================================
// DecayView
//==============================================================================
DecayView::DecayView (MyReverbAudioProcessor& p) : proc (p)
{
    setInterceptsMouseClicks (false, false);
    motes.resize (80);
    for (auto& m : motes)
        m = { rng.nextFloat(), rng.nextFloat(), 0.4f + rng.nextFloat(), 1.0f + rng.nextFloat() * 1.8f, rng.nextFloat() * 6.28f };
    tMax = juce::jlimit (0.8f, 34.0f, read().rtBass() * 1.2f);
    startTimerHz (30);
}

DecayView::Model DecayView::read() const
{
    auto v = [this] (const char* id) { return proc.apvts.getRawParameterValue (id)->load(); };
    return { v ("decay"), v ("size"), v ("preDelay"), v ("diffusion"), v ("damping"), v ("bass"),
             v ("lowCut"), v ("highCut"), v ("shimmer"), v ("reverse"), v ("freeze") > 0.5f };
}

// Envelope (dB) of each band at time t — mirrors the engine's decay model
float DecayView::envDb (const Model& m, int band, float t) const
{
    const float pre = m.pre * 0.001f;
    if (t < pre) return -120.0f;
    const float tt     = t - pre;
    const float scale  = 0.22f * std::pow (1.6f / 0.22f, m.size);
    const float attack = (0.006f + 0.035f * scale) * (0.4f + m.diffusion);
    const float riseDb = 20.0f * std::log10 (std::sin (0.5f * kPi * juce::jmin (1.0f, tt / attack)) + 1.0e-4f);
    const float hold   = m.freeze ? 0.0f : 1.0f;

    auto hp = [] (float fc, float f) { return -10.0f * std::log10 (1.0f + std::pow (fc / f, 4.0f)); };

    switch (band)
    {
        case 0:  return -1.5f + hp (m.lowCut, 160.0f)                                   + riseDb - hold * 60.0f * tt / m.rtBass();
        case 1:  return hp (m.lowCut, 900.0f) + hp (1200.0f, m.highCut)                  + riseDb - hold * 60.0f * tt / m.decay;
        case 2:  return -2.5f + hp (6500.0f, m.highCut)                                 + riseDb - hold * 60.0f * tt / m.rtAir();
        default:
        {
            if (m.shimmer < 0.01f) return -120.0f;
            // a bloom: swells as octaves stack, then fades a little slower than the body
            const float peak = juce::jmin (0.22f * m.decay, 2.5f) + 0.12f;
            const float x    = tt / peak;
            const float shape = m.freeze ? (x < 1.0f ? x * std::exp (1.0f - x) : 1.0f) : x * std::exp (1.0f - x);
            const float level = -20.0f + 16.0f * m.shimmer;
            return level + 20.0f * std::log10 (shape + 1.0e-5f) * (m.freeze ? 1.0f : 0.55f)
                         - hold * 60.0f * tt / (m.decay * 1.15f);
        }
    }
}

void DecayView::timerCallback()
{
    const auto m = read();

    // live energy from the wet bus — fast bloom, slow fade
    const float target = juce::jlimit (0.0f, 1.0f, (dbOf (proc.getWetLevel()) + 54.0f) / 42.0f);
    energy += (target - energy) * (target > energy ? 0.35f : 0.06f);

    // time axis breathes toward the new tail length
    const float want = m.freeze ? 8.0f : juce::jlimit (0.8f, 34.0f, juce::jmax (m.rtBass(), m.decay) * 1.15f + m.pre * 0.001f);
    tMax += (want - tMax) * 0.12f;
    frost += ((m.freeze ? 1.0f : 0.0f) - frost) * 0.08f;
    phase += 0.05f;

    const float drift = (1.0f - frost) * (0.25f + energy);
    for (auto& mt : motes)
    {
        mt.y += mt.speed * drift * 0.0035f * (1.0f + m.shimmer);
        mt.x += std::sin (phase * 0.7f + mt.twinkle) * 0.0004f;
        if (mt.y > 1.0f) { mt.y = 0.0f; mt.x = rng.nextFloat(); }
    }
    repaint();
}

void DecayView::paint (juce::Graphics& g)
{
    const auto b = getLocalBounds().toFloat();
    const auto m = read();
    const auto plot = b.withTrimmedLeft (22.0f).withTrimmedRight (22.0f).withTrimmedTop (64.0f).withTrimmedBottom (30.0f);

    {
        juce::Path clip;
        clip.addRoundedRectangle (b.reduced (1.0f), 13.0f);
        g.reduceClipRegion (clip);
    }

    auto xOf = [&] (float t) { return plot.getX() + plot.getWidth() * t / tMax; };
    auto yOf = [&] (float db, float s)
    {
        // amplitude, not dB: tails fall away in true exponential curves
        const float n = juce::jlimit (0.0f, 1.0f, (std::pow (10.0f, db / 36.0f) - 0.0215f) / (1.0f - 0.0215f));
        return plot.getBottom() - n * plot.getHeight() * s;
    };

    // ---- grid: time ticks + faint level lines
    {
        float step = 0.1f;
        for (float s : { 0.1f, 0.2f, 0.5f, 1.0f, 2.0f, 5.0f, 10.0f })
            if (tMax / s <= 8.0f) { step = s; break; }

        g.setFont (Theme::font (9.0f, true, 0.12f));
        for (float t = 0.0f; t <= tMax + 1.0e-3f; t += step)
        {
            const float x = xOf (t);
            g.setColour (juce::Colours::white.withAlpha (0.035f));
            g.fillRect (x, plot.getY(), 1.0f, plot.getHeight());
            g.setColour (Theme::textDim);
            const juce::String label = t < 1.0e-3f ? juce::String ("0")
                                     : (step < 1.0f ? juce::String (t, 1) : juce::String (juce::roundToInt (t))) + "s";
            g.drawText (label, (int) x - 20, (int) plot.getBottom() + 8, 40, 12, juce::Justification::centred, false);
        }
        for (float db : { -20.0f, -40.0f })
        {
            g.setColour (juce::Colours::white.withAlpha (0.03f));
            g.fillRect (plot.getX(), yOf (db, 1.0f), plot.getWidth(), 1.0f);
        }
    }

    // ---- onset glow, lit by the live signal
    {
        const float gx = xOf (m.pre * 0.001f), gy = plot.getBottom() - plot.getHeight() * 0.55f, rad = 190.0f;
        const auto  c  = Theme::space.interpolatedWith (Theme::ice, frost);
        g.setGradientFill (juce::ColourGradient (c.withAlpha (0.05f + 0.20f * energy), gx, gy, c.withAlpha (0.0f), gx + rad, gy, true));
        g.fillEllipse (gx - rad, gy - rad, rad * 2.0f, rad * 2.0f);
    }

    // ---- early reflections: visible as discrete taps when diffusion is low
    if (m.diffusion < 0.75f)
    {
        const float a     = (0.75f - m.diffusion) / 0.75f;
        const float scale = 0.22f * std::pow (1.6f / 0.22f, m.size);
        juce::Random r (7);
        for (int k = 0; k < 14; ++k)
        {
            const float t  = m.pre * 0.001f + (0.003f + 0.0045f * (float) k * (0.6f + 0.8f * r.nextFloat())) * scale * 4.0f;
            const float hh = plot.getHeight() * (0.75f - 0.04f * (float) k) * (0.6f + 0.4f * r.nextFloat());
            g.setColour (Theme::textBright.withAlpha (0.35f * a));
            g.fillRect (xOf (t), plot.getBottom() - hh, 1.2f, hh);
        }
    }

    // ---- the three bodies of the tail, back to front, plus the shimmer bloom
    struct Layer { int band; juce::Colour col; float scale; float fillA; };
    const Layer layers[] = { { 0, Theme::space,  1.00f, 0.30f },
                             { 1, Theme::tone,   0.86f, 0.26f },
                             { 2, Theme::output, 0.70f, 0.22f },
                             { 3, Theme::gold,   0.92f, 0.10f } };
    constexpr int N = 160;
    float midH[N + 1];   // remembered for the motes

    for (const auto& L : layers)
    {
        if (L.band == 3 && (m.shimmer < 0.01f || m.freeze)) continue;   // the engine mutes shimmer while frozen
        const auto col = L.col.interpolatedWith (Theme::ice, frost * 0.6f);

        juce::Path fill, edge;
        fill.startNewSubPath (plot.getX(), plot.getBottom());
        for (int k = 0; k <= N; ++k)
        {
            const float t = tMax * (float) k / (float) N;
            const float y = yOf (envDb (m, L.band, t), L.scale);
            fill.lineTo (xOf (t), y);
            if (k == 0) edge.startNewSubPath (xOf (t), y); else edge.lineTo (xOf (t), y);
            if (L.band == 1) midH[k] = (plot.getBottom() - y) / plot.getHeight();
        }
        fill.lineTo (plot.getRight(), plot.getBottom());
        fill.closeSubPath();

        const float a = L.fillA * (0.75f + 0.5f * energy);
        g.setGradientFill (juce::ColourGradient (col.withAlpha (a), 0.0f, plot.getY(),
                                                 col.withAlpha (0.01f), 0.0f, plot.getBottom(), false));
        g.fillPath (fill);

        g.setColour (col.withAlpha (0.10f + 0.12f * energy));
        g.strokePath (edge, juce::PathStrokeType (6.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (col.withAlpha (0.9f));
        g.strokePath (edge, juce::PathStrokeType (1.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    // ---- reverse swell: a rose wedge that grows into the onset
    if (m.reverse > 0.01f)
    {
        const float t0 = m.pre * 0.001f, len = juce::jmin (1.2f, tMax * 0.35f);
        juce::Path p, edge;
        p.startNewSubPath (xOf (t0), plot.getBottom());
        for (int k = 0; k <= 40; ++k)
        {
            const float u = (float) k / 40.0f;
            const juce::Point<float> pt (xOf (t0 + u * len), plot.getBottom() - plot.getHeight() * 0.55f * m.reverse * std::pow (u, 2.2f));
            p.lineTo (pt);
            if (k == 0) edge.startNewSubPath (pt); else edge.lineTo (pt);
        }
        p.lineTo (xOf (t0 + len), plot.getBottom());
        p.closeSubPath();
        g.setGradientFill (juce::ColourGradient (Theme::texture.withAlpha (0.35f * m.reverse), 0.0f, plot.getY(),
                                                 Theme::texture.withAlpha (0.0f), 0.0f, plot.getBottom(), false));
        g.fillPath (p);
        g.setColour (Theme::texture.withAlpha (0.7f * m.reverse));
        g.strokePath (edge, juce::PathStrokeType (1.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    // ---- motes: dust in the air of the room, lit by what's actually ringing
    {
        const auto moteCol = Theme::textBright.interpolatedWith (Theme::gold, juce::jmin (1.0f, m.shimmer * 1.4f))
                                              .interpolatedWith (Theme::ice, frost);
        for (const auto& mt : motes)
        {
            const int   k   = juce::jlimit (0, N, (int) (mt.x * (float) N));
            const float top = midH[k] + 0.08f;
            if (mt.y > top) continue;
            const float tw  = 0.55f + 0.45f * std::sin (phase * 1.7f + mt.twinkle);
            const float a   = (0.06f + 0.8f * energy) * tw * (1.0f - mt.y / top);
            const float x   = plot.getX() + mt.x * plot.getWidth();
            const float y   = plot.getBottom() - mt.y * plot.getHeight();
            g.setColour (moteCol.withAlpha (a * 0.25f));
            g.fillEllipse (x - mt.size * 1.8f, y - mt.size * 1.8f, mt.size * 3.6f, mt.size * 3.6f);
            g.setColour (moteCol.withAlpha (a));
            g.fillEllipse (x - mt.size * 0.5f, y - mt.size * 0.5f, mt.size, mt.size);
        }
    }

    // ---- frost veil
    if (frost > 0.01f)
    {
        g.setColour (Theme::ice.withAlpha (0.045f * frost));
        g.fillAll();
    }

    // ---- readouts
    {
        g.setColour (Theme::textDim);
        g.setFont (Theme::font (9.0f, true, 0.32f));
        g.drawText ("DECAY", 22, 14, 120, 12, juce::Justification::centredLeft, false);

        g.setColour (frost > 0.5f ? Theme::ice : Theme::textBright);
        g.setFont (Theme::lightFont (31.0f, 0.02f));
        const juce::String big = m.freeze ? juce::String (juce::CharPointer_UTF8 ("\xe2\x88\x9e"))
                                          : (m.decay < 10.0f ? juce::String (m.decay, 2) : juce::String (m.decay, 1)) + " s";
        g.drawText (big, 19, 24, 150, 36, juce::Justification::centredLeft, false);

        auto secs = [] (float v) { return v < 10.0f ? juce::String (v, 2) + " s" : juce::String (v, 1) + " s"; };
        g.setFont (Theme::font (9.5f, true, 0.16f));
        const int cx = 160;
        g.setColour (Theme::space);   g.fillEllipse ((float) cx, 22.0f, 5.0f, 5.0f);
        g.setColour (Theme::textMid); g.drawText ("LOW   " + secs (m.rtBass()), cx + 11, 17, 140, 14, juce::Justification::centredLeft, false);
        g.setColour (Theme::tone);    g.fillEllipse ((float) cx, 36.0f, 5.0f, 5.0f);
        g.setColour (Theme::textMid); g.drawText ("MID   " + secs (m.decay),    cx + 11, 31, 140, 14, juce::Justification::centredLeft, false);
        g.setColour (Theme::output);  g.fillEllipse ((float) cx, 50.0f, 5.0f, 5.0f);
        g.setColour (Theme::textMid); g.drawText ("HIGH  " + secs (m.rtAir()),  cx + 11, 45, 140, 14, juce::Justification::centredLeft, false);

        const auto right = b.withTrimmedRight (22.0f).withTrimmedTop (14.0f).withHeight (14.0f);
        g.setColour (Theme::textDim);
        g.setFont (Theme::font (9.0f, true, 0.26f));
        g.drawText ("PRE " + juce::String (juce::roundToInt (m.pre)) + " MS     SIZE " + juce::String (juce::roundToInt (m.size * 100.0f)) + "%",
                    right, juce::Justification::centredRight, false);

        if (m.shimmer > 0.01f && ! m.freeze)
        {
            g.setColour (Theme::gold.withAlpha (0.85f));
            g.drawText ("SHIMMER +12", right.translated (0.0f, 16.0f), juce::Justification::centredRight, false);
        }
        if (frost > 0.05f)
        {
            g.setColour (Theme::ice.withAlpha (frost));
            g.drawText ("FROZEN", right.translated (0.0f, 16.0f), juce::Justification::centredRight, false);
        }
    }
}

//==============================================================================
// Content
//==============================================================================
Content::Content (MyReverbAudioProcessor& p)
    : proc (p), decayView (p), freezeAttachment (p.apvts, "freeze", freezeButton)
{
    addAndMakeVisible (backdrop);
    backdrop.setBufferedToImage (true);
    addAndMakeVisible (decayView);

    struct Spec { const char* id; const char* name; int section; float centre; };
    const Spec specs[] =
    {
        { "size",    "Size",     0, 0.0f }, { "decay",    "Decay",    0, 0.0f },
        { "preDelay","Pre-Delay",0, 0.0f }, { "diffusion","Diffusion",0, 0.0f },
        { "damping", "Damping",  1, 0.0f }, { "bass",     "Bass",     1, 0.5f },
        { "lowCut",  "Low Cut",  1, 0.0f }, { "highCut",  "High Cut", 1, 0.0f },
        { "mod",     "Mod",      2, 0.0f }, { "shimmer",  "Shimmer",  2, 0.0f },
        { "reverse", "Reverse",  2, 0.0f }, { "noise",    "Air",      2, 0.0f },
        { "duck",    "Duck",     3, 0.0f }, { "width",    "Width",    3, 1.0f / 1.5f },
        { "dry",     "Dry",      3, 0.0f }, { "wet",      "Wet",      3, 0.0f },
    };
    for (const auto& s : specs)
    {
        knobs.push_back (std::make_unique<Knob> (p.apvts, s.id, s.name, kSections[s.section].hue, s.centre));
        addAndMakeVisible (*knobs.back());
    }

    // presets
    for (int i = 0; i < p.getNumPrograms(); ++i)
        presetBox.addItem (p.getProgramName (i), i + 1);
    presetBox.setSelectedId (p.getCurrentProgram() + 1, juce::dontSendNotification);
    presetBox.onChange = [this]
    {
        const int idx = presetBox.getSelectedId() - 1;
        if (idx >= 0) proc.setCurrentProgram (idx);
    };
    auto step = [this] (int dir)
    {
        const int n   = proc.getNumPrograms();
        const int idx = (proc.getCurrentProgram() + dir + n) % n;
        proc.setCurrentProgram (idx);
        presetBox.setSelectedId (idx + 1, juce::dontSendNotification);
    };
    prevButton.onClick = [step] { step (-1); };
    nextButton.onClick = [step] { step (+1); };
    addAndMakeVisible (presetBox);
    addAndMakeVisible (prevButton);
    addAndMakeVisible (nextButton);
    addAndMakeVisible (freezeButton);

    setSize (kW, kH);
    startTimerHz (4);
}

void Content::timerCallback()
{
    // keep the preset name in step with host-driven program changes
    const int id = proc.getCurrentProgram() + 1;
    if (presetBox.getSelectedId() != id)
        presetBox.setSelectedId (id, juce::dontSendNotification);
}

void Content::resized()
{
    backdrop.setBounds (getLocalBounds());

    // header controls, right-aligned
    const int hy = 22, hh = 30;
    int x = kW - kMargin - 100;
    freezeButton.setBounds (x, hy, 100, hh);
    x -= 16 + 30;  nextButton.setBounds (x, hy, 30, hh);
    x -= 6 + 210;  presetBox.setBounds  (x, hy, 210, hh);
    x -= 6 + 30;   prevButton.setBounds (x, hy, 30, hh);

    backdrop.hero = { kMargin, kHeroY, kW - 2 * kMargin, kHeroH };
    decayView.setBounds (backdrop.hero);

    const int pw = (kW - 2 * kMargin - 3 * kPanelGap) / 4;
    for (int s = 0; s < 4; ++s)
    {
        const juce::Rectangle<int> panel (kMargin + s * (pw + kPanelGap), kPanelY, pw, kPanelH);
        backdrop.panels[(size_t) s] = panel;

        const auto grid  = panel.withTrimmedTop (kPanelTop).reduced (6, 4);
        const int  cellW = grid.getWidth() / 2, cellH = grid.getHeight() / 2;
        for (int k = 0; k < 4; ++k)
            knobs[(size_t) (s * 4 + k)]->setBounds (grid.getX() + (k % 2) * cellW, grid.getY() + (k / 2) * cellH, cellW, cellH);
    }
    backdrop.repaint();
}

//==============================================================================
// Editor
//==============================================================================
MyReverbAudioProcessorEditor::MyReverbAudioProcessorEditor (MyReverbAudioProcessor& p)
    : AudioProcessorEditor (&p), content (p)
{
    setLookAndFeel (&lookAndFeel);
    addAndMakeVisible (content);

    setResizable (true, true);
    setResizeLimits ((int) (Content::kW * 0.75), (int) (Content::kH * 0.75),
                     (int) (Content::kW * 1.75), (int) (Content::kH * 1.75));
    getConstrainer()->setFixedAspectRatio ((double) Content::kW / (double) Content::kH);
    setSize (Content::kW, Content::kH);
}

MyReverbAudioProcessorEditor::~MyReverbAudioProcessorEditor()
{
    setLookAndFeel (nullptr);
}

void MyReverbAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (Theme::bgBottom);
}

void MyReverbAudioProcessorEditor::resized()
{
    // vector UI: scale the whole design, stays crisp at any size
    content.setTransform (juce::AffineTransform::scale ((float) getWidth() / (float) Content::kW));
    content.setBounds (0, 0, Content::kW, Content::kH);
}
