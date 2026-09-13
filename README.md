--------------------------------------------------------------------------------------------------
Copyright (c) 2026 William Ashley d/b/a William Ashley Music ( http://WilliamAshley.music )
This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License  (v3) 

This program is distributed in the hope that it will be useful to other audio programmers and music makers in their own plugin designs.
There is no WARRANTY expressed or implied including for MERCHANTABILITY or FITNESS FOR ANY PURPOSE. 
See the GNU General Public License for more details.

Attributtion is requested where possible if you use or modify any of the source,
Notice of use is requested so I can familiarize myself with how the code has been adapted for personal interest.
contact@WilliamAshley.music   
-----------------------------------------------------------------------------------------------------
[![License: GPL v3](https://img.shields.io/badge/License-GPLv3-blue.svg)](https://www.gnu.org/licenses/gpl-3.0)
[![JUCE](https://img.shields.io/badge/Built%20with-JUCE%208.0.12-blue)](https://juce.com)
[![Platform](https://img.shields.io/badge/Platform-Windows%20%7C%20-lightgrey)]()
[![Format](https://img.shields.io/badge/Format-VST3%20%7C%20-orange)]()
ITS AN EQ 
TO DO - put all EQs in a commmon EQ Master Folder Or make some way of seeing all plugin classes easier, as they are effectively built off one another.



AlphaEQ5
A four-band stepped programme equaliser VST3 plugin for Windows, inspired by the API 550 EQ. 
Built with JUCE 8.0.12.

AlphaEQ5 is a four-band programme EQ with stepped frequency and gain controls, asymmetric transformer saturation, and a real-time FFT spectrum analyser with EQ curve overlay. It is designed for precision tonal shaping in the style of classic API 550B hardware, with optional "British" filter character mode and a choice of soft or hard saturation models.

The plugin is intended for use in DAWs that support VST3 on Windows — tested in FL Studio.

Features
EQ Bands
Four independent bands: Low, Low-Mid, High-Mid, and High.
Each band provides:

Stepped frequency selection — 7 positions per band, matching API 550B hardware steps

Low / Low-Mid: 40 Hz, 75 Hz, 150 Hz, 300 Hz, 600 Hz, 1.2 kHz, 2.4 kHz
High-Mid / High: 800 Hz, 1.5 kHz, 3 kHz, 5 kHz, 7 kHz, 10 kHz, 12.5 kHz


Stepped gain: −12, −9, −6, −3, 0, +3, +6, +9, +12 dB
Mute — ramps the band to 0 dB gain smoothly (no click)
Bypass — skips the band entirely (no processing cost, no phase accumulation)
Shelf mode on the Low and High bands

Filter Design

Stereo biquad filters implemented via juce::dsp::ProcessorDuplicator, processing each channel independently with shared coefficients
Double-precision coefficient computation — all biquad math is performed in double and downcast to float for the audio path. This eliminates frequency drift at low frequencies (e.g. a 40 Hz peak in float shifts by several Hz at 44.1 kHz; in double it is sub-millihertz accurate)
Proportional Q mode — Q scales with gain amount, narrowing at high boost/cut values to match API 550B hardware behaviour. Proportional Q range is 0.7–2.2 (API) or 1.0–2.8 (British mode)

EQ Topology — API vs British (BRIT mode)
A toggle in the global controls strip switches between two filter character modes:
ParameterAPI mode (default)British modeShelf Q0.7 (Butterworth, flat)1.1 (resonant knee)Fixed peak Q1.52.5 (narrower bands)Proportional Q range0.7–2.21.0–2.8CharacterBroad, musicalTighter, more surgical
The British mode shelf Q of 1.1 produces a gentle resonant bump at the shelf knee, approximating the faster transition of passive LC-derived topologies.
Saturation
All saturation processing is 2× or 4× oversampled using juce::dsp::Oversampling with IIR half-band anti-aliasing filters, applied only around the waveshaper (not the linear EQ stage).
Asymmetric transformer saturation model:
The saturation uses an asymmetric waveshaper based on tanh(drive × x + k × x²) / drive where k = 0.15 is a fixed asymmetry coefficient.

The standard tanh function is perfectly odd-symmetric and produces only odd-order harmonics (3rd, 5th, 7th)
The x² term is an even function — it breaks this symmetry and injects 2nd-order harmonics (and smaller 4th, 6th order components)
Real output transformers produce strong 2nd harmonic due to the asymmetric B-H hysteresis curve of ferromagnetic cores — this is the "warmth" quality associated with vintage console hardware
The /drive normalisation preserves unity-gain headroom regardless of drive amount

Saturation modes:
ModeCurveCharacterSoft (default)tanh(drive·x + k·x²) / driveSmooth, asymptotic — valve-likeHardclamp(drive·x + k·x², −1.0, +0.93) / driveHard clip with asymmetric thresholds — more aggressive, transformer-like at high levels
The asymmetric clamp limits (+0.93 positive, −1.0 negative) reinforce even-harmonic content independently of the x² term.

Oversampling (HQ Mode)
ModeFactorAlias suppressionCPU cost (sat. stage)Standard2×Alias products > 40 kHzLowerHQ (4x HQ toggle)4×Alias products > 80 kHz~2× standard
Oversampling applies only to the waveshaper stage. Switching modes resets the active oversampler's delay lines; a brief transient on the transition is expected behaviour.
Output Trim
A ±4 dB output gain control in 1 dB steps, applied post-saturation with parameter smoothing (click-free). Useful for gain-staging against other processors or compensating for overall EQ level changes.
Spectrum Analyser
Real-time FFT display running at ~30 Hz on the GUI thread:

2048-point Hann-windowed FFT, fed from a lock-free SpectrumFifo double-buffer on the audio thread
Log-frequency X axis: 20 Hz – 20 kHz
Asymmetric time smoothing: fast attack (α = 0.5), slow release (α = 0.88) for a meter-like feel
Correct normalisation at all sample rates — bin → frequency mapping uses the actual session sample rate (fixes a 44.1 kHz-only assumption present in many analyser implementations)
EQ curve overlay: the combined H(z) magnitude response of all active, non-bypassed bands is evaluated at 512 log-spaced frequencies and drawn in pale yellow over the spectrum. The overlay reads from the audio thread's smoothed gain values (via std::atomic<float>), so it tracks parameter changes in real time including gain ramp interpolation. The EQ curve uses an independent ±14 dB Y-axis centred in the display, leaving the spectrum dBFS axis unaffected


Global Controls Strip
The strip between the title bar and band panels contains:
ControlLED ColourFunctionBRITGreenBritish filter topology (shelf Q = 1.1, narrower peaks)HARDOrangeHard asymmetric clip mode (vs soft tanh)4x HQCyan4× oversampling (vs 2× standard)OUT TRIM—±4 dB output level trim slider

Technical Notes
Thread Architecture
ThreadResponsibilitiesAudio threadprocessBlock: EQ filters, saturation, output trim, FIFO pushMessage threadparameterChanged: update smoother targets, set parametersChanged flagGUI thread (timer)FFT computation, EQ curve evaluation, repaint at 30 Hz
The audio thread and GUI thread share data via:

SpectrumFifo — lock-free double-buffer (audio → GUI)
std::atomic<float> currentXxxGainDb — smoothed gain values for EQ curve (audio → GUI, relaxed ordering)
std::atomic<bool> parametersChanged — dirty flag (message → audio thread)

Parameter Smoothing
All gain, drive, and trim parameters are smoothed over 100 ms using juce::SmoothedValue<float, Linear>. This eliminates zipper noise from stepped AudioParameterChoice values and ramps mute transitions smoothly to 0 dB (flat), not silence.
Signal Path Order
Input
  └─► Low band biquad (if not bypassed)
  └─► Low-Mid band biquad (if not bypassed)
  └─► High-Mid band biquad (if not bypassed)
  └─► High band biquad (if not bypassed)
  └─► Upsample 2× or 4×
  └─► Asymmetric waveshaper
  └─► Downsample to native rate
  └─► Output trim (±4 dB)
  └─► Spectrum FIFO push (mono sum)
Output

Building
Requirements:

Windows 10/11 x64
Visual Studio 2022 or 2026 (C++17 or later) (Built in Visual Studio 2026)
JUCE 8.0.12  
Projucer (to regenerate project files if needed)

Steps:

Build AlphaEQ5.jucer in Projucer (Plugin Basics + DSP Module) 
Set your JUCE modules path
Save and open the generated Visual Studio solution
Build the AlphaEQ5_VST3 target in Debug or Release configuration
The .vst3 file will be output to Builds/VisualStudio2026/x64/Release/VST3/

Install:
Copy AlphaEQ5.vst3 to C:\Program Files\Common Files\VST3\ and rescan plugins in your DAW.

Platform Support
PlatformStatusWindows 10/11 x64Supported, tested 
 
Alpha EQ 6 Changelog 
This is just some processing tweaks as I thought I would see if I could implement some
better handling of the FFT function. 

AlphaEQ5 Changelog

Efficiency / analyzer update
Dual-resolution spectrum analyzer: a second, anti-aliased and 4x-decimated FFT branch (lowBandSpectrumFifo) now feeds the bass end of the display, giving ~4x finer bin spacing below ~500 Hz without growing the main FFT or slowing down the treble update rate. The two branches are crossfaded together between 150-500 Hz. The low-band branch also recomputes at ~10 Hz instead of 30 Hz, since bass energy and the decimated FIFO both change slowly - this saves GUI-thread work without any loss of usable detail.
Silence fast-path: after 4 consecutive blocks of digital silence (peak magnitude below -140 dBFS), processBlock skips the EQ biquads, oversampled saturation stage, and output trim entirely for that block, since a settled linear filter fed with zero input produces zero output. This only affects CPU during gaps/pauses; the moment real signal returns, full processing resumes on the very next block.
Deliberately NOT changed: the actual EQ/saturation signal path is untouched. Selectively skipping computation for specific frequency regions inside a live time-domain filter chain (as opposed to skipping-when-silent) risks audible discontinuities for a saving that isn't worth it on a 4-band EQ, so that idea was scoped to the analyzer only, where it's free of audio risk.

Additions since AlphaEQ4 
EQ curve overlay on spectrum display (reads smoothed audio-thread gain values)
Asymmetric transformer saturation model (tanh + x² even term for 2nd-harmonic content)
Hard clip saturation mode (asymmetric thresholds)
Output trim ±4 dB (post-saturation, smoothed)
British filter topology mode (shelf Q = 1.1, narrower peak bands)
4× HQ oversampling mode
Double-precision coefficient computation for all biquad bands
Fixed multi-instance saturation drive bug (was a static member; now per-instance)
Fixed hardcoded 44.1 kHz assumption in spectrum analyser bin mapping
Fixed specPath start-point artefact (spurious horizontal segment at low end)

