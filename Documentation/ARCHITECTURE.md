# Architecture

```
in ─► input gain ─► [OS] 12AX7 stage [/OS] ─► gain residual ─► NAM capture blend (per channel, xfade)
   ─► make-up · Volume ─► tone residual EQ ─► [OS] harmonic engine ─► power amp (Master) [/OS]
   ─► dual-IR cabinet ─► output gain ─► out (stereo)
```

| Module | Responsibility |
|---|---|
| `NAMEngine` | `NamModel`: official NeuralAmpModelerCore (`nam::get_dsp`) wrapped for real time; Kaiser-windowed sinc resampler when host rate ≠ training rate; fixed reported latency. |
| `NAMMetadata` | Reads `.nam` metadata without building the network; parses knob/channel tokens; identity keys; identity guesses (never trusted without confirmation). |
| `NAMLoader` | Collects `.nam` files from drops/folders; loads metadata + network. |
| `ProfileAnalyzer` | Offline measurement of each capture (latency, polarity, loudness, compression, THD, even/odd, 5-band balance) and knob estimation from those features. |
| `ProfileInterpolation` | `ProfileInterpolator` (8-D truncated Shepard weights, continuous, exact at captures, covered-dimension aware) and `CaptureBlender` (parallel output-domain blending with loudness, alignment, polarity, glides, hold). |
| `AmplifierEngine` | `AmpDefinition` (persistent model), `AmpRuntime` (playable, built off-thread), `ToneResidual` (controls not covered by captures). |
| `ChannelManager` | Channel layout from evidence only; move/rename/add/remove channels; re-estimation. |
| `TubeCircuitDSP` | Koren 12AX7 load-line stage; push-pull pentode power amp with sag, transformer, NFB, speaker. |
| `HarmonicEngine` | Chebyshev harmonic generator with touch and attack preservation. |
| `IRLoader` / `Convolution` | IR slot (normalisation, onset, gain/pan/polarity/delay) and dual cabinet (blend, alignment). |
| `PresetManager` | Amp library on disk, presets, cab presets. |
| `AudioEngine` | `SignalChain`, oversampling, smoothing, lock-free runtime hand-over (`RealtimeSwap`). |
| `Plugin` / `GUI` | JUCE processor (VST3/AU/Standalone) and the editor. |

## Real-time rules
- The audio thread never allocates, locks or frees. New amp runtimes are published through `RealtimeSwap`; replaced runtimes are crossfaded out and deleted on the message thread.
- NAM networks run at their training rate; resampling latency is constant per runtime and reported to the host.
- All gains, drives and EQ moves are smoothed; channel, amp and preset changes crossfade.

## How a knob becomes sound
1. Per block, the interpolator computes capture weights for the current knobs over the *covered* controls (≥2 distinct captured values).
2. The blender glides each capture's weight toward its target (shared one-pole, so weights always sum to 1) and runs every capture with non-zero weight (or still in its hold time).
3. The difference between each knob and the blend's effective setting goes to the residual stages: input drive for Gain, tone-stack-voiced shelves/peaks for Bass/Middle/Treble, and Presence/Resonance either as EQ or inside the power amp's feedback loop.
