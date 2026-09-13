#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cmath>

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new Api550bAudioProcessor();
}

// =============================================================================
// Constructor
// =============================================================================
Api550bAudioProcessor::Api550bAudioProcessor()
    : AudioProcessor(BusesProperties()
          .withInput ("Input",  juce::AudioChannelSet::stereo())
          .withOutput("Output", juce::AudioChannelSet::stereo())),
      apvts(*this, nullptr, "Parameters", createParameterLayout())
{
    for (auto& id : { Params::LOW_FREQ,   Params::LOW_GAIN,   Params::LOW_SHELF,
                      Params::LOW_MUTE,   Params::LOW_BYPASS,
                      Params::LM_FREQ,    Params::LM_GAIN,
                      Params::LM_MUTE,    Params::LM_BYPASS,
                      Params::HM_FREQ,    Params::HM_GAIN,
                      Params::HM_MUTE,    Params::HM_BYPASS,
                      Params::HIGH_FREQ,  Params::HIGH_GAIN,  Params::HIGH_SHELF,
                      Params::HIGH_MUTE,  Params::HIGH_BYPASS,
                      Params::SAT_DRIVE,  Params::Q_MODE,
                      Params::OUT_TRIM,   Params::EQ_TOPO,
                      Params::SAT_MODE,   Params::HQ_MODE })
        apvts.addParameterListener(id, this);
}

// =============================================================================
// Parameter layout
// =============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout
Api550bAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    juce::StringArray lowFreqChoices { "40", "75", "150", "300", "600", "1.2k", "2.4k"  };
    juce::StringArray highFreqChoices{ "800", "1.5k", "3k", "5k", "7k", "10k", "12.5k" };
    juce::StringArray gainChoices    { "-12", "-9", "-6", "-3", "0", "3", "6", "9", "12"};
    juce::StringArray trimChoices    { "-4", "-3", "-2", "-1", "0", "+1", "+2", "+3", "+4" };

    auto addBand = [&](const juce::String& prefix, const juce::String& name,
                       const juce::StringArray& freqs, int defaultFreqIdx)
    {
        params.push_back(std::make_unique<juce::AudioParameterChoice>(
            prefix + "_FREQ",   name + " Freq",   freqs,      defaultFreqIdx));
        params.push_back(std::make_unique<juce::AudioParameterChoice>(
            prefix + "_GAIN",   name + " Gain",   gainChoices, 4)); // 0 dB
        params.push_back(std::make_unique<juce::AudioParameterBool>(
            prefix + "_MUTE",   name + " Mute",   false));
        params.push_back(std::make_unique<juce::AudioParameterBool>(
            prefix + "_BYPASS", name + " Bypass", false));
    };

    addBand("LOW",  "Low",      lowFreqChoices,  3);
    params.push_back(std::make_unique<juce::AudioParameterBool>(Params::LOW_SHELF,  "Low Shelf",  false));
    addBand("LM",   "Low Mid",  lowFreqChoices,  4);
    addBand("HM",   "High Mid", highFreqChoices, 2);
    addBand("HIGH", "High",     highFreqChoices, 3);
    params.push_back(std::make_unique<juce::AudioParameterBool>(Params::HIGH_SHELF, "High Shelf", false));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        Params::SAT_DRIVE, "Saturation Drive",
        juce::NormalisableRange<float>(0.0f, 10.0f, 0.01f), 2.0f));

    params.push_back(std::make_unique<juce::AudioParameterBool>(
        Params::Q_MODE, "Proportional Q", false));

    // AlphaEQ5
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        Params::OUT_TRIM, "Output Trim", trimChoices, 4)); // default 0 dB

    params.push_back(std::make_unique<juce::AudioParameterBool>(
        Params::EQ_TOPO, "EQ Topology (Neve Mode)", false));

    // SAT_MODE: false=soft tanh+asymmetry, true=hard asymmetric clip
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        Params::SAT_MODE, "Saturation Mode (Hard Clip)", false));

    // HQ_MODE: false=2x oversampling, true=4x oversampling
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        Params::HQ_MODE, "HQ Mode (4x Oversample)", false));

    return { params.begin(), params.end() };
}

// =============================================================================
// Helper
// =============================================================================
float Api550bAudioProcessor::getEffectiveGainDb(const juce::String& gainParam,
                                                  const juce::String& muteParam) const
{
    if (apvts.getRawParameterValue(muteParam)->load() > 0.5f)
        return 0.0f;
    int idx = juce::jlimit(0, (int)std::size(EQTables::gainDb) - 1,
                           (int)apvts.getRawParameterValue(gainParam)->load());
    return EQTables::gainDb[idx];
}

// =============================================================================
// applyDoublePrecisionCoeffs
//
// Computes IIR biquad coefficients using juce::dsp::IIR::Coefficients<double>
// (double-precision arithmetic throughout) and then downcasts each coefficient
// to float before writing into the StereoFilter's shared coefficient object.
//
// Why this matters:
//   A biquad peak filter at 40 Hz / 44.1 kHz has w0 = 2π × 40/44100 ≈ 0.005699.
//   The coefficient a1 = –2 × cos(w0) × (1/(1+α)) involves quantities very
//   close to 1.0.  In float (7 significant decimal digits), rounding shifts the
//   actual –3 dB frequency by several Hz.  In double (15 digits), the error is
//   sub-millihertz.  The downcast to float at the end preserves the accuracy of
//   the computation while keeping the audio sample pipeline in float.
//
// FUTURE — TPT filters:
//   A topology-preserving transform (state-variable) filter computes cutoff as
//   g = tan(π × fc/fs), which does not suffer the near-cancellation problem of
//   the direct-form biquad.  TPT also allows exact per-sample modulation of
//   cutoff and resonance without coefficient zipper noise.  This is a full
//   architecture replacement (custom filter class, remove ProcessorDuplicator)
//   and is tracked as a future session item.
// =============================================================================
void Api550bAudioProcessor::applyDoublePrecisionCoeffs(StereoFilter& filter,
                                                        DCoeffsPtr dc)
{
    if (dc == nullptr) return;
    const auto& dArr = dc->coefficients; // juce::Array<double>
    auto& fArr = filter.state->coefficients; // juce::Array<float>
    fArr.clearQuick();
    for (auto v : dArr)
        fArr.add(static_cast<float>(v));
}

// =============================================================================
// prepareToPlay
// =============================================================================
void Api550bAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    juce::dsp::ProcessSpec nativeSpec{
        sampleRate,
        static_cast<juce::uint32>(samplesPerBlock),
        static_cast<juce::uint32>(getTotalNumInputChannels()) };

    lowFilter    .prepare(nativeSpec);
    lowMidFilter .prepare(nativeSpec);
    highMidFilter.prepare(nativeSpec);
    highFilter   .prepare(nativeSpec);

    // Prepare both oversamplers — only one will be used per block at runtime.
    oversampler2x.reset();
    oversampler2x.initProcessing(static_cast<size_t>(samplesPerBlock));

    oversampler4x.reset();
    oversampler4x.initProcessing(static_cast<size_t>(samplesPerBlock));

    // Sync hqModeActive so we don't trigger a spurious reset on the first block.
    hqModeActive = apvts.getRawParameterValue(Params::HQ_MODE)->load() > 0.5f;

    // -------------------------------------------------------------------------
    // Low-band analyzer anti-alias filter + decimation reset.
    // Cutoff sits just under the post-decimation Nyquist (sr / (2 * factor)),
    // leaving headroom for the filter's own rolloff skirt so nothing above
    // the new Nyquist folds back into the decimated low-band FFT.
    // -------------------------------------------------------------------------
    {
        juce::dsp::ProcessSpec monoSpec{ sampleRate, static_cast<juce::uint32>(samplesPerBlock), 1u };
        lowBandAAFilter.prepare(monoSpec);
        const double aaCutoff = juce::jmin(sampleRate * 0.5 / (double)kLowBandDecimation * 0.9, 18000.0);
        lowBandAAFilter.coefficients = juce::dsp::IIR::Coefficients<float>::makeLowPass(
            sampleRate, static_cast<float>(aaCutoff), 0.707f);
        lowBandAAFilter.reset();
        decimationPhase = 0;
    }

    silentBlockCounter = 0;

    // -------------------------------------------------------------------------
    // Waveshaper — prepare at the highest possible oversampled rate so the
    // same WaveShaper works for both 2× and 4× without re-preparation.
    // -------------------------------------------------------------------------
    juce::dsp::ProcessSpec maxOsSpec{
        sampleRate * 4.0,
        static_cast<juce::uint32>(samplesPerBlock * 4),
        static_cast<juce::uint32>(getTotalNumInputChannels()) };
    saturation.prepare(maxOsSpec);

    // -------------------------------------------------------------------------
    // Asymmetric transformer saturation — assign the capturing lambda.
    //
    // The std::function<float(float)> template argument allows this lambda to
    // capture 'this', giving access to satDriveValue and satModeHard without
    // any static state.
    //
    // SOFT mode (satModeHard = false):
    //   f(x) = tanh(drive×x + k×x²) / drive
    //
    //   The x² term is an even function: it breaks the odd symmetry of tanh and
    //   injects 2nd-order harmonics (and smaller 4th, 6th, …).  Real output
    //   transformers are built around ferromagnetic cores whose B-H hysteresis
    //   curve is asymmetric, producing strong 2nd harmonic — the "warm" quality
    //   associated with vintage console hardware.
    //
    //   k = 0.15 was chosen empirically: enough 2nd harmonic to be audible at
    //   moderate drive, not so much that it shifts the DC operating point
    //   significantly.  Raise k toward ~0.3 for more pronounced warmth; lower
    //   toward 0 to approach symmetric tanh.
    //
    // HARD mode (satModeHard = true):
    //   f(x) = clamp(drive×x + k×x², −1.0, +0.93) / drive
    //
    //   Hard-clips at a fixed boundary instead of the asymptotic tanh curve.
    //   The asymmetric limits (−1.0 vs +0.93) preserve even-harmonic injection
    //   from the k×x² term AND add an additional level of asymmetry from the
    //   different positive/negative headroom.  This produces a more aggressive,
    //   "punchy" character — closer to iron-core transformer saturation at high
    //   signal levels than to thermionic soft-clipping.
    // -------------------------------------------------------------------------
    satModeHard = apvts.getRawParameterValue(Params::SAT_MODE)->load() > 0.5f;

    saturation.functionToUse = [this](float x) -> float
    {
        constexpr float k = 0.15f;

        if (satDriveValue <= 0.001f)
            return x;

        const float driven = satDriveValue * x + k * x * x;

        if (satModeHard)
        {
            // Hard asymmetric clip:
            // Positive half clips at 0.93 (slightly lower than −1.0 negative).
            // The asymmetry in the clip thresholds reinforces even harmonics.
            return juce::jlimit(-1.0f, 0.93f, driven) / satDriveValue;
        }
        else
        {
            // Soft asymmetric clip via tanh:
            return std::tanh(driven) / satDriveValue;
        }
    };

    // -------------------------------------------------------------------------
    // Initialise smoothers from current parameter state.
    // -------------------------------------------------------------------------
    constexpr double rampSecs = 0.10;

    auto initSmoother = [&](juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear>& sv,
                             float initVal)
    {
        sv.reset(sampleRate, rampSecs);
        sv.setCurrentAndTargetValue(initVal);
    };

    initSmoother(smoothedLowGain,  getEffectiveGainDb(Params::LOW_GAIN,  Params::LOW_MUTE));
    initSmoother(smoothedLmGain,   getEffectiveGainDb(Params::LM_GAIN,   Params::LM_MUTE));
    initSmoother(smoothedHmGain,   getEffectiveGainDb(Params::HM_GAIN,   Params::HM_MUTE));
    initSmoother(smoothedHighGain, getEffectiveGainDb(Params::HIGH_GAIN, Params::HIGH_MUTE));
    initSmoother(smoothedSatDrive, apvts.getRawParameterValue(Params::SAT_DRIVE)->load());

    {
        int tidx = juce::jlimit(0, (int)std::size(EQTables::outTrimDb) - 1,
                                (int)apvts.getRawParameterValue(Params::OUT_TRIM)->load());
        initSmoother(smoothedOutTrim, EQTables::outTrimDb[tidx]);
    }

    parametersChanged = true;
}

// =============================================================================
// releaseResources
// =============================================================================
void Api550bAudioProcessor::releaseResources()
{
    oversampler2x.reset();
    oversampler4x.reset();
}

// =============================================================================
// parameterChanged  (message thread)
// =============================================================================
void Api550bAudioProcessor::parameterChanged(const juce::String& paramID, float)
{
    parametersChanged = true;

    if      (paramID == Params::LOW_GAIN  || paramID == Params::LOW_MUTE)
        smoothedLowGain .setTargetValue(getEffectiveGainDb(Params::LOW_GAIN,  Params::LOW_MUTE));
    else if (paramID == Params::LM_GAIN   || paramID == Params::LM_MUTE)
        smoothedLmGain  .setTargetValue(getEffectiveGainDb(Params::LM_GAIN,   Params::LM_MUTE));
    else if (paramID == Params::HM_GAIN   || paramID == Params::HM_MUTE)
        smoothedHmGain  .setTargetValue(getEffectiveGainDb(Params::HM_GAIN,   Params::HM_MUTE));
    else if (paramID == Params::HIGH_GAIN || paramID == Params::HIGH_MUTE)
        smoothedHighGain.setTargetValue(getEffectiveGainDb(Params::HIGH_GAIN, Params::HIGH_MUTE));
    else if (paramID == Params::SAT_DRIVE)
        smoothedSatDrive.setTargetValue(apvts.getRawParameterValue(Params::SAT_DRIVE)->load());
    else if (paramID == Params::OUT_TRIM)
    {
        int idx = juce::jlimit(0, (int)std::size(EQTables::outTrimDb) - 1,
                               (int)apvts.getRawParameterValue(Params::OUT_TRIM)->load());
        smoothedOutTrim.setTargetValue(EQTables::outTrimDb[idx]);
    }
    // SAT_MODE is read directly in processBlock via the lambda — no smoother needed.
    // HQ_MODE transition is handled in processBlock (see hqModeActive check).
}

// =============================================================================
// updateFilters
//
// All coefficient math is performed in double precision via Coefficients<double>,
// then downcast to float via applyDoublePrecisionCoeffs().  See the header and
// applyDoublePrecisionCoeffs() comments for the rationale.
//
// EQ Topology (EQ_TOPO):
//
//   API mode (default):
//     Shelves: Q = 0.7 — Butterworth, maximally flat, no resonance at the knee.
//     Peaks:   fixed Q = 1.5, or proportional 0.7–2.2 when PROP_Q is on.
//
//   Neve mode:
//     Shelves: Q = 1.1 — slight resonant bump at the shelf knee, approximating
//              the faster transition of the Neve 1073's passive LC topology.
//     Peaks:   fixed Q = 2.5 (narrower bands), or proportional 1.0–2.8.
//
// Stable Q at high gain:
//   Proportional Q is clamped via jlimit before being passed to the coefficient
//   formulas.  At +12/–12 dB the formula gives Q ≈ 0.7+0.2×12 = 3.1; the clamp
//   prevents this from exceeding 2.8 (Neve) / 2.2 (API), which would create
//   excessive resonance artefacts or near-instability in the biquad.
// =============================================================================
void Api550bAudioProcessor::updateFilters(float lowGainDb, float lmGainDb,
                                           float hmGainDb,  float highGainDb)
{
    const double sr = getSampleRate();
    if (sr <= 0.0) return;

    const bool propQ    = apvts.getRawParameterValue(Params::Q_MODE)->load()  > 0.5f;
    const bool neveMode = apvts.getRawParameterValue(Params::EQ_TOPO)->load() > 0.5f;

    const float shelfQ     = neveMode ? 1.1f  : 0.7f;
    const float fixedPeakQ = neveMode ? 2.5f  : 1.5f;
    const float propQMin   = neveMode ? 1.0f  : 0.7f;
    const float propQMax   = neveMode ? 2.8f  : 2.2f;

    auto makePeakQ = [&](float gainDb) -> double {
        return propQ
            ? (double)juce::jlimit(propQMin, propQMax, 1.0f + 0.2f * std::abs(gainDb))
            : (double)fixedPeakQ;
    };

    auto getFreq = [&](const juce::String& param, const float* table, size_t n) -> double {
        size_t idx = juce::jlimit<size_t>(0, n - 1,
            static_cast<size_t>(apvts.getRawParameterValue(param)->load()));
        return (double)table[idx];
    };

    using DCoeffs = juce::dsp::IIR::Coefficients<double>;

    // --- Low band ---
    {
        double freq  = getFreq(Params::LOW_FREQ, EQTables::lowFreq, std::size(EQTables::lowFreq));
        double gain  = (double)juce::Decibels::decibelsToGain(lowGainDb);
        bool   shelf = apvts.getRawParameterValue(Params::LOW_SHELF)->load() > 0.5f;
        double q     = shelf ? (double)shelfQ : makePeakQ(lowGainDb);

        applyDoublePrecisionCoeffs(lowFilter,
            shelf ? DCoeffs::makeLowShelf  (sr, freq, q, gain)
                  : DCoeffs::makePeakFilter(sr, freq, q, gain));
    }

    // --- Low-Mid band ---
    {
        double freq = getFreq(Params::LM_FREQ, EQTables::lowFreq, std::size(EQTables::lowFreq));
        double q    = makePeakQ(lmGainDb);
        double gain = (double)juce::Decibels::decibelsToGain(lmGainDb);

        applyDoublePrecisionCoeffs(lowMidFilter,
            DCoeffs::makePeakFilter(sr, freq, q, gain));
    }

    // --- High-Mid band ---
    {
        double freq = getFreq(Params::HM_FREQ, EQTables::hiFreq, std::size(EQTables::hiFreq));
        double q    = makePeakQ(hmGainDb);
        double gain = (double)juce::Decibels::decibelsToGain(hmGainDb);

        applyDoublePrecisionCoeffs(highMidFilter,
            DCoeffs::makePeakFilter(sr, freq, q, gain));
    }

    // --- High band ---
    {
        double freq  = getFreq(Params::HIGH_FREQ, EQTables::hiFreq, std::size(EQTables::hiFreq));
        double gain  = (double)juce::Decibels::decibelsToGain(highGainDb);
        bool   shelf = apvts.getRawParameterValue(Params::HIGH_SHELF)->load() > 0.5f;
        double q     = shelf ? (double)shelfQ : makePeakQ(highGainDb);

        // 1.3× freq offset matches the original API 550B high-shelf knee position.
        applyDoublePrecisionCoeffs(highFilter,
            shelf ? DCoeffs::makeHighShelf (sr, freq * 1.3, q, gain)
                  : DCoeffs::makePeakFilter(sr, freq,       q, gain));
    }
}

// =============================================================================
// processBlock
// =============================================================================
void Api550bAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer,
                                          juce::MidiBuffer& /*midiMessages*/)
{
    juce::ScopedNoDenormals noDenormals;

    if (buffer.getNumChannels() == 0
        || getTotalNumInputChannels() != getTotalNumOutputChannels())
        return;

    const int numSamples = buffer.getNumSamples();

    // ------------------------------------------------------------------
    // 1. Advance smoothers.
    // ------------------------------------------------------------------
    smoothedLowGain .skip(numSamples);
    smoothedLmGain  .skip(numSamples);
    smoothedHmGain  .skip(numSamples);
    smoothedHighGain.skip(numSamples);
    smoothedSatDrive.skip(numSamples);
    smoothedOutTrim .skip(numSamples);

    // Publish smoothed gains for EQ curve overlay (GUI thread reads these).
    currentLowGainDb .store(smoothedLowGain .getCurrentValue(), std::memory_order_relaxed);
    currentLmGainDb  .store(smoothedLmGain  .getCurrentValue(), std::memory_order_relaxed);
    currentHmGainDb  .store(smoothedHmGain  .getCurrentValue(), std::memory_order_relaxed);
    currentHighGainDb.store(smoothedHighGain.getCurrentValue(), std::memory_order_relaxed);

    satDriveValue = smoothedSatDrive.getCurrentValue();

    // SAT_MODE: read directly — no smoother, the lambda reads satModeHard.
    satModeHard = apvts.getRawParameterValue(Params::SAT_MODE)->load() > 0.5f;

    // ------------------------------------------------------------------
    // 2. HQ mode switch.
    //
    // If the user toggled HQ_MODE since the last block, reset the newly
    // active oversampler's delay lines to avoid a transient from stale
    // internal state.  A brief artefact on the transition is acceptable
    // (this is a CPU-mode switch, not a real-time audio parameter).
    // ------------------------------------------------------------------
    const bool hqWanted = apvts.getRawParameterValue(Params::HQ_MODE)->load() > 0.5f;
    if (hqWanted != hqModeActive)
    {
        hqModeActive = hqWanted;
        if (hqModeActive) oversampler4x.reset();
        else              oversampler2x.reset();
    }
    auto& activeOS = hqModeActive ? oversampler4x : oversampler2x;

    // ------------------------------------------------------------------
    // 3. Recalculate EQ coefficients if needed.
    // ------------------------------------------------------------------
    const bool needsUpdate = parametersChanged.exchange(false)
                           || smoothedLowGain .isSmoothing()
                           || smoothedLmGain  .isSmoothing()
                           || smoothedHmGain  .isSmoothing()
                           || smoothedHighGain.isSmoothing();

    if (needsUpdate)
        updateFilters(smoothedLowGain .getCurrentValue(),
                      smoothedLmGain  .getCurrentValue(),
                      smoothedHmGain  .getCurrentValue(),
                      smoothedHighGain.getCurrentValue());

    // ------------------------------------------------------------------
    // 3b. Silence fast-path check.
    //
    // getMagnitude() across the whole buffer is a cheap O(numSamples) scan
    // — far cheaper than the biquad + oversampled-saturation chain it may
    // let us skip. See the kSilenceHoldBlocks / kSilenceThreshold comment
    // in the header for why this is safe (LTI zero-input, not a frequency-
    // selective shortcut on the actual signal).
    // ------------------------------------------------------------------
    const bool blockIsSilent = buffer.getMagnitude(0, numSamples) < kSilenceThreshold;
    if (blockIsSilent)
        silentBlockCounter = juce::jmin(silentBlockCounter + 1, kSilenceHoldBlocks);
    else
        silentBlockCounter = 0;

    const bool takeSilentFastPath = silentBlockCounter >= kSilenceHoldBlocks;

    if (!takeSilentFastPath)
    {
        // --------------------------------------------------------------
        // 4. EQ stage.
        // --------------------------------------------------------------
        juce::dsp::AudioBlock<float>            block(buffer);
        juce::dsp::ProcessContextReplacing<float> ctx(block);

        auto isBypassed = [&](const juce::String& param) {
            return apvts.getRawParameterValue(param)->load() > 0.5f;
        };

        if (!isBypassed(Params::LOW_BYPASS))  lowFilter    .process(ctx);
        if (!isBypassed(Params::LM_BYPASS))   lowMidFilter .process(ctx);
        if (!isBypassed(Params::HM_BYPASS))   highMidFilter.process(ctx);
        if (!isBypassed(Params::HIGH_BYPASS)) highFilter   .process(ctx);

        // --------------------------------------------------------------
        // 5. Saturation stage — 2× or 4× oversampled.
        // --------------------------------------------------------------
        auto oversampledBlock = activeOS.processSamplesUp(block);
        juce::dsp::ProcessContextReplacing<float> ovCtx(oversampledBlock);
        saturation.process(ovCtx);
        activeOS.processSamplesDown(block);

        // --------------------------------------------------------------
        // 6. Output trim — post-saturation, click-free via SmoothedValue.
        // --------------------------------------------------------------
        buffer.applyGain(juce::Decibels::decibelsToGain(smoothedOutTrim.getCurrentValue()));
    }
    // else: buffer is already (numerically) silent and every stage below is
    // an identity operation on zero input, so leaving it untouched produces
    // the same result as running the full chain — see header comment.

    // ------------------------------------------------------------------
    // 7. Push mono sum to spectrum FIFOs — native-rate FIFO plus the
    //    decimated low-band FIFO (anti-aliased, ÷kLowBandDecimation) that
    //    feeds the GUI's low-frequency analysis branch. See the
    //    lowBandSpectrumFifo comment in the header.
    // ------------------------------------------------------------------
    const int numCh = buffer.getNumChannels();
    for (int s = 0; s < numSamples; ++s)
    {
        float mono = 0.0f;
        for (int ch = 0; ch < numCh; ++ch)
            mono += buffer.getSample(ch, s);
        mono /= (float)numCh;

        spectrumFifo.pushSample(mono);

        const float aaOut = lowBandAAFilter.processSample(mono);
        if (++decimationPhase >= kLowBandDecimation)
        {
            decimationPhase = 0;
            lowBandSpectrumFifo.pushSample(aaOut);
        }
    }
}

// =============================================================================
// State persistence
// =============================================================================
void Api550bAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml(state.createXml());
    copyXmlToBinary(*xml, destData);
}

void Api550bAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xmlState(getXmlFromBinary(data, sizeInBytes));
    if (xmlState && xmlState->hasTagName(apvts.state.getType()))
        apvts.replaceState(juce::ValueTree::fromXml(*xmlState));
}

juce::AudioProcessorEditor* Api550bAudioProcessor::createEditor()
{
    return new Api550bAudioProcessorEditor(*this);
}
