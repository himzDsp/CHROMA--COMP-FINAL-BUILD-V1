#pragma once
// ============================================================================
//  CompressorCore.h  -  Chroma Comp DSP engine (no JUCE dependency)
//
//  Single transparent digital compressor:
//    * feed-forward, log-domain detector, stereo-linked (max of channels)
//    * quadratic soft knee (continuous in value AND slope at both knee edges)
//    * smooth decoupled peak detector (no overshoot, no zipper, accurate release)
//    * variable lookahead 0..10 ms: the audio is delayed, the detector is not,
//      and the gain reduction is held (sliding-window max) until the peak that
//      caused it has actually left the delay line. Dry path is delay-matched
//      for the Mix knob.
//    * hold: release is paused for holdMs after each gain-reduction peak
//    * range: caps the maximum gain reduction (dB)
//    * auto-release: program-dependent release. Transient spikes recover fast,
//      sustained compression recovers slowly (see Params::autoRelease)
//    * threshold / ratio / knee / makeup / mix are smoothed (no zipper on
//      automation or on the Adaptive tween)
//    * transient-adaptive timing: a per-sample TransientDetector scales attack and
//      release on the fly (Params::dynamicsAmount, 0 = off = fixed times)
//    * zero allocations / locks in process(); fast log2/exp2 approximations
//    * early-out: below the lower knee edge the log is never computed
//    * ripple hold: the detector keeps each gain-reduction peak for kRippleHoldMs before the release may start, so
//      the gain does not follow the waveform of bass notes (that ripple is what made fast attack / release settings
//      distort low frequencies). It is causal (no latency) and invisible for anything above ~40 Hz.
//    * exact unity: with no gain reduction and 0 dB makeup the output is bit-identical to the input
//    * the transient detector, the GR average (auto-release) and the hold window only run when they are in use
//
//  Threading: prepare() / setParams() / reset() / process() are all expected
//  to run on the audio thread (that is how ChromaCompProcessor calls them).
// ============================================================================
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>
#include "AdaptiveEngine.h"   // TransientDetector / DynamicTimes

namespace chroma
{
// log2 for x > 0, max abs error ~3e-5 (= 0.0002 dB)
inline float fastLog2 (float x) noexcept
{
    std::uint32_t i;
    std::memcpy (&i, &x, sizeof (i));
    const float e = (float) ((int) ((i >> 23) & 0xff) - 127);
    i = (i & 0x007fffffu) | 0x3f800000u;
    float m;
    std::memcpy (&m, &i, sizeof (m));
    const float p = -2.78684575f + m * (5.04697861f + m * (-3.49261964f
                  + m * (1.59397599f + m * (-0.40488876f + m * 0.04343132f))));
    return e + p;
}

// 2^x for x in about [-100, 100], relative error ~7e-6
inline float fastExp2 (float x) noexcept
{
    x = std::max (-100.0f, std::min (100.0f, x));
    const float fl = std::floor (x);
    const float f  = x - fl;
    const float p  = 1.00000727f + f * (0.69293141f + f * (0.24170999f
                   + f * (0.05166703f + f * 0.01367656f)));
    const std::uint32_t bits = (std::uint32_t) ((int) fl + 127) << 23;
    float s;
    std::memcpy (&s, &bits, sizeof (s));
    return p * s;
}

// Flush values too small to matter (and everything in the denormal range) to zero.
inline float flushDenormal (float x) noexcept
{
    return std::fabs (x) < 1.0e-30f ? 0.0f : x;
}

// One-pole step toward `target`. Returns true (and lands exactly on the target)
// once within `eps`. `eps` must be larger than half an ulp of the value divided
// by `coef`, otherwise float rounding stalls the state short of the snap window;
// the callers below use eps = 1e-4 (relative for gains) or 1e-3..2e-3 (dB values).
inline bool approach (float& v, float target, float coef, float eps) noexcept
{
    v += coef * (target - v);
    if (std::fabs (target - v) <= eps) { v = target; return true; }
    return false;
}

// O(1) amortised sliding-window maximum (monotonic deque in a fixed ring).
// push() returns the max of the last `len` values including the one just pushed.
struct SlidingMax
{
    void prepare (int maxLen)
    {
        int size = 4;
        while (size < maxLen + 2) size <<= 1;
        mask = size - 1;
        val.assign ((size_t) size, 0.0f);
        idx.assign ((size_t) size, 0u);
        reset();
    }
    void reset() noexcept { head = 0; count = 0; counter = 0; }
    bool empty() const noexcept { return count == 0; }

    float push (float v, int len) noexcept
    {
        // drop everything this value dominates
        while (count > 0 && val[(size_t) ((head + count - 1) & mask)] <= v) --count;
        const int pos = (head + count) & mask;
        val[(size_t) pos] = v;
        idx[(size_t) pos] = counter;
        ++count;

        // drop entries that fell out of the window
        const std::uint32_t L = (std::uint32_t) std::max (1, std::min (len, mask));
        while ((std::uint32_t) (counter - idx[(size_t) head]) >= L) { head = (head + 1) & mask; --count; }
        ++counter;
        return val[(size_t) head];
    }

private:
    std::vector<float> val;
    std::vector<std::uint32_t> idx;
    int mask = 0, head = 0, count = 0;
    std::uint32_t counter = 0;
};

class Compressor
{
public:
    static constexpr float lookaheadDefaultMs = 1.5f;
    static constexpr float lookaheadMs        = lookaheadDefaultMs;   // kept for existing callers (= default time)
    static constexpr float maxLookaheadMs     = 10.0f;
    static constexpr float kRippleHoldMs      = 12.0f;   // internal peak hold of the detector (see header)

    struct Params
    {
        float thresholdDb = -18.0f;
        float ratio       = 3.0f;
        float kneeDb      = 8.0f;
        float attackMs    = 10.0f;
        float releaseMs   = 120.0f;
        float makeupDb    = 0.0f;
        float mix         = 1.0f;     // 0..1 (1 = fully compressed)
        bool  lookahead   = false;
        float lookaheadTimeMs = lookaheadDefaultMs;   // 0..10 ms, used when `lookahead` is true
        float holdMs      = 0.0f;     // 0..500 ms; release is paused this long after each peak (0 = off)
        float rangeDb     = 60.0f;    // 0..120 dB; maximum gain reduction (60 = effectively unlimited)

        // Knee hardness 0..1. 0 = the normal soft quadratic knee of width kneeDb (unchanged behaviour);
        // 1 = hard knee (kneeDb ignored); in between the gain reduction is a continuous blend of the two.
        float kneeHardness = 0.0f;

        // Master switch of the adaptive engine. false = the real-time transient detector is not run at all and
        // dynamicsAmount is ignored (bit-identical to fixed times). true = as before.
        bool  adaptiveEnabled = true;

        // Program-dependent release. When on, releaseMs is the *centre* of the range:
        // the release slides between releaseMs/4 (isolated transients) and releaseMs*4
        // (sustained, dense compression) depending on how sustained the gain reduction is.
        bool  autoRelease = false;

        // Real-time transient-adaptive attack/release (see AdaptiveEngine.h, DynamicTimes).
        // dynamicsAmount 0 = off (bit-identical to fixed times), 1 = full effect.
        // preserveTransients: onsets get a slower attack (hit is let through); false: faster attack (hit is caught).
        // Release always speeds up right after a hit.
        float dynamicsAmount     = 0.0f;
        bool  preserveTransients = true;

        bool operator== (const Params& o) const noexcept
        {
            return thresholdDb == o.thresholdDb && ratio == o.ratio && kneeDb == o.kneeDb
                && attackMs == o.attackMs && releaseMs == o.releaseMs && makeupDb == o.makeupDb
                && mix == o.mix && lookahead == o.lookahead && lookaheadTimeMs == o.lookaheadTimeMs
                && holdMs == o.holdMs && rangeDb == o.rangeDb && autoRelease == o.autoRelease
                && dynamicsAmount == o.dynamicsAmount && preserveTransients == o.preserveTransients
                && kneeHardness == o.kneeHardness && adaptiveEnabled == o.adaptiveEnabled;
        }
    };

    // Lookahead length in samples for a given time; use this (or latencySamples()) for host latency
    // reporting once lookaheadTimeMs is exposed as a parameter.
    static int lookaheadSamplesFor (float ms, double sampleRate) noexcept
    {
        const double t = std::max (0.0, std::min ((double) maxLookaheadMs, (double) ms));
        return (int) std::lround (t * 0.001 * sampleRate);
    }

    // Quadratic soft knee. `over` = level - threshold (dB). Returns gain reduction (dB, >= 0).
    //   over < -W/2        : 0
    //   |over| <= W/2      : slope * (over + W/2)^2 / (2W)
    //   over > +W/2        : slope * over
    // Value and first derivative are continuous at both knee edges.
    static float computeGr (float over, float slope, float knee) noexcept
    {
        if (knee > 0.0f && 2.0f * over < knee)
        {
            const float k = std::max (0.0f, over + 0.5f * knee);
            return slope * k * k / (2.0f * knee);
        }
        return over > 0.0f ? slope * over : 0.0f;
    }

    // Soft/hard blend of the knee. hardness 0 returns computeGr() exactly; 1 returns the hard-knee line.
    // computeGr() >= hard-knee GR everywhere, so the blend never goes negative.
    static float computeGrBlend (float over, float slope, float knee, float hardness) noexcept
    {
        const float soft = computeGr (over, slope, knee);
        if (hardness <= 0.0f) return soft;
        const float hard = over > 0.0f ? slope * over : 0.0f;
        return soft + hardness * (hard - soft);
    }

    void prepare (double sampleRate, int numChannels)
    {
        sr = sampleRate > 0.0 ? sampleRate : 44100.0;
        nCh = std::max (1, std::min (2, numChannels));

        maxLookaheadSamples = lookaheadSamplesFor (maxLookaheadMs, sr);
        const int need = (int) std::ceil (maxLookaheadMs * 0.001 * sr) + 2;
        int size = 64;
        while (size < need) size <<= 1;
        ringSize = size;
        ringMask = size - 1;
        for (auto& r : ring) r.assign ((size_t) size, 0.0f);
        rippleHold = std::max (1, (int) std::lround (kRippleHoldMs * 0.001 * sr));
        window.prepare (maxLookaheadSamples + rippleHold + 1);
        detector.prepare (sr);

        smoothCoef = 1.0f - std::exp (-1.0f / (0.010f * (float) sr));   // 10 ms makeup / mix
        gcCoef     = 1.0f - std::exp (-1.0f / (0.005f * (float) sr));   //  5 ms threshold / ratio / knee
        avgCoef    = 1.0f - std::exp (-1.0f / (0.150f * (float) sr));   // 150 ms GR average (auto-release)

        setParams (params);
        reset();

        // start exactly on target (no ramp from stale values)
        thr = thrT;  slope = slopeT;  knee = kneeT;  hardness = hardnessT;
        kneeLowLin = fastExp2 ((thr - 0.5f * knee) * 0.16609640474f);
        gcMoving   = false;
        makeupLin  = targetMakeupLin;
        mixNow     = params.mix;
        makeupMoving = false;  mixMoving = false;
    }

    void reset()
    {
        for (auto& r : ring) std::fill (r.begin(), r.end(), 0.0f);
        window.reset();
        detector.reset();
        wpos = 0; peakHold = 0.0f; env = 0.0f; avgGr = 0.0f; blockGrMax = 0.0f;
        holdCounter = 0;
        releasing = false;  sustainLatch = 0.0f;  latchDisplay = 0.5f;  bypassClean = false;
    }

    // snap = true: the smoothed gain-computer values, makeup and mix jump straight to the new targets instead of ramping
    // from whatever they held before. Use it for the first call after prepare() / reset() / a core switch, so a fresh
    // start does not begin with a ramp from stale (default) settings.
    void setParams (const Params& p, bool snap = false)
    {
        const bool wasAuto = params.autoRelease;
        params = p;

        // gain-computer targets (smoothed per sample in process())
        thrT   = p.thresholdDb;
        kneeT  = std::max (0.0f, p.kneeDb);
        hardnessT = std::max (0.0f, std::min (1.0f, p.kneeHardness));
        slopeT = 1.0f - 1.0f / std::max (1.0f, p.ratio);
        gcMoving = true;

        // ballistics
        atkCoef = std::exp (-1.0f / (std::max (0.01f, p.attackMs)  * 0.001f * (float) sr));
        const float relMs = std::max (1.0f, p.releaseMs);
        relCoef = std::exp (-1.0f / (relMs * 0.001f * (float) sr));
        atkExpK = -1.44269504f / (std::max (0.01f, p.attackMs) * 0.001f * (float) sr);   // atkCoef = 2^(atkExpK * mul)
        relExpK = -1.44269504f / (relMs * 0.001f * (float) sr);
        // adaptive engine master switch: off -> no detector, no dynamic times. Re-arming starts from a clean detector.
        adaptiveOn  = p.adaptiveEnabled;
        const float newDyn = adaptiveOn ? std::max (0.0f, std::min (1.0f, p.dynamicsAmount)) : 0.0f;
        if (newDyn > 0.0f && dynAmount <= 0.0f) detector.reset();   // the detector only runs while it is used: re-arm from clean
        dynAmount   = newDyn;
        dynPreserve = p.preserveTransients;

        // auto-release: per-sample release rate slides (log-domain) between fast and slow
        const float fastMs = std::max (2.0f, relMs * 0.25f);
        const float slowMs = std::max (fastMs * 2.0f, relMs * 4.0f);
        relRateFast = 1.0f / (fastMs * 0.001f * (float) sr);
        relRateSlow = 1.0f / (slowMs * 0.001f * (float) sr);
        relRateK    = fastLog2 (relRateSlow / relRateFast);   // < 0
        relFastMs = fastMs;  relSlowMs = slowMs;

        targetMakeupLin = std::exp2 (p.makeupDb * 0.16609640474f);   // exactly 1.0f at 0 dB (this runs once per change, not per sample)
        if (p.autoRelease && ! wasAuto) avgGr = peakHold;           // the GR average is only tracked while auto-release is on

        // lookahead
        delaySamples = p.lookahead ? std::min (lookaheadSamplesFor (p.lookaheadTimeMs, sr), ringMask) : 0;

        const float hMs = std::max (0.0f, std::min (500.0f, p.holdMs));
        holdSamplesMax = (int) std::lround (hMs * 0.001 * sr);
        rangeLimit     = std::max (0.0f, std::min (120.0f, p.rangeDb));
        autoRel        = p.autoRelease;
        makeupMoving   = true;  mixMoving = true;      // smoothed toward the new targets until they settle

        if (snap)
        {
            thr = thrT;  slope = slopeT;  knee = kneeT;  hardness = hardnessT;
            kneeLowLin = fastExp2 ((thr - 0.5f * knee) * 0.16609640474f);
            gcMoving   = false;
            makeupLin  = targetMakeupLin;
            mixNow     = p.mix;
            makeupMoving = false;  mixMoving = false;
        }
    }

    // Host latency in samples for the current parameters.
    int latencySamples() const noexcept { return delaySamples; }

    // Largest gain reduction (dB, >= 0) seen since the last call. Audio thread only.
    // The release time (ms) auto-release used for the latest release: between Release/4 (spike) and Release*4 (steady
    // compression), the Release knob value itself before the first release. For the editor to show on the locked knob.
    float autoReleaseMs() const noexcept
    {
        if (relFastMs <= 0.0f || relSlowMs <= 0.0f) return 0.0f;
        return relFastMs * std::exp2 (std::log2 (relSlowMs / relFastMs) * latchDisplay);
    }

    float takeBlockGrMax() noexcept { const float g = blockGrMax; blockGrMax = 0.0f; return g; }

    // In-place processing of 1 or 2 channels
    void process (float* const* ch, int numCh, int n) noexcept
    {
        if (ringSize == 0) return;                     // prepare() not called yet
        const int nc = std::min (numCh, nCh);
        const int D  = delaySamples;
        // holdMs may have been shortened since the last block
        if (holdCounter > holdSamplesMax) holdCounter = holdSamplesMax;

        for (int i = 0; i < n; ++i)
        {
            // --- write input into ring, find link peak on the *undelayed* signal
            float peak = 0.0f;
            for (int c = 0; c < nc; ++c)
            {
                const float x = flushDenormal (ch[c][i]);
                ring[c][(size_t) wpos] = x;
                peak = std::max (peak, std::fabs (x));
            }

            // --- bypass / Mix 0 %: the output is the (delay-matched) dry signal, so the whole detector chain is skipped.
            //     Its state is cleared once, so switching back on starts from a clean detector (and Mix ramps in over 10 ms).
            const bool bypassed = ! mixMoving && mixNow == 0.0f;
            if (bypassed)
            {
                if (! bypassClean)
                {
                    env = 0.0f;  peakHold = 0.0f;  avgGr = 0.0f;  holdCounter = 0;  releasing = false;  sustainLatch = 0.0f;
                    window.reset();  detector.reset();  bypassClean = true;
                }
            }
            else bypassClean = false;

            // --- real-time transient tracking -> attack / release scaling (off when dynAmount == 0)
            float atkC = atkCoef, relMul = 1.0f;
            if (dynAmount > 0.0f && ! bypassed)       // detector runs only while the adaptive engine is on AND Adapt > 0
            {
                detector.process (peak);
                const auto o = DynamicTimes::octaves (detector.onset(), detector.activity(), detector.density(),
                                                      dynAmount, dynPreserve);
                atkC   = fastExp2 (atkExpK * fastExp2 (-o.attack));
                relMul = fastExp2 (-o.release);
            }

            // --- smoothed gain-computer parameters (only while they are moving)
            if (gcMoving)
            {
                const bool a = approach (thr,   thrT,   gcCoef, 2.0e-3f);
                const bool b = approach (slope, slopeT, gcCoef, 1.0e-4f);
                const bool d = approach (knee,  kneeT,  gcCoef, 2.0e-3f);
                const bool e = approach (hardness, hardnessT, gcCoef, 1.0e-3f);
                gcMoving = ! (a && b && d && e);
                kneeLowLin = fastExp2 ((thr - 0.5f * knee) * 0.16609640474f);
            }

            // --- idle fast path: nothing is being reduced, nothing is held and every state is at rest, so the gain computer,
            //     the hold window and the envelope would all stay at zero. Skip them (the usual state of a plugin that is
            //     below its threshold, bypassed, or on the Init preset). Bit-identical to running them.
            const bool idle = bypassed || (peak <= kneeLowLin && env == 0.0f && peakHold == 0.0f && window.empty()
                                           && (! autoRel || avgGr == 0.0f));
            if (! idle)
            {
                // --- gain computer (dB domain)
                float gr = 0.0f;
                if (peak > kneeLowLin)
                {
                    const float lvl = 6.02059991f * fastLog2 (peak);
                    gr = std::min (computeGrBlend (lvl - thr, slope, knee, hardness), rangeLimit);
                }

                // --- lookahead: hold the reduction until the peak has left the delay line
                // (and keep it for kRippleHoldMs afterwards, so bass waveforms do not ripple the gain)
                const float grW = (gr == 0.0f && window.empty()) ? 0.0f : window.push (gr, D + rippleHold + 1);

                // --- slow average of the reduction (drives auto-release)
                if (autoRel)
                {
                    avgGr += avgCoef * (grW - avgGr);
                    if (avgGr < 1.0e-6f && grW == 0.0f) avgGr = 0.0f;
                }

                // --- smooth decoupled peak detector (Giannoulis et al.) with hold + auto-release
                if (grW >= peakHold)                // new / sustained peak: follow it and (re)arm the hold timer
                {
                    peakHold    = grW;
                    holdCounter = holdSamplesMax;
                    releasing   = false;
                }
                else if (holdCounter > 0)           // hold: release is paused
                {
                    --holdCounter;
                    releasing = false;
                }
                else                                // release
                {
                    float relA = 1.0f - relCoef;
                    if (autoRel)
                    {
                        // sustain 0..1: ~0 for a spike on quiet material, ~1 when reduction has been steady.
                        // Judged ONCE, at the moment this release begins, and kept until the next peak re-arms the detector.
                        // (Re-reading it every sample made the release speed up on its own: once the signal had stopped, the
                        // 150 ms average of the reduction collapsed toward 0, so the tail of a sustained passage ended fast.)
                        if (! releasing)
                        {
                            sustainLatch = peakHold > 1.0e-3f ? std::min (1.0f, avgGr / peakHold) : 0.0f;
                            latchDisplay = sustainLatch;
                            releasing = true;
                        }
                        const float rate = relRateFast * fastExp2 (relRateK * sustainLatch);
                        relA = rate * relMul * (1.0f - 0.5f * rate * relMul);   // ~ 1 - exp(-rate)
                    }
                    else if (dynAmount > 0.0f)
                        relA = 1.0f - fastExp2 (relExpK * relMul);
                    peakHold += relA * (grW - peakHold);
                }
                if (peakHold < 1.0e-6f && grW < 1.0e-6f) peakHold = 0.0f;   // kill denormal tail
                env = atkC * env + (1.0f - atkC) * peakHold;
                if (env < 1.0e-6f && peakHold == 0.0f) env = 0.0f;
                env = flushDenormal (env);
                blockGrMax = std::max (blockGrMax, env);
            }

            // --- smoothed makeup / mix, then apply
            if (makeupMoving) makeupMoving = ! approach (makeupLin, targetMakeupLin, smoothCoef, 1.0e-4f * std::max (1.0f, targetMakeupLin));
            if (mixMoving)    mixMoving    = ! approach (mixNow,    params.mix,      smoothCoef, 1.0e-4f);
            const float g = env > 0.0f ? fastExp2 (-env * 0.16609640474f) * makeupLin : makeupLin;   // no reduction -> exactly makeup

            const int rpos = (wpos - D + ringSize) & ringMask;
            for (int c = 0; c < nc; ++c)
            {
                const float dry = ring[c][(size_t) rpos];
                ch[c][i] = mixNow >= 1.0f ? dry * g : dry + mixNow * (dry * g - dry);
            }
            wpos = (wpos + 1) & ringMask;
        }
    }

private:
    Params params;
    double sr = 44100.0;
    int nCh = 2;

    std::vector<float> ring[2];
    SlidingMax window;
    TransientDetector detector;
    float dynAmount = 0.0f, atkExpK = 0.0f, relExpK = 0.0f;
    bool  dynPreserve = true;
    int ringSize = 0, ringMask = 0, wpos = 0, delaySamples = 0, maxLookaheadSamples = 0;
    int holdSamplesMax = 0, holdCounter = 0, rippleHold = 1;
    bool autoRel = false, makeupMoving = false, mixMoving = false, bypassClean = false;
    float rangeLimit = 60.0f;

    // gain computer: current (smoothed) and target values
    float thr = -18.0f, knee = 8.0f, slope = 0.5f, kneeLowLin = 0.0f, hardness = 0.0f;
    float thrT = -18.0f, kneeT = 8.0f, slopeT = 0.5f, hardnessT = 0.0f;
    bool  adaptiveOn = true;
    bool  gcMoving = false;

    float atkCoef = 0.0f, relCoef = 0.0f, smoothCoef = 0.0f, gcCoef = 0.0f, avgCoef = 0.0f;
    float relRateFast = 0.0f, relRateSlow = 0.0f, relRateK = 0.0f;
    float peakHold = 0.0f, env = 0.0f, avgGr = 0.0f, blockGrMax = 0.0f;
    bool  releasing = false;          // auto-release: a release phase is under way (sustainLatch was taken when it began)
    float relFastMs = 0.0f, relSlowMs = 0.0f, latchDisplay = 0.5f;   // auto-release range (ms) and the latest latch, for autoReleaseMs()
    float sustainLatch = 0.0f;        // auto-release: 0 = spike on quiet material (fast release), 1 = steady reduction (slow)
    float makeupLin = 1.0f, targetMakeupLin = 1.0f, mixNow = 1.0f;
};
} // namespace chroma
