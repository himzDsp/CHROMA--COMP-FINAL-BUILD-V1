// Standalone regression tests for the Chroma Comp DSP core (no JUCE needed).
//   cmake -B build-tests -DCHROMA_BUILD_TESTS=ON && cmake --build build-tests --target ChromaCompCoreTests
//   ./build-tests/ChromaCompCoreTests          (exit code 0 = all passed)
// Or without CMake:  g++ -O2 -std=c++17 -ISource Tests/CoreTests.cpp -o coretests && ./coretests
#include "CompressorCore.h"
#include <cstdio>
#include <random>
#include <vector>

using chroma::Compressor;
static int failures = 0;
static void check (bool ok, const char* what, double value = 0.0)
{
    std::printf ("%s  %-62s %g\n", ok ? "PASS" : "FAIL", what, value);
    if (! ok) ++failures;
}
static const double kSr = 48000.0;
static const double kTwoPi = 6.283185307179586;

static void run (Compressor& c, std::vector<float>& l, std::vector<float>& r)
{
    const int n = (int) l.size(), B = 480;
    for (int pos = 0; pos < n; pos += B)
    {
        float* ch[2] = { l.data() + pos, r.data() + pos };
        c.process (ch, 2, std::min (B, n - pos));
    }
}

// non-fundamental energy of a steady sine, relative to the fundamental (dB), measured over the second half
static double residualDb (const std::vector<float>& x, double f)
{
    const int n0 = (int) x.size() / 2, n1 = (int) x.size();
    double re = 0, im = 0;
    for (int i = n0; i < n1; ++i) { const double ph = kTwoPi * f * i / kSr; re += x[(size_t) i] * std::cos (ph); im += x[(size_t) i] * std::sin (ph); }
    re *= 2.0 / (n1 - n0);  im *= 2.0 / (n1 - n0);
    double e = 0;
    for (int i = n0; i < n1; ++i) { const double ph = kTwoPi * f * i / kSr; const double y = x[(size_t) i] - (re * std::cos (ph) + im * std::sin (ph)); e += y * y; }
    return 20.0 * std::log10 (std::sqrt (e / (n1 - n0)) * 1.41421356 / std::sqrt (re * re + im * im) + 1e-12);
}

static std::vector<float> sine (double f, float amp, int n)
{
    std::vector<float> v ((size_t) n);
    for (int i = 0; i < n; ++i) v[(size_t) i] = amp * (float) std::sin (kTwoPi * f * i / kSr);
    return v;
}

int main()
{
    std::mt19937 rng (1);  std::uniform_real_distribution<float> u (-1.0f, 1.0f);

    // 1. Init settings (threshold 0 dB, ratio 1:1, no makeup, mix 100 %): output must be bit-identical, from the first sample
    {
        Compressor c;  Compressor::Params p;  p.thresholdDb = 0;  p.ratio = 1;  p.kneeDb = 0;  p.adaptiveEnabled = false;
        c.prepare (kSr, 2);  c.setParams (p, true);
        std::vector<float> l ((size_t) kSr * 2), r;  for (auto& s : l) s = 0.9f * u (rng);  r = l;
        const auto in = l;  run (c, l, r);
        double maxErr = 0;  for (size_t i = 0; i < l.size(); ++i) maxErr = std::max (maxErr, (double) std::fabs (l[i] - in[i]));
        check (maxErr == 0.0, "Init settings are bit-exact (unity, no start-up ramp)", maxErr);
    }
    // 2. Bypass (mix 0): dry signal, exactly, also delay-matched with lookahead
    for (bool look : { false, true })
    {
        Compressor c;  Compressor::Params p;  p.thresholdDb = -30;  p.ratio = 8;  p.mix = 0.0f;  p.lookahead = look;  p.lookaheadTimeMs = 2.0f;
        c.prepare (kSr, 2);  c.setParams (p, true);
        std::vector<float> l ((size_t) kSr), r;  for (auto& s : l) s = 0.8f * u (rng);  r = l;
        const auto in = l;  run (c, l, r);
        const int D = c.latencySamples();  double maxErr = 0;
        for (size_t i = (size_t) D; i < l.size(); ++i) maxErr = std::max (maxErr, (double) std::fabs (l[i] - in[i - (size_t) D]));
        check (maxErr == 0.0, look ? "Bypass is the exact dry signal, delayed by the lookahead" : "Bypass is the exact dry signal", maxErr);
    }
    // 3. Low-frequency distortion with the fastest settings (the detector must not follow the waveform)
    for (double f : { 50.0, 60.0, 100.0, 1000.0 })
    {
        Compressor c;  Compressor::Params p;  p.thresholdDb = -20;  p.ratio = 3;  p.kneeDb = 6;  p.attackMs = 0.1f;  p.releaseMs = 5;  p.adaptiveEnabled = false;
        c.prepare (kSr, 2);  c.setParams (p, true);
        auto l = sine (f, 0.5f, (int) kSr * 3);  auto r = l;  run (c, l, r);
        char name[96];  std::snprintf (name, sizeof name, "%4.0f Hz, attack 0.1 ms / release 5 ms: distortion below -100 dB", f);
        const double d = residualDb (l, f);  check (d < -100.0, name, d);
    }
    // 4. Static gain reduction matches the textbook curve: 1 kHz at -6.02 dBFS, threshold -20, 4:1, hard knee -> 10.49 dB
    {
        Compressor c;  Compressor::Params p;  p.thresholdDb = -20;  p.ratio = 4;  p.kneeDb = 0;  p.attackMs = 1;  p.releaseMs = 50;  p.adaptiveEnabled = false;
        c.prepare (kSr, 2);  c.setParams (p, true);
        auto l = sine (1000.0, 0.5f, (int) kSr * 2);  auto r = l;  const auto in = l;  run (c, l, r);
        double eo = 0, ei = 0;  for (size_t i = l.size() / 2; i < l.size(); ++i) { eo += l[i] * l[i];  ei += in[i] * in[i]; }
        const double gr = -10.0 * std::log10 (eo / ei);  check (std::fabs (gr - 10.49) < 0.1, "Gain reduction matches threshold / ratio (10.49 dB expected)", gr);
    }
    // 5. Auto-release reports the release time it uses: the knob value before any release, near Release x4 after steady
    //    compression, near Release / 4 after a short spike (this is what the locked Release knob displays)
    {
        auto autoMs = [&] (int toneMs, bool& beforeCentre)
        {
            Compressor c;  Compressor::Params p;  p.thresholdDb = -20;  p.ratio = 4;  p.kneeDb = 0;  p.attackMs = 1;  p.releaseMs = 100;
            p.autoRelease = true;  p.adaptiveEnabled = false;  c.prepare (kSr, 2);  c.setParams (p, true);
            beforeCentre = std::fabs (c.autoReleaseMs() - 100.0f) < 1.0f;
            const int tone = (int) (kSr * toneMs / 1000.0), total = tone + (int) kSr / 2;
            std::vector<float> l ((size_t) total, 0.0f), r;
            for (int i = 0; i < tone; ++i) l[(size_t) i] = 0.5f * (float) std::sin (kTwoPi * 1000.0 * i / kSr);
            r = l;  run (c, l, r);
            return (double) c.autoReleaseMs();
        };
        bool centre = false, dummy = false;
        const double steady = autoMs (3000, centre), spike = autoMs (15, dummy);
        check (centre, "Auto-release shows the knob value (centre of the range) before any release", 100.0);
        check (steady > 300.0 && steady <= 400.5, "Steady compression -> slow end of the range (about 400 ms)", steady);
        check (spike  >= 24.5 && spike  < 80.0,  "Short spike -> fast end of the range (about 25-60 ms)", spike);
    }
    // 6. Robustness: random parameters at six sample rates, mono and stereo, never produces NaN / inf
    {
        long long bad = 0;
        auto U = [&] (float a, float b) { return std::uniform_real_distribution<float> (a, b) (rng); };
        for (double sr : { 22050.0, 44100.0, 48000.0, 96000.0, 192000.0 })
            for (int chN : { 1, 2 })
            {
                Compressor c;  c.prepare (sr, chN);  Compressor::Params p;  c.setParams (p, true);
                for (int b = 0; b < 150; ++b)
                {
                    p.thresholdDb = U (-60, 0);  p.ratio = U (1, 20);  p.kneeDb = U (0, 30);  p.attackMs = U (0.1f, 200);  p.releaseMs = U (5, 1000);
                    p.makeupDb = U (0, 24);  p.mix = (rng() % 5 == 0) ? 0.f : U (0, 1);  p.lookahead = rng() % 2;  p.lookaheadTimeMs = U (0, 10);
                    p.holdMs = U (0, 500);  p.rangeDb = U (0, 120);  p.autoRelease = rng() % 2;  p.dynamicsAmount = U (0, 1);  p.adaptiveEnabled = rng() % 4 != 0;
                    c.setParams (p, rng() % 40 == 0);
                    const int n = 1 + (int) (rng() % 1500);  std::vector<float> a ((size_t) n), d ((size_t) n);
                    for (int i = 0; i < n; ++i) { a[(size_t) i] = U (-2, 2);  d[(size_t) i] = a[(size_t) i]; }
                    float* ch[2] = { a.data(), d.data() };  c.process (ch, chN, n);
                    for (int i = 0; i < n; ++i) if (! std::isfinite (a[(size_t) i]) || ! std::isfinite (d[(size_t) i])) ++bad;
                }
            }
        check (bad == 0, "Random parameters / sample rates: no NaN or inf", (double) bad);
    }

    std::printf (failures == 0 ? "\nAll tests passed.\n" : "\n%d test(s) FAILED.\n", failures);
    return failures == 0 ? 0 : 1;
}
