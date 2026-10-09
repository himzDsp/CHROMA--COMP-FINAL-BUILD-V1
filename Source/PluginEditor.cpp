#include "PluginEditor.h"

ChromaCompEditor::ChromaCompEditor (ChromaCompProcessor& p)
    : AudioProcessorEditor (&p), proc (p), waveform (p),
      kThr (p.apvts, "threshold", "THRESHOLD"), kRatio (p.apvts, "ratio", "RATIO", 1.0),
      kKnee (p.apvts, "knee", "KNEE"),          kAtk (p.apvts, "attack", "ATTACK"),
      kRel (p.apvts, "release", "RELEASE"),     kMakeup (p.apvts, "makeup", "MAKEUP"),
      kMix (p.apvts, "mix", "MIX"),
      kHold (p.apvts, "hold", "HOLD"),          kRange (p.apvts, "range", "RANGE"),
      kLookTime (p.apvts, "lookaheadTime", "LOOK TIME"), kAdapt (p.apvts, "adapt", "ADAPT"),
      kIn (p.apvts, "inputGain", "INPUT"),      kOut (p.apvts, "outputGain", "OUTPUT"),
      feelRow (p.apvts, "feel", { "TIGHT", "SUBTLE", "BALANCE", "GLUE" }, 101),
      transRow (p.apvts, "transients", { "PRESERVE", "UNPRESERVED" }, 102),
      intRow (p.apvts, "intensity", { "LOW", "MID", "HIGH" }, 103)
{
    // window size: read the remembered width FIRST. setResizeLimits() below constrains the still-empty editor to the minimum
    // size, which calls resized() and would overwrite the remembered width with the minimum before it could be read.
    // (key "uiWidthV2": the old "uiWidth" was always overwritten with the minimum by that very bug, so it is ignored and
    // every project starts at the new default size once)
    const int savedW = (int) proc.apvts.state.getProperty ("uiWidthV2", 0);

    // theme: remembered in the plugin state, so it survives closing the window and reloading the session
    const bool startDark = (bool) proc.apvts.state.getProperty ("darkMode", false);
    lnf.setDark (startDark);
    setLookAndFeel (&lnf);
    setOpaque (true);

    darkBtn.setToggleState (startDark, juce::dontSendNotification);
    darkBtn.onClick = [this] { applyTheme (darkBtn.getToggleState()); };
    addAndMakeVisible (darkBtn);

    // footer: oversampling dropdown (opens a floating list: 1x / 2x / 4x; picking one selects it and closes the list)
    osBox.addItemList (juce::StringArray { "1x", "2x", "4x" }, 1);
    osBox.getProperties().set ("footer", true);
    osAtt = std::make_unique<APVTS::ComboBoxAttachment> (p.apvts, "oversampling", osBox);
    addAndMakeVisible (osBox);

    // footer: reset (compressor + adaptive-engine settings only)
    resetBtn.getProperties().set ("footer", true);
    resetBtn.onClick = [this] { resetSettings(); };
    addAndMakeVisible (resetBtn);

    addAndMakeVisible (waveform);
    for (auto* k : { &kThr, &kRatio, &kKnee, &kAtk, &kRel, &kMakeup, &kMix, &kHold, &kRange, &kLookTime, &kAdapt })
        addAndMakeVisible (*k);

    for (auto* b : { &prevBtn, &nextBtn, &saveBtn, &delBtn, &lookBtn, &bypassBtn })
        addAndMakeVisible (*b);
    addAndMakeVisible (starBtn);
    addAndMakeVisible (adaptiveOnBtn);
    addAndMakeVisible (kIn);
    addAndMakeVisible (kOut);
    addAndMakeVisible (autoRelBtn);
    addAndMakeVisible (presetBox);

    juce::Component* adaptiveUi[] = { &feelRow, &transRow, &intRow, &adaptBtn, &adaptBar, &feelCap, &transCap, &intCap, &adaptStatus, &report };
    for (auto* c : adaptiveUi)
        addAndMakeVisible (*c);
    for (auto* l : { &feelCap, &transCap, &intCap })
        l->setColour (juce::Label::textColourId, ChromaLookAndFeel::dim);
    feelCap.setText ("FEEL", juce::dontSendNotification);
    transCap.setText ("TRANSIENTS", juce::dontSendNotification);
    intCap.setText ("INTENSITY", juce::dontSendNotification);
    adaptStatus.setJustificationType (juce::Justification::centredRight);
    adaptStatus.setColour (juce::Label::textColourId, ChromaLookAndFeel::dim);
    report.setJustificationType (juce::Justification::topLeft);
    report.setMinimumHorizontalScale (1.0f);
    report.setColour (juce::Label::textColourId, ChromaLookAndFeel::dim);
    adaptBtn.onClick = [this]
    {
        if (proc.analysisState.load() == ChromaCompProcessor::Idle) proc.startAnalysis(); else proc.cancelAnalysis();
        timerCallback();
    };

    lookBtn.setClickingTogglesState (true);
    bypassBtn.setClickingTogglesState (true);
    lookAtt    = std::make_unique<APVTS::ButtonAttachment> (p.apvts, "lookahead", lookBtn);
    bypassAtt  = std::make_unique<APVTS::ButtonAttachment> (p.apvts, "bypass", bypassBtn);
    autoRelAtt = std::make_unique<APVTS::ButtonAttachment> (p.apvts, "autoRelease", autoRelBtn);
    adaptiveOnAtt = std::make_unique<APVTS::ButtonAttachment> (p.apvts, "adaptiveOn", adaptiveOnBtn);
    autoRelBtn.onClick = [this] { syncAutoRelease(); };

    prevBtn.onClick = [this] { if (proc.presets.step (-1)) refreshPresetBox(); };
    nextBtn.onClick = [this] { if (proc.presets.step (+1)) refreshPresetBox(); };
    saveBtn.onClick = [this] { showSaveDialog(); };
    delBtn.onClick  = [this] { showDeleteConfirm(); };
    starBtn.onClick = [this]
    {
        const auto cur = proc.presets.getCurrentName();
        proc.presets.setFavourite (cur, ! proc.presets.isFavourite (cur));
        refreshPresetBox();                        // the preset is added to / removed from the Favourites group
    };
    presetBox.onChange = [this]
    {
        const int idx = presetBox.getSelectedId() - 1;
        if (juce::isPositiveAndBelow (idx, presetNames.size()))
        {
            proc.presets.load (presetNames[idx]);
            updatePresetButtons();
        }
    };
    refreshPresetBox();

    // limits keep the same 600 : 524 ratio as kBaseW : kBaseH (589 x 514 ... 900 x 786); opens at the minimum size
    setResizable (true, true);
    setResizeLimits (kMinW, kMinH, kMaxW, kMaxH);
    getConstrainer()->setFixedAspectRatio ((double) kBaseW / (double) kBaseH);
    // reopen at the size the user last dragged it to (kept in the processor state, so it survives closing the plugin
    // window / mixer-channel tab and is saved with the project); first open uses the default start size
    const int startW = savedW > 0 ? juce::jlimit (kMinW, kMaxW, savedW) : juce::roundToInt ((float) kBaseW * kStartScale);   // old smaller saved sizes are lifted to the new minimum
    setSize (startW, juce::roundToInt ((float) startW * (float) kBaseH / (float) kBaseW));
    sizeRestored = true;      // from here on a resize is the user's (or the host's) and is remembered

    ++proc.analyzerUsers;     // the audio thread only feeds the analyser while at least one editor is open
    timerCallback();
    startTimerHz (15);
}

ChromaCompEditor::~ChromaCompEditor()
{
    stopTimer();
    --proc.analyzerUsers;
    setLookAndFeel (nullptr);
}

// Back to the values a fresh instance starts with: the "Init" preset covers exactly the compressor knobs / switches
// (threshold ... adapt, auto release, lookahead) and the adaptive engine (on/off, feel, intensity, transients). Input / output
// trim, bypass, oversampling and the theme are not part of it, so they are left alone.
void ChromaCompEditor::resetSettings()
{
    if (proc.analysisState.load() != ChromaCompProcessor::Idle)
        proc.cancelAnalysis();                     // a listening run would otherwise finish and overwrite the reset values
    if (proc.presets.load ("Init"))
        refreshPresetBox();                        // the preset box follows (it shows Init again)
    proc.analysisOk = false;
    proc.analysisReport = "Settings reset. Pick a feel and intensity, play your track, then press Adaptive analyze.";
    timerCallback();
}

ChromaCompEditor::FooterLayout ChromaCompEditor::footerLayout (float s) const
{
    auto textWidth = [] (const juce::Font& f, const juce::String& t)
    {
        juce::GlyphArrangement ga;
        ga.addLineOfText (f, t, 0, 0);
        return ga.getBoundingBox (0, -1, true).getWidth();
    };
    const auto f = ChromaLookAndFeel::makeFont (9.0f * s);     // same font as the footer text drawn in paint()
    FooterLayout fl;
    fl.capX = 14.0f * s + textWidth (f, "DEVELOPED BY HIM'Z DSP") + 14.0f * s;
    fl.capW = textWidth (f, "OVERSAMPLING");
    return fl;
}

void ChromaCompEditor::applyTheme (bool dark)
{
    lnf.setDark (dark);
    proc.apvts.state.setProperty ("darkMode", dark, nullptr);
    sendLookAndFeelChange();      // children re-read their colours and repaint
    repaint();
}

// The engine switch is a normal parameter (host automation / presets can flip it), so follow its button state here.
void ChromaCompEditor::syncAdaptiveEnabled()
{
    const bool on = adaptiveOnBtn.getToggleState();
    if (on == adaptiveShown) return;
    adaptiveShown = on;

    for (juce::Component* c : { (juce::Component*) &kAdapt, (juce::Component*) &feelRow, (juce::Component*) &transRow,
                                (juce::Component*) &intRow, (juce::Component*) &adaptBtn, (juce::Component*) &adaptBar })
    {
        c->setEnabled (on);
        c->setAlpha (on ? 1.0f : 0.4f);
    }
    if (! on)
    {
        if (proc.analysisState.load() != ChromaCompProcessor::Idle) proc.cancelAnalysis();
        proc.analysisReport = "The adaptive engine is off: Adapt and Adaptive analyze do nothing. Switch it on to use them.";
    }
    else
        proc.analysisReport = "Adaptive engine on. Pick a feel and intensity, play your track, then press Adaptive analyze.";
}

// Auto release on -> the Release knob goes dull and is locked (the detector ignores it); off -> only the A button is dull.
// Also follows host automation / preset changes, so it is called from the timer as well as from the button.
void ChromaCompEditor::syncAutoRelease()
{
    const int on = autoRelBtn.getToggleState() ? 1 : 0;
    if (on == autoRelState) return;
    autoRelState = on;
    kRel.setEnabled (on == 0);
    kRel.setAlpha (on != 0 ? 0.5f : 1.0f);      // dull, but still readable: it keeps moving to show what auto-release does
    autoRelShown = 0.0f;
    if (on == 0) kRel.clearDisplayValue();
}

// Auto release on: the dull Release knob follows the release time the engine is using right now (pointer, arc and the ms
// text), smoothed so it glides instead of stepping at the timer rate. The parameter itself is not touched.
void ChromaCompEditor::updateAutoReleaseDisplay()
{
    if (! autoRelBtn.getToggleState()) return;

    float target = proc.autoReleaseMsShown.load (std::memory_order_relaxed);
    if (target <= 0.0f)                                  // nothing processed yet: the knob's own value is the centre of the range
        if (auto* r = proc.apvts.getRawParameterValue ("release")) target = r->load();
    target = juce::jmax (1.0f, target);

    autoRelShown = autoRelShown <= 0.0f ? target
                 : std::exp2 (std::log2 (autoRelShown) + 0.45f * (std::log2 (target) - std::log2 (autoRelShown)));
    kRel.setDisplayValue ((double) autoRelShown);
}

void ChromaCompEditor::timerCallback()
{
    syncAdaptiveEnabled();
    syncAutoRelease();
    updateAutoReleaseDisplay();
    if (proc.analysisAborted.exchange (false))
        proc.analysisReport = "Settings changed. Press Adaptive analyze to listen again and retune.";

    const int st = proc.analysisState.load();
    const juce::String btn = st == ChromaCompProcessor::Idle ? "ADAPTIVE ANALYZE" : "CANCEL";
    if (adaptBtn.getButtonText() != btn) adaptBtn.setButtonText (btn);
    adaptBtn.setToggleState (st != ChromaCompProcessor::Idle, juce::dontSendNotification);

    juce::String status;
    float prog = 0.0f;
    switch (st)
    {
        case ChromaCompProcessor::Waiting:   status = "waiting for audio"; break;
        case ChromaCompProcessor::Listening:
        {
            const float sec = proc.analysisSeconds.load();
            prog = sec / 5.0f;
            status = "listening " + juce::String (juce::jmin (5.0f, sec), 1) + " / 5 s";
            break;
        }
        case ChromaCompProcessor::Deciding:  status = "analysing"; prog = 1.0f; break;
        default:                             status = proc.analysisOk ? "done" : "idle"; prog = proc.analysisOk ? 1.0f : 0.0f; break;
    }
    if (! adaptiveShown) { status = "engine off"; prog = 0.0f; }
    adaptBar.set (prog);
    adaptStatus.setText (status, juce::dontSendNotification);
    report.setText (proc.analysisReport, juce::dontSendNotification);
}

void ChromaCompEditor::refreshPresetBox()
{
    presetBox.clear (juce::dontSendNotification);
    presetNames.clear();

    // Three groups, always in this order: Factory (built-in Init + the .json files in the Presets\Factory folder), Favourites, User.
    // Favourites is a shortcut group: a favourite is listed there AND stays in its own group, so the Factory group always
    // shows Init. Every entry has its own id (presetNames[id - 1]).
    const auto favs = proc.presets.getFavourites();
    int id = 1;
    bool any = false;
    auto addGroup = [&] (const juce::String& heading, const juce::StringArray& names)
    {
        if (names.isEmpty()) return;
        if (any) presetBox.addSeparator();
        presetBox.addSectionHeading (heading);
        for (auto& n : names) { presetNames.add (n); presetBox.addItem (n, id++); }
        any = true;
    };

    addGroup ("Factory",    proc.presets.getFactoryNames());
    addGroup ("Favourites", favs);
    addGroup ("User",       proc.presets.getUserNames());

    const int cur = presetNames.indexOf (proc.presets.getCurrentName());
    if (cur >= 0) presetBox.setSelectedId (cur + 1, juce::dontSendNotification);
    updatePresetButtons();
}

void ChromaCompEditor::updatePresetButtons()
{
    const auto cur = proc.presets.getCurrentName();
    delBtn.setEnabled (! proc.presets.isFactory (cur));
    starBtn.setToggleState (proc.presets.isFavourite (cur), juce::dontSendNotification);
}

void ChromaCompEditor::showSaveDialog()
{
    auto* aw = new juce::AlertWindow ("Save preset", "Preset name:", juce::MessageBoxIconType::NoIcon, this);
    const auto cur = proc.presets.getCurrentName();
    aw->addTextEditor ("name", proc.presets.isFactory (cur) ? juce::String() : cur, juce::String());
    aw->addButton ("Save",   1, juce::KeyPress (juce::KeyPress::returnKey));
    aw->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));

    juce::Component::SafePointer<ChromaCompEditor> safe (this);
    aw->enterModalState (true, juce::ModalCallbackFunction::create ([safe, aw] (int result)
    {
        if (result != 1 || safe == nullptr) return;
        const auto name = aw->getTextEditorContents ("name").trim();
        if (safe->proc.presets.save (name))
            safe->refreshPresetBox();
        else
            juce::AlertWindow::showAsync (juce::MessageBoxOptions::makeOptionsOk (
                juce::MessageBoxIconType::WarningIcon, "Could not save",
                "Please enter a name that is not a factory preset name."), [] (int) {});
    }), true);
}

void ChromaCompEditor::showDeleteConfirm()
{
    const auto name = proc.presets.getCurrentName();
    if (proc.presets.isFactory (name)) return;

    juce::Component::SafePointer<ChromaCompEditor> safe (this);
    juce::AlertWindow::showAsync (
        juce::MessageBoxOptions::makeOptionsOkCancel (juce::MessageBoxIconType::QuestionIcon, "Delete preset",
                                                      "Delete \"" + name + "\"?", "Delete", "Cancel", this),
        [safe, name] (int result)
        {
            if (result != 1 || safe == nullptr) return;
            if (safe->proc.presets.remove (name))
            {
                safe->proc.presets.load ("Init");
                safe->refreshPresetBox();
            }
        });
}

void ChromaCompEditor::paint (juce::Graphics& g)
{
    using L = ChromaLookAndFeel;
    const auto& pal = paletteOf (*this);
    const float s = (float) getWidth() / (float) kBaseW;

    g.setGradientFill (juce::ColourGradient (pal.bgTop, 0, 0, pal.bgBottom, 0, (float) getHeight(), false));
    g.fillAll();

    // title
    juce::GlyphArrangement ga;
    const auto f1 = L::makeFont (19.0f * s, true);
    ga.addLineOfText (f1, "CHROMA ", 0, 0);
    const float w1 = ga.getBoundingBox (0, -1, true).getWidth();
    g.setFont (f1);
    g.setColour (pal.ink);
    g.drawText ("CHROMA", juce::Rectangle<float> (14 * s, 6 * s, 120 * s, 32 * s), juce::Justification::centredLeft);
    g.setColour (L::accent);
    g.drawText ("COMP", juce::Rectangle<float> (14 * s + w1, 6 * s, 80 * s, 32 * s), juce::Justification::centredLeft);

    // neon rule under the header
    const float ry = 44.0f * s;
    g.setColour (L::accent.withAlpha (0.18f));
    g.fillRect (12 * s, ry - 1.0f, getWidth() - 24 * s, 3.0f);
    g.setGradientFill (juce::ColourGradient (L::accent.withAlpha (0.0f), 12 * s, ry, L::accent, getWidth() * 0.5f, ry, false));
    g.fillRect (12 * s, ry, getWidth() * 0.5f - 12 * s, 1.0f);
    g.setGradientFill (juce::ColourGradient (L::accent, getWidth() * 0.5f, ry, L::accent.withAlpha (0.0f), getWidth() - 12 * s, ry, false));
    g.fillRect (getWidth() * 0.5f, ry, getWidth() * 0.5f - 12 * s, 1.0f);

    // bottom card (adaptive engine; 98 px tall: header row with the on/off button, then the controls)
    const auto card = juce::Rectangle<float> (12 * s, 406 * s, getWidth() - 24 * s, 98 * s);
    g.setColour (pal.surface);  g.fillRoundedRectangle (card, 8.0f * s);
    g.setColour (pal.line);     g.drawRoundedRectangle (card.reduced (0.5f), 8.0f * s, 1.0f);

    // caption next to the small engine on/off button
    g.setColour (L::dim);
    g.setFont (L::makeFont (9.0f * s, true));
    g.drawText ("ADAPTIVE ENGINE", juce::Rectangle<float> (44 * s, 411 * s, 140 * s, 17 * s), juce::Justification::centredLeft, false);

    // separate INPUT / OUTPUT panel (whole-plugin gain), in columns 5-7 of knob row 2
    {
        const float kwf = (float) (getWidth() - juce::roundToInt (24 * s)) / 7.0f;
        const auto io = juce::Rectangle<float> (12 * s + 4.0f * kwf + 4 * s, 331 * s, 3.0f * kwf - 4 * s, 73 * s);   // taller than the knob cells (336-402) so caption and value text have padding inside the card
        g.setColour (pal.surface);  g.fillRoundedRectangle (io, 8.0f * s);
        g.setColour (pal.line);     g.drawRoundedRectangle (io.reduced (0.5f), 8.0f * s, 1.0f);
    }

    g.setColour (L::dim);
    g.setFont (L::makeFont (9.0f * s));
    g.drawText ("DEVELOPED BY HIM'Z DSP", juce::Rectangle<float> (14 * s, getHeight() - 17 * s, 220 * s, 14 * s), juce::Justification::left);
    g.drawText ("V1", juce::Rectangle<float> (getWidth() - 60 * s, getHeight() - 17 * s, 46 * s, 14 * s), juce::Justification::right);

    // caption of the oversampling dropdown, right after the credit text
    const auto fl = footerLayout (s);
    g.drawText ("OVERSAMPLING", juce::Rectangle<float> (fl.capX, getHeight() - 17 * s, fl.capW + 6.0f * s, 14 * s), juce::Justification::left);
}

void ChromaCompEditor::resized()
{
    if (sizeRestored && getWidth() >= kMinW)      // not while the constructor is still setting the window up
        proc.apvts.state.setProperty ("uiWidthV2", getWidth(), nullptr);      // remembered for the next time the window opens

    const float s = (float) getWidth() / (float) kBaseW;
    auto S = [s] (float v) { return juce::roundToInt (v * s); };
    lnf.fontScale = s;

    // header row (preset browser + toggles), smaller buttons
    const int by = S (11), bh = S (22);
    prevBtn.setBounds   (S (156), by, S (20),  bh);
    presetBox.setBounds (S (178), by, S (124), bh);
    nextBtn.setBounds   (S (304), by, S (20),  bh);
    saveBtn.setBounds   (S (332), by, S (38),  bh);
    delBtn.setBounds    (S (374), by, S (32),  bh);
    starBtn.setBounds   (S (412), by, S (22),  bh);
    lookBtn.setBounds   (S (446), by, S (80),  bh);
    bypassBtn.setBounds (S (530), by, S (58),  bh);

    // Layout budget (base px, total height 524): header 0-48, waveform 50-256, knob row 1 260-326,
    // gap 326-336, knob row 2 336-402, card 406-504, footer text below.
    waveform.setBounds (S (12), S (50), getWidth() - S (24), S (206));

    // footer: dark-mode switch, immediately left of the "V1" text (which ends 14 px from the right edge)
    darkBtn.setBounds (getWidth() - S (100), getHeight() - S (18), S (66), S (16));

    // footer, left side: [DEVELOPED BY HIM'Z DSP]  OVERSAMPLING [2x v]  [RESET]
    const auto fl = footerLayout (s);
    const int osX = juce::roundToInt (fl.capX + fl.capW) + S (6);
    osBox.setBounds    (osX, getHeight() - S (18), S (44), S (16));
    resetBtn.setBounds (osX + S (44) + S (8), getHeight() - S (18), S (46), S (16));

    // knob row 1: 7 columns, smaller knobs (cell 66 px high -> knob about 39 px)
    const int ky = S (260), kh = S (66), strip = S (10);   // knob cell 66 (was 72): knobs are 15 % smaller
    const int kw = (getWidth() - S (24)) / 7;
    Knob* knobs[] = { &kThr, &kRatio, &kKnee, &kAtk, &kRel, &kMakeup, &kMix };
    for (int i = 0; i < 7; ++i)
        knobs[i]->setBounds (S (12) + i * kw, ky, kw, kh);

    // round auto-release "A": inside the Release cell, lower-left of the knob beside the value read-out
    // (centre at 9 % of the cell width (clear of the knob and the value read-out) and 78 % of its height: the reference position, adjusted for the smaller knob)
    const int ad = S (20);   // button box; the drawn circle is about 3 px smaller. Centre stays on the reference spot
    autoRelBtn.setBounds (S (12) + 4 * kw + juce::roundToInt ((float) kw * 0.09f) - ad / 2,
                          ky + juce::roundToInt ((float) kh * 0.78f) - ad / 2, ad, ad);
    autoRelBtn.toFront (false);

    // knob row 2: same column grid, so Hold / Range / Look time / Adapt line up under Threshold ... Attack
    const int ky2 = ky + kh + strip, kh2 = S (66);
    Knob* knobs2[] = { &kHold, &kRange, &kLookTime, &kAdapt };
    for (int i = 0; i < 4; ++i)
        knobs2[i]->setBounds (S (12) + i * kw, ky2, kw, kh2);

    // adaptive card header (card spans 406-504): small engine on/off button at the far left, directly above FEEL
    adaptiveOnBtn.setBounds (S (18), S (408), S (23), S (23));   // visual circle about 15 px, rest is room for the glow

    // INPUT / OUTPUT panel: two knobs in the free columns 5-7 of row 2 (whole-plugin gain, independent of the adaptive card)
    kIn.setBounds  (S (12) + 4 * kw + S (6),           ky2, (3 * kw) / 2 - S (4), kh2);
    kOut.setBounds (S (12) + 4 * kw + (3 * kw) / 2 + S (2), ky2, (3 * kw) / 2 - S (4), kh2);

    // adaptive card contents, smaller buttons
    const float capF = 9.0f * s;
    for (auto* l : { &feelCap, &transCap, &intCap }) l->setFont (ChromaLookAndFeel::makeFont (capF, true));
    adaptStatus.setFont (ChromaLookAndFeel::makeFont (9.5f * s));
    report.setFont (ChromaLookAndFeel::makeFont (9.5f * s));

    const int ra = S (431), rb = S (453), rh = S (19);
    feelCap.setBounds  (S (22),  ra, S (40),  rh);   feelRow.setBounds  (S (64),  ra, S (240), rh);
    transCap.setBounds (S (326), ra, S (72),  rh);   transRow.setBounds (S (400), ra, S (178), rh);   // ends at 578: 10 px inside the card edge (588), same inset as the captions on the left
    intCap.setBounds   (S (22),  rb, S (58),  rh);   intRow.setBounds   (S (82),  rb, S (160), rh);
    adaptBtn.setBounds (S (260), rb, S (118), rh);
    adaptBar.setBounds (S (386), rb, S (96),  rh);
    adaptStatus.setBounds (S (486), rb, S (102), rh);
    report.setBounds   (S (22),  S (475), getWidth() - S (44), S (27));
}
