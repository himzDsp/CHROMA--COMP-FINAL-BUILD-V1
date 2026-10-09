#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_dsp/juce_dsp.h>       // dsp::Oversampling (add juce_dsp to target_link_libraries / the Projucer modules)
#include <memory>
#include <array>
#include <atomic>
#include <cstring>
#include <vector>
#include "CompressorCore.h"
#include "LoudnessMeter.h"
#include "AdaptiveEngine.h"
#include "PresetManager.h"

// Single-producer / single-consumer sample FIFO used to feed the waveform display.
struct AnalyzerFifo
{
    static constexpr int size = 1 << 14;

    void push (const float* src, int n) noexcept
    {
        int s1, z1, s2, z2;
        fifo.prepareToWrite (n, s1, z1, s2, z2);
        if (z1 > 0) std::memcpy (data.data() + s1, src, (size_t) z1 * sizeof (float));
        if (z2 > 0) std::memcpy (data.data() + s2, src + z1, (size_t) z2 * sizeof (float));
        fifo.finishedWrite (z1 + z2);
    }
    int pull (float* dst, int n) noexcept
    {
        int s1, z1, s2, z2;
        fifo.prepareToRead (n, s1, z1, s2, z2);
        if (z1 > 0) std::memcpy (dst, data.data() + s1, (size_t) z1 * sizeof (float));
        if (z2 > 0) std::memcpy (dst + z1, data.data() + s2, (size_t) z2 * sizeof (float));
        fifo.finishedRead (z1 + z2);
        return z1 + z2;
    }
    int available() const noexcept { return fifo.getNumReady(); }

private:
    juce::AbstractFifo fifo { size };
    std::array<float, (size_t) size> data {};
};

class ChromaCompProcessor final : public juce::AudioProcessor,
                                  private juce::AsyncUpdater
{
public:
    ChromaCompProcessor();
    ~ChromaCompProcessor() override { cancelPendingUpdate(); }

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    void reset() override;
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using juce::AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Chroma Comp"; }
    bool acceptsMidi() const override  { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    juce::AudioProcessorParameter* getBypassParameter() const override { return apvts.getParameter ("bypass"); }

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    juce::AudioProcessorValueTreeState apvts;
    PresetManager presets { apvts };

    // ---- Adaptive analyze (message thread API; audio thread does the listening) ----
    enum AnalysisState { Idle = 0, Waiting = 1, Listening = 2, Deciding = 3 };
    void startAnalysis();
    void cancelAnalysis();
    std::atomic<int>   analysisState { Idle };
    std::atomic<float> analysisSeconds { 0.0f };
    std::atomic<bool>  analysisAborted { false };
    juce::String analysisReport = "Pick a feel and intensity, play your track, then press Adaptive analyze. It listens for at least 5 seconds before it sets the controls.";
    bool analysisOk = false;

    // read by the GUI
    std::atomic<float> autoReleaseMsShown { 0.0f };   // release time auto-release is currently using (ms); the editor shows it on the locked Release knob
    std::atomic<float> grMeterDb { 0.0f };
    std::atomic<int>   analyzerUsers { 0 };    // open editors: the audio thread feeds the analyser / meters only while this is > 0
                                               // (a counter, so a host that opens a new editor before closing the old one cannot switch it off)
    AnalyzerFifo inFifo, outFifo;
    chroma::LoudnessMeter loudIn, loudOut;     // momentary LUFS of the input / output (run only while the editor is open)

private:
    void handleAsyncUpdate() override;
    void finishAnalysis();

    struct Tween final : private juce::Timer
    {
        explicit Tween (juce::AudioProcessorValueTreeState& s) : state (s) {}
        ~Tween() override
        {
            // balance any gestures still open if we are destroyed mid-tween
            if (isTimerRunning())
            {
                stopTimer();
                for (auto* p : params) if (p != nullptr) p->endChangeGesture();
            }
        }
        void start (const float* targets);
    private:
        void timerCallback() override;
        juce::AudioProcessorValueTreeState& state;
        float from[8] {}, to[8] {};
        juce::RangedAudioParameter* params[8] {};
        juce::uint32 t0 = 0;
    } tween { apvts };

    chroma::Listener listener;
    int listenFeel = 0, listenInt = 0, listenTrans = 0;
    void feedAnalyzer (AnalyzerFifo&, const juce::AudioBuffer<float>&, int numCh);

    // ---- selectable oversampling around the compressor core: 1x / 2x / 4x (parameter "oversampling", default 2x) ----
    // The compressor core runs at (host rate x factor); its own time constants, lookahead and detector are all derived
    // from that rate in prepare(). One core + one oversampler per mode are prepared up front, so switching modes while
    // playing is only a pointer swap on the audio thread (no allocation). Mode index m means factor 2^m: 0 = 1x, 1 = 2x, 2 = 4x.
    // Reported latency = oversampling filter latency of the active mode + lookahead, both in host samples.
    static constexpr int  kNumOsModes = 3;
    static constexpr bool kLinearPhaseOversampling = true;            // FIR equiripple (linear phase, more latency) vs polyphase IIR
    std::array<std::unique_ptr<juce::dsp::Oversampling<float>>, (size_t) kNumOsModes> oversamplers;   // [0] stays empty: 1x = no up / down sampling
    std::array<int, (size_t) kNumOsModes> osLatencies {};      // oversampling filter latency per mode, host samples (0 for 1x)
    std::array<chroma::Compressor, (size_t) kNumOsModes> comps;   // one core per mode, prepared at host rate x 2^mode
    int    activeOs = 1;               // mode in use (audio thread only); follows the "oversampling" parameter
    int    osMaxBlock = 512;           // largest block the oversamplers were prepared for (longer host blocks are chunked)
    double hostSampleRate = 0.0;       // set in prepareToPlay; used to round the lookahead time to host samples
    int    lookaheadHost = 0;          // lookahead length, host samples (a whole number, so latency stays an integer);
                                       // re-derived from the "lookaheadTime" parameter every block (audio thread only)
    float  lookaheadQuantMs = chroma::Compressor::lookaheadMs;   // lookaheadHost expressed in ms (-> Params::lookaheadTimeMs)
    void runCompressorOversampled (juce::AudioBuffer<float>&, int numCh);

    chroma::Compressor::Params lastParams;
    bool paramsValid = false;
    std::atomic<int> latencyTarget { 0 };
    std::vector<float> scratch;
    float lastInGain = 1.0f, lastOutGain = 1.0f;   // linear gains used by the previous block (audio thread only; ramped per block)

    std::atomic<float>* pThr = nullptr; std::atomic<float>* pRatio = nullptr; std::atomic<float>* pKnee = nullptr;
    std::atomic<float>* pAtk = nullptr; std::atomic<float>* pRel = nullptr;   std::atomic<float>* pMakeup = nullptr;
    std::atomic<float>* pMix = nullptr; std::atomic<float>* pLook = nullptr;  std::atomic<float>* pBypass = nullptr;
    std::atomic<float>* pFeel = nullptr; std::atomic<float>* pInt = nullptr;  std::atomic<float>* pTrans = nullptr;
    std::atomic<float>* pHold = nullptr;     std::atomic<float>* pRange = nullptr;   // hold ms, range dB
    std::atomic<float>* pLookTime = nullptr; std::atomic<float>* pAuto = nullptr;    // lookahead ms, auto release (bool)
    std::atomic<float>* pAdapt = nullptr;                                            // adapt amount, 0..100 %
    std::atomic<float>* pAdaptiveOn = nullptr;                                       // adaptive engine master switch (bool)
    std::atomic<float>* pInGain = nullptr; std::atomic<float>* pOutGain = nullptr;   // input / output trim, dB
    std::atomic<float>* pOs = nullptr;                                               // oversampling mode index: 0 = 1x, 1 = 2x, 2 = 4x

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChromaCompProcessor)
};
