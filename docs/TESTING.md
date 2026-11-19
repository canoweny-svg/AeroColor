# Testing AeroGlow

There are three layers of tests, from fastest to most realistic:

| Layer | Binary | Needs | What it proves |
|---|---|---|---|
| 1. DSP core | `AeroGlowCoreTests` | nothing (no JUCE, no display) | the maths: harmonics, aliasing, latency, phase, gain, robustness, CPU |
| 2. Plug-in host | `AeroGlowHostTest <plugin>` | the built `.vst3` / `.component`, a display (Xvfb on Linux) | the shipped binary loads and behaves inside a real plug-in host |
| 3. DAW acceptance | your DAWs | a person | the checklist in §4 |

Full captured outputs: [`core-test-output.txt`](core-test-output.txt) and [`host-test-output.txt`](host-test-output.txt).

---

## 1. DSP core (`AeroGlowCoreTests`)

```bash
./build/AeroGlowCoreTests              # full suite + CPU benchmark
```
```bash
./build/AeroGlowCoreTests --quick      # CI subset (this is what ctest runs)
```
```bash
./build/AeroGlowCoreTests --wav out    # + listening files: sweep / pad / drums × dry / luminous / neon
```
```bash
./build/AeroGlowCoreTests --calibrate  # re-measure the auto make-up tables after changing any voicing
```
The exit code is the number of failed checks.

### 1.1 Function test: a sine through each mode (first values after the latency)
1 kHz at −6 dBFS, Saturation +12 dB, Mix 100 %, Auto Gain on. The input is shown against the output, latency-aligned:
```
  Luminous (latency 55 smp)            Neon     (latency 55 smp)
   n      in        out                 n      in        out
  2404  +0.25000  +0.21530            2404  +0.25000  +0.32102
  2407  +0.39668  +0.22922            2407  +0.39668  +0.36035
  2411  +0.49572  +0.23381  ← round   2411  +0.49572  +0.39761  ← still rising to the rail
```
Luminous flattens the positive peak early and softly (the asymmetric firm half). Neon stays linear until it hits the rail.

### 1.2 Static curves
```
      x     Luminous      Neon
  -2.00    -1.7405    -1.0000      Luminous: soft half keeps going (asymmetry → 2nd harmonic)
  -1.00    -0.9841    -0.9900
  +1.00    +0.5318    +0.9900      Neon: flat above the rail = hard clip
  +2.00    +0.5778    +1.0000
```

### 1.3 Harmonic signature (FFT, 32768 points, Blackman-Harris)
```
  mode      drive    H2      H3      H4      H5      H6      H7    | even    odd     THD  (dB re H1)
  Luminous     6    -24.2   -32.7   -58.6   -53.6   -70.6   -68.0 |  -24.2  -32.6  -23.6
  Luminous    18    -15.5   -23.4   -39.6   -37.2   -61.6   -48.4 |  -15.5  -23.2  -14.8
  Luminous    30    -12.5   -18.1   -23.9   -29.1   -35.5   -36.2 |  -12.2  -17.7  -11.1
  Neon         6   -150.0  -150.0  -150.0  -150.0  -150.0  -150.0 | -150.0 -150.0 -150.0
  Neon        18   -150.0   -20.4  -150.0   -32.9  -150.0   -41.8 | -150.0  -20.1  -20.1
  Neon        30   -150.0   -11.4  -150.0   -17.9  -150.0   -22.9 | -150.0  -10.3  -10.3
```
* **Luminous is even-dominant** (H2 > H3 at the default drive), and the orders fall off quickly: warm and smooth.
* **Neon is odd-only** at Bias 0 (even harmonics at the −150 dB floor), with a slow 1/n-style fall-off: edgy.
  At +6 dB it's still clean, because of the −6 dB headroom shift, which is the "lower default drive".
* Neon with Bias +100 % lifts the even harmonics from −150 dB to −18 dB.

### 1.4 Aliasing (inharmonic energy in 20 Hz–20 kHz, 4.99 kHz tone, +30 dB drive)
```
  Luminous   1x  -24.7 dB    2x  -49.7 dB    4x  -73.6 dB
  Neon       1x  -13.2 dB    2x  -21.9 dB    4x  -36.7 dB
```
Every oversampling step helps. Neon's remaining aliasing comes from the physics of hard-clipping a 5 kHz tone at +24 dB over
the rail; ordinary program material is far less demanding.

### 1.5 Latency, bypass and phase
```
  fs  44100 Hz: latency 55 samples (1.247 ms)        fs 192000 Hz: latency 47 (one OS stage fewer)
  [ ok ] bypass / dry / Luminous 2x / Luminous 4x / Neon 4x: burst centre exactly at 1000 + 55
  [ ok ] bypass output == input delayed by exactly 55 samples (max diff 0)
  [ ok ] Luminous   1000 Hz: wet−dry phase  0.93°, mix 50% vs mean -0.00 dB
  [ ok ] Neon      12000 Hz: wet−dry phase  2.22°, mix 50% vs mean -0.00 dB
```

### 1.6 Auto Gain (pink noise at −18 dBFS RMS, wet vs dry, Saturation 0…36 dB in 3 dB steps)
```
  Luminous  -0.0 -0.0 -0.1 -0.1 -0.1 -0.1 -0.1 -0.2 -0.2 -0.2 -0.2 -0.2 -0.2 dB
  Neon      +0.0 +0.1 +0.1 +0.1 +0.1 +0.1 +0.1 +0.1 +0.0 +0.0 +0.0 +0.0 -0.0 dB
```

### 1.7 Click-free switching, robustness and CPU
```
  [ ok ] max |Δ²| with 15 switches = 0.0001 vs steady-state max 0.0001 (ratio 1.06)
  [ ok ] 563 randomised blocks (1…2048 smp, NaN/Inf/+24 dBFS/DC input, out-of-range params): all output finite
  [ ok ] safety limiter holds −0.3 dBFS ceiling at +24 dB in / +36 dB drive / +24 dB out
  Luminous 2x  2.0 % of one core     Luminous 4x  2.1 %     Neon 4x  1.9 %     (stereo, 48 kHz)
```

---

## 2. Plug-in host test (`AeroGlowHostTest`)

```bash
./build/AeroGlowHostTest_artefacts/Release/AeroGlowHostTest "build/AeroGlow_artefacts/Release/VST3/AeroGlow Saturator.vst3"
```
On headless Linux, wrap the command in `xvfb-run -a`. On macOS you can also pass the `.component` to test the AU.

### Meter view: input vs output when switching modes
This is the same thing the IN/OUT meters show. Input is a 220 Hz sine at −12 dBFS, with default settings (Saturation +18 dB, Auto Gain on).
```
  setting                        IN pk    OUT pk    IN rms   OUT rms
  Bypass                       -12.01   -12.01   -15.02   -15.02  dBFS
  Luminous Warmth, mix 50%     -12.01   -11.82   -15.02   -15.19  dBFS
  Luminous Warmth, mix 100%    -12.01   -11.63   -15.02   -15.31  dBFS
  Neon Crunch, mix 50%         -12.01   -11.71   -15.02   -14.70  dBFS
  Neon Crunch, mix 100%        -12.01   -11.43   -15.02   -14.40  dBFS
```
Loudness stays within about 0.6 dB of bypass in every mode (Auto Gain), while the waveform changes. Peak goes up and
RMS stays put, which is the crest-factor signature of added harmonics. The screenshots in the README show the same
meters with a pad chord running.

Other checks: one plugin per binary; 13 automatable parameters; a host bypass parameter; latency at 44.1, 48, 96 and 192 kHz;
19,711 blocks at block sizes 1…4096 with random automation, all output finite; state saved and restored into a fresh instance
with 0 mismatches; a corrupt state blob ignored; the editor created and destroyed.

### Editor screenshots
```bash
./build/AeroGlowSnapshot docs/screenshots     # renders both modes at 2× with live meters
```

### Third-party validators (recommended before release)
```bash
pluginval --strictness-level 10 --validate "AeroGlow Saturator.vst3"
```
```bash
auval -v aufx AgSt Aero
```

---

## 3. Per-control verification
Put a 220 Hz sine at −12 dBFS (or the `pad` / `drums` files from `--wav`) on a track, open a spectrum analyser after
AeroGlow, and watch AeroGlow's own meters.

| Control | Action | Expected |
|---|---|---|
| Saturation | 0 → +36 dB, Mix 100 % | Harmonics grow smoothly with no level jump (Auto Gain on). Luminous: H2 leads. Neon: H3, H5, H7… |
| Mode | toggle during playback | Different character, no click, no timing shift (null against a copy with no PDC change) |
| Mix | 0 → 100 % | Smooth blend, no hollow/flanged phase at 50 % |
| Input | +12 dB | Hits the curve harder: more harmonics. Output level stays similar with Auto Gain on. |
| Output | −6 dB | OUT meter drops exactly 6 dB |
| Tone | −100 / +100 | Wet tilts dark / bright around 900 Hz (±6 dB at the extremes) |
| Bias | ±100 % | Luminous: 2nd harmonic up/down. Neon: even harmonics appear from 0 |
| High Cut | Neon, 20 k → 3 k | Fizz rolls off. The 20 k position reads "Off" |
| Low Protect | Neon, 30 → 250 Hz on bass | More of the low end stays clean/round, less "farty" sub |
| Auto Gain | off | Level now rises with Saturation (up to about +20 dB, so turn Output down first!) |
| Limiter | on, Output +24 dB | OUT meter peak-hold stops at −0.3 dB |
| Oversampling | Luminous, 2x → 4x at max drive on hi-hats | Slightly cleaner top end. Latency is unchanged |
| Bypass | toggle | 20 ms fade, output == input (bit-exact once settled), latency kept |
| Automation | draw fast ramps on every parameter | No zipper noise or clicks. Stays stable at 32-sample buffers |

---

## 4. DAW acceptance checklist
Do this once per DAW and OS (FL Studio, Studio One, Ableton Live 12, Cubase 12, Logic Pro 11 (AU), Pro Tools 2024 (AAX)).

- [ ] Plug-in scans and appears as **AeroGlow Audio › AeroGlow Saturator** (Effect / Distortion).
- [ ] Opens on mono and stereo tracks; the UI resizes from the corner.
- [ ] Latency compensation: duplicate a drum track, put AeroGlow at Mix 0 % on one and polarity-invert the other. They should **null**
      (the dry path is an exact delayed copy, apart from the 20 Hz HPF and crossover allpass, so expect a deep but not perfect null).
      With Bypass on, the null is **bit-exact**.
- [ ] Automate Mode, Oversampling, Saturation and Mix with fast curves: no clicks, no PDC change.
- [ ] Save the project, close, reopen: every setting and the preset name are restored.
- [ ] Save a preset, load it on another instance, and use the `<` `>` buttons to cycle presets.
- [ ] Offline bounce / freeze matches realtime playback.
- [ ] 44.1, 48, 96 and 192 kHz sessions: no glitches. CPU < 3 % per instance on a mid-range machine.
- [ ] 50+ instances, then remove them all: no crash or leak (Debug build: the JUCE leak detector stays quiet).
