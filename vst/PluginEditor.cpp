#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cmath>

/* ------------------------------------------------------------------ */
/* grass-green palette (Zoom MS/G1-series green-backlit LCD theme)     */
/* ------------------------------------------------------------------ */
namespace Ui {
static const juce::Colour panel(0xff17191e);      /* chassis */
static const juce::Colour chip(0xff22241f);       /* control chips (green-tinted dark) */
static const juce::Colour chipHover(0xff2a2d26);
static const juce::Colour chipBorder(0xff3a3d30);
static const juce::Colour text(0xffeef2e8);
static const juce::Colour textDim(0xff93a08b);
static const juce::Colour lcdBg(0xff0d2416);      /* green LCD backlight */
static const juce::Colour lcdLit(0xffa9e87c);     /* lit pixel */
static const juce::Colour lcdDim(0xff3f7a3a);     /* dimmed pixel */
static const juce::Colour lcdBorder(0xff4a4e57);
static const juce::Colour accent(0xff8fce4d);     /* knob arc / LED / active tab */
static const juce::Colour accentSoft(0x338fce4d); /* accent @ 20% alpha */
static const juce::Colour slotTrough(0xff1e4628);
static const juce::Colour ledOff(0xff3a3f3a);
static const juce::Colour mGreen(0xff6ede6a);     /* input level ok */
static const juce::Colour mYellow(0xffe8c34a);    /* input level warm */
static const juce::Colour mRed(0xffe05252);       /* input level hot */
static const juce::Colour mBg(0xff12331d);        /* meter trough */
}

/* dashed arc helper (JUCE has no dashed path stroke that is version-safe) */
static void drawDashedArc(juce::Graphics& g, juce::Point<float> centre, float radius,
                          float start, float end, float dashFrac, float gapFrac,
                          float thickness, juce::Colour colour) {
    const float total = end - start;
    const float dash = total * dashFrac, gap = total * gapFrac;
    float pos = 0.0f;
    juce::Path p;
    while (pos < total) {
        float a = start + pos;
        float stop = juce::jmin(pos + dash, total);
        p.addArc(centre.x - radius, centre.y - radius, radius * 2.0f, radius * 2.0f,
                 a, start + stop, true);
        pos = stop + gap;
    }
    g.setColour(colour);
    g.strokePath(p, juce::PathStrokeType(thickness));
}

/* ------------------------------------------------------------------ */
/* App look-and-feel: ticks knob, chip buttons (bypass/tabs), dark     */
/* popup menus.                                                        */
/* ------------------------------------------------------------------ */
class AppLookAndFeel : public juce::LookAndFeel_V4 {
public:
    void drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
                          float sliderPos, float rotaryStartAngle, float rotaryEndAngle,
                          juce::Slider& slider) override {
        using namespace Ui;
        auto bounds = juce::Rectangle<int>(x, y, width, height).toFloat().reduced(5.0f);
        auto cx = bounds.getCentreX();
        auto cy = bounds.getCentreY();
        auto r = bounds.getWidth() * 0.5f;
        bool hot = slider.isMouseOverOrDragging() && slider.isEnabled();
        float angle = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);

        /* body: outer dark ring, lighter inner disc, top-left sheen */
        g.setColour(panel);
        g.fillEllipse(bounds);
        g.setColour(juce::Colour(0xff2c3028));
        g.fillEllipse(bounds.reduced(r * 0.14f));
        g.setColour(juce::Colour(0x22ffffff));
        g.fillEllipse(juce::Rectangle<float>(cx - r * 0.55f, cy - r * 0.55f,
                                             r * 1.1f, r * 1.1f)
                          .translated(-r * 0.18f, -r * 0.18f));

        /* dashed tick ring */
        float tr = r * 0.82f;
        drawDashedArc(g, juce::Point<float>(cx, cy), tr,
                      rotaryStartAngle, rotaryEndAngle, 0.055f, 0.05f,
                      1.2f, juce::Colour(0xff3a4036));

        /* translucent value arc + solid core */
        if (sliderPos > 0.001f) {
            juce::Path arc;
            arc.addArc(cx - tr, cy - tr, tr * 2.0f, tr * 2.0f, rotaryStartAngle, angle, true);
            g.setColour(accentSoft);
            g.strokePath(arc, juce::PathStrokeType(4.5f));
            g.setColour(accent);
            g.strokePath(arc, juce::PathStrokeType(2.0f));
        }

        /* pointer with glow */
        float pr = r * 0.78f;
        auto tip = juce::Point<float>(cx + pr * std::cos(angle), cy + pr * std::sin(angle));
        g.setColour(accentSoft);
        g.drawLine(cx, cy, tip.x, tip.y, hot ? 6.0f : 4.5f);
        g.setColour(hot ? juce::Colour(0xffffffff) : lcdLit);
        g.drawLine(cx, cy, tip.x, tip.y, 2.5f);

        /* centre hub */
        g.setColour(panel);
        g.fillEllipse(cx - r * 0.16f, cy - r * 0.16f, r * 0.32f, r * 0.32f);
        g.setColour(chipBorder);
        g.drawEllipse(cx - r * 0.16f, cy - r * 0.16f, r * 0.32f, r * 0.32f, 1.0f);
    }

    void drawButtonBackground(juce::Graphics& g, juce::Button& b,
                              const juce::Colour&, bool, bool) override {
        using namespace Ui;
        auto r = b.getLocalBounds().toFloat();
        const juce::String tag = b.getName();
        bool on = b.getToggleState();
        bool hot = b.isMouseOver() || b.isDown();
        bool en = b.isEnabled();
        juce::Colour bg = hot ? chipHover : chip;
        if (tag == "tab" && on) bg = accentSoft;
        g.setColour(bg);
        g.fillRoundedRectangle(r, 7.0f);
        g.setColour(chipBorder);
        g.drawRoundedRectangle(r.reduced(0.5f), 7.0f, 1.0f);

        if (tag == "bypass") {
            auto led = juce::Rectangle<float>(10.0f, (r.getHeight() - 8.0f) * 0.5f, 8.0f, 8.0f);
            if (en && on) { g.setColour(accentSoft); g.fillEllipse(led.expanded(3.0f)); }
            g.setColour(en && on ? accent : ledOff);
            g.fillEllipse(led);
            g.setColour(en ? (on ? text : textDim) : textDim.withAlpha(0.5f));
            g.setFont(juce::Font(12.0f, juce::Font::bold));
            g.drawText(b.getButtonText(), r.withTrimmedLeft(26.0f),
                       juce::Justification::centredLeft, false);
        } else if (tag == "tab") {
            /* physical push-button: raised body + bevel + drop shadow,
             * sinks 1 px when pressed; active module gets an LED */
            bool down = b.isDown();
            auto body = r.reduced(0.5f).translated(0, down ? 1 : 0);
            g.setColour(juce::Colour(0x66000000));
            g.fillRoundedRectangle(body.translated(0, 2), 6.0f);
            juce::ColourGradient grad(juce::Colour(0xff34382b), 0, body.getY(),
                                      juce::Colour(0xff15170f), 0, body.getBottom(), false);
            g.setGradientFill(grad);
            g.fillRoundedRectangle(body, 6.0f);
            g.setColour(juce::Colour(0x2effffff));
            g.fillRoundedRectangle(body.withTrimmedBottom(body.getHeight() - 2.0f), 6.0f);
            g.setColour(on ? accent : chipBorder);
            g.drawRoundedRectangle(body, 6.0f, 1.0f);
            auto led = juce::Rectangle<float>(r.getCentreX() - 3.0f, r.getY() + 4.0f, 6.0f, 6.0f);
            if (on) { g.setColour(accentSoft); g.fillEllipse(led.expanded(2.0f)); }
            g.setColour(on ? accent : ledOff);
            g.fillEllipse(led);
        } else if (tag == "icon-left" || tag == "icon-right") {
            auto c = r.getCentre();
            float s = 4.5f;
            juce::Path p;
            if (tag == "icon-left")
                p.addTriangle(c.x + s * 0.5f, c.y - s, c.x + s * 0.5f, c.y + s, c.x - s, c.y);
            else
                p.addTriangle(c.x - s * 0.5f, c.y - s, c.x - s * 0.5f, c.y + s, c.x + s, c.y);
            g.setColour(en ? (hot ? text : textDim) : textDim.withAlpha(0.4f));
            g.fillPath(p);
        } else if (tag == "icon-save") {
            auto f = r.reduced(7.0f, 6.0f);
            g.setColour(en ? (hot ? text : textDim) : textDim.withAlpha(0.4f));
            g.fillRoundedRectangle(f, 2.0f);
            g.setColour(panel);
            g.fillRect(f.removeFromTop(f.getHeight() * 0.28f).reduced(3.0f, 1.0f));
            g.setColour(panel);
            g.fillRect(f.removeFromLeft(f.getWidth() * 0.38f).reduced(1.0f, 2.0f));
        } else if (tag == "icon-del") {
            auto c = r.getCentre();
            float s = 5.0f;
            g.setColour(en ? (hot ? text : textDim) : textDim.withAlpha(0.4f));
            g.drawLine(c.x - s, c.y - s, c.x + s, c.y + s, 1.8f);
            g.drawLine(c.x - s, c.y + s, c.x + s, c.y - s, 1.8f);
        }
    }

    void drawButtonText(juce::Graphics& g, juce::TextButton& b, bool, bool) override {
        using namespace Ui;
        const juce::String tag = b.getName();
        if (tag == "bypass") return;  /* drawn in background */
        auto r = b.getLocalBounds().toFloat();
        bool on = b.getToggleState();
        bool en = b.isEnabled();
        if (tag == "tab") r = r.withTrimmedTop(4.0f);   /* LED sits at the top */
        g.setColour(!en ? textDim.withAlpha(0.5f) : (on ? accent : text));
        g.setFont(juce::Font(12.0f, juce::Font::bold));
        g.drawText(b.getButtonText(), r, juce::Justification::centred, false);
    }

    void drawPopupMenuBackground(juce::Graphics& g, int width, int height) override {
        using namespace Ui;
        g.fillAll(panel);
        g.setColour(chipBorder);
        g.drawRect(0, 0, width, height, 1);
    }

    void drawPopupMenuItem(juce::Graphics& g, const juce::Rectangle<int>& area,
                           bool isSeparator, bool isActive, bool isHighlighted,
                           bool isTicked, bool hasSubMenu, const juce::String& text,
                           const juce::String& shortcutKeyText, const juce::Drawable* icon,
                           const juce::Colour* textColour) override {
        juce::ignoreUnused(textColour);
        using namespace Ui;
        if (isSeparator) {
            g.setColour(chipBorder);
            auto sep = area;
            g.fillRect(sep.removeFromBottom(1).reduced(8, 0));
            return;
        }
        auto r = area.reduced(2, 2);
        if (isHighlighted) {
            g.setColour(accentSoft);
            g.fillRoundedRectangle(r.toFloat(), 5.0f);
        }
        if (isTicked) {
            g.setColour(accent);
            g.setFont(juce::Font(12.0f, juce::Font::bold));
            auto tick = r;
            g.drawText("\u2713", tick.removeFromLeft(18), juce::Justification::centred, false);
        }
        g.setColour(isActive ? Ui::text : Ui::textDim.withAlpha(0.5f));
        g.setFont(juce::Font(13.0f));
        auto label = r;
        g.drawText(text, label.removeFromLeft(label.getWidth() - (hasSubMenu ? 16 : 0)),
                   juce::Justification::centredLeft, false);
    }
};

/* ------------------------------------------------------------------ */
/* Preset strip: ◀ name ▶ | SAVE DEL. Pure UI; all actions go through  */
/* callbacks wired by the editor.                                      */
/* ------------------------------------------------------------------ */
class PresetStrip : public juce::Component {
public:
    PresetStrip() {
        for (auto* b : { &leftBtn, &rightBtn, &nameBtn, &saveBtn, &delBtn })
            addAndMakeVisible(b);
        leftBtn.setButtonText("");   leftBtn.setName("icon-left");
        rightBtn.setButtonText("");  rightBtn.setName("icon-right");
        saveBtn.setButtonText("");   saveBtn.setName("icon-save");
        delBtn.setButtonText("");    delBtn.setName("icon-del");
        leftBtn.onClick = [this] { if (onCycle) onCycle(-1); };
        rightBtn.onClick = [this] { if (onCycle) onCycle(1); };
        nameBtn.onClick = [this] { showPopup(); };
        saveBtn.onClick = [this] { if (onSave) onSave(); };
        delBtn.onClick = [this] { if (onDelete) onDelete(); };
    }

    std::function<void(int)> onCycle;   /* dir ±1 */
    std::function<void(int)> onFactory; /* factory index 0..4 */
    std::function<void(int)> onUser;    /* index into user list */
    std::function<void()> onSave;
    std::function<void()> onDelete;

    void setLists(const juce::StringArray& factoryNames, const juce::StringArray& userNamesIn) {
        factoryNamesList = factoryNames;
        userNames = userNamesIn;
    }

    void setCurrent(const juce::String& name, int selectedId) {
        currentId = selectedId;
        nameBtn.setButtonText(name.isEmpty() ? "PRESET" : name);
        delBtn.setEnabled(selectedId >= 100);
    }

    juce::String currentName() const { return nameBtn.getButtonText(); }
    int currentIdForSave() const { return currentId; }

    void resized() override {
        auto r = getLocalBounds();
        auto h = r.getHeight();
        leftBtn.setBounds(r.removeFromLeft(h));
        rightBtn.setBounds(r.removeFromRight(h));
        auto icons = r.removeFromRight(2 * h);
        delBtn.setBounds(icons.removeFromRight(h));
        saveBtn.setBounds(icons);
        nameBtn.setBounds(r);
    }

private:
    void showPopup() {
        juce::PopupMenu menu;
        for (int i = 0; i < factoryNamesList.size(); ++i)
            menu.addItem(1 + i, factoryNamesList[i], true, currentId == 1 + i);
        menu.addSeparator();
        if (userNames.isEmpty())
            menu.addItem(-1, "(no custom presets)", false);
        else
            for (int i = 0; i < userNames.size(); ++i)
                menu.addItem(100 + i, userNames[i], true, currentId == 100 + i);
        menu.setLookAndFeel(&getLookAndFeel());
        menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(nameBtn),
                           [this](int result) {
                               if (result >= 1 && result <= 5 && onFactory) onFactory(result - 1);
                               else if (result >= 100 && onUser) onUser(result - 100);
                           });
    }

    juce::TextButton leftBtn, rightBtn, nameBtn, saveBtn, delBtn;
    juce::StringArray factoryNamesList, userNames;
    int currentId = 0;
};

/* ------------------------------------------------------------------ */
/* Editor                                                              */
/* ------------------------------------------------------------------ */
/* Module pages: 3 knobs each. REV (Mix/Rev/Space), TONE (Tone/Grain/
 * Duck), MODE (Mode/Trig/Predelay) - mirrors the pedal page model. */
const char* ReversonAudioProcessorEditor::ids[3][3] = {
    {"mix", "rev", "space"},
    {"tone", "grain", "duck"},
    {"mode", "trig", "predelay"}
};
const char* ReversonAudioProcessorEditor::names[3][3] = {
    {"Mix", "Rev", "Space"},
    {"Tone", "Grain", "Duck"},
    {"Mode", "Trig", "Predelay"}
};
const char* ReversonAudioProcessorEditor::moduleNames[3] = { "REV", "TONE", "MODE" };

/* Factory presets: {mix, rev, space, tone, grain, duck, trig, predelay,
 * mode}. The first three are the acceptance presets (same map6 curves as
 * the pedal + render tool); Wash/Gated drive the core mode tables with
 * level-friendly knobs. */
const ReversonAudioProcessorEditor::FactoryPreset
ReversonAudioProcessorEditor::factoryPresets[5] = {
    { "mbv",      { 0.60f, 0.85f, 0.55f, 0.50f, 0.60f, 0.10f, 0.35f, 0.10f, 0.0f } },
    { "diiv",     { 0.55f, 0.25f, 0.60f, 0.50f, 0.40f, 0.50f, 0.55f, 0.00f, 0.0f } },
    { "slowdive", { 0.70f, 0.35f, 0.85f, 0.45f, 0.55f, 0.30f, 0.70f, 0.00f, 0.0f } },
    { "Wash",     { 0.60f, 0.80f, 0.55f, 0.50f, 0.60f, 0.30f, 0.35f, 0.10f, 0.2f } },
    { "Gated",    { 0.70f, 0.95f, 0.45f, 0.50f, 0.50f, 0.10f, 0.35f, 0.00f, 0.6f } }
};

ReversonAudioProcessorEditor::ReversonAudioProcessorEditor(ReversonAudioProcessor& p)
    : AudioProcessorEditor(&p), processor(p) {
    appLaf = std::make_unique<AppLookAndFeel>();
    setLookAndFeel(appLaf.get());

    /* module tabs + bypass */
    for (int i = 0; i < 3; ++i) {
        pageTabs[i].setButtonText(moduleNames[i]);
        pageTabs[i].setName("tab");
        pageTabs[i].setRadioGroupId(991);
        pageTabs[i].setClickingTogglesState(true);
        pageTabs[i].onClick = [this, i] { setPage(i); };
        addAndMakeVisible(pageTabs[i]);
    }
    bypassButton.setName("bypass");
    addAndMakeVisible(bypassButton);
    bypassAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        processor.apvts, "bypass", bypassButton);

    /* preset strip */
    presetStrip = std::make_unique<PresetStrip>();
    presetStrip->onCycle = [this](int dir) { cyclePreset(dir); };
    presetStrip->onFactory = [this](int idx) { applyFactoryPreset(idx); };
    presetStrip->onUser = [this](int idx) {
        if (idx < userPresetFiles.size()) loadUserPreset(userPresetFiles[idx]);
    };
    presetStrip->onSave = [this] { saveUserPreset(); };
    presetStrip->onDelete = [this] { deleteUserPreset(); };
    addAndMakeVisible(presetStrip.get());

    /* knobs */
    for (int i = 0; i < 3; ++i) {
        knobs[i].setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        knobs[i].setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
        knobs[i].setRotaryParameters(juce::MathConstants<float>::pi * 0.75f,
                                     juce::MathConstants<float>::pi * 2.25f, true);
        knobs[i].setScrollWheelEnabled(true);
        knobs[i].onValueChange = [this, i] {
            if (knobs[i].isMouseButtonDown()) setFocus(i);
        };
        addAndMakeVisible(knobs[i]);
    }

    resizeConstrainer = std::make_unique<juce::ComponentBoundsConstrainer>();
    resizeConstrainer->setFixedAspectRatio(400.0 / 440.0);
    setConstrainer(resizeConstrainer.get());
    setResizeLimits(340, 374, 640, 704);
    setResizable(true, false);
    setPage(0);
    refreshPresetList();
    setSize(400, 440);
    startTimerHz(30);   /* keep LCD values live while knobs move */
}

ReversonAudioProcessorEditor::~ReversonAudioProcessorEditor() {
    stopTimer();
    setLookAndFeel(nullptr);
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

juce::Rectangle<int> ReversonAudioProcessorEditor::knobRect(int index) const {
    auto lcd = lcdRect();
    /* knob column spans from below the LCD to above the button row;
     * always square so the rotary is a circle at any editor size. */
    int top = lcd.getBottom() + 12;
    int bottom = getHeight() - 34 - 30 - 8;
    int h = bottom - top;
    if (h < 40) h = 40;
    auto kArea = juce::Rectangle<int>(lcd.getX(), top, lcd.getWidth(), h);
    int w = kArea.getWidth() / 4;
    int side = (w < h) ? w : h;
    int x = kArea.getX() + (index + 1) * w + (w - side) / 2;
    int y = kArea.getY() + (h - side) / 2;
    return juce::Rectangle<int>(x, y, side, side);
}

void ReversonAudioProcessorEditor::setPage(int page) {
    currentPage = page;
    for (int i = 0; i < 3; ++i) {
        pageTabs[i].setToggleState(i == page, juce::dontSendNotification);
        attachments[i].reset();
        bool has = (ids[currentPage][i][0] != '\0');
        knobs[i].setEnabled(has);
        if (has) {
            if (strcmp(ids[currentPage][i], "mode") == 0)
                knobs[i].setRange(0.0, 1.0, 0.2);   /* 5-position switch */
            else
                knobs[i].setRange(0.0, 1.0, 0.001);
            attachments[i] = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
                processor.apvts, ids[currentPage][i], knobs[i]);
            if (auto* p = processor.apvts.getParameter(ids[currentPage][i]))
                knobs[i].setDoubleClickReturnValue(true, (double)p->getDefaultValue());
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
        if (slotRect(i).contains(e.getPosition())) { setFocus(i); return; }
    }
    if (pageHitRect.contains(e.getPosition())) { setPage((currentPage + 1) % 3); return; }
}

bool ReversonAudioProcessorEditor::keyPressed(const juce::KeyPress& k) {
    if (k == juce::KeyPress::leftKey)  { cyclePreset(-1); return true; }
    if (k == juce::KeyPress::rightKey) { cyclePreset(1);  return true; }
    if (k == juce::KeyPress::spaceKey) {
        if (auto* param = processor.apvts.getParameter("bypass"))
            param->setValueNotifyingHost(param->getValue() < 0.5f ? 1.0f : 0.0f);
        return true;
    }
    return false;
}

void ReversonAudioProcessorEditor::paint(juce::Graphics& g) {
    using namespace Ui;
    g.fillAll(panel);

    auto lcd = lcdRect();
    g.setColour(lcdBorder);
    g.fillRect(lcd);
    auto screen = lcd.reduced(5);
    g.setColour(lcdBg);
    g.fillRect(screen);

    /* header: effect name + page (click cycles the module, pedal style) */
    auto header = screen.removeFromTop(30);
    g.setColour(lcdLit);
    g.setFont(juce::Font(juce::Font::getDefaultMonospacedFontName(), 15.0f, juce::Font::bold));
    g.drawText("REVERSON", header.removeFromLeft(180), juce::Justification::centredLeft, false);
    g.setFont(juce::Font(juce::Font::getDefaultMonospacedFontName(), 13.0f, juce::Font::bold));
    auto pageArea = header.removeFromRight(42);
    pageHitRect = pageArea;   /* click cycles the module (pedal style) */
    g.setColour(lcdLit);
    g.drawText(juce::String(moduleNames[currentPage]), pageArea,
               juce::Justification::centredRight, false);

    /* focused param (middle): name + big value + bar */
    auto mid = screen.removeFromTop(88);
    const char* focusName = names[currentPage][focusedSlot];
    float focusVal = 0.0f;
    if (auto* v = processor.apvts.getRawParameterValue(ids[currentPage][focusedSlot]))
        focusVal = v->load();

    g.setColour(lcdDim);
    g.setFont(juce::Font(juce::Font::getDefaultMonospacedFontName(), 13.0f, juce::Font::plain));
    g.drawText(focusName, mid.removeFromTop(20), juce::Justification::centred, false);

    g.setColour(lcdLit);
    g.setFont(juce::Font(juce::Font::getDefaultMonospacedFontName(), 30.0f, juce::Font::bold));
    {
        static const char* MODE_NAMES[6] = {"Off", "Wash", "Reverse", "Gated", "Shoegaze", "Space"};
        juce::String focusText;
        if (juce::String(ids[currentPage][focusedSlot]) == "mode") {
            int mi = (int)(focusVal * 5.0f + 0.5f);
            if (mi < 0) mi = 0;
            if (mi > 5) mi = 5;
            focusText = MODE_NAMES[mi];
            g.setFont(juce::Font(juce::Font::getDefaultMonospacedFontName(), 22.0f, juce::Font::bold));
        } else {
            focusText = juce::String((int)(focusVal * 100.0f + 0.5f));
        }
        g.drawText(focusText, mid.removeFromTop(38), juce::Justification::centred, false);
    }

    /* value bar */
    auto bar = mid.removeFromTop(14).reduced(30, 0);
    g.setColour(slotTrough);
    g.fillRect(bar);
    g.setColour(accent);
    g.fillRect(bar.withWidth((int)(bar.getWidth() * focusVal)));

    /* knob slots (bottom of screen) */
    for (int i = 0; i < 3; ++i) {
        auto r = slotRect(i);
        bool focused = (i == focusedSlot);
        g.setColour(lcdDim);
        g.drawRect(r, 1);
        g.setFont(juce::Font(juce::Font::getDefaultMonospacedFontName(), 11.0f,
                             focused ? juce::Font::bold : juce::Font::plain));
        g.drawText(juce::String("K") + juce::String(i + 1), r.removeFromTop(14),
                   juce::Justification::centred, false);
        g.setFont(juce::Font(juce::Font::getDefaultMonospacedFontName(), 12.0f,
                             focused ? juce::Font::bold : juce::Font::plain));
        g.drawText(names[currentPage][i], r.removeFromTop(16), juce::Justification::centred, false);

        /* mini value bar */
        float val = 0.0f;
        if (auto* v = processor.apvts.getRawParameterValue(ids[currentPage][i]))
            val = v->load();
        auto mini = r.removeFromTop(8).reduced(8, 1);
        g.setColour(slotTrough);
        g.fillRect(mini);
        g.setColour(focused ? lcdLit : lcdDim);
        g.fillRect(mini.withWidth((int)(mini.getWidth() * val)));

        juce::String sv("--");
        if (auto* v = processor.apvts.getRawParameterValue(ids[currentPage][i])) {
            if (juce::String(ids[currentPage][i]) == "mode") {
                static const char* MODE_NAMES[6] = {"Off", "Wash", "Reverse", "Gated", "Shoegaze", "Space"};
                int mi = (int)(v->load() * 5.0f + 0.5f);
                if (mi < 0) mi = 0;
                if (mi > 5) mi = 5;
                sv = MODE_NAMES[mi];
            } else {
                sv = juce::String((int)(v->load() * 100.0f + 0.5f));
            }
        }
        if (focused) { g.setColour(lcdBg); }
        else { g.setColour(lcdDim); }
        g.drawText(sv, r, juce::Justification::centred, false);
    }

    /* knob labels under the rotary row */
    for (int i = 0; i < 3; ++i) {
        auto kr = knobRect(i);
        g.setColour(textDim);
        g.setFont(juce::Font(10.0f, juce::Font::bold));
        g.drawText(names[currentPage][i], kr.getX() - 10, kr.getBottom() + 4,
                   kr.getWidth() + 20, 12, juce::Justification::centred, false);
    }
}

void ReversonAudioProcessorEditor::resized() {
    auto lcd = lcdRect();
    for (int i = 0; i < 3; ++i)
        knobs[i].setBounds(knobRect(i));
    auto area = getLocalBounds();
    auto btnRow = area.removeFromBottom(34);
    bypassButton.setBounds(16, btnRow.getY(), 108, 26);
    int tabW = 52;
    int x = btnRow.getRight() - 16 - 3 * tabW;
    for (int i = 0; i < 3; ++i)
        pageTabs[i].setBounds(x + i * tabW, btnRow.getY(), tabW - 4, 26);
    auto presetRow = area.removeFromBottom(32);
    presetStrip->setBounds(presetRow.getX() + 16, presetRow.getY() + 1,
                           presetRow.getWidth() - 32, 26);
}

void ReversonAudioProcessorEditor::timerCallback() {
    repaint();
}

/* ------------------------------------------------------------------ */
/* Preset management (factory + user XML presets in app-data)          */
/* ------------------------------------------------------------------ */
static const char* presetParamIds[9] = {
    "mix", "rev", "space", "tone", "grain", "duck",
    "trig", "predelay", "mode"
};

juce::File ReversonAudioProcessorEditor::presetDir() const {
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("Reverson").getChildFile("Presets");
}

void ReversonAudioProcessorEditor::refreshPresetList() {
    juce::StringArray factoryNames;
    for (int i = 0; i < 5; ++i) factoryNames.add(factoryPresets[i].name);
    userPresetFiles.clear();
    auto dir = presetDir();
    auto files = dir.findChildFiles(juce::File::findFiles, false, "*.xml");
    files.sort();
    juce::StringArray userNames;
    for (int i = 0; i < files.size(); ++i) {
        userPresetFiles.add(files[i]);
        userNames.add(files[i].getFileNameWithoutExtension());
    }
    presetStrip->setLists(factoryNames, userNames);
    presetStrip->setCurrent("", 0);
}

void ReversonAudioProcessorEditor::applyFactoryPreset(int index) {
    if (index < 0 || index >= 5) return;
    const FactoryPreset& p = factoryPresets[index];
    for (int i = 0; i < 9; ++i)
        if (auto* param = processor.apvts.getParameter(presetParamIds[i]))
            param->setValueNotifyingHost(p.v[i]);
    if (auto* param = processor.apvts.getParameter("bypass"))
        param->setValueNotifyingHost(0.0f);
    presetStrip->setCurrent(factoryPresets[index].name, index + 1);
    repaint();
}

void ReversonAudioProcessorEditor::cyclePreset(int dir) {
    juce::StringArray all;
    for (int i = 0; i < 5; ++i) all.add(factoryPresets[i].name);
    for (auto& f : userPresetFiles) all.add(f.getFileNameWithoutExtension());
    if (all.isEmpty()) return;
    int idx = all.indexOf(presetStrip->currentName());
    if (idx < 0) idx = 0;
    idx = (idx + dir + all.size()) % all.size();
    if (idx < 5) applyFactoryPreset(idx);
    else loadUserPreset(userPresetFiles[idx - 5]);
}

void ReversonAudioProcessorEditor::saveUserPreset() {
    auto dir = presetDir();
    if (!dir.isDirectory() && !dir.createDirectory()) return;
    juce::File target;
    int sel = presetStrip->currentIdForSave();   /* 0 = none, 100+ = custom slot */
    if (sel >= 100 && sel - 100 < userPresetFiles.size())
        target = userPresetFiles[sel - 100];               /* overwrite selected */
    else {
        int n = 1;
        while (dir.getChildFile(juce::String("Custom ") + juce::String(n) + ".xml").exists())
            ++n;
        target = dir.getChildFile(juce::String("Custom ") + juce::String(n) + ".xml");
    }
    auto state = processor.apvts.copyState();
    /* drop bypass from the stored state so presets always load un-bypassed */
    for (int i = state.getNumChildren() - 1; i >= 0; --i)
        if (state.getChild(i).getProperty("id").toString() == "bypass")
            state.removeChild(i, nullptr);
    state.setProperty("presetName", target.getFileNameWithoutExtension(), nullptr);
    std::unique_ptr<juce::XmlElement> xml(state.createXml());
    xml->writeTo(target);
    refreshPresetList();
    int idx = userPresetFiles.indexOf(target);
    if (idx >= 0)
        presetStrip->setCurrent(target.getFileNameWithoutExtension(), 100 + idx);
}

void ReversonAudioProcessorEditor::deleteUserPreset() {
    int sel = presetStrip->currentIdForSave();
    if (sel < 100 || sel - 100 >= userPresetFiles.size()) return;
    userPresetFiles[sel - 100].deleteFile();
    refreshPresetList();
    presetStrip->setCurrent(factoryPresets[1].name, 2);   /* show the default */
}

void ReversonAudioProcessorEditor::loadUserPreset(const juce::File& file) {
    std::unique_ptr<juce::XmlElement> xml(juce::XmlDocument::parse(file));
    if (xml == nullptr || !xml->hasTagName(processor.apvts.state.getType())) return;
    processor.apvts.replaceState(juce::ValueTree::fromXml(*xml));
    if (auto* param = processor.apvts.getParameter("bypass"))
        param->setValueNotifyingHost(0.0f);
    int idx = userPresetFiles.indexOf(file);
    presetStrip->setCurrent(file.getFileNameWithoutExtension(), idx >= 0 ? 100 + idx : 0);
    repaint();
}
