#pragma once
#include <JuceHeader.h>
#include <memory>

class ReversonAudioProcessor;
class PresetStrip;

/* Zoom MS-series style editor, AmpNeve-grade polish with a grass-green
   palette (the MS/G1 series LCDs are green backlit, so the LCD reads like
   the real pedal). LCD screen (effect name, focused param with big value
   + bar, and the three knob slots K1/K2/K3) above FOUR rotary knobs:
   Mix is the resident rightmost knob (like AmpNeve's resident INPUT),
   the three module knobs switch per page. Module tabs REV/TONE/MODE,
   a preset strip (◀ name ▶ | SAVE DEL) and a BYPASS chip (toggle with
   LED, AmpNeve-style). Params: Mix resident; P1 Rev/Space, P2 Tone/
   Grain/Duck, P3 Mode/Trig/Predelay (mirrors the pedal page model). */
class ReversonAudioProcessorEditor : public juce::AudioProcessorEditor,
                                     private juce::Timer {
public:
    ReversonAudioProcessorEditor(ReversonAudioProcessor& p);
    ~ReversonAudioProcessorEditor() override;
    void resized() override;

private:
    struct FactoryPreset {
        const char* name;
        float v[9];   /* mix rev space tone grain duck trig predelay mode */
    };
    static const FactoryPreset factoryPresets[5];
    /* knob positions the UI jumps to when the user picks mode 1..5:
       {mix, rev, space, tone, grain, duck, trig, predelay} - the mode
       switch then returns to 0 (manual) so every knob stays editable;
       the settings live only in this plugin instance until saved. */
    static const float modeKnobPresets[5][8];

    void setPage(int page);
    void setFocus(int slot);
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    bool keyPressed(const juce::KeyPress&) override;
    void timerCallback() override;

    void refreshPresetList();
    void applyFactoryPreset(int index);
    void applyModeToKnobs(int mode);
    void armModeDebounce();      /* restart the mode-release debounce timer */
    void saveUserPreset();
    void deleteUserPreset();
    void loadUserPreset(const juce::File& file);
    void cyclePreset(int dir);
    juce::File presetDir() const;

    juce::Rectangle<int> lcdRect() const;
    juce::Rectangle<int> slotRect(int slot) const;
    juce::Rectangle<int> knobRect(int index) const;   /* 0..2 page knobs, 3 = Mix */

    ReversonAudioProcessor& processor;

    std::unique_ptr<juce::LookAndFeel> appLaf;    /* AmpNeve-style look */
    std::unique_ptr<juce::ComponentBoundsConstrainer> resizeConstrainer;
    std::unique_ptr<PresetStrip> presetStrip;
    juce::Array<juce::File> userPresetFiles;

    juce::TextButton pageTabs[3];                 /* REV / TONE / MODE */
    juce::ToggleButton bypassButton;              /* AmpNeve-style BYPASS chip */
    juce::Slider knobs[3];                        /* page knobs */
    juce::Slider mixKnob;                         /* resident Mix (rightmost) */
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachments[3];
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> mixAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> bypassAttachment;

    int currentPage = 0;
    int focusedSlot = 0;
    int modeDebounceLeft = 0;   /* timer ticks left before applying the mode */
    juce::Rectangle<int> pageHitRect;             /* LCD header click cycles page */

    static const char* ids[3][3];
    static const char* names[3][3];
    static const char* moduleNames[3];
};
