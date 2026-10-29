# HexedVoiceMod V1.0

A real-time voice changer for the **SA-MP Nezuko Android client**: voice presets, pitch and formant
control, Auto-Tune with chord voices, classic **and AI** noise suppression, a full dynamics section
(gate, compressor, de-esser, limiter), an effect rack, hear-myself monitoring and a one-tap
**V1 / legacy A-B comparison**.

It is an AML mod: the native library `libHexedVoiceMod.so` (armeabi-v7a) hooks libBASS's
`BASS_RecordStart`, attaches a DSP to the microphone channel and processes your voice in place. A
MoonLoader menu script (`HexedVoiceMod_Menu.lua`) drives it through the `vfx_*` C API.

## Build status

| Workflow | Status |
|----------|--------|
| `build-hexedvoicemod` (host tests + ndk-build armeabi-v7a) | ![build](https://img.shields.io/badge/build-see_Actions_tab-blue) *(replace with the real badge once the repository URL is known - do not invent one)* |

## Features

- **Voice presets** - 43 one-tap voices (clean / high / deep / filter / space / FX / tune categories),
  favorites, search, replaceable PNG icons.
- **Pitch and formant controls** - ±12 semitones, formant 0.5-2.0, formant preservation, dry/wet mix.
- **Auto-Tune and chords** - YIN pitch tracking (about 2 cents accuracy in the host tests), 8 scales,
  automatic key detection, retune time 0-400 ms, correction amount, optional **vibrato preservation**
  and a pitch-analysis **lookahead** (0-60 ms) that aligns the correction with your voice, 9 chord
  styles with scale-aware harmony and optional formant-preserving (PSOLA) chord voices.
- **Classic and AI noise suppression** - spectral Wiener filter with minimum-statistics noise
  tracking and a keyboard-click guard, or RNNoise ("AI"), cross-faded, selectable per menu.
- **Dynamics** - reworked noise gate (hysteresis, hold, soft expansion, 5 ms lookahead), feed-forward
  compressor with soft knee and automatic make-up, split-band de-esser, true-peak lookahead limiter
  with a configurable ceiling, plus mandatory input conditioning (DC blocker + 75 Hz high-pass).
- **Effect rack** - high/low-pass, bass/treble shelves, anti-aliased drive, bit-crusher, tremolo,
  chorus/flanger/vibrato, echo with damping, modulated 8-line FDN reverb with predelay and damping,
  robot ring modulator, 20-band vocoder whose carrier follows your pitch or snaps to the key.
- **Hear-myself monitoring** - push-stream monitor with a latency servo (targets ~50 ms, adjusts the
  playback rate by at most ±1 % instead of dropping audio).
- **V1 / legacy comparison** - the Dynamics page button "Compare: V1 / v8" (or `/hvm ab`) fades over
  to the original engine bit-exactly, so you can always hear exactly what changed.

## Requirements

- SA-MP Nezuko Android client (armeabi-v7a, 32-bit; records 48 kHz mono 16-bit).
- AML / Modloader (AndroidModLoader) with MoonLoader/MonetLoader and **mimgui** for the menu.
- The menu also uses the bundled `menu/lib/fAwesome6_solid.lua` if present (falls back to PNG icons).
- No root required; the mod writes only its config/log on `/storage/emulated/0`.

## Installation

1. Download the build artifact `HexedVoiceMod` from a finished GitHub Actions run (or build locally)
   and put **`libHexedVoiceMod.so`** into the Nezuko AML mods folder.
   - If an old `libvoicefx.so` is still in the mods folder, **delete it** - the mod refuses to install
     its hook while the old library is loaded and shows a red warning instead of processing audio twice.
2. Copy the contents of `menu/` into your MoonLoader folder (`HexedVoiceMod_Menu.lua`, `lib/`,
   `HEXED/Icons/VoiceFX/`, `HEXED/Fonts/` - the icon/font paths are unchanged from the previous mod
   name, existing assets keep working without moving anything).
3. Start the game, then run **`/hvm icons`** once so the menu (re)loads its icons.
4. Open the menu with `/hvm`, pick a voice, hold your mic.

Configuration and logs: `/storage/emulated/0/HexedVoiceMod.cfg` (imported from the old
`/storage/emulated/0/voicefx.cfg` on first run) and `/storage/emulated/0/HexedVoiceMod_log.txt`.
The menu stores `config/HexedVoiceMod_settings.cfg` and `config/HexedVoiceMod_favorites.txt`
(one-time import from the `VoiceFX_*` files; the originals are never deleted).

## Commands

All of these open/drive the mod; `/hvm` and `/hexedvoice` are the canonical names, `/vfx` and
`/voicefx` are kept as aliases:

| Command | Effect |
|---------|--------|
| `/hvm` (or `/hexedvoice`, `/vfx`, `/voicefx`) | toggle the menu |
| `/hvm on` / `/hvm off` | voice changer on/off |
| `/hvm pitch <-12..12>` | pitch shift in semitones |
| `/hvm formant <0.5..2>` | formant shift |
| `/hvm gain <dB>` | mic gain |
| `/hvm gate <-80..-20>` | noise gate threshold (-80 = off) |
| `/hvm mix <0..1>` | dry/wet mix |
| `/hvm hear on|off`, `/hvm hearvol <0..2>` | hear-myself monitor + volume (headphones only!) |
| `/hvm preserve on|off`, `/hvm robot on|off`, `/hvm robothz <Hz>` | formant preserve, robot ring-mod |
| `/hvm preset <name>` | apply a preset by name |
| `/hvm tune on|off`, `/hvm tune key <C..B|auto>`, `/hvm tune scale <0-7|name>` | Auto-Tune |
| `/hvm chord <0-8|name>` | chord voice style |
| `/hvm noise on|off|<0..1>` | noise suppression on/off/strength |
| `/hvm ab` | **toggle the V1 / legacy comparison** (never saved) |
| `/hvm set <name> <value>` | set any effect parameter by its config name |
| `/hvm quality auto|high|lite` | pitch engine (auto falls back to Lite on CPU overload) |
| `/hvm status` | one-screen status dump in chat |
| `/hvm hook` | retry the BASS hook |
| `/hvm save`, `/hvm icons` | save settings now / reload icons |

## Menu pages

- **Voices** - searchable preset grid with categories and favorites; side panel with gain, pitch,
  formant, mix, gate and quick Noise/Tune buttons.
- **Status** - library/hook state, mic sessions, chunk/rate, pitch engine + total latency, CPU
  average/peak, **per-stage CPU timing**, underruns/gap resets, format, monitor state with its
  measured queue latency, mic levels, and a red "old library detected" warning when applicable.
- **Favorites** - your starred voices.
- **Effects** - Clean (noise engine Classic/AI, strength, click guard), Dynamics (compare switch,
  gate hold/hysteresis/depth, compressor, de-esser, limiter ceiling), Voice (engine choice, mix,
  gain, gate), Tune (key/scale/retune/amount/lookahead/vibrato), Chords (style, level, width,
  PSOLA formant voices), Tone, Modulate, Crunch, Space (echo + reverb with damping/predelay),
  Robot (robot + vocoder with carrier mode).
- **Settings** - menu size, icon folder, config/log paths, save/close.

## Signal chain

```
Input -> DC/HPF -> Noise Suppression -> Gate -> Compressor -> Pitch/Auto-Tune/Chords -> De-esser
      -> FX Rack -> Limiter -> BASS
```

The pitch stage feeds the engine a delayed copy of the input when Tune lookahead is set, so the
pitch tracker runs ahead of the audio it corrects; the dry path is delayed by the same amount and
stays sample-aligned. Added latency in V1 mode: 3 ms (limiter, always) + 5 ms (gate, while on)
+ tune lookahead (0-60 ms, while the pitch engine runs) + 10 ms (AI noise suppression, while on).
All of it is included in the Status page "Pitch engine / latency" figure.

## Performance tips

- **Pitch engine** (Effects > Voice): *Auto* uses RubberBand and drops to *Lite* automatically when
  the CPU cannot keep up (probe-based recovery tries the high engine again every 20 s). *Lite* is
  the cheapest engine; *High* forces RubberBand.
- Watch the **CPU and dropout meters** on the Status page; the per-stage timing shows which stage
  eats the budget (engine = pitch, out = effects/limiter).
- **Managing latency**: the biggest contributors are the pitch engine and AI noise suppression. If
  you need low latency: Lite engine, noise Classic, Tune lookahead 0.
- **Overload troubleshooting**: underruns rising + auto-switched engine = close background apps or
  lower the game resolution; the limiter keeps the output click-free, but the mic may stutter.

## Building with GitHub Actions

1. Push this project to a repository (keep the `jni/`, `menu/`, `test/`, `tools/` and `.github/`
   folders) and open the **Actions** tab - `build-hexedvoicemod` runs on every push or manually
   via *Run workflow*.
2. The `test` job runs the prohibited-term check and the host engine tests in **both**
   configurations (RubberBand stand-in and Lite-only).
3. The `build` job downloads **RubberBand 4.0.0** and **RNNoise 0.2** itself (they are *not*
   stored in this repository), runs `ndk-build` for **armeabi-v7a**, verifies the exported symbols
   with `readelf`/`nm`, and uploads the artifact **`HexedVoiceMod`** containing
   `libHexedVoiceMod.so`.
4. Download the artifact from the finished run's Artifacts section.

## Local build notes

- Requirements: Android NDK (r25+ recommended) with `ndk-build`, plus a C++17 toolchain for the
  host tests. The `jni/mod/` headers are the AML mod-loader interface and compile for ARM only.
- Host tests (engine only, runs anywhere):
  `g++ -std=c++17 -O2 -ffast-math -fno-finite-math-only -I test/fake test/host_test.cpp -o host_test`
  (add `-DVFX_NO_RUBBERBAND` for the Lite-only configuration). The RubberBand header used here is a
  pure-delay **stand-in** - it verifies latency/plumbing exactly, not pitch quality.
- Without `jni/rubberband-src/` and/or `jni/rnnoise-src/` the build still succeeds (Lite pitch
  engine / classic noise engine only) - both dependencies are optional at build time.
- Per-stage CPU profiling can be compiled out with `-DVFX_NO_PROFILING`.

## Configuration and log files

| File | Purpose |
|------|---------|
| `/storage/emulated/0/HexedVoiceMod.cfg` | native parameters (imported from `voicefx.cfg` on first run; the old file is never deleted) |
| `/storage/emulated/0/HexedVoiceMod_log.txt` | native log - send this when the hook fails |
| `config/HexedVoiceMod_settings.cfg` | menu state (one-time import from `VoiceFX_settings.cfg`) |
| `config/HexedVoiceMod_favorites.txt` | favorite voices (one-time import from `VoiceFX_favorites.txt`) |

## Upgrading from the VoiceFX v8 beta

1. Delete the old `libvoicefx.so` from the mods folder, then install `libHexedVoiceMod.so`.
2. Keep your existing `HEXED/Icons/VoiceFX/` and `HEXED/Fonts/` folders - nothing moved.
3. Start the game: the native config (`voicefx.cfg`) and the menu settings/favorites
   (`VoiceFX_settings.cfg`, `VoiceFX_favorites.txt`) are imported once automatically; the old
   files are preserved untouched.
4. The old commands `/vfx` and `/voicefx` still work; the new names are `/hvm` and `/hexedvoice`.
   The chat prefix is now `[HexedVoiceMod]`.
5. If the menu says "Old libvoicefx.so found in the mods folder - delete it", the old library is
   still loaded (both mods installed): remove the old file and restart the game.
6. If the menu says "Update libHexedVoiceMod.so", the library on the device is older than the menu
   (library API must be >= 9): update the .so.
7. Use `Compare: V1 / v8` (or `/hvm ab`) to A-B the new engine against the old one - the original
   processing is preserved bit-exactly behind the comparison switch.

## Troubleshooting

- **Hook failure** - Status > BASS hook shows FAILED: send `/storage/emulated/0/HexedVoiceMod_log.txt`.
- **"Old libvoicefx.so found in the mods folder - delete it"** - both libraries are installed; the
  new one will not double-process your mic. Delete the old file and restart.
- **Missing icons** - run `/hvm icons`, reopen the menu; check the icons are in
  `<moonloader>/HEXED/Icons/VoiceFX/`.
- **Menu does not open** - mimgui/MoonLoader not installed, or the menu script is missing from
  `<moonloader>/`; the log prints the menu errors.
- **"Update libHexedVoiceMod.so"** - the loaded library's API is older than the menu needs
  (menu requires library API 9). Update the .so artifact.
- **Missing optional dependencies** - without RubberBand the pitch engine falls back to Lite
  (no formant control); without RNNoise the AI noise engine falls back to Classic. The Status
  page shows which engines are compiled in.

## Known limitations

- Mono 16-bit 48 kHz only (the Nezuko recording format); other formats pass through untouched.
- Auto-Tune needs a clear single voice - no tuning of speech over heavy noise; enable noise
  suppression first. Very fast melismas or growls can confuse the tracker for a moment.
- Chord voices have no formant control on the LitePitch path (the PSOLA path does); the PSOLA
  voice can sit up to about 1 ms behind the lead voice.
- The pitch path (any pitch change or Auto-Tune) adds engine latency; only the untouched direct
  path is zero-latency.
- The hear-myself monitor plays only while your mic is actively recording (BASS limitation).
- RubberBand and RNNoise are downloaded by CI - a build without network access runs with the
  built-in Lite engine and the classic noise engine.

## Running the host tests

```
g++ -std=c++17 -O2 -ffast-math -fno-finite-math-only -I test/fake test/host_test.cpp -o host_test_rb
./host_test_rb                      # RubberBand stand-in mode
g++ -std=c++17 -O2 -ffast-math -fno-finite-math-only -I test/fake -DVFX_NO_RUBBERBAND \
    test/host_test.cpp -o host_test_lite
./host_test_lite                    # Lite-only mode
```

All 18 test groups cover the plumbing (bit-exact legacy passthrough, sample-exact alignment),
every DSP stage, the V1/legacy comparison, chunk-size independence (including 1-sample chunks),
NaN/robustness, and the Phase 3/5 controllers via deterministic simulations.

## Credits and licensing

- HexedVoiceMod - HEXED Studio / brruham. License: **GPL-2.0-or-later** (see `LICENSE`).
- [RubberBand](https://github.com/breakfastquay/rubberband) by Chris Cannam / breakfastquay -
  GPL-2.0-or-later (fetched by CI, not stored here).
- [RNNoise](https://github.com/xiph/rnnoise) by Jean-Marc Valin / Xiph.Org - BSD-3-Clause
  (fetched by CI, not stored here).
- [AndroidModLoader (AML)](https://github.com/RusJJ/AndroidModLoader.git) by RusJJ - the mod
  loader interface in `jni/mod/`.
- [stb_sprintf](https://github.com/nothings/stb) by Jeff Roberts / Sean Barrett - public domain
  or MIT.
- Voicemod's "Magic Chords" demo was analysed by ear for the chord-voice design; this is an
  independent recreation, not their code, and will not sound identical.
