/*
  ==============================================================================
    MyReverb  —  PluginProcessor.cpp
  ==============================================================================
*/
#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
// Parameters — IDs from v1 are kept so old sessions still load
//==============================================================================
namespace
{
    using Range = juce::NormalisableRange<float>;

    juce::String fmtSeconds (float v, int)  { return v < 1.0f ? juce::String (juce::roundToInt (v * 1000.0f)) + " ms"
                                                              : juce::String (v, v < 10.0f ? 2 : 1) + " s"; }
    juce::String fmtMs      (float v, int)  { return juce::String (juce::roundToInt (v)) + " ms"; }
    juce::String fmtHz      (float v, int)  { return v < 1000.0f ? juce::String (juce::roundToInt (v)) + " Hz"
                                                                 : juce::String (v / 1000.0f, 1) + " kHz"; }
    juce::String fmtPct     (float v, int)  { return juce::String (juce::roundToInt (v * 100.0f)) + "%"; }
    juce::String fmtMult    (float v, int)  { return juce::String (v, 2) + "x"; }
    juce::String fmtGain    (float v, int)  { return v < 0.001f ? juce::String ("-inf")
                                                                : juce::String (juce::Decibels::gainToDecibels (v), 1) + " dB"; }

    // log-ish frequency range
    Range freqRange (float lo, float hi)
    {
        return { lo, hi,
                 [] (float s, float e, float t) { return s * std::pow (e / s, t); },
                 [] (float s, float e, float v) { return std::log (v / s) / std::log (e / s); },
                 [] (float, float, float v)     { return v; } };
    }

    std::unique_ptr<juce::AudioParameterFloat> makeFloat (const char* id, const char* name, Range r, float def,
                                                          juce::String (*fmt) (float, int))
    {
        // v1 parameters keep version hint 1; parameters added in v2 get 2
        static const juce::StringArray v1Ids { "decay", "preDelay", "shimmer", "mod", "noise", "reverse", "wet", "dry", "width" };
        return std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { id, v1Ids.contains (id) ? 1 : 2 }, name, r, def,
            juce::AudioParameterFloatAttributes().withStringFromValueFunction (fmt));
    }
}

juce::AudioProcessorValueTreeState::ParameterLayout
MyReverbAudioProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    Range decay (0.2f, 30.0f);    decay.setSkewForCentre (3.0f);
    Range pre   (0.0f, 250.0f);   pre.setSkewForCentre (50.0f);
    Range bass  (0.5f, 2.0f);     bass.setSkewForCentre (1.0f);

    // defaults mirror the first preset (Concert Hall), which the preset menu shows on load
    // SPACE
    layout.add (makeFloat ("size",      "Size",      { 0.0f, 1.0f }, 0.75f,  fmtPct));
    layout.add (makeFloat ("decay",     "Decay",     decay,          3.2f,   fmtSeconds));
    layout.add (makeFloat ("preDelay",  "Pre-Delay", pre,            28.0f,  fmtMs));
    layout.add (makeFloat ("diffusion", "Diffusion", { 0.0f, 1.0f }, 0.85f,   fmtPct));
    // TONE
    layout.add (makeFloat ("damping",   "Damping",   { 0.0f, 1.0f }, 0.45f,  fmtPct));
    layout.add (makeFloat ("bass",      "Bass",      bass,           1.15f,  fmtMult));
    layout.add (makeFloat ("lowCut",    "Low Cut",   freqRange (20.0f, 1000.0f),    100.0f,    fmtHz));
    layout.add (makeFloat ("highCut",   "High Cut",  freqRange (1000.0f, 20000.0f), 11000.0f, fmtHz));
    // TEXTURE
    layout.add (makeFloat ("mod",       "Mod",       { 0.0f, 1.0f }, 0.35f,  fmtPct));
    layout.add (makeFloat ("shimmer",   "Shimmer",   { 0.0f, 1.0f }, 0.0f,   fmtPct));
    layout.add (makeFloat ("reverse",   "Reverse",   { 0.0f, 1.0f }, 0.0f,   fmtPct));
    layout.add (makeFloat ("noise",     "Air",       { 0.0f, 1.0f }, 0.0f,   fmtPct));
    // OUTPUT
    layout.add (makeFloat ("duck",      "Duck",      { 0.0f, 1.0f }, 0.0f,   fmtPct));
    layout.add (makeFloat ("width",     "Width",     { 0.0f, 1.5f }, 1.0f,   fmtPct));
    layout.add (makeFloat ("dry",       "Dry",       { 0.0f, 1.0f }, 1.0f,   fmtGain));
    layout.add (makeFloat ("wet",       "Wet",       { 0.0f, 1.0f }, 0.32f,   fmtGain));

    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "freeze", 2 }, "Freeze", false));
    return layout;
}

//==============================================================================
MyReverbAudioProcessor::MyReverbAudioProcessor()
#ifndef JucePlugin_PreferredChannelConfigurations
    : AudioProcessor (BusesProperties()
                    #if ! JucePlugin_IsMidiEffect
                     #if ! JucePlugin_IsSynth
                      .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                     #endif
                      .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
                    #endif
                      ),
#endif
      apvts (*this, nullptr, "Parameters", createParameterLayout())
{
    auto p = [this] (const char* id) { return apvts.getRawParameterValue (id); };
    raw = { p ("size"), p ("decay"), p ("preDelay"), p ("diffusion"), p ("damping"), p ("bass"),
            p ("lowCut"), p ("highCut"), p ("mod"), p ("shimmer"), p ("reverse"), p ("noise"),
            p ("duck"), p ("width"), p ("dry"), p ("wet"), p ("freeze") };
}

MyReverbAudioProcessor::~MyReverbAudioProcessor() {}

//==============================================================================
const juce::String MyReverbAudioProcessor::getName() const { return JucePlugin_Name; }
bool MyReverbAudioProcessor::acceptsMidi()  const { return false; }
bool MyReverbAudioProcessor::producesMidi() const { return false; }
bool MyReverbAudioProcessor::isMidiEffect() const { return false; }

double MyReverbAudioProcessor::getTailLengthSeconds() const
{
    return raw.decay->load() * 1.5 + raw.preDelay->load() * 0.001 + 0.5;
}

// ---------------------------------------------------------------------------
// Factory presets
// ---------------------------------------------------------------------------
namespace
{
    struct ReverbPreset
    {
        const char* name;
        float size, decay, preDelay, diffusion;
        float damping, bass, lowCut, highCut;
        float mod, shimmer, reverse, noise;
        float duck, width, dry, wet;
    };

    static const ReverbPreset kPresets[] =
    {
        // name                size  decay  pre   diff   damp  bass  lowCut highCut   mod   shim  rev   air    duck  width dry   wet
        { "Concert Hall",      0.75f,  3.2f, 28.f, 0.85f, 0.45f, 1.15f, 100.f, 11000.f, 0.35f, 0.0f, 0.0f, 0.0f,  0.0f, 1.0f, 1.0f, 0.32f },
        { "Velvet Room",       0.30f,  0.9f,  8.f, 0.85f, 0.40f, 1.00f, 150.f, 12000.f, 0.45f, 0.0f, 0.0f, 0.0f,  0.0f, 0.9f, 1.0f, 0.30f },
        { "Bright Plate",      0.50f,  1.8f,  4.f, 1.00f, 0.15f, 0.85f, 220.f, 16000.f, 0.20f, 0.0f, 0.0f, 0.0f,  0.0f, 1.0f, 1.0f, 0.28f },
        { "Vocal Plate",       0.45f,  2.4f, 60.f, 0.95f, 0.30f, 0.80f, 280.f, 13000.f, 0.30f, 0.0f, 0.0f, 0.0f,  0.35f,1.0f, 1.0f, 0.30f },
        { "Dark Chamber",      0.55f,  2.2f, 15.f, 0.80f, 0.80f, 1.20f,  80.f,  6000.f, 0.30f, 0.0f, 0.0f, 0.05f, 0.0f, 0.9f, 1.0f, 0.32f },
        { "Cathedral",         0.95f,  9.0f, 55.f, 0.90f, 0.55f, 1.30f,  70.f,  9000.f, 0.40f, 0.0f, 0.0f, 0.0f,  0.0f, 1.1f, 1.0f, 0.38f },
        { "Ambience",          0.20f,  0.5f,  0.f, 0.70f, 0.35f, 1.00f, 120.f, 14000.f, 0.30f, 0.0f, 0.0f, 0.0f,  0.0f, 1.0f, 1.0f, 0.25f },
        { "Shimmer Pad",       0.90f, 12.0f, 45.f, 0.90f, 0.45f, 1.10f, 160.f, 10000.f, 0.55f, 0.55f,0.0f, 0.0f,  0.0f, 1.2f, 1.0f, 0.45f },
        { "Infinite Shimmer",  1.00f, 30.0f, 80.f, 0.95f, 0.40f, 1.00f, 200.f,  9500.f, 0.70f, 0.80f,0.0f, 0.05f, 0.0f, 1.3f, 0.9f, 0.55f },
        { "Dreaming",          0.85f,  8.0f, 30.f, 0.85f, 0.55f, 1.10f, 120.f,  9000.f, 0.75f, 0.30f,0.20f,0.15f, 0.2f, 1.2f, 1.0f, 0.45f },
        { "Ghost",             0.80f, 10.0f, 20.f, 0.80f, 0.60f, 1.00f, 150.f,  8000.f, 0.60f, 0.15f,0.60f,0.30f, 0.0f, 1.1f, 1.0f, 0.45f },
        { "Ducked Bloom",      0.70f,  5.0f, 35.f, 0.90f, 0.40f, 1.00f, 140.f, 12000.f, 0.45f, 0.10f,0.0f, 0.0f,  0.75f,1.2f, 1.0f, 0.50f },
        { "Void",              1.00f, 25.0f,120.f, 0.90f, 0.70f, 1.60f,  60.f,  7000.f, 0.85f, 0.45f,0.40f,0.25f, 0.0f, 1.4f, 0.7f, 0.65f },
    };

    static constexpr int kNumPresets = (int) (sizeof (kPresets) / sizeof (kPresets[0]));

    void setParam (juce::AudioProcessorValueTreeState& apvts, const char* id, float v)
    {
        if (auto* p = apvts.getParameter (id))
            p->setValueNotifyingHost (p->convertTo0to1 (v));
    }
}

int  MyReverbAudioProcessor::getNumPrograms()    { return kNumPresets; }
int  MyReverbAudioProcessor::getCurrentProgram() { return currentProgram; }

void MyReverbAudioProcessor::setCurrentProgram (int index)
{
    if (index < 0 || index >= kNumPresets) return;
    currentProgram = index;
    const auto& p = kPresets[index];
    setParam (apvts, "size",      p.size);
    setParam (apvts, "decay",     p.decay);
    setParam (apvts, "preDelay",  p.preDelay);
    setParam (apvts, "diffusion", p.diffusion);
    setParam (apvts, "damping",   p.damping);
    setParam (apvts, "bass",      p.bass);
    setParam (apvts, "lowCut",    p.lowCut);
    setParam (apvts, "highCut",   p.highCut);
    setParam (apvts, "mod",       p.mod);
    setParam (apvts, "shimmer",   p.shimmer);
    setParam (apvts, "reverse",   p.reverse);
    setParam (apvts, "noise",     p.noise);
    setParam (apvts, "duck",      p.duck);
    setParam (apvts, "width",     p.width);
    setParam (apvts, "dry",       p.dry);
    setParam (apvts, "wet",       p.wet);
    setParam (apvts, "freeze",    0.0f);
}

const juce::String MyReverbAudioProcessor::getProgramName (int index)
{
    if (index >= 0 && index < kNumPresets) return kPresets[index].name;
    return {};
}

void MyReverbAudioProcessor::changeProgramName (int, const juce::String&) {}

//==============================================================================
void MyReverbAudioProcessor::prepareToPlay (double sr, int /*samplesPerBlock*/)
{
    engine.prepare (sr);
}

void MyReverbAudioProcessor::releaseResources()
{
    engine.reset();
}

#ifndef JucePlugin_PreferredChannelConfigurations
bool MyReverbAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    // stereo out; stereo or mono in
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo()) return false;
    const auto in = layouts.getMainInputChannelSet();
    return in == juce::AudioChannelSet::stereo() || in == juce::AudioChannelSet::mono();
}
#endif

//==============================================================================
void MyReverbAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    if (buffer.getNumChannels() < 2) return;

    // mono in → duplicate to both channels
    if (getTotalNumInputChannels() == 1)
        buffer.copyFrom (1, 0, buffer, 0, 0, buffer.getNumSamples());

    mr::Params p;
    p.size       = raw.size->load();
    p.decay      = raw.decay->load();
    p.preDelayMs = raw.preDelay->load();
    p.diffusion  = raw.diffusion->load();
    p.damping    = raw.damping->load();
    p.bass       = raw.bass->load();
    p.lowCut     = raw.lowCut->load();
    p.highCut    = raw.highCut->load();
    p.mod        = raw.mod->load();
    p.shimmer    = raw.shimmer->load();
    p.reverse    = raw.reverse->load();
    p.noise      = raw.noise->load();
    p.duck       = raw.duck->load();
    p.width      = raw.width->load();
    p.dry        = raw.dry->load();
    p.wet        = raw.wet->load();
    p.freeze     = raw.freeze->load() > 0.5f;
    engine.setParams (p);

    engine.process (buffer.getWritePointer (0), buffer.getWritePointer (1), buffer.getNumSamples());
}

//==============================================================================
bool MyReverbAudioProcessor::hasEditor() const { return true; }

juce::AudioProcessorEditor* MyReverbAudioProcessor::createEditor()
{
    return new MyReverbAudioProcessorEditor (*this);
}

void MyReverbAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty ("program", currentProgram, nullptr);
    std::unique_ptr<juce::XmlElement> xml (state.createXml());
    copyXmlToBinary (*xml, destData);
}

void MyReverbAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xml (getXmlFromBinary (data, sizeInBytes));
    if (xml && xml->hasTagName (apvts.state.getType()))
    {
        auto tree = juce::ValueTree::fromXml (*xml);
        currentProgram = juce::jlimit (0, kNumPresets - 1, (int) tree.getProperty ("program", 0));
        apvts.replaceState (tree);
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new MyReverbAudioProcessor();
}
