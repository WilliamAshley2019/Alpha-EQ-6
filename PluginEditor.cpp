#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cmath>

// =============================================================================
// Colour palette
// =============================================================================
namespace Palette
{
    const juce::Colour faceplateTop{ 0xff3a3d42 };
    const juce::Colour faceplateBot{ 0xff1e2025 };
    const juce::Colour panelFill{ 0xff22252a };
    const juce::Colour panelEdgeLight{ 0xff4a4e55 };
    const juce::Colour panelEdgeDark{ 0xff0d0f12 };
    const juce::Colour legend{ 0xffcccccc };
    const juce::Colour legendDim{ 0xff888888 };
    const juce::Colour ledAmber{ 0xffff9900 };
    const juce::Colour ledRed{ 0xffdd2200 };
    const juce::Colour ledOff{ 0xff181a1d };
    const juce::Colour specLine{ 0xcc00aaff };
    const juce::Colour specFillTop{ 0x9900ccff };
    const juce::Colour specFillBot{ 0x220055aa };
    const juce::Colour eqCurve{ 0xccffee77 }; // pale yellow at ~80% alpha
}

// =============================================================================
// ApiLookAndFeel
// =============================================================================
class ApiLookAndFeel : public juce::LookAndFeel_V4
{
public:
    ApiLookAndFeel()
    {
        setColour(juce::Slider::thumbColourId, juce::Colours::whitesmoke);
        setColour(juce::Slider::rotarySliderOutlineColourId, juce::Colours::black.withAlpha(0.7f));
        setColour(juce::Label::textColourId, Palette::legend);
        setColour(juce::Slider::trackColourId, juce::Colour(0xff3a3d42));
    }

    // -------------------------------------------------------------------------
    // Rotary knob
    // -------------------------------------------------------------------------
    void drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
        float sliderPos, const float rotaryStartAngle,
        const float rotaryEndAngle, juce::Slider&) override
    {
        auto bounds = juce::Rectangle<float>((float)x, (float)y, (float)width, (float)height).reduced(10.0f);
        auto radius = juce::jmin(bounds.getWidth(), bounds.getHeight()) / 2.0f;
        auto centre = bounds.getCentre();
        auto toAngle = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);

        g.setColour(juce::Colour(0xff090a0c));
        g.fillEllipse(bounds.expanded(2.0f));

        juce::ColourGradient body(
            juce::Colour(0xff4e5258), centre.getX() - radius * 0.3f, centre.getY() - radius * 0.4f,
            juce::Colour(0xff1a1c1f), centre.getX() + radius * 0.2f, centre.getY() + radius * 0.5f,
            true);
        g.setGradientFill(body);
        g.fillEllipse(bounds);

        g.setColour(juce::Colours::white.withAlpha(0.08f));
        g.drawEllipse(bounds.reduced(1.5f), 1.5f);

        juce::Path p;
        p.addEllipse(-2.0f, -(radius * 0.78f) - 2.0f, 4.0f, 4.0f);
        p.applyTransform(juce::AffineTransform::rotation(toAngle).translated(centre));
        g.setColour(juce::Colours::whitesmoke);
        g.fillPath(p);
    }

    // -------------------------------------------------------------------------
    // Horizontal linear slider (OUT_TRIM global strip)
    // -------------------------------------------------------------------------
    void drawLinearSlider(juce::Graphics& g, int x, int y, int width, int height,
        float sliderPos, float, float,
        const juce::Slider::SliderStyle style, juce::Slider& slider) override
    {
        if (style != juce::Slider::LinearHorizontal)
        {
            LookAndFeel_V4::drawLinearSlider(g, x, y, width, height,
                sliderPos, 0.0f, (float)width, style, slider);
            return;
        }

        const float trackY = y + height * 0.5f;
        const float trackH = 4.0f;

        juce::ColourGradient groove(
            juce::Colour(0xff0a0c0e), (float)x, trackY - 2.0f,
            juce::Colour(0xff2a2d32), (float)x, trackY + 2.0f, false);
        g.setGradientFill(groove);
        g.fillRoundedRectangle((float)x, trackY - trackH * 0.5f, (float)width, trackH, 2.0f);

        // Centre-notch (0 dB mark)
        float cx = x + width * 0.5f;
        g.setColour(Palette::legend.withAlpha(0.35f));
        g.fillRect(cx - 0.5f, trackY - trackH, 1.0f, trackH * 2.0f);

        const float thumbH = (float)height * 0.72f;
        const float thumbW = 12.0f;
        const float tx = sliderPos - thumbW * 0.5f;
        const float ty = trackY - thumbH * 0.5f;

        g.setColour(juce::Colours::black.withAlpha(0.5f));
        g.fillRoundedRectangle(tx + 1.5f, ty + 1.5f, thumbW, thumbH, 2.5f);

        juce::ColourGradient thumbGrad(
            juce::Colour(0xff5a5e65), tx, ty,
            juce::Colour(0xff1e2025), tx + thumbW, ty + thumbH, false);
        g.setGradientFill(thumbGrad);
        g.fillRoundedRectangle(tx, ty, thumbW, thumbH, 2.5f);

        g.setColour(juce::Colours::white.withAlpha(0.3f));
        g.fillRect(tx + thumbW * 0.5f - 0.5f, ty + thumbH * 0.2f, 1.0f, thumbH * 0.6f);
    }

    // -------------------------------------------------------------------------
    // LED toggle button — colour from "ledMode" property on the button
    // -------------------------------------------------------------------------
    void drawToggleButton(juce::Graphics& g, juce::ToggleButton& button,
        bool, bool) override
    {
        auto bounds = button.getLocalBounds().toFloat();
        auto circleBounds = bounds.withSizeKeepingCentre(
            bounds.getHeight(), bounds.getHeight()).reduced(2.5f);

        juce::String mode = button.getProperties()["ledMode"].toString();
        juce::Colour onColour =
            (mode == "amber") ? Palette::ledAmber :
            (mode == "green") ? juce::Colour(0xff22dd55) :
            (mode == "cyan") ? juce::Colour(0xff00ccee) :
            (mode == "orange") ? juce::Colour(0xffff6600) :
            Palette::ledRed;

        if (button.getToggleState())
        {
            g.setColour(onColour.withAlpha(0.28f));
            g.fillEllipse(circleBounds.expanded(4.0f));

            juce::ColourGradient cap(
                juce::Colours::white,
                circleBounds.getCentreX(),
                circleBounds.getCentreY() - circleBounds.getHeight() * 0.35f,
                onColour.darker(0.1f),
                circleBounds.getCentreX(),
                circleBounds.getCentreY() + circleBounds.getHeight() * 0.65f,
                true);
            g.setGradientFill(cap);
            g.fillEllipse(circleBounds);

            g.setColour(juce::Colours::white.withAlpha(0.5f));
            auto ss = circleBounds.getHeight() * 0.25f;
            g.fillEllipse(circleBounds.getCentreX() - ss * 0.5f,
                circleBounds.getY() + circleBounds.getHeight() * 0.18f,
                ss, ss);
        }
        else
        {
            juce::ColourGradient recess(
                Palette::ledOff.brighter(0.12f),
                circleBounds.getCentreX(),
                circleBounds.getCentreY() - circleBounds.getHeight() * 0.35f,
                Palette::ledOff.darker(0.4f),
                circleBounds.getCentreX(),
                circleBounds.getCentreY() + circleBounds.getHeight() * 0.65f,
                true);
            g.setGradientFill(recess);
            g.fillEllipse(circleBounds);

            g.setColour(juce::Colours::black.withAlpha(0.6f));
            g.drawEllipse(circleBounds, 1.0f);
        }
    }
};

class SpectrumAnalyzerComponent : public juce::Component, public juce::Timer
{
public:
    explicit SpectrumAnalyzerComponent(Api550bAudioProcessor& p)
        : processor(p),
        spectrumFifo(p.spectrumFifo),
        lowBandFifo(p.lowBandSpectrumFifo),
        forwardFFT(SpectrumFifo::fftOrder),
        window(static_cast<size_t>(SpectrumFifo::fftSize + 1), juce::dsp::WindowingFunction<float>::hann)
    {
        scopeData.fill(-80.0f);
        fastMagnitudes.fill(0.0f);
        lowMagnitudes.fill(0.0f);
        startTimerHz(30);
    }

    ~SpectrumAnalyzerComponent() override { stopTimer(); }

    void timerCallback() override
    {
        const bool updatedFast = pullAndTransform(spectrumFifo, fastMagnitudes);

        bool updatedLow = false;
        if (++lowBandTickCounter >= 3)
        {
            lowBandTickCounter = 0;
            updatedLow = pullAndTransform(lowBandFifo, lowMagnitudes);
        }

       
        if (!updatedFast && !updatedLow)
            return;

        const double sr = processor.getSampleRate();
        const float  actSr = (sr > 0.0) ? (float)sr : 44100.0f;
        const float  lowSr = actSr / (float)Api550bAudioProcessor::kLowBandDecimation;

        const int   numBins = SpectrumFifo::fftSize / 2;
        const int   numCols = (int)scopeData.size();
        const float logMin = std::log10(20.0f);
        const float logMax = std::log10(20000.0f);
        const float fastBinWidth = actSr / (float)SpectrumFifo::fftSize;
        const float lowBinWidth  = lowSr  / (float)SpectrumFifo::fftSize;

        constexpr float xfLowHz  = 150.0f;
        constexpr float xfHighHz = 500.0f;

        for (int col = 0; col < numCols; ++col)
        {
            float t = (float)col / (float)(numCols - 1);
            float freq = std::pow(10.0f, logMin + t * (logMax - logMin));

            const float dBfast = binToDb(fastMagnitudes, freq, fastBinWidth, numBins);
            const float dBlow  = binToDb(lowMagnitudes,  freq, lowBinWidth,  numBins);

            const float blend = juce::jlimit(0.0f, 1.0f,
                (freq - xfLowHz) / (xfHighHz - xfLowHz)); // 0 = low branch, 1 = fast branch
            const float dB = dBlow * (1.0f - blend) + dBfast * blend;

            // Asymmetric smoothing: fast attack (0.5), slow release (0.88)
            float prev = scopeData[col];
            float alpha = (dB > prev) ? 0.5f : 0.88f;
            scopeData[col] = prev * alpha + dB * (1.0f - alpha);
        }

        repaint();
    }

    void paint(juce::Graphics& g) override
    {
        auto bounds = getLocalBounds().toFloat().reduced(2.0f);
        if (bounds.isEmpty()) return;

        // Background
        juce::ColourGradient bg(
            juce::Colour(0xff111316), bounds.getCentreX(), bounds.getY(),
            juce::Colour(0xff0a0c0e), bounds.getCentreX(), bounds.getBottom(), false);
        g.setGradientFill(bg);
        g.fillRoundedRectangle(bounds, 6.0f);

        g.setColour(Palette::panelEdgeDark);
        g.drawRoundedRectangle(bounds, 6.0f, 1.0f);

        // dBFS horizontal grid lines
        g.setFont(juce::FontOptions(9.0f));
        for (float db : { 0.0f, -12.0f, -24.0f, -48.0f })
        {
            float y = dbFsToY(db, bounds);
            g.setColour(juce::Colours::white.withAlpha(0.07f));
            g.drawHorizontalLine(juce::roundToInt(y),
                bounds.getX() + 1.0f, bounds.getRight() - 1.0f);
            g.setColour(Palette::legendDim);
            g.drawText(juce::String((int)db) + "dB",
                juce::Rectangle<float>(bounds.getX() + 2.0f, y - 8.0f, 30.0f, 10.0f),
                juce::Justification::left, false);
        }

        // Frequency grid lines
        const float logMin = std::log10(20.0f);
        const float logMax = std::log10(20000.0f);
        for (float hz : { 100.0f, 1000.0f, 10000.0f })
        {
            float t = (std::log10(hz) - logMin) / (logMax - logMin);
            float x = bounds.getX() + t * bounds.getWidth();
            g.setColour(juce::Colours::white.withAlpha(0.07f));
            g.drawVerticalLine(juce::roundToInt(x), bounds.getY() + 1.0f, bounds.getBottom() - 1.0f);
            juce::String label = (hz >= 1000.0f)
                ? juce::String((int)(hz / 1000)) + "k"
                : juce::String((int)hz);
            g.setColour(Palette::legendDim);
            g.drawText(label,
                juce::Rectangle<float>(x + 2.0f, bounds.getBottom() - 14.0f, 24.0f, 12.0f),
                juce::Justification::left, false);
        }

        // EQ curve 0 dB reference line
        {
            float y0 = eqDbToY(0.0f, bounds);
            g.setColour(Palette::eqCurve.withAlpha(0.18f));
            g.drawHorizontalLine(juce::roundToInt(y0),
                bounds.getX() + 1.0f, bounds.getRight() - 1.0f);
        }

        // Spectrum path
        const int  numCols = (int)scopeData.size();
        juce::Path specPath;

        for (int col = 0; col < numCols; ++col)
        {
            float x = bounds.getX() + (float)col / (float)(numCols - 1) * bounds.getWidth();
            float y = dbFsToY(scopeData[col], bounds);
            if (col == 0) specPath.startNewSubPath(x, y);
            else          specPath.lineTo(x, y);
        }
        specPath.lineTo(bounds.getRight(), bounds.getBottom());
        specPath.lineTo(bounds.getX(), bounds.getBottom());
        specPath.closeSubPath();

        juce::ColourGradient fill(
            Palette::specFillTop, bounds.getCentreX(), bounds.getY(),
            Palette::specFillBot, bounds.getCentreX(), bounds.getBottom(), false);
        g.setGradientFill(fill);
        g.fillPath(specPath);

        g.setColour(Palette::specLine);
        g.strokePath(specPath, juce::PathStrokeType(1.3f));

        // EQ curve overlay (drawn last, on top of spectrum)
        drawEqCurve(g, bounds);
    }

private:
    // -------------------------------------------------------------------------
    // Spectrum Y-axis: -72..+6 dBFS
    // -------------------------------------------------------------------------
    float dbFsToY(float dB, const juce::Rectangle<float>& b) const
    {
        constexpr float minDb = -72.0f, maxDb = 6.0f;
        float norm = juce::jlimit(0.0f, 1.0f, (dB - maxDb) / (minDb - maxDb));
        return b.getY() + norm * b.getHeight();
    }

    // -------------------------------------------------------------------------
    // EQ curve Y-axis: ±14 dB centred in the display.
    // Independent of the spectrum dBFS axis so neither scale is distorted.
    // -------------------------------------------------------------------------
    float eqDbToY(float eqDb, const juce::Rectangle<float>& b) const
    {
        constexpr float maxEq = 14.0f, minEq = -14.0f;
        float norm = juce::jlimit(0.0f, 1.0f, (eqDb - maxEq) / (minEq - maxEq));
        return b.getY() + norm * b.getHeight();
    }

    // -------------------------------------------------------------------------
    // drawEqCurve
    //
    // Evaluates the combined H(z) magnitude at 512 log-spaced frequencies and
    // draws the result as a pale yellow curve over the spectrum display.
    //
    // Thread safety: all Coefficients objects are created locally on the GUI
    // thread — no audio-thread filter state is touched.  The processor's
    // currentXxxGainDb atomics give the smoothed gain values actually being
    // applied; Q and frequency parameters are read directly from apvts.
    //
    // The coefficient and Q logic mirrors updateFilters() exactly so the
    // displayed curve matches what the DSP is doing.  Any divergence between
    // the two would be a maintenance bug.
    // -------------------------------------------------------------------------
    void drawEqCurve(juce::Graphics& g, const juce::Rectangle<float>& bounds)
    {
        const double sr = processor.getSampleRate();
        if (sr <= 0.0) return;

        const float lowGainDb = processor.currentLowGainDb.load(std::memory_order_relaxed);
        const float lmGainDb = processor.currentLmGainDb.load(std::memory_order_relaxed);
        const float hmGainDb = processor.currentHmGainDb.load(std::memory_order_relaxed);
        const float highGainDb = processor.currentHighGainDb.load(std::memory_order_relaxed);

        // Skip drawing when the combined curve would be visually flat (all ≤ 0.05 dB)
        if (std::abs(lowGainDb) < 0.05f && std::abs(lmGainDb) < 0.05f &&
            std::abs(hmGainDb) < 0.05f && std::abs(highGainDb) < 0.05f)
            return;

        auto getParam = [&](const juce::String& id) -> float {
            return processor.apvts.getRawParameterValue(id)->load();
            };

        const bool lowBypassed = getParam(Params::LOW_BYPASS) > 0.5f;
        const bool lmBypassed = getParam(Params::LM_BYPASS) > 0.5f;
        const bool hmBypassed = getParam(Params::HM_BYPASS) > 0.5f;
        const bool highBypassed = getParam(Params::HIGH_BYPASS) > 0.5f;
        const bool propQ = getParam(Params::Q_MODE) > 0.5f;
        const bool neveMode = getParam(Params::EQ_TOPO) > 0.5f;
        const bool lowShelf = getParam(Params::LOW_SHELF) > 0.5f;
        const bool hiShelf = getParam(Params::HIGH_SHELF) > 0.5f;

        const float shelfQ = neveMode ? 1.1f : 0.7f;
        const float fixedPeakQ = neveMode ? 2.5f : 1.5f;
        const float propQMin = neveMode ? 1.0f : 0.7f;
        const float propQMax = neveMode ? 2.8f : 2.2f;

        auto makePeakQ = [&](float gainDb) -> double {
            return propQ
                ? (double)juce::jlimit(propQMin, propQMax, 1.0f + 0.2f * std::abs(gainDb))
                : (double)fixedPeakQ;
            };
        auto getFreq = [&](const juce::String& param, const float* table, size_t n) -> double {
            size_t idx = juce::jlimit<size_t>(0, n - 1, (size_t)getParam(param));
            return (double)table[idx];
            };

        using DCoeffs = juce::dsp::IIR::Coefficients<double>;
        using DCoeffsPtr = juce::ReferenceCountedObjectPtr<DCoeffs>;

        DCoeffsPtr lowC, lmC, hmC, highC;

        if (!lowBypassed)
        {
            double freq = getFreq(Params::LOW_FREQ, EQTables::lowFreq, std::size(EQTables::lowFreq));
            double gain = (double)juce::Decibels::decibelsToGain(lowGainDb);
            double q = lowShelf ? (double)shelfQ : makePeakQ(lowGainDb);
            lowC = lowShelf ? DCoeffs::makeLowShelf(sr, freq, q, gain)
                : DCoeffs::makePeakFilter(sr, freq, q, gain);
        }
        if (!lmBypassed)
        {
            double freq = getFreq(Params::LM_FREQ, EQTables::lowFreq, std::size(EQTables::lowFreq));
            double q = makePeakQ(lmGainDb);
            double gain = (double)juce::Decibels::decibelsToGain(lmGainDb);
            lmC = DCoeffs::makePeakFilter(sr, freq, q, gain);
        }
        if (!hmBypassed)
        {
            double freq = getFreq(Params::HM_FREQ, EQTables::hiFreq, std::size(EQTables::hiFreq));
            double q = makePeakQ(hmGainDb);
            double gain = (double)juce::Decibels::decibelsToGain(hmGainDb);
            hmC = DCoeffs::makePeakFilter(sr, freq, q, gain);
        }
        if (!highBypassed)
        {
            double freq = getFreq(Params::HIGH_FREQ, EQTables::hiFreq, std::size(EQTables::hiFreq));
            double gain = (double)juce::Decibels::decibelsToGain(highGainDb);
            double q = hiShelf ? (double)shelfQ : makePeakQ(highGainDb);
            highC = hiShelf ? DCoeffs::makeHighShelf(sr, freq * 1.3, q, gain)
                : DCoeffs::makePeakFilter(sr, freq, q, gain);
        }

        if (!lowC && !lmC && !hmC && !highC) return;

        constexpr int  numPoints = 512;
        const float    logMin = std::log10(20.0f);
        const float    logMax = std::log10(20000.0f);

        juce::Path curvePath;
        bool first = true;

        for (int i = 0; i < numPoints; ++i)
        {
            float  t = (float)i / (float)(numPoints - 1);
            double freq = std::pow(10.0, (double)(logMin + t * (logMax - logMin)));

            double mag = 1.0;
            if (lowC)  mag *= lowC->getMagnitudeForFrequency(freq, sr);
            if (lmC)   mag *= lmC->getMagnitudeForFrequency(freq, sr);
            if (hmC)   mag *= hmC->getMagnitudeForFrequency(freq, sr);
            if (highC) mag *= highC->getMagnitudeForFrequency(freq, sr);

            float dB = (float)juce::Decibels::gainToDecibels(mag);
            float x = bounds.getX() + t * bounds.getWidth();
            float y = eqDbToY(dB, bounds);

            if (first) { curvePath.startNewSubPath(x, y); first = false; }
            else         curvePath.lineTo(x, y);
        }

        g.setColour(Palette::eqCurve);
        g.strokePath(curvePath, juce::PathStrokeType(
            1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    // -------------------------------------------------------------------------
    // Pull one block from a SpectrumFifo (if ready), window it, run the
    // shared forwardFFT, and write normalised linear magnitudes into magsOut.
    // Returns false (leaving magsOut untouched) when the FIFO has no new
    // block yet — the caller keeps using the previous frame's values.
    // -------------------------------------------------------------------------
    bool pullAndTransform(SpectrumFifo& fifo,
                          std::array<float, SpectrumFifo::fftSize / 2>& magsOut)
    {
        std::array<float, SpectrumFifo::fftSize> block{};
        if (!fifo.pullBlock(block))
            return false;

        window.multiplyWithWindowingTable(block.data(), static_cast<size_t>(SpectrumFifo::fftSize));
        std::fill(fftData.begin(), fftData.end(), 0.0f);
        std::copy(block.begin(), block.end(), fftData.begin());
        forwardFFT.performFrequencyOnlyForwardTransform(fftData.data());

        // Normalise: divide by (fftSize/2) for FFT scaling, ×2 for Hann coherent
        // gain correction.  Combined factor = divide by (fftSize/4).
        constexpr float normFactor = 1.0f / (float)(SpectrumFifo::fftSize / 4);
        for (size_t i = 0; i < magsOut.size(); ++i)
            magsOut[i] = fftData[i] * normFactor;

        return true;
    }

    float binToDb(const std::array<float, SpectrumFifo::fftSize / 2>& mags,
                  float freq, float binWidth, int numBins) const
    {
        int bin = juce::jlimit(0, numBins - 1, juce::roundToInt(freq / binWidth));
        return juce::Decibels::gainToDecibels(mags[bin], -80.0f);
    }

    Api550bAudioProcessor& processor;
    SpectrumFifo& spectrumFifo;
    SpectrumFifo& lowBandFifo;
    juce::dsp::FFT                               forwardFFT;
    juce::dsp::WindowingFunction<float>          window;
    std::array<float, SpectrumFifo::fftSize * 2> fftData{};
    std::array<float, SpectrumFifo::fftSize / 2> fastMagnitudes{};
    std::array<float, SpectrumFifo::fftSize / 2> lowMagnitudes{};
    int                                           lowBandTickCounter = 0;
    std::array<float, 512>                       scopeData{};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SpectrumAnalyzerComponent)
};

// =============================================================================
// Helper
// =============================================================================
static void setLedMode(juce::ToggleButton& b, const juce::String& mode)
{
    b.getProperties().set("ledMode", mode);
}

// =============================================================================
// Constructor
// =============================================================================
Api550bAudioProcessorEditor::Api550bAudioProcessorEditor(Api550bAudioProcessor& p)
    : AudioProcessorEditor(&p), audioProcessor(p)
{
    laf = std::make_unique<ApiLookAndFeel>();
    setLookAndFeel(laf.get());

    setupSlider(lowFreqSlider);
    setupSlider(lowMidFreqSlider);
    setupSlider(highMidFreqSlider);
    setupSlider(highFreqSlider);
    setupSlider(lowGainSlider);
    setupSlider(lowMidGainSlider);
    setupSlider(highMidGainSlider);
    setupSlider(highGainSlider);
    setupSlider(satDriveSlider);
    setupStripSlider(outTrimSlider);

    auto setupLabel = [&](juce::Label& label, const juce::String& text, float fontSize = 12.0f)
        {
            label.setText(text, juce::dontSendNotification);
            label.setJustificationType(juce::Justification::centred);
            label.setFont(juce::FontOptions(fontSize));
            label.setColour(juce::Label::textColourId, Palette::legend);
            addAndMakeVisible(label);
        };

    setupLabel(lowBandLabel, "LOW", 15.0f);
    setupLabel(lowMidBandLabel, "LOW-MID", 15.0f);
    setupLabel(highMidBandLabel, "HIGH-MID", 15.0f);
    setupLabel(highBandLabel, "HIGH", 15.0f);
    setupLabel(lowMuteLabel, "MUTE", 10.0f);
    setupLabel(lowBypassLabel, "BYPASS", 10.0f);
    setupLabel(lmMuteLabel, "MUTE", 10.0f);
    setupLabel(lmBypassLabel, "BYPASS", 10.0f);
    setupLabel(hmMuteLabel, "MUTE", 10.0f);
    setupLabel(hmBypassLabel, "BYPASS", 10.0f);
    setupLabel(highMuteLabel, "MUTE", 10.0f);
    setupLabel(highBypassLabel, "BYPASS", 10.0f);
    setupLabel(satDriveLabel, "DRIVE", 13.0f);
    setupLabel(lowShelfLabel, "SHELF", 10.0f);
    setupLabel(highShelfLabel, "SHELF", 10.0f);
    setupLabel(qModeLabel, "PROP Q", 10.0f);
    setupLabel(outTrimLabel, "OUT TRIM", 10.0f);
    setupLabel(eqTopoLabel, "BRIT", 10.0f);
    setupLabel(satModeLabel, "HARD", 10.0f);
    setupLabel(hqModeLabel, "4x HQ", 10.0f);

    auto setupToggle = [&](juce::ToggleButton& button, const juce::String& ledMode)
        {
            setLedMode(button, ledMode);
            button.setToggleState(false, juce::dontSendNotification);
            addAndMakeVisible(button);
        };

    setupToggle(lowMuteButton, "red");
    setupToggle(lowBypassButton, "red");
    setupToggle(lmMuteButton, "red");
    setupToggle(lmBypassButton, "red");
    setupToggle(hmMuteButton, "red");
    setupToggle(hmBypassButton, "red");
    setupToggle(highMuteButton, "red");
    setupToggle(highBypassButton, "red");
    setupToggle(lowShelfButton, "amber");
    setupToggle(highShelfButton, "amber");
    setupToggle(qModeButton, "amber");
    setupToggle(eqTopoButton, "green");  // Brit (Neve inspired) topology
    setupToggle(satModeButton, "orange"); // hard clip mode
    setupToggle(hqModeButton, "cyan");   // 4x HQ oversampling

    // APVTS attachments
    lowFreqAttachment = std::make_unique<SliderAttachment>(audioProcessor.apvts, Params::LOW_FREQ, lowFreqSlider);
    lowGainAttachment = std::make_unique<SliderAttachment>(audioProcessor.apvts, Params::LOW_GAIN, lowGainSlider);
    lowShelfAttachment = std::make_unique<ButtonAttachment>(audioProcessor.apvts, Params::LOW_SHELF, lowShelfButton);
    lowMuteAttachment = std::make_unique<ButtonAttachment>(audioProcessor.apvts, Params::LOW_MUTE, lowMuteButton);
    lowBypassAttachment = std::make_unique<ButtonAttachment>(audioProcessor.apvts, Params::LOW_BYPASS, lowBypassButton);

    lowMidFreqAttachment = std::make_unique<SliderAttachment>(audioProcessor.apvts, Params::LM_FREQ, lowMidFreqSlider);
    lowMidGainAttachment = std::make_unique<SliderAttachment>(audioProcessor.apvts, Params::LM_GAIN, lowMidGainSlider);
    lmMuteAttachment = std::make_unique<ButtonAttachment>(audioProcessor.apvts, Params::LM_MUTE, lmMuteButton);
    lmBypassAttachment = std::make_unique<ButtonAttachment>(audioProcessor.apvts, Params::LM_BYPASS, lmBypassButton);

    highMidFreqAttachment = std::make_unique<SliderAttachment>(audioProcessor.apvts, Params::HM_FREQ, highMidFreqSlider);
    highMidGainAttachment = std::make_unique<SliderAttachment>(audioProcessor.apvts, Params::HM_GAIN, highMidGainSlider);
    hmMuteAttachment = std::make_unique<ButtonAttachment>(audioProcessor.apvts, Params::HM_MUTE, hmMuteButton);
    hmBypassAttachment = std::make_unique<ButtonAttachment>(audioProcessor.apvts, Params::HM_BYPASS, hmBypassButton);

    highFreqAttachment = std::make_unique<SliderAttachment>(audioProcessor.apvts, Params::HIGH_FREQ, highFreqSlider);
    highGainAttachment = std::make_unique<SliderAttachment>(audioProcessor.apvts, Params::HIGH_GAIN, highGainSlider);
    highShelfAttachment = std::make_unique<ButtonAttachment>(audioProcessor.apvts, Params::HIGH_SHELF, highShelfButton);
    highMuteAttachment = std::make_unique<ButtonAttachment>(audioProcessor.apvts, Params::HIGH_MUTE, highMuteButton);
    highBypassAttachment = std::make_unique<ButtonAttachment>(audioProcessor.apvts, Params::HIGH_BYPASS, highBypassButton);

    satDriveAttachment = std::make_unique<SliderAttachment>(audioProcessor.apvts, Params::SAT_DRIVE, satDriveSlider);
    qModeAttachment = std::make_unique<ButtonAttachment>(audioProcessor.apvts, Params::Q_MODE, qModeButton);
    outTrimAttachment = std::make_unique<SliderAttachment>(audioProcessor.apvts, Params::OUT_TRIM, outTrimSlider);
    eqTopoAttachment = std::make_unique<ButtonAttachment>(audioProcessor.apvts, Params::EQ_TOPO, eqTopoButton);
    satModeAttachment = std::make_unique<ButtonAttachment>(audioProcessor.apvts, Params::SAT_MODE, satModeButton);
    hqModeAttachment = std::make_unique<ButtonAttachment>(audioProcessor.apvts, Params::HQ_MODE, hqModeButton);

    spectrumAnalyzer = std::make_unique<SpectrumAnalyzerComponent>(audioProcessor);
    addAndMakeVisible(spectrumAnalyzer.get());

    setSize(700, 640);
    setResizable(true, true);
    setResizeLimits(600, 550, 1200, 1000);
}

Api550bAudioProcessorEditor::~Api550bAudioProcessorEditor()
{
    setLookAndFeel(nullptr);
}

// =============================================================================
// paint
// =============================================================================
void Api550bAudioProcessorEditor::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();

    juce::ColourGradient faceplate(
        Palette::faceplateTop, bounds.getCentreX(), bounds.getY(),
        Palette::faceplateBot, bounds.getCentreX(), bounds.getBottom(), false);
    g.setGradientFill(faceplate);
    g.fillAll();

    // Top highlight
    g.setColour(juce::Colour(0xff606570));
    g.fillRect(juce::Rectangle<float>(0.0f, 0.0f, bounds.getWidth(), 1.5f));

    // Title bar
    auto titleStrip = juce::Rectangle<float>(0.0f, 0.0f, bounds.getWidth(), 46.0f);
    juce::ColourGradient titleGrad(
        juce::Colour(0xff2e3138), bounds.getCentreX(), titleStrip.getY(),
        juce::Colour(0xff1c1e22), bounds.getCentreX(), titleStrip.getBottom(), false);
    g.setGradientFill(titleGrad);
    g.fillRect(titleStrip);

    g.setColour(juce::Colours::white);
    g.setFont(juce::FontOptions(20.0f, juce::Font::bold));
    g.drawText("ALPHA EQ 6", titleStrip, juce::Justification::centred, true);

    g.setColour(Palette::legendDim);
    g.setFont(juce::FontOptions(9.5f));
    g.drawText("PROGRAMME EQUALISER", titleStrip.translated(0.0f, 13.0f),
        juce::Justification::centred, false);

    // Divider
    g.setColour(juce::Colour(0xff0d0f12));
    g.fillRect(juce::Rectangle<float>(0.0f, 46.0f, bounds.getWidth(), 1.5f));

    // Global controls strip
    auto stripRect = juce::Rectangle<float>(0.0f, 47.5f, bounds.getWidth(), 46.0f);
    juce::ColourGradient stripGrad(
        juce::Colour(0xff272b30), bounds.getCentreX(), stripRect.getY(),
        juce::Colour(0xff1e2126), bounds.getCentreX(), stripRect.getBottom(), false);
    g.setGradientFill(stripGrad);
    g.fillRect(stripRect);

    g.setColour(juce::Colour(0xff0d0f12));
    g.fillRect(juce::Rectangle<float>(0.0f, stripRect.getBottom(), bounds.getWidth(), 1.5f));

    // Band panel recesses
    auto panelArea = getLocalBounds().toFloat();
    panelArea.removeFromTop(96.0f);
    panelArea.removeFromBottom(100.0f);
    panelArea.reduce(10.0f, 10.0f);

    const int   numBands = 4;
    const float panelW = panelArea.getWidth() / numBands;

    for (int i = 0; i < numBands; ++i)
    {
        auto panel = panelArea.withX(panelArea.getX() + (float)i * panelW).withWidth(panelW - 5.0f);

        juce::ColourGradient recess(
            Palette::panelFill.darker(0.15f), panel.getX(), panel.getY(),
            Palette::panelFill.brighter(0.05f), panel.getX(), panel.getBottom(), false);
        g.setGradientFill(recess);
        g.fillRoundedRectangle(panel, 8.0f);

        g.setColour(Palette::panelEdgeDark);
        g.drawRoundedRectangle(panel.reduced(0.5f), 8.0f, 1.0f);
        g.setColour(Palette::panelEdgeLight.withAlpha(0.35f));
        g.drawRoundedRectangle(panel.translated(1.0f, 1.0f).reduced(1.5f), 8.0f, 0.75f);
    }

    // Spectrum border
    auto specBorder = getLocalBounds().toFloat();
    specBorder.removeFromTop((float)getHeight() - 100.0f);
    specBorder.reduce(10.0f, 5.0f);
    g.setColour(Palette::panelEdgeDark);
    g.drawRoundedRectangle(specBorder, 6.0f, 1.0f);
}

// =============================================================================
// resized
// =============================================================================
void Api550bAudioProcessorEditor::resized()
{
    auto bounds = getLocalBounds();
    bounds.removeFromTop(50); // title bar

    // ---- Global controls strip ----
    // Layout: [brit][HARD][4×HQ]  ............. [OUT TRIM  --------0--------  ]
    auto globalStrip = bounds.removeFromTop(46);
    {
        auto strip = globalStrip.reduced(14, 6);

        const int btnSize = 20;
        const int btnLabelH = 13;
        const int btnColW = 44;

        // Three global LED buttons on the left
        for (auto pair : {
            std::pair<juce::ToggleButton*, juce::Label*>{ &eqTopoButton,& eqTopoLabel },
            std::pair<juce::ToggleButton*, juce::Label*>{ &satModeButton,& satModeLabel },
            std::pair<juce::ToggleButton*, juce::Label*>{ &hqModeButton,& hqModeLabel }
            })
        {
            auto col = strip.removeFromLeft(btnColW);
            pair.first->setBounds(col.removeFromTop(btnSize).withSizeKeepingCentre(btnSize, btnSize));
            pair.second->setBounds(col.removeFromTop(btnLabelH));
            strip.removeFromLeft(4);
        }

        strip.removeFromLeft(8);

        // OUT_TRIM on the right
        auto trimRight = strip.removeFromRight(juce::jmin(240, strip.getWidth()));
        auto trimLabelR = trimRight.removeFromTop(btnLabelH);
        outTrimLabel.setBounds(trimLabelR);
        outTrimSlider.setBounds(trimRight);
    }

    // ---- Spectrum analyser ----
    auto spectrumArea = bounds.removeFromBottom(100);
    spectrumArea.reduce(10, 5);
    spectrumAnalyzer->setBounds(spectrumArea);

    // ---- Band columns ----
    bounds.reduce(15, 15);
    const int numBands = 4;
    const int bandWidth = (bounds.getWidth() - (numBands - 1) * 10) / numBands;

    auto layoutBand = [&](juce::Rectangle<int> area,
        juce::Label& bandLabel, juce::Slider& freqSlider, juce::Slider& gainSlider,
        juce::Button* muteBtn, juce::Label* muteLabel,
        juce::Button* bypassBtn, juce::Label* bypassLabel,
        juce::Component* extra, juce::Label* extraLabel)
        {
            const int labelH = 22, knobSize = 68, textBoxH = 18;
            const int sliderH = knobSize + textBoxH;
            const int btnSize = 18, btnLabelH = 14, spacing = 8;

            bandLabel.setBounds(area.removeFromTop(labelH));
            area.removeFromTop(spacing);
            freqSlider.setBounds(area.removeFromTop(sliderH).withSizeKeepingCentre(knobSize, sliderH));
            area.removeFromTop(spacing);
            gainSlider.setBounds(area.removeFromTop(sliderH).withSizeKeepingCentre(knobSize, sliderH));
            area.removeFromTop(spacing);

            if (muteBtn && bypassBtn)
            {
                auto row = area.removeFromTop(btnSize + btnLabelH);
                int  half = row.getWidth() / 2;
                auto ma = row.removeFromLeft(half);
                muteBtn->setBounds(ma.removeFromTop(btnSize).withSizeKeepingCentre(btnSize, btnSize));
                if (muteLabel) muteLabel->setBounds(ma);
                bypassBtn->setBounds(row.removeFromTop(btnSize).withSizeKeepingCentre(btnSize, btnSize));
                if (bypassLabel) bypassLabel->setBounds(row);
                area.removeFromTop(spacing);
            }

            if (extra)
            {
                bool isSlider = (dynamic_cast<juce::Slider*>(extra) != nullptr);
                if (isSlider)
                {
                    if (extraLabel) extraLabel->setBounds(area.removeFromTop(labelH));
                    extra->setBounds(area.removeFromTop(sliderH).withSizeKeepingCentre(knobSize, sliderH));
                }
                else
                {
                    extra->setBounds(area.removeFromTop(btnSize).withSizeKeepingCentre(btnSize, btnSize));
                    if (extraLabel) extraLabel->setBounds(area.removeFromTop(btnLabelH));
                }
            }
        };

    for (int i = 0; i < numBands; ++i)
    {
        auto col = bounds.withX(bounds.getX() + i * (bandWidth + 10)).withWidth(bandWidth);
        if (i == 0)
            layoutBand(col, lowBandLabel, lowFreqSlider, lowGainSlider,
                &lowMuteButton, &lowMuteLabel, &lowBypassButton, &lowBypassLabel,
                &lowShelfButton, &lowShelfLabel);
        else if (i == 1)
            layoutBand(col, lowMidBandLabel, lowMidFreqSlider, lowMidGainSlider,
                &lmMuteButton, &lmMuteLabel, &lmBypassButton, &lmBypassLabel,
                &satDriveSlider, &satDriveLabel);
        else if (i == 2)
            layoutBand(col, highMidBandLabel, highMidFreqSlider, highMidGainSlider,
                &hmMuteButton, &hmMuteLabel, &hmBypassButton, &hmBypassLabel,
                &qModeButton, &qModeLabel);
        else
            layoutBand(col, highBandLabel, highFreqSlider, highGainSlider,
                &highMuteButton, &highMuteLabel, &highBypassButton, &highBypassLabel,
                &highShelfButton, &highShelfLabel);
    }
}

// =============================================================================
// Slider helpers
// =============================================================================
void Api550bAudioProcessorEditor::setupSlider(juce::Slider& slider)
{
    slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 60, 18);
    addAndMakeVisible(slider);
}

void Api550bAudioProcessorEditor::setupStripSlider(juce::Slider& slider)
{
    slider.setSliderStyle(juce::Slider::LinearHorizontal);
    slider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 42, 18);
    addAndMakeVisible(slider);
}