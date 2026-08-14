#pragma once
#include <JuceHeader.h>
#include <memory>

class ReversonAudioProcessor;

/* Zoom MS-series style editor: an LCD screen (effect name, focused param
   with big value + bar, and the three knob slots K1/K2/K3) above three
   rotary knobs, plus a PAGE button (like pressing knob 3 on the pedal)
   and a BYPASS toggle. All 9 params are laid out as 3 pages x 3 knobs,
   mirroring the pedal's page model (P3 = Mode/Trig/Predelay). */
class ReversonAudioProcessorEditor : public juce::AudioProcessorEditor,
                                     private juce::Timer {
public:
    explicit ReversonAudioProcessorEditor(ReversonAudioProcessor& p);
    ~ReversonAudioProcessorEditor() override = default;
    void resized() override;

private:
    void setPage(int page);
    void setFocus(int slot);
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void timerCallback() override;

    juce::Rectangle<int> lcdRect() const;
    juce::Rectangle<int> slotRect(int slot) const;

    ReversonAudioProcessor& processor;

    std::unique_ptr<juce::LookAndFeel> knobLaf;   /* pedal-style rotary knobs */

    juce::TextButton pageButton;
    juce::ToggleButton bypassButton;
    juce::Slider knobs[3];
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachments[3];
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> bypassAttachment;

    int currentPage = 0;
    int focusedSlot = 0;

    static const char* ids[3][3];
    static const char* names[3][3];
};
