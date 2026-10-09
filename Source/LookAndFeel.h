#pragma once
#include <juce_audio_utils/juce_audio_utils.h>

// Colours that differ between the light and the dark theme. The neon colours (accent, accentHi, coral, power green) and the
// greys (dim, the wave input colour, the power-off glyph) are NOT in here: they are identical in both themes.
struct ChromaPalette
{
    bool dark = false;
    juce::Colour bgTop, bgBottom, surface, surfaceHot, panel, ink, line, rim,
                 segTrack, meterTrack, miniTrack, grid, gridMid, tick,
                 sliderTrackA, sliderTrackB, thumbA, thumbB, thumbRim,
                 bezelA, bezelB, bezelRimA, bezelRimB, capA, capB, capRimA, capRimB,
                 gloss, shadow, capShadow;

    // press / click tint: darkens on the light theme, lightens on the dark one
    juce::Colour pressed (float alpha) const { return (dark ? juce::Colours::white : juce::Colours::black).withAlpha (alpha); }
};

// Original white theme (values unchanged from v1).
inline const ChromaPalette& chromaLightPalette()
{
    static const ChromaPalette p = []
    {
        using juce::Colour;
        ChromaPalette c;
        c.dark = false;
        c.bgTop = juce::Colours::white;      c.bgBottom = Colour (0xfff1f5fa);
        c.surface = juce::Colours::white;    c.surfaceHot = Colour (0xfff2f9ff);   c.panel = Colour (0xfff7f9fc);
        c.ink = Colour (0xff1c2b3d);         c.line = Colour (0xffdbe3ee);         c.rim = Colour (0xffd3dde9);
        c.segTrack = Colour (0xffeef2f8);    c.meterTrack = Colour (0xffe7edf5);   c.miniTrack = Colour (0xffe3eaf3);
        c.grid = Colour (0xffe7edf5);        c.gridMid = Colour (0xffd3deeb);      c.tick = Colour (0xffc9d5e3);
        c.sliderTrackA = Colour (0xffc3cad4); c.sliderTrackB = Colour (0xffd3d9e1);
        c.thumbA = juce::Colours::white;     c.thumbB = Colour (0xffeef3f8);       c.thumbRim = Colour (0xffd2dae5);
        c.bezelA = juce::Colours::white;     c.bezelB = Colour (0xffd3dce9);
        c.bezelRimA = juce::Colours::white;  c.bezelRimB = Colour (0xffbccadb);
        c.capA = juce::Colours::white;       c.capB = Colour (0xffe3eaf3);
        c.capRimA = juce::Colours::white;    c.capRimB = Colour (0xffcbd6e4);
        c.gloss = juce::Colours::white.withAlpha (0.95f);
        c.shadow = Colour (0xff5c6f8a);      c.capShadow = Colour (0xff7d8fa8);
        return c;
    }();
    return p;
}

// Dark theme: background #1A1A1A, all dark text white (#FFFFFF). Greys and neon stay as they are in the light theme.
// The surfaces that were white (cards, buttons, knob domes, tracks) become dark so the white text stays readable.
inline const ChromaPalette& chromaDarkPalette()
{
    static const ChromaPalette p = []
    {
        using juce::Colour;
        ChromaPalette c;
        c.dark = true;
        c.bgTop = Colour (0xff1a1a1a);       c.bgBottom = Colour (0xff1a1a1a);
        c.surface = Colour (0xff1a1a1a);     c.surfaceHot = Colour (0xff262626);   c.panel = Colour (0xff1a1a1a);
        c.ink = Colour (0xffffffff);         c.line = Colour (0xff3a3a3a);         c.rim = Colour (0xff3a3a3a);
        c.segTrack = Colour (0xff2a2a2a);    c.meterTrack = Colour (0xff2c2c2c);   c.miniTrack = Colour (0xff2c2c2c);
        c.grid = Colour (0xff2c2c2c);        c.gridMid = Colour (0xff444444);      c.tick = Colour (0xff555555);
        c.sliderTrackA = Colour (0xff333333); c.sliderTrackB = Colour (0xff3d3d3d);
        c.thumbA = Colour (0xff3c3c3c);      c.thumbB = Colour (0xff262626);       c.thumbRim = Colour (0xff555555);
        c.bezelA = Colour (0xff3f3f3f);      c.bezelB = Colour (0xff141414);
        c.bezelRimA = Colour (0xff5a5a5a);   c.bezelRimB = Colour (0xff2a2a2a);
        c.capA = Colour (0xff3a3a3a);        c.capB = Colour (0xff202020);
        c.capRimA = Colour (0xff5a5a5a);     c.capRimB = Colour (0xff303030);
        c.gloss = juce::Colours::white.withAlpha (0.12f);
        c.shadow = juce::Colours::black;     c.capShadow = juce::Colours::black;
        return c;
    }();
    return p;
}

// White surface + neon-blue accents, with soft 3D depth on knobs and buttons. Has a dark theme (setDark).
class ChromaLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    static inline const juce::Colour accent   { 0xff00a8ff };
    static inline const juce::Colour accentHi { 0xff63d3ff };
    static inline const juce::Colour dim      { 0xff7f90a8 };
    static inline const juce::Colour coral    { 0xffff3d71 };

    float fontScale = 1.0f;

    ChromaLookAndFeel() { applyColours(); }

    // light <-> dark. Call sendLookAndFeelChange() on the editor afterwards so everything repaints.
    void setDark (bool shouldBeDark)  { dark = shouldBeDark; applyColours(); }
    bool isDark() const               { return dark; }
    const ChromaPalette& palette() const { return dark ? chromaDarkPalette() : chromaLightPalette(); }

    static juce::Font makeFont (float h, bool bold = false)
    {
        return juce::Font (juce::FontOptions (h, bold ? juce::Font::bold : juce::Font::plain));
    }

    juce::Font getLabelFont (juce::Label& l) override
    {
        if (dynamic_cast<juce::Slider*> (l.getParentComponent()) != nullptr)
            return makeFont (11.0f * fontScale);
        return l.getFont();
    }
    // Footer controls (property "footer") are shorter than the header ones, so their text gets a bigger share of the height.
    juce::Font getTextButtonFont (juce::TextButton& b, int h) override
    {
        const float k = (bool) b.getProperties()["footer"] ? 0.6f : 0.44f;
        return makeFont ((float) h * k, true).withExtraKerningFactor (0.06f);
    }
    juce::Font getComboBoxFont (juce::ComboBox& c) override
    {
        const float k = (bool) c.getProperties()["footer"] ? 0.62f : 0.5f;
        return makeFont ((float) c.getHeight() * k);
    }

    // The floating list of the footer dropdown: comfortable rows even though the box itself is small.
    juce::PopupMenu::Options getOptionsForComboBoxPopupMenu (juce::ComboBox& box, juce::Label& label) override
    {
        auto o = LookAndFeel_V4::getOptionsForComboBoxPopupMenu (box, label);
        if ((bool) box.getProperties()["footer"])
            o = o.withStandardItemHeight (22).withMinimumWidth (juce::jmax (box.getWidth(), 56));
        return o;
    }

    // Modern soft-3D knob: thin flat value arc, a floating dome (bezel + domed cap) with a diffuse shadow, a soft
    // highlight at the top left and a short thin pointer. Colours come from the current palette.
    void drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                           float startAngle, float endAngle, juce::Slider& s) override
    {
        using namespace juce;
        const auto& p = palette();
        const float shown = (float) s.getProperties().getWithDefault ("displayPos", -1.0f);   // a knob that displays a value other than its own (locked Release knob under Auto)
        if (shown >= 0.0f) pos = shown;
        const auto b = Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (2.0f);
        const float r = jmin (b.getWidth(), b.getHeight()) * 0.5f;
        const auto c = b.getCentre();
        const float ang = startAngle + pos * (endAngle - startAngle);
        const float thick = jmax (1.8f, r * 0.075f);
        const float arcR = r - thick;
        const bool hot = s.isMouseOverOrDragging();

        // ---- value arc: subtle track + flat accent arc with a soft glow ----
        Path track;  track.addCentredArc (c.x, c.y, arcR, arcR, 0.0f, startAngle, endAngle, true);
        g.setColour (p.line);
        g.strokePath (track, PathStrokeType (thick, PathStrokeType::curved, PathStrokeType::rounded));
        if (pos > 0.002f)
        {
            Path val;  val.addCentredArc (c.x, c.y, arcR, arcR, 0.0f, startAngle, ang, true);
            g.setColour (accent.withAlpha (hot ? 0.30f : 0.16f));
            g.strokePath (val, PathStrokeType (thick * 2.3f, PathStrokeType::curved, PathStrokeType::rounded));
            g.setColour (accent);
            g.strokePath (val, PathStrokeType (thick, PathStrokeType::curved, PathStrokeType::rounded));
        }

        // ---- floating body: diffuse shadow, bezel, domed cap ----
        const float br = arcR - thick * 1.5f;                         // bezel radius
        for (int i = 0; i < 5; ++i)                                   // soft layered shadow, falling to the bottom right
        {
            const float k = (float) i;
            const float grow = k * br * 0.05f;
            g.setColour (p.shadow.withAlpha (0.04f));
            g.fillEllipse (c.x - br - grow + br * 0.05f, c.y - br - grow + br * 0.11f + k * br * 0.02f,
                           (br + grow) * 2.0f, (br + grow) * 2.0f);
        }

        g.setGradientFill (ColourGradient (p.bezelA, c.x - br * 0.7f, c.y - br * 0.8f,
                                           p.bezelB, c.x + br * 0.8f, c.y + br * 0.9f, false));
        g.fillEllipse (c.x - br, c.y - br, br * 2.0f, br * 2.0f);     // bezel
        g.setGradientFill (ColourGradient (p.bezelRimA, c.x - br * 0.7f, c.y - br * 0.8f,
                                           p.bezelRimB, c.x + br * 0.8f, c.y + br * 0.9f, false));
        g.drawEllipse (c.x - br + 0.5f, c.y - br + 0.5f, br * 2.0f - 1.0f, br * 2.0f - 1.0f, 0.9f);

        const float dr = br * 0.80f;                                  // cap radius
        g.setColour (p.capShadow.withAlpha (0.10f));                  // tiny contact shadow between cap and bezel
        g.fillEllipse (c.x - dr + dr * 0.04f, c.y - dr + dr * 0.09f, dr * 2.0f, dr * 2.0f);
        g.setGradientFill (ColourGradient (p.capA, c.x - dr * 0.55f, c.y - dr * 0.65f,
                                           p.capB, c.x + dr * 0.70f, c.y + dr * 0.85f, false));
        g.fillEllipse (c.x - dr, c.y - dr, dr * 2.0f, dr * 2.0f);
        g.setGradientFill (ColourGradient (p.gloss, c.x - dr * 0.32f, c.y - dr * 0.42f,
                                           Colours::white.withAlpha (0.0f), c.x - dr * 0.32f + dr * 0.95f, c.y - dr * 0.42f, true));
        g.fillEllipse (c.x - dr, c.y - dr, dr * 2.0f, dr * 2.0f);     // soft highlight, top left
        g.setGradientFill (ColourGradient (p.capRimA, c.x - dr * 0.7f, c.y - dr * 0.8f,
                                           p.capRimB, c.x + dr * 0.8f, c.y + dr * 0.9f, false));
        g.drawEllipse (c.x - dr + 0.4f, c.y - dr + 0.4f, dr * 2.0f - 0.8f, dr * 2.0f - 0.8f, 0.8f);

        // ---- pointer: short thin accent line ----
        const float pw = jmax (1.8f, dr * 0.085f);
        Path ptr;
        ptr.startNewSubPath (c.x + dr * 0.30f * std::sin (ang), c.y - dr * 0.30f * std::cos (ang));
        ptr.lineTo          (c.x + dr * 0.76f * std::sin (ang), c.y - dr * 0.76f * std::cos (ang));
        g.setColour (accent.withAlpha (hot ? 0.30f : 0.16f));
        g.strokePath (ptr, PathStrokeType (pw * 2.4f, PathStrokeType::curved, PathStrokeType::rounded));
        g.setColour (accent);
        g.strokePath (ptr, PathStrokeType (pw, PathStrokeType::curved, PathStrokeType::rounded));
    }

    int getSliderThumbRadius (juce::Slider& s) override
    {
        return s.isHorizontal() ? juce::jmin (9, (int) ((float) s.getHeight() * 0.5f)) : 7;
    }

    // Knee soft / hard slider: flat grey track, blue fill from the left end up to the thumb, round thumb with a
    // soft shadow and a blue centre dot. Other slider styles fall back to the stock drawing.
    void drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float minPos, float maxPos,
                           juce::Slider::SliderStyle style, juce::Slider& s) override
    {
        using namespace juce;
        if (style != Slider::LinearHorizontal)
        {
            LookAndFeel_V4::drawLinearSlider (g, x, y, w, h, pos, minPos, maxPos, style, s);
            return;
        }
        const auto& p = palette();
        const float cy = (float) y + (float) h * 0.5f;
        const float thick = jlimit (5.0f, 10.0f, (float) h * 0.46f);             // track thickness
        const float tr = jmin ((float) h * 0.5f - 0.5f, thick * 1.4f);           // thumb radius
        const bool hot = s.isMouseOverOrDragging();

        // track spans the thumb's travel plus a rounded cap at each end
        const Rectangle<float> track (minPos - thick * 0.5f, cy - thick * 0.5f, (maxPos - minPos) + thick, thick);
        const float tcr = thick * 0.5f;

        g.setGradientFill (ColourGradient (p.sliderTrackA, 0, track.getY(), p.sliderTrackB, 0, track.getBottom(), false));
        g.fillRoundedRectangle (track, tcr);                                      // grey line

        if (pos > minPos + 0.5f)                                                  // blue line, left end -> thumb
        {
            const auto fill = track.withRight (jmax (track.getX() + thick, pos));
            if (hot)
            {
                g.setColour (accent.withAlpha (0.22f));
                g.fillRoundedRectangle (fill.expanded (1.2f), tcr + 1.2f);
            }
            g.setGradientFill (ColourGradient (accentHi, 0, fill.getY(), accent, 0, fill.getBottom(), false));
            g.fillRoundedRectangle (fill, tcr);
        }

        // thumb: soft shadow, body, thin rim, blue dot with a faint halo
        g.setColour (Colours::black.withAlpha (0.07f));
        g.fillEllipse (pos - tr - 1.0f, cy - tr + 0.8f, tr * 2.0f + 2.0f, tr * 2.0f + 2.0f);
        g.setColour (Colours::black.withAlpha (0.12f));
        g.fillEllipse (pos - tr, cy - tr + 1.4f, tr * 2.0f, tr * 2.0f);
        g.setGradientFill (ColourGradient (p.thumbA, 0, cy - tr, p.thumbB, 0, cy + tr, false));
        g.fillEllipse (pos - tr, cy - tr, tr * 2.0f, tr * 2.0f);
        g.setColour (p.thumbRim);
        g.drawEllipse (pos - tr + 0.5f, cy - tr + 0.5f, tr * 2.0f - 1.0f, tr * 2.0f - 1.0f, 1.0f);
        g.setColour (accent.withAlpha (0.22f));
        g.fillEllipse (pos - tr * 0.58f, cy - tr * 0.58f, tr * 1.16f, tr * 1.16f);
        g.setColour (accent);
        g.fillEllipse (pos - tr * 0.38f, cy - tr * 0.38f, tr * 0.76f, tr * 0.76f);
    }

    // Modern flat buttons: clean pill with a hairline border and a soft contact shadow. Hover tints it blue, on = solid
    // accent with a soft glow. Buttons tagged "segment" (SegmentRow) are transparent inside a track; only the selected one is lit.
    void drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&,
                               bool highlighted, bool down) override
    {
        using namespace juce;
        const auto& p = palette();
        const bool on  = b.getToggleState();
        const bool seg = (bool) b.getProperties()["segment"];
        auto r = b.getLocalBounds().toFloat().reduced (1.0f);
        const float cr = r.getHeight() * 0.4f;

        if (seg)
        {
            if (on)
            {
                g.setColour (accent.withAlpha (0.22f));
                g.fillRoundedRectangle (r.expanded (1.0f).translated (0.0f, 0.8f), cr + 1.0f);
                g.setGradientFill (ColourGradient (accentHi.interpolatedWith (accent, 0.35f), 0, r.getY(), accent, 0, r.getBottom(), false));
                g.fillRoundedRectangle (r, cr);
            }
            else if (highlighted)
            {
                g.setColour (accent.withAlpha (0.10f));
                g.fillRoundedRectangle (r, cr);
            }
            if (down) { g.setColour (p.pressed (0.07f)); g.fillRoundedRectangle (r, cr); }
            return;
        }

        if (on)
        {
            g.setColour (accent.withAlpha (highlighted ? 0.30f : 0.20f));
            g.fillRoundedRectangle (r.expanded (1.6f), cr + 1.6f);                                   // soft glow
            g.setGradientFill (ColourGradient (accentHi.interpolatedWith (accent, 0.35f), 0, r.getY(), accent, 0, r.getBottom(), false));
            g.fillRoundedRectangle (r, cr);
        }
        else
        {
            g.setColour (p.shadow.withAlpha (0.07f));                                                 // soft shadow below
            g.fillRoundedRectangle (r.translated (0.0f, 1.2f), cr);
            g.setColour (highlighted ? p.surfaceHot : p.surface);
            g.fillRoundedRectangle (r, cr);
            g.setColour (highlighted ? accent.withAlpha (0.65f) : p.rim);
            g.drawRoundedRectangle (r.reduced (0.5f), cr, 1.0f);
        }

        if (down) { g.setColour (p.pressed (0.07f)); g.fillRoundedRectangle (r, cr); }
    }

    void drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox&) override
    {
        using namespace juce;
        const auto& p = palette();
        const auto r = Rectangle<float> (0, 0, (float) w, (float) h).reduced (1.0f);
        const float cr = r.getHeight() * 0.4f;
        g.setColour (p.shadow.withAlpha (0.07f));
        g.fillRoundedRectangle (r.translated (0.0f, 1.2f), cr);
        g.setColour (p.surface);
        g.fillRoundedRectangle (r, cr);
        g.setColour (p.rim);
        g.drawRoundedRectangle (r.reduced (0.5f), cr, 1.0f);
        Path a;  const float cx = r.getRight() - h * 0.45f, cy = r.getCentreY(), s = h * 0.13f;
        a.startNewSubPath (cx - s, cy - s * 0.5f);  a.lineTo (cx, cy + s * 0.6f);  a.lineTo (cx + s, cy - s * 0.5f);
        g.setColour (accent);
        g.strokePath (a, PathStrokeType (1.8f, PathStrokeType::curved, PathStrokeType::rounded));
    }

    void positionComboBoxText (juce::ComboBox& box, juce::Label& label) override
    {
        const int pad = juce::roundToInt ((float) box.getHeight() * 0.4f);
        label.setBounds (pad, 1, box.getWidth() - pad - juce::roundToInt ((float) box.getHeight() * 0.9f), box.getHeight() - 2);
        label.setFont (getComboBoxFont (box));
    }

private:
    // (re)apply the stock JUCE colour slots for the current theme: text -> palette ink, surfaces -> palette surface
    void applyColours()
    {
        const auto& p = palette();
        setColour (juce::ResizableWindow::backgroundColourId, p.bgTop);
        setColour (juce::Slider::textBoxTextColourId, p.ink);
        setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
        setColour (juce::Slider::textBoxHighlightColourId, accent.withAlpha (0.25f));
        setColour (juce::Label::textColourId, p.ink);
        setColour (juce::TextButton::textColourOffId, p.ink);
        setColour (juce::TextButton::textColourOnId, juce::Colours::white);
        setColour (juce::ComboBox::textColourId, p.ink);
        setColour (juce::PopupMenu::backgroundColourId, p.surface);
        setColour (juce::PopupMenu::textColourId, p.ink);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, accent);
        setColour (juce::PopupMenu::highlightedTextColourId, juce::Colours::white);
        setColour (juce::PopupMenu::headerTextColourId, dim);
        setColour (juce::TextEditor::backgroundColourId, p.surface);
        setColour (juce::TextEditor::textColourId, p.ink);
        setColour (juce::TextEditor::outlineColourId, p.line);
        setColour (juce::TextEditor::focusedOutlineColourId, accent);
        setColour (juce::AlertWindow::backgroundColourId, p.surface);
        setColour (juce::AlertWindow::textColourId, p.ink);
        setColour (juce::AlertWindow::outlineColourId, p.line);
    }

    bool dark = false;
};

// Palette of the theme the component is currently drawn in (light if it is not inside a ChromaLookAndFeel).
inline const ChromaPalette& paletteOf (const juce::Component& c)
{
    if (auto* l = dynamic_cast<const ChromaLookAndFeel*> (&c.getLookAndFeel()))
        return l->palette();
    return chromaLightPalette();
}
