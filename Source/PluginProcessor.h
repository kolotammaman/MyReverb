/*
  ==============================================================================
    MyReverb  —  PluginProcessor.h
  ==============================================================================
*/
#pragma once
#include <JuceHeader.h>
#include "ReverbDSP.h"

class MyReverbAudioProcessor  : public juce::AudioProcessor
{
public:
    MyReverbAudioProcessor();
    ~MyReverbAudioProcessor() override;

    void prepareToPlay  (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

   #ifndef JucePlugin_PreferredChannelConfigurations
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
   #endif

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    const juce::String getName() const override;
    bool acceptsMidi()  const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    int  getNumPrograms() override;
    int  getCurrentProgram() override;
    void setCurrentProgram  (int) override;
    const juce::String getProgramName    (int) override;
    void changeProgramName  (int, const juce::String&) override;

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    juce::AudioProcessorValueTreeState apvts;
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    // For the editor's visualiser (lock-free)
    float getWetLevel() const { return engine.getWetLevel(); }

private:
    mr::ReverbEngine engine;

    // Cached parameter pointers — no string lookups on the audio thread
    struct RawParams
    {
        std::atomic<float> *size, *decay, *preDelay, *diffusion, *damping, *bass, *lowCut, *highCut,
                           *mod, *shimmer, *reverse, *noise, *duck, *width, *dry, *wet, *freeze;
    } raw {};

    int currentProgram = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MyReverbAudioProcessor)
};
