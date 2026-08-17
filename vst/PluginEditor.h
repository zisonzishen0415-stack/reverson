#pragma once
#include <JuceHeader.h>
#include <memory>

class ReversonAudioProcessor;

/* Zoom MS-series style editor, AmpNeve-grade polish with a grass-green
   palette (the MS/G1 series LCDs are green backlit, so the LCD reads like
   the real pedal). LCD screen (effect name, focused param with big value
   + bar, and the three knob slots K1/K2/K3) above three rotary knobs;
   module tabs REV/TONE/MODE replace the PAGE button, plus a BYPASS chip.
   All 9 params are laid out as 3 pages x 3 knobs, mirroring the pedal's
   page model (P3 = Mode/Trig/Predelay). */
class ReversonAudioProcessorEditor : public juce::AudioProcessorEditor,
                                     private juce::Timer {
public:
    explicit ReversonAudioProcessorEditor(ReversonAudioProcessor& p);
    ~ReversonAudioProcessorEditor() override;
    void resized() override;

private:
    void setPage(int page);
    void setFocus(int slot);
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void timerCallback() override;

    juce::Rectangle<int> lcdRect() const;
    juce::Rectangle<int> slotRect(int slot) const;
    juce::Rectangle<int> knobRect(int index) const;

    ReversonAudioProcessor& processor;

    std::unique_ptr<juce::LookAndFeel> appLaf;    /* AmpNeve-style look */
    std::unique_ptr<juce::ComponentBoundsConstrainer> resizeConstrainer;

    juce::TextButton pageTabs[3];                 /* REV / TONE / MODE */
    juce::TextButton bypassButton;
    juce::Slider knobs[3];
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachments[3];
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> bypassAttachment;

    int currentPage = 0;
    int focusedSlot = 0;
    juce::Rectangle<int> pageHitRect;             /* LCD header click cycles page */

    static const char* ids[3][3];
    static const char* names[3][3];
    static const char* moduleNames[3];
};
