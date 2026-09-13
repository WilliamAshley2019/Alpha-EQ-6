#pragma once
#include <JuceHeader.h>
#include <atomic>
#include <array>
#include <functional>   // required for std::function — see WaveShaper note below

// =============================================================================
// Parameter IDs
// =============================================================================
namespace Params
{
    inline const juce::String LOW_FREQ   { "LOW_FREQ"   };
    inline const juce::String LOW_GAIN   { "LOW_GAIN"   };
    inline const juce::String LOW_SHELF  { "LOW_SHELF"  };
    inline const juce::String LOW_MUTE   { "LOW_MUTE"   };
    inline const juce::String LOW_BYPASS { "LOW_BYPASS" };
    inline const juce::String LM_FREQ    { "LM_FREQ"    };
    inline const juce::String LM_GAIN    { "LM_GAIN"    };
    inline const juce::String LM_MUTE    { "LM_MUTE"    };
    inline const juce::String LM_BYPASS  { "LM_BYPASS"  };
    inline const juce::String HM_FREQ    { "HM_FREQ"    };
    inline const juce::String HM_GAIN    { "HM_GAIN"    };
    inline const juce::String HM_MUTE    { "HM_MUTE"    };
    inline const juce::String HM_BYPASS  { "HM_BYPASS"  };
    inline const juce::String HIGH_FREQ  { "HIGH_FREQ"  };
    inline const juce::String HIGH_GAIN  { "HIGH_GAIN"  };
    inline const juce::String HIGH_SHELF { "HIGH_SHELF" };
    inline const juce::String HIGH_MUTE  { "HIGH_MUTE"  };
    inline const juce::String HIGH_BYPASS{ "HIGH_BYPASS"};
    inline const juce::String SAT_DRIVE  { "SAT_DRIVE"  };
    inline const juce::String Q_MODE     { "Q_MODE"     };
    // AlphaEQ5 additions
    inline const juce::String OUT_TRIM   { "OUT_TRIM"   }; // ±4 dB post-saturation trim
    inline const juce::String EQ_TOPO    { "EQ_TOPO"    }; // false=API, true=Neve
    inline const juce::String SAT_MODE   { "SAT_MODE"   }; // false=soft (tanh), true=hard clip
    inline const juce::String HQ_MODE    { "HQ_MODE"    }; // false=2x oversample, true=4x
}

// Lookup tables shared between processor and editor
namespace EQTables
{
    constexpr float gainDb[]    = { -12.f, -9.f, -6.f, -3.f, 0.f, 3.f, 6.f, 9.f, 12.f };
    constexpr float lowFreq[]   = { 40.f, 75.f, 150.f, 300.f, 600.f, 1200.f, 2400.f };
    constexpr float hiFreq[]    = { 800.f, 1500.f, 3000.f, 5000.f, 7000.f, 10000.f, 12500.f };
    constexpr float outTrimDb[] = { -4.f, -3.f, -2.f, -1.f, 0.f, 1.f, 2.f, 3.f, 4.f };
}

// =============================================================================
// SpectrumFifo
// Lock-free double-buffer: audio thread pushes samples, GUI pulls 2048-sample
// blocks at ~30 Hz. Missed updates = skipped repaint, never corruption.
// =============================================================================
struct SpectrumFifo
{
    static constexpr int fftOrder = 11;
    static constexpr int fftSize  = 1 << fftOrder; // 2048

    void pushSample(float s) noexcept
    {
        fifo[writeIdx++] = s;
        if (writeIdx >= fftSize)
        {
            writeIdx = 0;
            if (!blockReady.load(std::memory_order_relaxed))
            {
                pendingBlock = fifo;
                blockReady.store(true, std::memory_order_release);
            }
        }
    }

    bool pullBlock(std::array<float, fftSize>& dest) noexcept
    {
        if (!blockReady.load(std::memory_order_acquire))
            return false;
        dest = pendingBlock;
        blockReady.store(false, std::memory_order_release);
        return true;
    }

private:
    std::array<float, fftSize> fifo{};
    std::array<float, fftSize> pendingBlock{};
    int writeIdx = 0;
    std::atomic<bool> blockReady{ false };
};

// =============================================================================
// Api550b inspired AudioProcessor
// =============================================================================
class Api550bAudioProcessor : public juce::AudioProcessor,
                              private juce::AudioProcessorValueTreeState::Listener
{
public:
    Api550bAudioProcessor();
    ~Api550bAudioProcessor() override = default;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "AlphaEQ5"; }
    bool acceptsMidi()  const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int  getNumPrograms()    override { return 1; }
    int  getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    juce::AudioProcessorValueTreeState apvts;

    // -------------------------------------------------------------------------
    // Multirate spectrum analysis ("dual-resolution" analyzer)
    //
    // A single 2048-point FFT at native sample rate has a bin width of
    // ~21.5 Hz @ 44.1 kHz — far too coarse to resolve bass content (a 40 Hz
    // vs 75 Hz API band step lands inside 3-4 bins). Rather than growing the
    // FFT (which costs O(N log N) and adds analysis latency for every part
    // of the spectrum, including the treble, where it isn't needed), the
    // signal is analysed on two independent branches:
    //
    //   spectrumFifo         — native sample rate, 2048-pt FFT (GUI side).
    //                          Good resolution above ~500 Hz, responsive.
    //   lowBandSpectrumFifo  — anti-aliased + decimated by kLowBandDecimation
    //                          (4x), so the *same* 2048-pt FFT run on this
    //                          FIFO's contents effectively has 4x finer bin
    //                          spacing. Good resolution below ~500 Hz.
    //
    // This is "scope narrowing" applied to analysis resolution: spend FFT
    // resolution only where the octave-spaced, log-frequency display and
    // human bass perception actually need it, instead of uniformly
    // oversized main FFT. See lowBandAAFilter / decimationPhase below for
    // the anti-alias + decimation stage that feeds it. The GUI
    // (SpectrumAnalyzerComponent) reads both FIFOs and crossfades between
    // them around 150-500 Hz.
    //
    // This affects only the visual analyzer — the audio signal path (the
    // actual EQ/saturation processing) is completely unaffected.
    // -------------------------------------------------------------------------
    SpectrumFifo spectrumFifo;
    SpectrumFifo lowBandSpectrumFifo;
    static constexpr int kLowBandDecimation = 4;

    // Smoothed EQ gains published to the GUI for the EQ curve overlay.
    // Written on audio thread, read on GUI thread via relaxed atomics.
    std::atomic<float> currentLowGainDb  { 0.0f };
    std::atomic<float> currentLmGainDb   { 0.0f };
    std::atomic<float> currentHmGainDb   { 0.0f };
    std::atomic<float> currentHighGainDb { 0.0f };

private:
    // -------------------------------------------------------------------------
    // ProcessorDuplicator: applies a mono IIR::Filter to each channel of a
    // stereo AudioBlock independently, sharing the same coefficient object.
    // -------------------------------------------------------------------------
    using StereoFilter = juce::dsp::ProcessorDuplicator<
        juce::dsp::IIR::Filter<float>,
        juce::dsp::IIR::Coefficients<float>>;

    StereoFilter lowFilter, lowMidFilter, highMidFilter, highFilter;

    // -------------------------------------------------------------------------
    // Two oversamplers — only one is active per block.
    //
    //   oversampler2x:  2× (factor=2^1). Default. Lower CPU.
    //   oversampler4x:  4× (factor=2^2). HQ mode. Better alias suppression
    //                   above ~10 kHz at high drive; ~2× the CPU of 2× mode
    //                   for the saturation stage only.
    //
    // Both are prepared in prepareToPlay. The active one is selected in
    // processBlock by reading the HQ_MODE parameter.
    // -------------------------------------------------------------------------
    juce::dsp::Oversampling<float> oversampler2x {
        2u, 1u,
        juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR,
        false };
    juce::dsp::Oversampling<float> oversampler4x {
        2u, 2u,
        juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR,
        false };

    bool hqModeActive = false; // audio thread only — tracks which oversampler is live

    // -------------------------------------------------------------------------
    // Anti-alias filter feeding the decimated low-band analyzer FIFO.
    // A simple one-pole-per-stage IIR lowpass (2nd order via IIR::Filter) set
    // around Fs/(2*kLowBandDecimation) so decimating by kLowBandDecimation
    // afterwards does not alias energy from above the new Nyquist back into
    // the low-band FFT. Audio-thread only; entirely separate from the EQ's
    // own filters — it never touches the actual signal path.
    // -------------------------------------------------------------------------
    juce::dsp::IIR::Filter<float> lowBandAAFilter;
    int decimationPhase = 0; // audio thread only

    // -------------------------------------------------------------------------
    // WaveShaper — THE LAMBDA FIX
    //
    // juce::dsp::WaveShaper<SampleType> has a second template parameter
    // `Function` that defaults to `SampleType(*)(SampleType)` — a raw function
    // pointer.  A capturing lambda is NOT implicitly convertible to a raw
    // function pointer (the standard only allows the conversion for *non-
    // capturing* lambdas).  The compiler error in AlphaEQ4 was:
    //
    //   "no suitable conversion from lambda [this](float)->float to
    //    float (*)(float)"
    //
    // The fix is to explicitly provide std::function<float(float)> as the
    // second template argument.  std::function is an *erased* callable wrapper
    // that accepts any callable — including capturing lambdas — at the cost of
    // one heap allocation per assignment and an indirect call per sample.
    // Both costs are negligible here: assignment happens once in prepareToPlay,
    // and the indirect call is dwarfed by the tanh/pow themselves.
    //
    // Also fixed: satDriveValue is now an instance member (not static), so
    // two instances in the same DAW process have independent drive values.
    // The lambda captures 'this' and reads it directly.
    // -------------------------------------------------------------------------
    juce::dsp::WaveShaper<float, std::function<float(float)>> saturation;

    float satDriveValue = 2.0f; // audio thread only
    bool  satModeHard   = false; // audio thread only; synced from parameterChanged

    // -------------------------------------------------------------------------
    // Parameter smoothing — see thread-safety note in AlphaEQ4 comments.
    // -------------------------------------------------------------------------
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothedLowGain;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothedLmGain;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothedHmGain;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothedHighGain;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothedSatDrive;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothedOutTrim;

    float getEffectiveGainDb(const juce::String& gainParam,
                             const juce::String& muteParam) const;

    // -------------------------------------------------------------------------
    // Silence fast-path — "temporal" scope narrowing on the audio path.
    //
    // This is deliberately NOT frequency-domain scope narrowing. The EQ
    // biquads and saturation stage are a linear/nonlinear time-domain chain;
    // selectively skipping computation for a frequency *region* would require
    // either accepting audible discontinuities or a full FFT-domain rebuild
    // of the effect, neither of which is worth it for a 4-band EQ. Skipping
    // computation on a block that is provably digital silence is safe by
    // contrast: a stable, already-settled linear filter fed with zero input
    // produces zero output, so nothing about the audio result changes.
    //
    // silentBlockCounter counts consecutive input blocks whose peak sample
    // magnitude (across all channels) is below kSilenceThreshold. Once it
    // reaches kSilenceHoldBlocks, prior filter/oversampler state has had time
    // to decay to (numerically) zero, and processBlock takes the fast path:
    // EQ, saturation/oversampling, and output trim are skipped entirely for
    // that block (the buffer is already silent, so the result is identical).
    // The counter resets the instant real signal reappears, so there is no
    // audible risk — only CPU saved during gaps, pauses, and song sections
    // with silent tracks.
    // -------------------------------------------------------------------------
    int silentBlockCounter = 0; // audio thread only
    static constexpr int   kSilenceHoldBlocks = 4;
    static constexpr float kSilenceThreshold  = 1.0e-7f; // ~ -140 dBFS

    std::atomic<bool> parametersChanged{ true };
    void parameterChanged(const juce::String& paramID, float newValue) override;

    // updateFilters uses double-precision arithmetic for coefficient computation.
    // See implementation notes in PluginProcessor.cpp.
    void updateFilters(float lowGainDb, float lmGainDb,
                       float hmGainDb,  float highGainDb);

    // Compute IIR coefficients in double, then downcast and write into a float
    // StereoFilter's shared coefficient object.
    using DCoeffsPtr = juce::ReferenceCountedObjectPtr<juce::dsp::IIR::Coefficients<double>>;
    static void applyDoublePrecisionCoeffs(StereoFilter& filter, DCoeffsPtr dc);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Api550bAudioProcessor)
};
