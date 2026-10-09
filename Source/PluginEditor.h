#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include "PluginProcessor.h"
#include "LookAndFeel.h"
#include "WaveformDisplay.h"

using APVTS = juce::AudioProcessorValueTreeState;

// Rotary slider that can snap to a coarse step (e.g. whole ratios 1.0, 2.0, 3.0 ...) while dragging or using the mouse wheel.
// Holding Ctrl lifts the snapping, so the value follows the mouse freely for fine tuning (the parameter's own 0.1 resolution still applies).
class StepSlider final : public juce::Slider
{
public:
    using juce::Slider::Slider;

    void setCoarseStep (double step) noexcept { coarseStep = step; }   // 0 = no stepping

    // Show a value on the knob (pointer, arc and text) without changing the parameter: used to display what auto-release is
    // doing on the locked Release knob. The look-and-feel reads the position from the "displayPos" property.
    void setDisplayValue (double v)
    {
        v = juce::jlimit (getMinimum(), getMaximum(), v);
        if (displayActive && std::abs (v - displayValue) < 1.0e-4) return;
        displayActive = true;  displayValue = v;
        getProperties().set ("displayPos", (float) valueToProportionOfLength (v));
        updateText();  repaint();
    }

    void clearDisplayValue()
    {
        if (! displayActive) return;
        displayActive = false;
        getProperties().remove ("displayPos");
        updateText();  repaint();
    }

    juce::String getTextFromValue (double v) override { return juce::Slider::getTextFromValue (displayActive ? displayValue : v); }

    double snapValue (double attempted, DragMode mode) override
    {
        if (coarseStep <= 0.0 || juce::ModifierKeys::currentModifiers.isCtrlDown())
            return attempted;

        double snapped = std::round (attempted / coarseStep) * coarseStep;

        if (mode == DragMode::notDragging)          // mouse wheel / keys: always move by one whole step in the scrolled direction
        {
            const double cur = getValue();
            if (attempted > cur + 1.0e-9)      snapped = std::ceil  (attempted / coarseStep - 1.0e-9) * coarseStep;
            else if (attempted < cur - 1.0e-9) snapped = std::floor (attempted / coarseStep + 1.0e-9) * coarseStep;
            else                               snapped = cur;
        }
        return juce::jlimit (getMinimum(), getMaximum(), snapped);
    }

private:
    double coarseStep = 0.0;
    bool   displayActive = false;
    double displayValue = 0.0;
};

// One caption + rotary knob + value read-out, bound to a parameter.
// coarseStep > 0 makes it a step knob (snaps to multiples of coarseStep); hold Ctrl for free fine tuning.
class Knob final : public juce::Component
{
public:
    Knob (APVTS& apvts, const juce::String& paramId, const juce::String& caption, double coarseStep = 0.0)
        : slider (juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow)
    {
        addAndMakeVisible (slider);
        addAndMakeVisible (label);
        label.setText (caption, juce::dontSendNotification);
        label.setJustificationType (juce::Justification::centred);
        label.setInterceptsMouseClicks (false, false);
        label.setColour (juce::Label::textColourId, ChromaLookAndFeel::dim);

        slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.2f, juce::MathConstants<float>::pi * 2.8f, true);
        slider.setMouseDragSensitivity (220);
        slider.setCoarseStep (coarseStep);
        slider.setTextBoxIsEditable (true);
        attachment = std::make_unique<APVTS::SliderAttachment> (apvts, paramId, slider);
        if (auto* p = apvts.getParameter (paramId))
            slider.setDoubleClickReturnValue (true, p->convertFrom0to1 (p->getDefaultValue()));
    }

    void setDisplayValue (double v)  { slider.setDisplayValue (v); }     // see StepSlider
    void clearDisplayValue()         { slider.clearDisplayValue(); }

    void resized() override
    {
        auto r = getLocalBounds();
        auto top = r.removeFromTop (juce::jmax (12, (int) (getHeight() * 0.12f)));
        label.setBounds (top);
        label.setFont (ChromaLookAndFeel::makeFont ((float) top.getHeight() * 0.82f, true));
        slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, getWidth(), juce::jmax (14, (int) (getHeight() * 0.13f)));
        slider.setBounds (r);
    }

private:
    StepSlider slider;
    juce::Label label;
    std::unique_ptr<APVTS::SliderAttachment> attachment;
};

// Small round "A" toggle (auto release). Lit blue ring + glow when on, dull grey when off.
class AutoButton final : public juce::Button
{
public:
    AutoButton() : juce::Button ("Auto release")
    {
        setClickingTogglesState (true);
        setTooltip ("Auto release");
    }

    void paintButton (juce::Graphics& g, bool highlighted, bool down) override
    {
        using L = ChromaLookAndFeel;
        const auto& pal = paletteOf (*this);
        const bool on = getToggleState();
        const auto area = getLocalBounds().toFloat().reduced (1.5f);
        const float d = juce::jmin (area.getWidth(), area.getHeight());
        const auto r = juce::Rectangle<float> (d, d).withCentre (area.getCentre());

        if (on)
        {
            g.setColour (L::accent.withAlpha (highlighted ? 0.30f : 0.20f));
            g.fillEllipse (r.expanded (1.8f));                                                  // soft glow
            g.setGradientFill (juce::ColourGradient (L::accentHi.interpolatedWith (L::accent, 0.35f), r.getX(), r.getY(),
                                                     L::accent, r.getX(), r.getBottom(), false));
            g.fillEllipse (r);
        }
        else
        {
            g.setColour (pal.shadow.withAlpha (0.07f));                                         // soft shadow below
            g.fillEllipse (r.translated (0.0f, 1.2f));
            g.setColour (highlighted ? pal.surfaceHot : pal.surface);
            g.fillEllipse (r);
            g.setColour (highlighted ? L::accent.withAlpha (0.65f) : pal.rim);
            g.drawEllipse (r.reduced (0.5f), 1.0f);
        }

        g.setColour (on ? juce::Colours::white : L::dim);
        g.setFont (L::makeFont (d * 0.58f, true));
        g.drawText ("A", r, juce::Justification::centred, false);

        if (down) { g.setColour (pal.pressed (0.08f)); g.fillEllipse (r); }
    }
};

// Small round power button (adaptive engine on / off). On: only the power symbol glows neon green. Off: dull grey symbol.
class PowerButton final : public juce::Button
{
public:
    PowerButton() : juce::Button ("Adaptive engine")
    {
        setClickingTogglesState (true);
        setTooltip ("Adaptive engine on / off");
    }

    void paintButton (juce::Graphics& g, bool highlighted, bool down) override
    {
        using L = ChromaLookAndFeel;
        const auto& pal = paletteOf (*this);
        const juce::Colour neon   (0xff39ff14);   // neon green
        const bool on = getToggleState();
        const auto area = getLocalBounds().toFloat();
        const float d = juce::jmin (area.getWidth(), area.getHeight()) * 0.68f;   // leaves room around the circle for the glow
        const auto r = juce::Rectangle<float> (d, d).withCentre (area.getCentre());
        const auto c = r.getCentre();

        g.setColour (pal.shadow.withAlpha (0.07f));                                // soft shadow below, no halo
        g.fillEllipse (r.translated (0.0f, 1.0f));
        g.setColour (highlighted ? pal.surfaceHot : pal.surface);
        g.fillEllipse (r);
        g.setColour (highlighted ? L::accent.withAlpha (0.65f) : pal.rim);         // neutral rim in both states
        g.drawEllipse (r.reduced (0.5f), 1.0f);

        // power glyph: open ring with a short stroke through the gap
        const float rad = d * 0.27f;
        juce::Path p;
        p.addCentredArc (c.x, c.y + d * 0.02f, rad, rad, 0.0f, 0.65f, juce::MathConstants<float>::twoPi - 0.65f, true);
        p.startNewSubPath (c.x, c.y - rad * 1.2f);
        p.lineTo (c.x, c.y - rad * 0.1f);
        const float t = juce::jmax (1.2f, d * 0.11f);
        if (on)
        {
            g.setColour (neon.withAlpha (0.14f));                             // glyph glow: only the symbol lights up
            g.strokePath (p, juce::PathStrokeType (t * 4.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            g.setColour (neon.withAlpha (0.30f));
            g.strokePath (p, juce::PathStrokeType (t * 2.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
        g.setColour (on ? neon : juce::Colour (0xff98a3b2));
        g.strokePath (p, juce::PathStrokeType (t, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        if (down) { g.setColour (pal.pressed (0.08f)); g.fillEllipse (r); }
    }
};

// Radio-style segmented selector bound to a choice parameter (Feel / Intensity / Transients).
class SegmentRow final : public juce::Component
{
public:
    SegmentRow (APVTS& apvts, const juce::String& paramId, const juce::StringArray& names, int radioGroup)
        : param (*apvts.getParameter (paramId)),
          attach (param, [this] (float v) { setIndex (juce::roundToInt (v)); })
    {
        for (int i = 0; i < names.size(); ++i)
        {
            auto* b = buttons.add (new juce::TextButton (names[i]));
            addAndMakeVisible (b);
            b->getProperties().set ("segment", true);   // LookAndFeel draws it as a segment of the track
            b->setClickingTogglesState (true);
            b->setRadioGroupId (radioGroup);
            b->onClick = [this, i] { attach.setValueAsCompleteGesture ((float) i); };
        }
        attach.sendInitialUpdate();
    }

    void paint (juce::Graphics& g) override
    {
        const auto& pal = paletteOf (*this);
        const auto r = getLocalBounds().toFloat().reduced (0.5f);
        const float cr = r.getHeight() * 0.4f;
        g.setColour (pal.segTrack);
        g.fillRoundedRectangle (r, cr);
        g.setColour (pal.line);
        g.drawRoundedRectangle (r, cr, 1.0f);
    }

    void resized() override
    {
        const int n = buttons.size(), pad = 2, gap = 1;
        const auto inner = getLocalBounds().reduced (pad);
        const int w = (inner.getWidth() - gap * (n - 1)) / juce::jmax (1, n);
        for (int i = 0; i < n; ++i) buttons[i]->setBounds (inner.getX() + i * (w + gap), inner.getY(), w, inner.getHeight());
    }

private:
    void setIndex (int idx)
    {
        for (int i = 0; i < buttons.size(); ++i)
            buttons[i]->setToggleState (i == idx, juce::dontSendNotification);
    }

    juce::OwnedArray<juce::TextButton> buttons;   // declared before attach: attach is destroyed first
    juce::RangedAudioParameter& param;
    juce::ParameterAttachment attach;
};

// Small "DARK" switch for the footer (left of the version text). Off: grey track, thumb on the left. On: neon-blue track,
// thumb on the right. Bound to the editor's theme, not to a host parameter.
class DarkToggle final : public juce::Button
{
public:
    DarkToggle() : juce::Button ("Dark mode")
    {
        setClickingTogglesState (true);
        setTooltip ("Dark mode");
    }

    void paintButton (juce::Graphics& g, bool highlighted, bool) override
    {
        using L = ChromaLookAndFeel;
        const auto& pal = paletteOf (*this);
        const bool on = getToggleState();
        const auto area = getLocalBounds().toFloat();
        const float h = area.getHeight() * 0.86f, w = h * 1.9f, cr = h * 0.5f;
        const juce::Rectangle<float> sw (area.getRight() - w - 1.0f, area.getCentreY() - h * 0.5f, w, h);

        if (on)
        {
            g.setColour (L::accent.withAlpha (highlighted ? 0.30f : 0.20f));
            g.fillRoundedRectangle (sw.expanded (1.4f), cr + 1.4f);                               // soft glow
            g.setGradientFill (juce::ColourGradient (L::accentHi.interpolatedWith (L::accent, 0.35f), 0, sw.getY(),
                                                     L::accent, 0, sw.getBottom(), false));
            g.fillRoundedRectangle (sw, cr);
        }
        else
        {
            g.setColour (pal.miniTrack);
            g.fillRoundedRectangle (sw, cr);
            g.setColour (highlighted ? L::accent.withAlpha (0.65f) : pal.rim);
            g.drawRoundedRectangle (sw.reduced (0.5f), cr, 1.0f);
        }

        const float tr = cr - 2.0f;                                                               // thumb radius
        const float tx = on ? sw.getRight() - cr : sw.getX() + cr;
        g.setColour (juce::Colours::black.withAlpha (0.18f));
        g.fillEllipse (tx - tr, sw.getCentreY() - tr + 0.8f, tr * 2.0f, tr * 2.0f);
        g.setColour (juce::Colours::white);
        g.fillEllipse (tx - tr, sw.getCentreY() - tr, tr * 2.0f, tr * 2.0f);

        g.setColour (L::dim);
        g.setFont (L::makeFont (h * 0.72f, true));
        g.drawText ("DARK", area.withRight (sw.getX() - 4.0f), juce::Justification::centredRight, false);
    }
};

// Favourite star next to the preset controls: just the star, no circle around it. Grey outline = not a favourite,
// filled neon blue with a soft glow = favourite. Hover lights the outline blue, pressing shrinks it a little.
class StarButton final : public juce::Button
{
public:
    StarButton() : juce::Button ("Favourite")
    {
        setTooltip ("Favourite preset");
    }

    void paintButton (juce::Graphics& g, bool highlighted, bool down) override
    {
        using L = ChromaLookAndFeel;
        const bool on = getToggleState();
        const auto area = getLocalBounds().toFloat().reduced (1.0f);
        const float d = juce::jmin (area.getWidth(), area.getHeight());
        const auto c = area.getCentre();

        juce::Path star;
        star.addStar (c.translated (0.0f, d * 0.02f), 5, d * 0.115f, d * 0.26f);
        if (down)
            star.applyTransform (juce::AffineTransform::scale (0.9f, 0.9f, c.x, c.y));

        if (on)
        {
            g.setColour (L::accent.withAlpha (highlighted ? 0.38f : 0.28f));         // soft glow
            g.strokePath (star, juce::PathStrokeType (d * 0.16f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            g.setColour (L::accent);
            g.fillPath (star);
            g.strokePath (star, juce::PathStrokeType (d * 0.05f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
        else
        {
            g.setColour (highlighted ? L::accent : L::dim);
            g.strokePath (star, juce::PathStrokeType (d * 0.075f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
    }
};

// Slim neon progress bar
class MiniBar final : public juce::Component
{
public:
    void set (float v) { v = juce::jlimit (0.0f, 1.0f, v); if (v != value) { value = v; repaint(); } }
    void paint (juce::Graphics& g) override
    {
        using L = ChromaLookAndFeel;
        const auto r = getLocalBounds().toFloat().withSizeKeepingCentre ((float) getWidth(), juce::jmin (8.0f, (float) getHeight()));
        g.setColour (paletteOf (*this).miniTrack);  g.fillRoundedRectangle (r, r.getHeight() * 0.5f);
        if (value > 0.0f)
        {
            auto f = r.withWidth (juce::jmax (r.getHeight(), r.getWidth() * value));
            g.setColour (L::accent.withAlpha (0.25f));  g.fillRoundedRectangle (f.expanded (0.0f, 2.0f), r.getHeight());
            g.setGradientFill (juce::ColourGradient (L::accentHi, f.getX(), 0, L::accent, f.getRight(), 0, false));
            g.fillRoundedRectangle (f, r.getHeight() * 0.5f);
        }
    }
private:
    float value = 0.0f;
};

class ChromaCompEditor final : public juce::AudioProcessorEditor,
                               private juce::Timer
{
public:
    explicit ChromaCompEditor (ChromaCompProcessor&);
    ~ChromaCompEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void refreshPresetBox();
    void showSaveDialog();
    void showDeleteConfirm();
    void updatePresetButtons();      // DEL enabled only for user presets; star lit when the current preset is a favourite
    void timerCallback() override;
    void applyTheme (bool dark);     // light <-> dark: restyles the look-and-feel, repaints, remembers the choice in the plugin state
    void resetSettings();            // compressor + adaptive-engine settings back to their start values (the "Init" values)

    // Footer: "DEVELOPED BY HIM'Z DSP", then the OVERSAMPLING caption + dropdown and the RESET button, DARK switch and "V1" on the right.
    struct FooterLayout { float capX = 0.0f, capW = 0.0f; };
    FooterLayout footerLayout (float s) const;     // where the OVERSAMPLING caption goes (just after the credit text)

    // Two knob rows (Hold / Range / Look time / Adapt in the second). The Release "A" button sits inside the Release
    // cell, lower-left of the knob. A separate INPUT / OUTPUT panel (whole-plugin gain trim) sits in
    // the free columns 5-7 of row 2; the bottom card is the adaptive engine only (small on/off button at its top left).
    // Base grid 600 x 524 (first open at kStartScale = 112 % of it = 672 x 587, so the text reads well; it can be dragged down to
    // the 589 x 514 minimum and up to 900 x 786), fixed aspect ratio.
    static constexpr int kBaseW = 600, kBaseH = 524;      // layout grid (base px); the window is opened at kStartScale of this
    static constexpr float kStartScale = 1.12f;           // default size = 672 px wide (the previous default 589 + about 14 %)
    static constexpr int   kMinW = 589, kMinH = 514;      // smallest size allowed (same 600 : 524 ratio)
    static constexpr int   kMaxW = 900, kMaxH = 786;

    void syncAdaptiveEnabled();      // greys out / disables the adaptive controls to follow the engine switch
    bool adaptiveShown = true;
    void updateAutoReleaseDisplay();   // locked Release knob: shows the release time auto-release is using
    float autoRelShown = 0.0f;         // smoothed value shown there (ms), 0 = not started
    void syncAutoRelease();          // auto release on -> Release knob goes dull and locked; off -> the A button is dull
    int  autoRelState = -1;

    ChromaCompProcessor& proc;
    bool sizeRestored = false;                  // false until the constructor has applied the remembered window size
    ChromaLookAndFeel lnf;

    WaveformPanel waveform;     // waveform + GR meter + loudness meter + IN / OUT buttons
    Knob kThr, kRatio, kKnee, kAtk, kRel, kMakeup, kMix;
    Knob kHold, kRange, kLookTime, kAdapt;      // second row: parameter ids hold / range / lookaheadTime / adapt
    Knob kIn, kOut;                             // input / output panel (whole plugin): parameter ids inputGain / outputGain
    AutoButton autoRelBtn;                      // under the Release knob: parameter id autoRelease

    juce::TextButton prevBtn { "<" }, nextBtn { ">" }, saveBtn { "SAVE" }, delBtn { "DEL" };
    StarButton starBtn;                         // header, right of DEL: marks the current preset as a favourite
    juce::TextButton lookBtn { "LOOKAHEAD" }, bypassBtn { "BYPASS" };
    DarkToggle darkBtn;                         // footer, left of the version text; state kept in the plugin state ("darkMode")
    juce::ComboBox osBox;                       // footer dropdown: 1x / 2x / 4x oversampling (parameter id oversampling)
    juce::TextButton resetBtn { "RESET" };      // footer: resets compressor + adaptive-engine settings only
    PowerButton adaptiveOnBtn;                  // small on / off at the top left of the adaptive tab (parameter id adaptiveOn)
    SegmentRow feelRow, transRow, intRow;
    juce::TextButton adaptBtn { "ADAPTIVE ANALYZE" };
    MiniBar adaptBar;
    juce::Label feelCap, transCap, intCap, adaptStatus, report;
    juce::ComboBox presetBox;
    juce::StringArray presetNames;
    std::unique_ptr<APVTS::ButtonAttachment> lookAtt, bypassAtt, autoRelAtt, adaptiveOnAtt;
    std::unique_ptr<APVTS::ComboBoxAttachment> osAtt;     // after osBox: destroyed first

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChromaCompEditor)
};
