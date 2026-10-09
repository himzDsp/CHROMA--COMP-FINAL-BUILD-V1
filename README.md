# Chroma Comp

A single, transparent digital compressor plugin (VST3 / AU / Standalone) built with JUCE 9.

* Threshold, Ratio (step knob: snaps to whole ratios 1.0:1, 2.0:1, 3.0:1 ... hold **Ctrl** while dragging for fine tuning), Knee, Attack, Release, Makeup, Mix, optional 1.5 ms Lookahead, Bypass
* **Oversampling 1x / 2x / 4x**: dropdown in the footer (right of the credit text), default 2x. 1x = no oversampling and no extra latency (the cheapest and the most transparent setting: the audio is never filtered); 2x / 4x run the compressor at 2 / 4 times the host rate with linear-phase filters (a little more latency, reported to the host). Stored in the plugin state, not in presets. Sessions saved before the selector existed open at 4x so they sound the same
* **Reset** button (footer): puts every compressor setting and every adaptive-engine setting back to the start values (the built-in *Init* preset: no compression, adaptive engine off). Input / Output trim, Bypass, Oversampling and the dark-mode switch are not touched
* Log-domain, stereo-linked detector with smooth decoupled peak envelope (no overshoot, no zipper noise)
* **Adaptive analyze**: listens to >= 5 s of your audio (play your track, press the button) and sets Threshold, Ratio, Knee, Attack, Release and Makeup from what it hears, steered by *Feel* (Tight / Subtle / Balance / Glue), *Intensity* (Low / Mid / High) and *Transients* (Preserve / Unpreserved). It measures level distribution, crest factor, transient rise time, tempo and low-end weight.
* Separate **Input / Output** panel (whole-plugin gain trim, -24 to +24 dB: input before the compressor and analyser, output after the compressor; not stored in presets, unity while bypassed). The bottom card is the adaptive engine only, with a small on/off button at its top left
* **Favourite star** (header, right of DEL): lights up when the current preset is a favourite; click it to add / remove the preset. Favourites are listed in their own *Favourites* group (a shortcut copy: the preset also stays in its Factory / User group). Each favourite is a copy of the preset in the `Favourites` folder, so they are shared by every instance and project
* Preset list order: **Factory** (built-in **Init** + the `Factory` folder), then **Favourites**, then **User**
* **Presets are plain JSON files** in one folder: `C:\Users\<you>\Chroma-Comp\Presets\` (macOS / Linux: `~/Chroma-Comp/Presets/`) with three sub-folders, `Factory\` (read-only, holds `Init.json`, drop your own `.json` files in), `Favourites\` (a copy of every preset you starred) and `User\` (what SAVE writes). The file name is the preset name. Presets saved by older versions (`.ccpreset`, `Favourites.txt`) are converted once on first run. See `FactoryPresets/README.txt` for the file format
* 600 x 392 compact, resizable GUI - white surface, neon-blue accents, 3D knobs
* **Dark mode**: small DARK switch in the footer, left of the version text. Background becomes `#1A1A1A` and all dark text becomes `#FFFFFF`; the grey labels and the neon colours are unchanged. The choice is stored in the plugin state (`darkMode`), so it is remembered per instance and per project
* v-sync-driven waveform display (4 s scrolling min/max envelope of input and/or output) with a gain-reduction meter, a BS.1770 momentary loudness (LUFS) meter and IN / OUT buttons; the display and meters cost 0 CPU while the editor is closed

## Sound and CPU notes

* **Transparent by construction**: with no gain reduction and 0 dB makeup the output is bit-identical to the input, from the very first sample (the Init preset, Bypass and Mix 0 % are exact pass-throughs). The compressor starts on the real settings after load / play / an oversampling change, instead of ramping from stale defaults.
* **No bass distortion**: the detector holds each gain-reduction peak for 12 ms before the release can start (`kRippleHoldMs` in `CompressorCore.h`), so the gain no longer follows the waveform of low notes. Measured on a 50 Hz - 1 kHz sine with attack 0.1 ms / release 5 ms: distortion fell from about -20 dB to below -140 dB. The cost is that a release starts 12 ms later than its nominal time.
* **Light on the CPU**: about 0.17 % of one core per stereo instance at 48 kHz while compressing (about 0.04 % when idle, below threshold or bypassed), measured on a desktop CPU with the standalone core, at 1x; 2x / 4x add the oversampling filters on top. The transient detector, the auto-release average and the hold window only run when they are in use, and an idle / bypassed instance skips the whole detector chain (bit-identical to running it).
* **Oversampling** 2x / 4x filter the audio with a linear-phase FIR (more latency and noticeably more CPU than 1x). 2x is the default; switch to 1x for the lowest CPU, no added latency and a signal path that is never filtered.
* **Tests**: `Tests/CoreTests.cpp` checks the bit-exact unity / bypass, the bass distortion, the gain-reduction curve and robustness (random settings and sample rates, no NaN). Run it with `cmake -B build-tests -DCHROMA_BUILD_TESTS=ON && cmake --build build-tests --target ChromaCompCoreTests && ctest --test-dir build-tests`.

## Get the plugin without installing anything (GitHub Actions)

1. Create a new GitHub repository and push this folder to it (or drag-and-drop the files on github.com).
2. Open the **Actions** tab - the *Build Chroma Comp* workflow starts automatically (~6-10 min).
3. Open the finished run, scroll to **Artifacts**, download `ChromaComp-Windows`, `-macOS` or `-Linux`.
4. Copy the `.vst3` to
   * Windows: `C:\Program Files\Common Files\VST3`
   * macOS: `~/Library/Audio/Plug-Ins/VST3` (and `.component` to `~/Library/Audio/Plug-Ins/Components`)
   * Linux: `~/.vst3`

Publish a downloadable release: `git tag v1.0.0 && git push --tags` - the workflow attaches zips to a GitHub Release.

macOS note: CI builds are not code-signed. If Gatekeeper blocks the plugin run
`xattr -dr com.apple.quarantine "Chroma Comp.vst3"` or sign/notarize it with your Apple Developer ID.

## Build locally

```
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel
```
Needs CMake >= 3.22 and a C++17 compiler. JUCE is downloaded automatically (tag set by `JUCE_GIT_TAG`).

## Licence

JUCE is dual-licensed (AGPLv3 / commercial). If you distribute this plugin you must either publish its
source under a compatible open-source licence or hold a JUCE commercial licence. VST is a trademark of Steinberg.
