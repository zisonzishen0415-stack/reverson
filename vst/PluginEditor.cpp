#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cmath>

/* Pedal-style rotary knob: dark body with a pointer line, like the G1on /
   MS-series parameter knobs. */
class PedalKnobLookAndFeel : public juce::LookAndFeel_V4 {
public:
    void drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
                          float sliderPos, float rotaryStartAngle, float rotaryEndAngle,
                          juce::Slider&) override {
        auto bounds = juce::Rectangle<int>(x, y, width, height).toFloat().reduced(5.0f);
        auto cx = bounds.getCentreX();
        auto cy = bounds.getCentreY();
        auto r = bounds.getWidth() * 0.5f;
        g.setColour(juce::Colour(0xff272a30));
        g.fillEllipse(bounds);
        g.setColour(juce::Colour(0xff5b6069));
        g.drawEllipse(bounds, 2.0f);
        float angle = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);
        juce::Point<float> tip(cx + r * 0.72f * std::cos(angle),
                               cy + r * 0.72f * std::sin(angle));
        g.setColour(juce::Colour(0xffe8eef5));
        g.drawLine(cx, cy, tip.x, tip.y, 3.0f);
        g.fillEllipse(cx - 3.5f, cy - 3.5f, 7.0f, 7.0f);
    }
};

const char* ReversonAudioProcessorEditor::ids[2][3] = {
    {"mix", "rev", "space"},
    {"tone", "grain", "duck"}
};
const char* ReversonAudioProcessorEditor::names[2][3] = {
    {"Mix", "Rev", "Space"},
    {"Tone", "Grain", "Duck"}
};

static const juce::Colour lcdBg(0xff0d2239);     /* deep blue LCD backlight */
static const juce::Colour lcdDim(0xff5a7ea0);    /* dimmed pixel */
static const juce::Colour lcdLit(0xffcfe9ff);    /* lit pixel */
static const juce::Colour body(0xff20232a);      /* pedal chassis */
static const juce::Colour bezel(0xff4a4e57);     /* screen bezel */

ReversonAudioProcessorEditor::ReversonAudioProcessorEditor(ReversonAudioProcessor& p)
    : AudioProcessorEditor(&p), processor(p) {
    pageButton.setButtonText("PAGE");
    pageButton.onClick = [this] { setPage((currentPage + 1) % 2); };
    addAndMakeVisible(pageButton);

    bypassButton.setButtonText("BYPASS");
    addAndMakeVisible(bypassButton);
    bypassAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        processor.apvts, "bypass", bypassButton);

    for (int i = 0; i < 3; ++i) {
        knobs[i].setSliderStyle(juce::Slider::RotaryVerticalDrag);
        knobs[i].setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
        knobs[i].setRange(0.0, 1.0, 0.001);
        addAndMakeVisible(knobs[i]);
    }
    knobLaf = std::make_unique<PedalKnobLookAndFeel>();
    setLookAndFeel(knobLaf.get());
    setPage(0);
    setSize(400, 380);
    startTimerHz(30);   /* keep the LCD values live while knobs move */
}

juce::Rectangle<int> ReversonAudioProcessorEditor::lcdRect() const {
    return { 16, 16, getWidth() - 32, 186 };
}

juce::Rectangle<int> ReversonAudioProcessorEditor::slotRect(int slot) const {
    auto screen = lcdRect().reduced(8, 8);
    auto bottom = screen.removeFromBottom(56);
    int w = bottom.getWidth() / 3;
    juce::Rectangle<int> r(bottom.getX() + slot * w, bottom.getY(), w, bottom.getHeight());
    return r.reduced(4, 2);
}

void ReversonAudioProcessorEditor::setPage(int page) {
    currentPage = page;
    attachments[0].reset();
    attachments[1].reset();
    attachments[2].reset();
    for (int i = 0; i < 3; ++i) {
        attachments[i].reset();
        bool has = (ids[currentPage][i][0] != '\0');
        knobs[i].setEnabled(has);
        if (has) {
            attachments[i] = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
                processor.apvts, ids[currentPage][i], knobs[i]);
        }
    }
    repaint();
}

void ReversonAudioProcessorEditor::setFocus(int slot) {
    focusedSlot = slot;
    repaint();
}

void ReversonAudioProcessorEditor::mouseDown(const juce::MouseEvent& e) {
    for (int i = 0; i < 3; ++i) {
        if (slotRect(i).contains(e.getPosition())) {
            setFocus(i);
            return;
        }
    }
}

void ReversonAudioProcessorEditor::paint(juce::Graphics& g) {
    g.fillAll(body);

    /* --- LCD bezel + screen ------------------------------------------- */
    auto lcd = lcdRect();
    g.setColour(bezel);
    g.fillRect(lcd);
    auto screen = lcd.reduced(5);
    g.setColour(lcdBg);
    g.fillRect(screen);

    /* header: effect name + page */
    auto header = screen.removeFromTop(30);
    g.setColour(lcdLit);
    g.setFont(juce::Font(juce::Font::getDefaultMonospacedFontName(), 15.0f, juce::Font::bold));
    g.drawText("REVERSON", header.removeFromLeft(180), juce::Justification::centredLeft, false);
    g.setFont(juce::Font(juce::Font::getDefaultMonospacedFontName(), 13.0f, juce::Font::bold));
    g.drawText(juce::String("P") + juce::String(currentPage + 1), header,
               juce::Justification::centredRight, false);

    /* --- focused param (middle) ---------------------------------------- */
    auto mid = screen.removeFromTop(78);
    const char* focusName = names[currentPage][focusedSlot];
    float focusVal = 0.0f;
    if (auto* v = processor.apvts.getRawParameterValue(ids[currentPage][focusedSlot]))
        focusVal = v->load();

    g.setColour(lcdDim);
    g.setFont(juce::Font(juce::Font::getDefaultMonospacedFontName(), 13.0f, juce::Font::plain));
    g.drawText(focusName, mid.removeFromTop(20), juce::Justification::centred, false);

    g.setColour(lcdLit);
    g.setFont(juce::Font(juce::Font::getDefaultMonospacedFontName(), 30.0f, juce::Font::bold));
    g.drawText(juce::String((int)(focusVal * 100.0f + 0.5f)), mid.removeFromTop(38),
               juce::Justification::centred, false);

    /* value bar */
    auto bar = mid.removeFromTop(14).reduced(30, 0);
    g.setColour(lcdDim);
    g.fillRect(bar);
    g.setColour(lcdLit);
    g.fillRect(bar.withWidth((int)(bar.getWidth() * focusVal)));

    /* --- knob slots (bottom of screen) --------------------------------- */
    for (int i = 0; i < 3; ++i) {
        auto r = slotRect(i);
        bool focused = (i == focusedSlot);
        if (focused) {
            g.setColour(lcdLit);
            g.fillRect(r);
            g.setColour(lcdBg);
        } else {
            g.setColour(lcdDim);
        }
        g.setFont(juce::Font(juce::Font::getDefaultMonospacedFontName(), 11.0f,
                             focused ? juce::Font::bold : juce::Font::plain));
        g.drawText(juce::String("K") + juce::String(i + 1), r.removeFromTop(14),
                   juce::Justification::centred, false);
        g.setFont(juce::Font(juce::Font::getDefaultMonospacedFontName(), 12.0f,
                             focused ? juce::Font::bold : juce::Font::plain));
        g.drawText(names[currentPage][i], r.removeFromTop(16), juce::Justification::centred, false);
        juce::String sv("--");
        if (auto* v = processor.apvts.getRawParameterValue(ids[currentPage][i]))
            sv = juce::String((int)(v->load() * 100.0f + 0.5f));
        g.drawText(sv, r, juce::Justification::centred, false);
    }
}

void ReversonAudioProcessorEditor::resized() {
    auto area = getLocalBounds();
    auto lcd = lcdRect();
    area.removeFromTop(lcd.getBottom());

    auto knobsArea = area.removeFromTop(120);
    int w = knobsArea.getWidth() / 3;
    for (int i = 0; i < 3; ++i) {
        auto r = knobsArea.removeFromLeft(w).reduced(30, 8);
        knobs[i].setBounds(r);
    }

    auto bottom = area.reduced(16, 6);
    pageButton.setBounds(bottom.removeFromLeft(bottom.getWidth() / 2).withSizeKeepingCentre(90, 30));
    bypassButton.setBounds(bottom.withSizeKeepingCentre(110, 30));
}

void ReversonAudioProcessorEditor::timerCallback() {
    repaint();
}
