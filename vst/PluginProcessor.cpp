#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <memory>

juce::AudioProcessorValueTreeState::ParameterLayout
ReversonAudioProcessor::createParameterLayout() {
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    auto add = [&](const juce::String& id, const juce::String& name, float def) {
        layout.add(std::make_unique<juce::AudioParameterFloat>(
            id, name, juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f), def));
    };
    /* 6-knob ergonomic UI; the 13 internal params are derived via
       Reverson_map6 so the knobs never fight. Page 3 adds the 5-position
       mode switch plus the trig/predelay knobs. */
    add("mix", "Mix", 0.65f);
    add("rev", "Rev", 0.50f);
    add("space", "Space", 0.60f);
    add("tone", "Tone", 0.50f);
    add("grain", "Grain", 0.60f);
    add("duck", "Duck", 0.40f);
    add("trig", "Trig", 0.35f);
    add("predelay", "Predelay", 0.0f);
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "mode", "Mode", juce::NormalisableRange<float>(0.0f, 1.0f, 0.2f), 0.0f));
    layout.add(std::make_unique<juce::AudioParameterBool>("bypass", "Bypass", false));
    return layout;
}

ReversonAudioProcessor::ReversonAudioProcessor()
    : AudioProcessor(BusesProperties().withInput("Input", juce::AudioChannelSet::stereo(), true)
                                           .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "Parameters", createParameterLayout()) {}

void ReversonAudioProcessor::prepareToPlay(double sampleRate, int) {
    if (sampleRate < 8000.0 || sampleRate > 192000.0) sampleRate = 44100.0;
    uint32_t need = Reverson_state_size((float)sampleRate);
    stateMem.resize((need + sizeof(float) - 1u) / sizeof(float));
    core = Reverson_init(stateMem.data(), (uint32_t)(stateMem.size() * sizeof(float)), (float)sampleRate);
    if (core != nullptr) Reverson_reset(core);
}

void ReversonAudioProcessor::releaseResources() {
    core = nullptr;
    stateMem.clear();
}

void ReversonAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) {
    juce::ScopedNoDenormals noDenormals;
    auto* pMix = apvts.getRawParameterValue("mix");
    auto* pRev = apvts.getRawParameterValue("rev");
    auto* pSpace = apvts.getRawParameterValue("space");
    auto* pTone = apvts.getRawParameterValue("tone");
    auto* pGrain = apvts.getRawParameterValue("grain");
    auto* pDuck = apvts.getRawParameterValue("duck");
    auto* pTrig = apvts.getRawParameterValue("trig");
    auto* pPredelay = apvts.getRawParameterValue("predelay");
    auto* pMode = apvts.getRawParameterValue("mode");
    auto* pBypass = apvts.getRawParameterValue("bypass");

    if (core == nullptr) { buffer.clear(); return; }
    if (*pBypass > 0.5f) return;  /* bypass: dry passthrough (VST3 processes in place) */

    Reverson_set_6knob(core, *pMix, *pRev, *pSpace, *pTone, *pGrain, *pDuck);
    Reverson_set_param(core, REVERSON_PARAM_TRIG, *pTrig);
    Reverson_set_param(core, REVERSON_PARAM_PREDELAY, *pPredelay);
    {
        int mode = (int)(*pMode * 5.0f + 0.5f);   /* 0 = custom knobs */
        if (mode >= 1) {
            ReversonParams mp;
            Reverson_mode(mode, &mp);
            Reverson_set_param(core, REVERSON_PARAM_MIX, mp.mix);
            Reverson_set_param(core, REVERSON_PARAM_DECAY, mp.decay);
            Reverson_set_param(core, REVERSON_PARAM_TONE, mp.tone);
            Reverson_set_param(core, REVERSON_PARAM_REVLEN, mp.revlen);
            Reverson_set_param(core, REVERSON_PARAM_DUCK, mp.duck);
            Reverson_set_param(core, REVERSON_PARAM_GATE, mp.gate);
            Reverson_set_param(core, REVERSON_PARAM_SHAPE, mp.shape);
            Reverson_set_param(core, REVERSON_PARAM_MOD, mp.mod);
            Reverson_set_param(core, REVERSON_PARAM_SAT, mp.sat);
            Reverson_set_param(core, REVERSON_PARAM_WIDTH, mp.width);
            Reverson_set_param(core, REVERSON_PARAM_DENSITY, mp.density);
            Reverson_set_param(core, REVERSON_PARAM_BASS, mp.bass);
            Reverson_set_param(core, REVERSON_PARAM_DIFFUSION, mp.diffusion);
        }
    }

    const int numSamples = buffer.getNumSamples();
    /* stereo-in: the mono sum drives the wet engine, the dry image is kept */
    const float* inL = buffer.getReadPointer(0);
    const float* inR = buffer.getNumChannels() > 1 ? buffer.getReadPointer(1) : nullptr;
    float* outL = buffer.getWritePointer(0);
    float* outR = buffer.getNumChannels() > 1 ? buffer.getWritePointer(1) : nullptr;

    for (int i = 0; i < numSamples; ++i) {
        float l = 0.0f, r = 0.0f;
        float xl = inL[i];
        float xr = (inR != nullptr) ? inR[i] : xl;
        Reverson_process_stereo(core, xl, xr, &l, &r);
        if (outR != nullptr) {
            outL[i] = l;
            outR[i] = r;
        } else {
            outL[i] = 0.5f * (l + r);
        }
    }
}

bool ReversonAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const {
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::mono()
        && layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;
    if (layouts.getMainInputChannelSet() != juce::AudioChannelSet::mono()
        && layouts.getMainInputChannelSet() != juce::AudioChannelSet::stereo())
        return false;
    return true;
}

juce::AudioProcessorEditor* ReversonAudioProcessor::createEditor() {
    return new ReversonAudioProcessorEditor(*this);
}

void ReversonAudioProcessor::getStateInformation(juce::MemoryBlock& destData) {
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml(state.createXml());
    copyXmlToBinary(*xml, destData);
}

void ReversonAudioProcessor::setStateInformation(const void* data, int sizeInBytes) {
    std::unique_ptr<juce::XmlElement> xml(getXmlFromBinary(data, sizeInBytes));
    if (xml != nullptr && xml->hasTagName(apvts.state.getType())) {
        apvts.replaceState(juce::ValueTree::fromXml(*xml));
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
    return new ReversonAudioProcessor();
}
