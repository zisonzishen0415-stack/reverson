#pragma once
#include <JuceHeader.h>
#include <memory>

class ReversonAudioProcessor;

class ReversonAudioProcessorEditor : public juce::AudioProcessorEditor {
public:
    explicit ReversonAudioProcessorEditor(ReversonAudioProcessor& p);
    ~ReversonAudioProcessorEditor() override = default;
    void resized() override;

private:
    void setPage(int page);
    ReversonAudioProcessor& processor;
    juce::Label title;
    juce::TextButton pageButton;
    juce::Slider knobs[3];
    juce::Label knobLabels[3];
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachments[3];
    juce::ToggleButton bypassButton;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> bypassAttachment;
    int currentPage = 0;
    static const char* ids[2][3];
    static const char* names[2][3];
};
