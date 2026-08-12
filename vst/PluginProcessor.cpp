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
    add("mix", "Mix", 0.55f);
    add("decay", "Decay", 0.60f);
    add("tone", "Tone", 0.60f);
    add("revlen", "RevLen", 0.40f);
    add("duck", "Duck", 0.50f);
    add("gate", "Gate", 0.00f);
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
    if (core == nullptr) return;

    auto* pMix = apvts.getRawParameterValue("mix");
    auto* pDecay = apvts.getRawParameterValue("decay");
    auto* pTone = apvts.getRawParameterValue("tone");
    auto* pRevLen = apvts.getRawParameterValue("revlen");
    auto* pDuck = apvts.getRawParameterValue("duck");
    auto* pGate = apvts.getRawParameterValue("gate");
    Reverson_set_param(core, REVERSON_PARAM_MIX, *pMix);
    Reverson_set_param(core, REVERSON_PARAM_DECAY, *pDecay);
    Reverson_set_param(core, REVERSON_PARAM_TONE, *pTone);
    Reverson_set_param(core, REVERSON_PARAM_REVLEN, *pRevLen);
    Reverson_set_param(core, REVERSON_PARAM_DUCK, *pDuck);
    Reverson_set_param(core, REVERSON_PARAM_GATE, *pGate);

    const int numSamples = buffer.getNumSamples();
    const float* in = buffer.getReadPointer(0);
    float* outL = buffer.getWritePointer(0);
    float* outR = buffer.getNumChannels() > 1 ? buffer.getWritePointer(1) : nullptr;

    for (int i = 0; i < numSamples; ++i) {
        float l = 0.0f, r = 0.0f;
        Reverson_process(core, in[i], &l, &r);
        outL[i] = l;
        if (outR != nullptr) outR[i] = r;
    }
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