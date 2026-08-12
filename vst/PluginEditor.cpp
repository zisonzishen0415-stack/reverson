#include "PluginProcessor.h"
#include "PluginEditor.h"

const char* ReversonAudioProcessorEditor::ids[4][3] = {
    {"mix", "decay", "tone"},
    {"revlen", "duck", "gate"},
    {"density", "bass", "shape"},
    {"mod", "sat", "width"}
};
const char* ReversonAudioProcessorEditor::names[4][3] = {
    {"Mix", "Decay", "Tone"},
    {"RevLen", "Duck", "Gate"},
    {"Density", "Bass", "Shape"},
    {"Mod", "Sat", "Width"}
};

ReversonAudioProcessorEditor::ReversonAudioProcessorEditor(ReversonAudioProcessor& p)
    : AudioProcessorEditor(&p), processor(p) {
    title.setText("Reverson", juce::dontSendNotification);
    title.setJustificationType(juce::Justification::centred);
    title.setFont(juce::Font(20.0f, juce::Font::bold));
    addAndMakeVisible(title);

    pageButton.setButtonText("P1");
    pageButton.onClick = [this] { setPage((currentPage + 1) % 4); };
    addAndMakeVisible(pageButton);

    bypassButton.setButtonText("BYPASS");
    addAndMakeVisible(bypassButton);
    bypassAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        processor.apvts, "bypass", bypassButton);

    for (int i = 0; i < 3; ++i) {
        knobs[i].setSliderStyle(juce::Slider::RotaryVerticalDrag);
        knobs[i].setTextBoxStyle(juce::Slider::TextBoxBelow, false, 60, 16);
        knobs[i].setRange(0.0, 1.0, 0.001);
        addAndMakeVisible(knobs[i]);
        knobLabels[i].setJustificationType(juce::Justification::centred);
        addAndMakeVisible(knobLabels[i]);
    }
    setPage(0);
    setSize(300, 230);
}

void ReversonAudioProcessorEditor::setPage(int page) {
    currentPage = page;
    pageButton.setButtonText(page == 0 ? "P1" : (page == 1 ? "P2" : (page == 2 ? "P3" : "P4")));
    attachments[0].reset();
    attachments[1].reset();
    attachments[2].reset();
    for (int i = 0; i < 3; ++i) {
        knobLabels[i].setText(names[page][i], juce::dontSendNotification);
        attachments[i] = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
            processor.apvts, ids[page][i], knobs[i]);
    }
}

void ReversonAudioProcessorEditor::resized() {
    auto area = getLocalBounds();
    title.setBounds(area.removeFromTop(30));
    auto knobArea = area.removeFromTop(160);
    int w = knobArea.getWidth() / 3;
    for (int i = 0; i < 3; ++i) {
        auto k = knobArea.removeFromLeft(w);
        knobLabels[i].setBounds(k.removeFromTop(22));
        knobs[i].setBounds(k.reduced(8));
    }
    auto bottom = area.removeFromTop(32);
    pageButton.setBounds(bottom.removeFromLeft(bottom.getWidth() / 2).withSizeKeepingCentre(64, 26));
    bypassButton.setBounds(bottom.withSizeKeepingCentre(80, 26));
}
