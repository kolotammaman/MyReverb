/*
  ==============================================================================
    MyReverb  —  PluginEditor.h
  ==============================================================================
*/
#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "MyReverbLookAndFeel.h"

// ---------------------------------------------------------------------------
// Knob — rotary slider with its name and live value drawn underneath
// ---------------------------------------------------------------------------
class Knob : public juce::Component
{
public:
    Knob (juce::AudioProcessorValueTreeState& apvts, const juce::String& paramId,
          const juce::String& name, juce::Colour accent, float bipolarCentre = 0.0f);

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit  (const juce::MouseEvent&) override { repaint(); }

private:
    juce::Slider slider;
    juce::String name;
    juce::Colour accent;
    juce::AudioProcessorValueTreeState::SliderAttachment attachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Knob)
};

// ---------------------------------------------------------------------------
// Backdrop — everything static. Rendered once into a cached image.
// ---------------------------------------------------------------------------
class Backdrop : public juce::Component
{
public:
    Backdrop();
    void paint (juce::Graphics&) override;

    std::array<juce::Rectangle<int>, 4> panels;
    juce::Rectangle<int> hero;

private:
    juce::Image grain;
};

// ---------------------------------------------------------------------------
// DecayView — the hero: a live portrait of the tail.
//   Layered bass / mid / air envelopes from the actual decay model,
//   shimmer bloom, reverse swell, freeze, and particles lit by the wet signal.
// ---------------------------------------------------------------------------
class DecayView : public juce::Component, private juce::Timer
{
public:
    explicit DecayView (MyReverbAudioProcessor&);
    ~DecayView() override { stopTimer(); }
    void paint (juce::Graphics&) override;

private:
    void timerCallback() override;

    struct Model
    {
        float decay, size, pre, diffusion, damping, bass, lowCut, highCut, shimmer, reverse;
        bool  freeze;
        float rtBass() const { return decay * bass; }
        float rtAir()  const { return decay / (1.0f + 11.0f * damping * damping); }
    };
    Model read() const;
    float envDb (const Model&, int band, float t) const;   // band: 0 bass, 1 mid, 2 air, 3 shimmer

    MyReverbAudioProcessor& proc;
    float tMax = 5.0f, energy = 0.0f, phase = 0.0f, frost = 0.0f;

    struct Mote { float x, y, speed, size, twinkle; };
    std::vector<Mote> motes;
    juce::Random rng { 1977 };
};

// ---------------------------------------------------------------------------
// Content — the whole UI at its design size; the editor scales it.
// ---------------------------------------------------------------------------
class Content : public juce::Component, private juce::Timer
{
public:
    static constexpr int kW = 920, kH = 580;

    explicit Content (MyReverbAudioProcessor&);
    ~Content() override { stopTimer(); }
    void resized() override;

private:
    void timerCallback() override;

    MyReverbAudioProcessor& proc;
    Backdrop  backdrop;
    DecayView decayView;

    std::vector<std::unique_ptr<Knob>> knobs;

    juce::ComboBox     presetBox;
    juce::TextButton   prevButton { "<" }, nextButton { ">" };
    juce::ToggleButton freezeButton;
    juce::AudioProcessorValueTreeState::ButtonAttachment freezeAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Content)
};

// ---------------------------------------------------------------------------
class MyReverbAudioProcessorEditor  : public juce::AudioProcessorEditor
{
public:
    MyReverbAudioProcessorEditor (MyReverbAudioProcessor&);
    ~MyReverbAudioProcessorEditor() override;

    void paint   (juce::Graphics&) override;
    void resized () override;

private:
    MyReverbLookAndFeel lookAndFeel;   // declared first — outlives every child
    Content content;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MyReverbAudioProcessorEditor)
};
