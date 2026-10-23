/*
  ==============================================================================
    MyReverbLookAndFeel.h  —  "Nocturne": night-observatory palette,
    luminous arcs on deep ink, one hue per section.
  ==============================================================================
*/
#pragma once
#include <JuceHeader.h>

namespace Theme
{
    // surfaces
    inline const juce::Colour bgTop        { 0xFF0B0C18 };
    inline const juce::Colour bgBottom     { 0xFF05060B };
    inline const juce::Colour panel        { 0x0AFFFFFF };
    inline const juce::Colour panelEdge    { 0x14FFFFFF };
    inline const juce::Colour knobBodyHi   { 0xFF1E2138 };
    inline const juce::Colour knobBodyLo   { 0xFF0A0B15 };
    inline const juce::Colour track        { 0xFF1A1D30 };

    // text
    inline const juce::Colour textBright   { 0xFFE9EBF8 };
    inline const juce::Colour textMid      { 0xFF9196B8 };
    inline const juce::Colour textDim      { 0xFF555A7A };

    // section hues
    inline const juce::Colour space        { 0xFF9C8CFF };   // lavender
    inline const juce::Colour tone         { 0xFF5FD0E0 };   // glacier teal
    inline const juce::Colour texture      { 0xFFF4B8D6 };   // rose quartz
    inline const juce::Colour output       { 0xFFC9D0FF };   // moonlight
    inline const juce::Colour gold         { 0xFFFFD39A };   // shimmer
    inline const juce::Colour ice          { 0xFFAEEBFF };   // freeze

    inline juce::Font font (float height, bool bold = false, float kerning = 0.0f)
    {
        return juce::Font (juce::FontOptions ("Avenir Next", bold ? "Demi Bold" : "Medium", height)
                               .withKerningFactor (kerning));
    }
    inline juce::Font lightFont (float height, float kerning = 0.0f)
    {
        return juce::Font (juce::FontOptions ("Avenir Next", "Ultra Light", height).withKerningFactor (kerning));
    }
}

class MyReverbLookAndFeel : public juce::LookAndFeel_V4
{
public:
    MyReverbLookAndFeel()
    {
        setColour (juce::PopupMenu::backgroundColourId,            juce::Colour (0xF0101222));
        setColour (juce::PopupMenu::textColourId,                  Theme::textMid);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, Theme::space.withAlpha (0.18f));
        setColour (juce::PopupMenu::highlightedTextColourId,       Theme::textBright);
        setColour (juce::ComboBox::textColourId,                   Theme::textBright);
        setColour (juce::ComboBox::arrowColourId,                  Theme::textMid);
    }

    // ---------------------------------------------------------------- knobs
    // Accent colour comes from Slider::rotarySliderFillColourId.
    // Optional property "centre" (0–1) draws the arc from that point (bipolar controls).
    void drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                           float a0, float a1, juce::Slider& s) override
    {
        const auto  accent = s.findColour (juce::Slider::rotarySliderFillColourId);
        const bool  active = s.isMouseOverOrDragging();
        const auto  b      = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (8.0f);
        const float r      = juce::jmin (b.getWidth(), b.getHeight()) * 0.5f;
        const auto  c      = b.getCentre();
        const float ang    = a0 + pos * (a1 - a0);
        const float arcR   = r - 1.5f;
        const float bodyR  = r * 0.70f;

        // track
        juce::Path trackP;
        trackP.addCentredArc (c.x, c.y, arcR, arcR, 0.0f, a0, a1, true);
        g.setColour (Theme::track);
        g.strokePath (trackP, juce::PathStrokeType (2.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        // value arc (+ soft glow)
        const float centre = (float) s.getProperties().getWithDefault ("centre", 0.0);
        const float from   = a0 + centre * (a1 - a0);
        if (std::abs (ang - from) > 0.01f)
        {
            juce::Path valP;
            valP.addCentredArc (c.x, c.y, arcR, arcR, 0.0f, juce::jmin (from, ang), juce::jmax (from, ang), true);
            g.setColour (accent.withAlpha (active ? 0.22f : 0.12f));
            g.strokePath (valP, juce::PathStrokeType (8.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            g.setColour (accent.withAlpha (active ? 1.0f : 0.85f));
            g.strokePath (valP, juce::PathStrokeType (2.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }

        // body: soft lit sphere
        juce::ColourGradient body (Theme::knobBodyHi, c.x - bodyR * 0.6f, c.y - bodyR * 0.8f,
                                   Theme::knobBodyLo, c.x + bodyR * 0.5f, c.y + bodyR, true);
        g.setGradientFill (body);
        g.fillEllipse (c.x - bodyR, c.y - bodyR, bodyR * 2.0f, bodyR * 2.0f);
        g.setColour (juce::Colours::white.withAlpha (active ? 0.10f : 0.06f));
        g.drawEllipse (c.x - bodyR, c.y - bodyR, bodyR * 2.0f, bodyR * 2.0f, 1.0f);

        // pointer
        const float sn = std::sin (ang), cs = -std::cos (ang);
        g.setColour (Theme::textBright.withAlpha (active ? 1.0f : 0.85f));
        g.drawLine (c.x + sn * bodyR * 0.30f, c.y + cs * bodyR * 0.30f,
                    c.x + sn * bodyR * 0.78f, c.y + cs * bodyR * 0.78f, 2.0f);

        // rim dot
        g.setColour (accent);
        g.fillEllipse (c.x + sn * arcR - 2.5f, c.y + cs * arcR - 2.5f, 5.0f, 5.0f);
    }

    // ---------------------------------------------------------------- freeze pill
    void drawToggleButton (juce::Graphics& g, juce::ToggleButton& b, bool over, bool) override
    {
        const auto  r  = b.getLocalBounds().toFloat().reduced (1.0f);
        const bool  on = b.getToggleState();
        const float rr = r.getHeight() * 0.5f;

        if (on)
        {
            g.setColour (Theme::ice.withAlpha (0.16f));
            g.fillRoundedRectangle (r, rr);
            g.setColour (Theme::ice.withAlpha (0.9f));
        }
        else
        {
            g.setColour (juce::Colours::white.withAlpha (over ? 0.06f : 0.03f));
            g.fillRoundedRectangle (r, rr);
            g.setColour (Theme::panelEdge.withAlpha (over ? 0.5f : 0.3f));
        }
        g.drawRoundedRectangle (r, rr, 1.0f);

        // six-spoke snowflake
        const auto  ic = juce::Point<float> (r.getX() + rr + 4.0f, r.getCentreY());
        const float ir = rr * 0.42f;
        g.setColour (on ? Theme::ice : Theme::textMid);
        for (int k = 0; k < 3; ++k)
        {
            const float a = juce::MathConstants<float>::pi * (float) k / 3.0f;
            g.drawLine (ic.x - std::cos (a) * ir, ic.y - std::sin (a) * ir,
                        ic.x + std::cos (a) * ir, ic.y + std::sin (a) * ir, 1.2f);
        }

        g.setFont (Theme::font (11.0f, true, 0.18f));
        g.setColour (on ? Theme::ice : Theme::textMid);
        g.drawText ("FREEZE", r.withTrimmedLeft (rr * 2.0f + 4.0f).withTrimmedRight (8.0f),
                    juce::Justification::centred, false);
    }

    // ---------------------------------------------------------------- arrows (preset prev/next)
    void drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down) override
    {
        const auto r = b.getLocalBounds().toFloat().reduced (1.0f);
        g.setColour (juce::Colours::white.withAlpha (down ? 0.09f : over ? 0.06f : 0.03f));
        g.fillRoundedRectangle (r, 6.0f);
        g.setColour (Theme::panelEdge.withAlpha (0.3f));
        g.drawRoundedRectangle (r, 6.0f, 1.0f);
    }

    void drawButtonText (juce::Graphics& g, juce::TextButton& b, bool over, bool) override
    {
        const auto  r   = b.getLocalBounds().toFloat();
        const bool  fwd = b.getButtonText() == ">";
        const auto  c   = r.getCentre();
        juce::Path p;
        const float s = 3.5f;
        if (fwd) p.addTriangle (c.x - s * 0.6f, c.y - s, c.x - s * 0.6f, c.y + s, c.x + s * 0.8f, c.y);
        else     p.addTriangle (c.x + s * 0.6f, c.y - s, c.x + s * 0.6f, c.y + s, c.x - s * 0.8f, c.y);
        g.setColour (over ? Theme::textBright : Theme::textMid);
        g.fillPath (p);
    }

    // ---------------------------------------------------------------- preset combo
    void drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& box) override
    {
        const auto r = juce::Rectangle<float> (0.0f, 0.0f, (float) w, (float) h).reduced (1.0f);
        g.setColour (juce::Colours::white.withAlpha (box.isMouseOver (true) ? 0.06f : 0.03f));
        g.fillRoundedRectangle (r, 6.0f);
        g.setColour (Theme::panelEdge.withAlpha (0.3f));
        g.drawRoundedRectangle (r, 6.0f, 1.0f);

        juce::Path chevron;
        const float cx = (float) w - 14.0f, cy = (float) h * 0.5f;
        chevron.startNewSubPath (cx - 3.5f, cy - 1.5f);
        chevron.lineTo (cx, cy + 2.0f);
        chevron.lineTo (cx + 3.5f, cy - 1.5f);
        g.setColour (Theme::textMid);
        g.strokePath (chevron, juce::PathStrokeType (1.3f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    juce::Font getComboBoxFont (juce::ComboBox&) override { return Theme::font (13.0f, false, 0.03f); }

    void positionComboBoxText (juce::ComboBox& box, juce::Label& label) override
    {
        label.setBounds (10, 1, box.getWidth() - 30, box.getHeight() - 2);
        label.setFont (getComboBoxFont (box));
        label.setJustificationType (juce::Justification::centredLeft);
    }

    juce::Font getPopupMenuFont() override { return Theme::font (13.0f); }

    void drawPopupMenuBackground (juce::Graphics& g, int w, int h) override
    {
        g.fillAll (findColour (juce::PopupMenu::backgroundColourId));
        g.setColour (Theme::panelEdge);
        g.drawRect (0, 0, w, h, 1);
    }
};
