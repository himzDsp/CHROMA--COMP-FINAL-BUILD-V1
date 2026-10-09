#include "PluginProcessor.h"
#include "PluginEditor.h"

using juce::String;

juce::AudioProcessorValueTreeState::ParameterLayout ChromaCompProcessor::createLayout()
{
    using NR = juce::NormalisableRange<float>;
    using Attr = juce::AudioParameterFloatAttributes;
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    // Knob taper without a dead zone. The stock setSkewForCentre curve has zero slope at the bottom end, so the first
    // stretch of knob travel (e.g. ratio 1.0:1 -> 1.1:1) changed nothing. This exponential taper starts with a real slope:
    // the knob responds from the first degree, like the linear makeup knob, while the upper end is still stretched.
    // 'centre' is the value reached at half travel (must lie below the middle of the range).
    auto tapered = [] (float start, float end, float centre, float interval)
    {
        const float k = 2.0f * std::log ((end - start) / (centre - start) - 1.0f);
        const float ek = std::exp (k) - 1.0f;
        return NR (start, end,
                   [k, ek] (float s, float e, float t) { return s + (e - s) * (std::exp (k * t) - 1.0f) / ek; },
                   [k, ek] (float s, float e, float v) { return std::log (1.0f + juce::jlimit (0.0f, 1.0f, (v - s) / (e - s)) * ek) / k; },
                   [interval] (float s, float e, float v) { return juce::jlimit (s, e, s + interval * std::round ((v - s) / interval)); });
    };

    auto fromText = [] (const String& s) { return s.retainCharacters ("0123456789.-+").getFloatValue(); };
    auto add = [&] (const char* id, const char* name, NR range, float def, std::function<String (float, int)> toText)
    {
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { id, 1 }, name, range, def,
            Attr().withStringFromValueFunction (std::move (toText)).withValueFromStringFunction (fromText)));
    };

    add ("threshold", "Threshold", NR (-60.0f, 0.0f, 0.1f), -18.0f,
         [] (float v, int) { return String (v, 1) + " dB"; });

    auto ratio = tapered (1.0f, 20.0f, 6.0f, 0.1f);
    add ("ratio", "Ratio", ratio, 3.0f, [] (float v, int) { return String (v, 1) + ":1"; });

    // Knee width: 0 = hard knee, up to 30 dB of soft knee. Tapered so the useful 0-12 dB range gets most of the travel.
    auto knee = tapered (0.0f, 30.0f, 6.0f, 0.1f);
    add ("knee", "Knee", knee, 0.0f, [] (float v, int) { return v < 0.05f ? String ("Hard") : String (v, 1) + " dB"; });

    auto atk = tapered (0.1f, 200.0f, 10.0f, 0.01f);
    add ("attack", "Attack", atk, 10.0f,
         [] (float v, int) { return String (v, v < 10.0f ? 2 : (v < 100.0f ? 1 : 0)) + " ms"; });

    auto rel = tapered (5.0f, 1000.0f, 120.0f, 0.1f);
    add ("release", "Release", rel, 120.0f, [] (float v, int) { return String (v, v < 100.0f ? 1 : 0) + " ms"; });

    add ("makeup", "Makeup", NR (0.0f, 24.0f, 0.1f), 0.0f, [] (float v, int) { return "+" + String (v, 1) + " dB"; });
    add ("mix", "Mix", NR (0.0f, 100.0f, 1.0f), 100.0f, [] (float v, int) { return String (juce::roundToInt (v)) + " %"; });

    // ---- hold / range / lookahead time / adapt ----
    auto hold = tapered (0.0f, 500.0f, 50.0f, 0.1f);
    add ("hold", "Hold", hold, 0.0f,
         [] (float v, int) { return v < 0.05f ? String ("Off") : String (v, v < 100.0f ? 1 : 0) + " ms"; });

    add ("range", "Range", NR (0.0f, 120.0f, 0.1f), 60.0f,
         [] (float v, int) { return String (v, 1) + " dB"; });

    add ("lookaheadTime", "Lookahead Time", NR (0.0f, chroma::Compressor::maxLookaheadMs, 0.01f),
         chroma::Compressor::lookaheadDefaultMs,
         [] (float v, int) { return String (v, 2) + " ms"; });

    add ("adapt", "Adapt", NR (0.0f, 100.0f, 1.0f), 0.0f,
         [] (float v, int) { return v < 0.5f ? String ("Off") : String (juce::roundToInt (v)) + " %"; });

    // ---- input / output trim (separate tab in the GUI; not part of presets) ----
    auto dbText = [] (float v, int) { return (v > 0.04f ? "+" : "") + String (v, 1) + " dB"; };
    add ("inputGain",  "Input",  NR (-24.0f, 24.0f, 0.1f), 0.0f, dbText);
    add ("outputGain", "Output", NR (-24.0f, 24.0f, 0.1f), 0.0f, dbText);

    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "feel", 1 }, "Feel",
                juce::StringArray { "Tight", "Subtle", "Balance", "Glue" }, 1));
    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "intensity", 1 }, "Intensity",
                juce::StringArray { "Low", "Mid", "High" }, 1));
    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "transients", 1 }, "Transients",
                juce::StringArray { "Preserve", "Unpreserved" }, 0));
    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "lookahead", 1 }, "Lookahead", false));
    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "autoRelease", 1 }, "Auto Release", false));
    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "adaptiveOn", 1 }, "Adaptive Engine", true));
    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "bypass", 1 }, "Bypass", false));
    // oversampling of the compressor core (not part of presets). Default 2x (1x = no filtering, no added latency, lowest CPU); sessions saved before this existed are set to 4x
    // in setStateInformation so they keep sounding the same.
    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "oversampling", 1 }, "Oversampling",
                juce::StringArray { "1x", "2x", "4x" }, 1));
    return layout;
}

ChromaCompProcessor::ChromaCompProcessor()
    : AudioProcessor (BusesProperties().withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "STATE", createLayout())
{
    pThr = apvts.getRawParameterValue ("threshold");  pRatio = apvts.getRawParameterValue ("ratio");
    pKnee = apvts.getRawParameterValue ("knee");      pAtk = apvts.getRawParameterValue ("attack");
    pRel = apvts.getRawParameterValue ("release");    pMakeup = apvts.getRawParameterValue ("makeup");
    pMix = apvts.getRawParameterValue ("mix");        pLook = apvts.getRawParameterValue ("lookahead");
    pBypass = apvts.getRawParameterValue ("bypass");
    pFeel = apvts.getRawParameterValue ("feel");  pInt = apvts.getRawParameterValue ("intensity");
    pTrans = apvts.getRawParameterValue ("transients");
    pHold = apvts.getRawParameterValue ("hold");              pRange = apvts.getRawParameterValue ("range");
    pLookTime = apvts.getRawParameterValue ("lookaheadTime"); pAuto = apvts.getRawParameterValue ("autoRelease");
    pAdapt = apvts.getRawParameterValue ("adapt");
    pAdaptiveOn = apvts.getRawParameterValue ("adaptiveOn");
    pInGain = apvts.getRawParameterValue ("inputGain");      pOutGain = apvts.getRawParameterValue ("outputGain");
    pOs = apvts.getRawParameterValue ("oversampling");
    presets.load ("Init");   // a fresh instance starts on the Init preset: no compression, adaptive engine off
}

bool ChromaCompProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    const auto& o = l.getMainOutputChannelSet();
    return (o == juce::AudioChannelSet::mono() || o == juce::AudioChannelSet::stereo())
           && o == l.getMainInputChannelSet();
}

void ChromaCompProcessor::prepareToPlay (double sr, int blockSize)
{
    const int channels = juce::jmax (1, getTotalNumInputChannels());

    // oversamplers for 2x and 4x (1x has none). useIntegerLatency = true keeps the reported latency a whole number of host samples.
    using OS = juce::dsp::Oversampling<float>;
    osMaxBlock = juce::jmax (1, blockSize);
    for (int m = 0; m < kNumOsModes; ++m)
    {
        auto& os = oversamplers[(size_t) m];
        if (m == 0)
        {
            os.reset();
            osLatencies[0] = 0;
        }
        else
        {
            os = std::make_unique<OS> ((size_t) channels, (size_t) m,
                                       kLinearPhaseOversampling ? OS::filterHalfBandFIREquiripple
                                                                : OS::filterHalfBandPolyphaseIIR,
                                       true, true);
            os->initProcessing ((size_t) osMaxBlock);
            os->reset();
            osLatencies[(size_t) m] = (int) std::lround (os->getLatencyInSamples());
        }
        comps[(size_t) m].prepare (sr * (double) (1 << m), channels);   // each core runs at host rate x its factor
    }
    activeOs = juce::jlimit (0, kNumOsModes - 1, juce::roundToInt (pOs != nullptr ? pOs->load() : 1.0f));

    // lookahead is a whole number of *host* samples, so it is an exact multiple of the oversampling factor.
    // The time is a parameter now; processBlock re-derives these whenever it changes.
    hostSampleRate = sr;
    const float timeMs = pLookTime != nullptr ? pLookTime->load() : chroma::Compressor::lookaheadDefaultMs;
    lookaheadHost    = chroma::Compressor::lookaheadSamplesFor (timeMs, sr);
    lookaheadQuantMs = (float) ((double) lookaheadHost * 1000.0 / sr);

    loudIn.prepare (sr);  loudOut.prepare (sr);                 // BS.1770 K-weighting is sample-rate dependent
    listener.prepare (sr);                                      // analysis stays at the host rate
    if (analysisState.load() != Idle) analysisState = Idle;
    scratch.assign ((size_t) juce::jmax (64, blockSize), 0.0f);
    paramsValid = false;
    lastInGain  = pInGain  != nullptr ? juce::Decibels::decibelsToGain (pInGain->load())  : 1.0f;   // no fade-in on start
    lastOutGain = pOutGain != nullptr ? juce::Decibels::decibelsToGain (pOutGain->load()) : 1.0f;

    const bool look = pLook != nullptr && pLook->load() > 0.5f;
    const int lat = osLatencies[(size_t) activeOs] + (look ? lookaheadHost : 0);
    latencyTarget = lat;
    setLatencySamples (lat);
}

void ChromaCompProcessor::reset()
{
    for (auto& os : oversamplers) if (os != nullptr) os->reset();   // flush the up/down filter state
    for (auto& c : comps) c.reset();
    loudIn.reset();  loudOut.reset();
}

// Up-sample -> compressor at 1x / 2x / 4x -> down-sample, in place. Dry and wet both travel through the oversampler, so the
// Mix knob and bypass stay phase-aligned. Host blocks longer than the prepared size are processed in chunks.
// 1x: the core runs directly at the host rate (no oversampler, no filter latency).
void ChromaCompProcessor::runCompressorOversampled (juce::AudioBuffer<float>& buffer, int numCh)
{
    auto& core = comps[(size_t) activeOs];
    auto* oversampling = oversamplers[(size_t) activeOs].get();

    float* const* data = buffer.getArrayOfWritePointers();
    const int total = buffer.getNumSamples();

    if (oversampling == nullptr)
    {
        core.process (data, numCh, total);                // (also the case before prepareToPlay: the core ignores calls until prepared)
        return;
    }

    for (int pos = 0; pos < total; pos += osMaxBlock)
    {
        const int n = juce::jmin (osMaxBlock, total - pos);
        juce::dsp::AudioBlock<float> block (data, (size_t) numCh, (size_t) pos, (size_t) n);

        auto up = oversampling->processSamplesUp (block);
        float* ptrs[2] = { up.getChannelPointer (0), numCh > 1 ? up.getChannelPointer (1) : nullptr };
        core.process (ptrs, numCh, (int) up.getNumSamples());

        oversampling->processSamplesDown (block);
    }
}

void ChromaCompProcessor::feedAnalyzer (AnalyzerFifo& fifo, const juce::AudioBuffer<float>& b, int numCh)
{
    const int n = b.getNumSamples();
    const float* l = b.getReadPointer (0);
    const float* r = b.getReadPointer (numCh > 1 ? 1 : 0);
    for (int pos = 0; pos < n;)
    {
        const int m = juce::jmin ((int) scratch.size(), n - pos);
        for (int i = 0; i < m; ++i) scratch[(size_t) i] = 0.5f * (l[pos + i] + r[pos + i]);
        fifo.push (scratch.data(), m);
        pos += m;
    }
}

void ChromaCompProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    const int numCh = juce::jmin (2, getTotalNumInputChannels());
    for (int c = getTotalNumInputChannels(); c < getTotalNumOutputChannels(); ++c)
        buffer.clear (c, 0, n);
    if (numCh == 0 || n == 0) return;

    // null-safe parameter read: a missing parameter falls back to its default
    auto get = [] (const std::atomic<float>* a, float def) noexcept { return a != nullptr ? a->load() : def; };

    // lookahead time -> whole host samples (so latency is an integer and the 4x core delay is an exact multiple of 4)
    const bool look = get (pLook, 0.0f) > 0.5f;
    const float timeMs = juce::jlimit (0.0f, chroma::Compressor::maxLookaheadMs,
                                       get (pLookTime, chroma::Compressor::lookaheadDefaultMs));
    if (hostSampleRate > 0.0)
    {
        lookaheadHost    = chroma::Compressor::lookaheadSamplesFor (timeMs, hostSampleRate);
        lookaheadQuantMs = (float) ((double) lookaheadHost * 1000.0 / hostSampleRate);
    }

    // oversampling mode (1x / 2x / 4x): all three cores are prepared, so a change just selects another one
    const int osMode = juce::jlimit (0, kNumOsModes - 1, juce::roundToInt (get (pOs, 1.0f)));
    if (osMode != activeOs)
    {
        activeOs = osMode;
        if (auto* os = oversamplers[(size_t) activeOs].get()) os->reset();   // start the newly selected path from silence
        comps[(size_t) activeOs].reset();
        paramsValid = false;                                                  // hand the current settings to the new core below
    }

    chroma::Compressor::Params p;
    p.thresholdDb = get (pThr, -18.0f);   p.ratio     = get (pRatio, 3.0f);
    p.kneeDb      = get (pKnee, 0.0f);    p.attackMs  = get (pAtk, 10.0f);
    p.releaseMs   = get (pRel, 120.0f);   p.makeupDb  = get (pMakeup, 0.0f);
    const bool bypass = get (pBypass, 0.0f) > 0.5f;
    p.mix         = bypass ? 0.0f : get (pMix, 100.0f) * 0.01f;   // bypass = delay-matched dry, click-free
    p.lookahead   = look;
    p.lookaheadTimeMs = lookaheadQuantMs;   // whole host samples -> exact multiple of the oversampling factor
    p.holdMs      = get (pHold, 0.0f);
    p.rangeDb     = get (pRange, 60.0f);
    p.autoRelease = get (pAuto, 0.0f) > 0.5f;
    p.dynamicsAmount     = juce::jlimit (0.0f, 1.0f, get (pAdapt, 0.0f) * 0.01f);
    p.preserveTransients = get (pTrans, 0.0f) < 0.5f;   // existing "transients" choice: 0 = Preserve
    p.adaptiveEnabled    = get (pAdaptiveOn, 1.0f) > 0.5f;   // off: detector + listener are not run at all

    // reported latency = oversampler latency + lookahead (host samples); re-report whenever it changes
    const int lat = osLatencies[(size_t) activeOs] + (look ? lookaheadHost : 0);
    if (! paramsValid || lat != latencyTarget.load (std::memory_order_relaxed))
    {
        latencyTarget = lat;
        triggerAsyncUpdate();
    }

    if (! paramsValid || ! (p == lastParams))
    {
        comps[(size_t) activeOs].setParams (p, ! paramsValid);   // first call after prepare / core switch: start on the settings, no ramp from stale ones
        lastParams = p;
        paramsValid = true;
    }

    // ---- input trim (before everything, so the analyser, meters and compressor all see the trimmed signal) ----
    // Bypass = unity, so a bypassed plugin is a true (delay-matched) pass-through. Gain is ramped across the block.
    auto applyGain = [&] (float& last, float target)
    {
        if (last == 1.0f && target == 1.0f) return;
        for (int c = 0; c < numCh; ++c) buffer.applyGainRamp (c, 0, n, last, target);
        last = target;
    };
    const float inG  = bypass ? 1.0f : juce::Decibels::decibelsToGain (get (pInGain, 0.0f));
    const float outG = bypass ? 1.0f : juce::Decibels::decibelsToGain (get (pOutGain, 0.0f));
    applyGain (lastInGain, inG);

    // ---- adaptive listening: hears the input *before* compression ----
    int ast = analysisState.load (std::memory_order_relaxed);
    if (! p.adaptiveEnabled && (ast == Waiting || ast == Listening))
    {
        // adaptive engine switched off: abandon any running analysis, the listener is not touched
        int e = Waiting;  if (! analysisState.compare_exchange_strong (e, Idle)) { e = Listening; analysisState.compare_exchange_strong (e, Idle); }
        ast = analysisState.load (std::memory_order_relaxed);
    }
    if (ast == Waiting)
    {
        listener.begin();
        listenFeel = juce::roundToInt (get (pFeel, 1.0f));  listenInt = juce::roundToInt (get (pInt, 1.0f));
        listenTrans = juce::roundToInt (get (pTrans, 0.0f));
        analysisSeconds = 0.0f;
        analysisState = Listening;
        ast = Listening;
    }
    if (ast == Listening)
    {
        if (listenFeel != juce::roundToInt (get (pFeel, 1.0f)) || listenInt != juce::roundToInt (get (pInt, 1.0f))
            || listenTrans != juce::roundToInt (get (pTrans, 0.0f)))
        {
            analysisAborted = true;               // user changed feel / intensity / transients mid-listen
            analysisState = Idle;
        }
        else
        {
            const float* l = buffer.getReadPointer (0);
            const float* r = buffer.getReadPointer (numCh > 1 ? 1 : 0);
            const bool enough = listener.process (l, r, n);
            analysisSeconds = (float) listener.seconds();
            if (enough) { analysisState = Deciding; triggerAsyncUpdate(); }
        }
    }

    const bool analyze = analyzerUsers.load (std::memory_order_relaxed) > 0;
    if (analyze)
    {
        feedAnalyzer (inFifo, buffer, numCh);
        loudIn.process (buffer.getArrayOfReadPointers(), numCh, n);
    }

    runCompressorOversampled (buffer, numCh);
    applyGain (lastOutGain, outG);          // output trim, after the compressor and before the output meters

    auto& activeCore = comps[(size_t) activeOs];
    grMeterDb.store (bypass ? 0.0f : activeCore.takeBlockGrMax(), std::memory_order_relaxed);
    if (bypass) activeCore.takeBlockGrMax();
    if (analyze) autoReleaseMsShown.store (activeCore.autoReleaseMs(), std::memory_order_relaxed);
    if (analyze)
    {
        feedAnalyzer (outFifo, buffer, numCh);
        loudOut.process (buffer.getArrayOfReadPointers(), numCh, n);
    }
}

// ============================ Adaptive analyze ============================
void ChromaCompProcessor::startAnalysis()
{
    if (analysisState.load() != Idle) return;
    if (pAdaptiveOn != nullptr && pAdaptiveOn->load() < 0.5f)
    {
        analysisReport = "The adaptive engine is off. Switch it on to use Adaptive analyze.";
        return;
    }
    analysisOk = false;
    analysisAborted = false;
    analysisSeconds = 0.0f;
    analysisReport = "Play your track. Listening for at least 5 seconds...";
    analysisState = Waiting;
}

void ChromaCompProcessor::cancelAnalysis()
{
    int e = Waiting;  if (! analysisState.compare_exchange_strong (e, Idle)) { e = Listening; analysisState.compare_exchange_strong (e, Idle); }
    analysisReport = "Analysis cancelled. Press Adaptive analyze to listen again.";
}

void ChromaCompProcessor::handleAsyncUpdate()
{
    setLatencySamples (latencyTarget.load());
    if (analysisState.load() == Deciding) finishAnalysis();
}

void ChromaCompProcessor::finishAnalysis()
{
    if (pAdaptiveOn != nullptr && pAdaptiveOn->load() < 0.5f)   // switched off while deciding: apply nothing
    {
        analysisReport = "The adaptive engine is off. Switch it on to use Adaptive analyze.";
        analysisState = Idle;
        return;
    }
    const auto a = chroma::analyse (listener);
    if (! a.ok)
    {
        analysisReport = "Not enough signal to decide. Press Adaptive analyze again on a louder section.";
        analysisState = Idle;
        return;
    }
    const auto d = chroma::decide (a, juce::roundToInt (pFeel != nullptr ? pFeel->load() : 1.0f),
                                      juce::roundToInt (pInt != nullptr ? pInt->load() : 1.0f),
                                      (pTrans != nullptr ? pTrans->load() : 0.0f) < 0.5f);
    const float targets[8] = { d.threshold, d.ratio, d.knee, d.attack, d.release, d.makeup, d.range, d.adapt * 100.0f };
    tween.start (targets);
    analysisReport = juce::String (d.report);
    analysisOk = true;
    analysisState = Idle;
}

void ChromaCompProcessor::Tween::start (const float* targets)
{
    static const char* ids[8] = { "threshold", "ratio", "knee", "attack", "release", "makeup", "range", "adapt" };
    if (isTimerRunning()) for (auto* p : params) if (p != nullptr) p->endChangeGesture();
    for (int i = 0; i < 8; ++i)
    {
        params[i] = state.getParameter (ids[i]);
        if (params[i] == nullptr) continue;   // unknown ID: skip it (timerCallback also skips nulls)
        from[i] = params[i]->convertFrom0to1 (params[i]->getValue());
        to[i] = targets[i];
        params[i]->beginChangeGesture();
    }
    t0 = juce::Time::getMillisecondCounter();
    startTimerHz (60);
}

void ChromaCompProcessor::Tween::timerCallback()
{
    const float t = juce::jmin (1.0f, (float) (juce::Time::getMillisecondCounter() - t0) / 800.0f);
    const float e = 1.0f - std::pow (1.0f - t, 3.0f);                       // ease-out, as in the prototype
    for (int i = 0; i < 8; ++i)
        if (auto* p = params[i])
            p->setValueNotifyingHost (p->convertTo0to1 (from[i] + (to[i] - from[i]) * e));
    if (t >= 1.0f)
    {
        stopTimer();
        for (auto* p : params) if (p != nullptr) p->endChangeGesture();
    }
}

juce::AudioProcessorEditor* ChromaCompProcessor::createEditor() { return new ChromaCompEditor (*this); }

void ChromaCompProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    if (auto xml = apvts.copyState().createXml()) copyXmlToBinary (*xml, dest);
}

void ChromaCompProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType()))
        {
            const auto tree = juce::ValueTree::fromXml (*xml);
            const bool hadOversampling = tree.getChildWithProperty ("id", "oversampling").isValid();
            apvts.replaceState (tree);

            // sessions saved before the selector existed always ran at 4x: keep them sounding the same
            if (! hadOversampling)
                if (auto* op = apvts.getParameter ("oversampling"))
                    op->setValueNotifyingHost (op->convertTo0to1 (2.0f));
        }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new ChromaCompProcessor(); }
