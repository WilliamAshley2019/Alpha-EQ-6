#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"

class ApiLookAndFeel;
class SpectrumAnalyzerComponent;

class Api550bAudioProcessorEditor : public juce::AudioProcessorEditor
{
public:
    Api550bAudioProcessorEditor(Api550bAudioProcessor&);
    ~Api550bAudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    Api550bAudioProcessor& audioProcessor;

    // --- EQ band rotary knobs ---
    juce::Slider lowFreqSlider, lowMidFreqSlider, highMidFreqSlider, highFreqSlider;
    juce::Slider lowGainSlider, lowMidGainSlider, highMidGainSlider, highGainSlider;
    juce::Slider satDriveSlider;

    // --- Global strip (AlphaEQ5) ---
    juce::Slider outTrimSlider;    // horizontal linear, ±4 dB post-saturation trim

    // --- LED toggle buttons ---
    juce::ToggleButton lowShelfButton, highShelfButton, qModeButton;
    juce::ToggleButton lowMuteButton,  lowBypassButton;
    juce::ToggleButton lmMuteButton,   lmBypassButton;
    juce::ToggleButton hmMuteButton,   hmBypassButton;
    juce::ToggleButton highMuteButton, highBypassButton;
    // AlphaEQ5 global toggles
    juce::ToggleButton eqTopoButton;   // green LED: API vs Neve filter topology
    juce::ToggleButton satModeButton;  // orange LED: soft (tanh) vs hard clip
    juce::ToggleButton hqModeButton;   // cyan LED: 2× vs 4× oversampling

    // --- Labels ---
    juce::Label lowBandLabel, lowMidBandLabel, highMidBandLabel, highBandLabel;
    juce::Label lowMuteLabel,  lowBypassLabel;
    juce::Label lmMuteLabel,   lmBypassLabel;
    juce::Label hmMuteLabel,   hmBypassLabel;
    juce::Label highMuteLabel, highBypassLabel;
    juce::Label satDriveLabel;
    juce::Label lowShelfLabel, highShelfLabel, qModeLabel;
    // AlphaEQ5
    juce::Label outTrimLabel;
    juce::Label eqTopoLabel, satModeLabel, hqModeLabel;

    // --- APVTS attachments ---
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

    std::unique_ptr<SliderAttachment> lowFreqAttachment,     lowGainAttachment;
    std::unique_ptr<SliderAttachment> lowMidFreqAttachment,  lowMidGainAttachment;
    std::unique_ptr<SliderAttachment> highMidFreqAttachment, highMidGainAttachment;
    std::unique_ptr<SliderAttachment> highFreqAttachment,    highGainAttachment;
    std::unique_ptr<SliderAttachment> satDriveAttachment;
    std::unique_ptr<SliderAttachment> outTrimAttachment;

    std::unique_ptr<ButtonAttachment> lowShelfAttachment,  highShelfAttachment, qModeAttachment;
    std::unique_ptr<ButtonAttachment> lowMuteAttachment,   lowBypassAttachment;
    std::unique_ptr<ButtonAttachment> lmMuteAttachment,    lmBypassAttachment;
    std::unique_ptr<ButtonAttachment> hmMuteAttachment,    hmBypassAttachment;
    std::unique_ptr<ButtonAttachment> highMuteAttachment,  highBypassAttachment;
    std::unique_ptr<ButtonAttachment> eqTopoAttachment, satModeAttachment, hqModeAttachment;

    // --- Spectrum analyser ---
    std::unique_ptr<SpectrumAnalyzerComponent> spectrumAnalyzer;

    // --- Look and feel ---
    std::unique_ptr<ApiLookAndFeel> laf;

    void setupSlider(juce::Slider& slider);
    void setupStripSlider(juce::Slider& slider);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Api550bAudioProcessorEditor)
};
