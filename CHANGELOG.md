# HexedVoiceMod - Changelog

## V1.0 (library API 9) - release

**Summary.** HexedVoiceMod V1.0 is the first stable release of the voice changer, renamed from the
VoiceFX v8 beta with full configuration migration and double-load protection. V1.0 adds mandatory
input conditioning (DC blocker + 75 Hz high-pass), a reworked dynamics section (hysteresis gate with
hold and lookahead, feed-forward compressor, split-band de-esser, true-peak lookahead limiter), an
Auto-Tune upgrade (pitch-analysis lookahead, adaptive YIN window, vibrato preservation), a
servo-controlled hear-myself monitor (50 ms target, ±1 % rate trimming instead of drops), optional
RNNoise AI noise suppression, formant-preserving PSOLA chord voices, an anti-aliased drive, a
modulated 8-line FDN reverb with predelay and damping, echo damping, a 20-band vocoder whose carrier
follows the voice or snaps to the key, and lookup-table oscillators with per-stage CPU profiling and
automatic RubberBand recovery. The original v8 processing is preserved bit-exactly behind the
V1 / legacy comparison switch (`/hvm ab`, "Compare: V1 / v8" button) - not approximated.

**Expected additional latency vs v8 (all included in the reported `vfx_statf(2)`):**
limiter 3 ms (V1, always); gate 5 ms (while on); tune lookahead 0-60 ms (while the pitch engine
runs, default 0 = legacy timing); RNNoise 10 ms (AI engine only). CPU cost (estimates, host-measured
machine, not a phone): conditioning < 0.5 %, gate/compressor/de-esser < 1 % combined, limiter
~0.3 %, RNNoise model (device: unmeasured), PSOLA voices ~2x a Lite voice while blended in.
Marked as estimates where not measured on the target device.

**New commands:** `/hvm`, `/hexedvoice` (plus the kept `/vfx`, `/voicefx` aliases), `/hvm ab`.
**New parameters (ids appended, 0..42 unchanged):** 43 legacy_mode (never saved), 44 gate_hold_ms,
45 gate_range_db, 46 gate_hyst_db, 47 comp_on, 48 comp_thresh_db, 49 comp_ratio, 50 comp_attack_ms,
51 comp_release_ms, 52 comp_makeup_db, 53 comp_auto_mkup, 54 deess_on, 55 deess_amt, 56 deess_freq,
57 limit_ceil_db, 58 tune_lookahead_ms, 59 tune_keep_vibrato, 60 ns_engine, 61 chord_formant,
62 rev_predelay_ms, 63 rev_damp, 64 echo_damp, 65 voc_carrier.
**New stats:** `vfx_stat(17)` = product version * 100 (= 100), `vfx_stat(18)` = old-library flag,
`vfx_statf(9)` = monitor queue ms, `vfx_statf(10)` = gate gain, `vfx_statf(11..16)` = per-stage
CPU ms (avg/peak).

### Not verified (requires the real Nezuko client / Android NDK - all host tests ran on a PC)

- BASS hook behaviour on the target Nezuko build (installed via AML Hook or Dobby, verified only
  by symbol-level checks and the syntax pass against stand-in headers).
- The double-load guard's `dlopen("libvoicefx.so", RTLD_NOW | RTLD_NOLOAD)` behaviour on Android's
  linker (logic follows POSIX semantics; not executed on a device).
- Actual ndk-build / GitHub Actions Android build and the exported-symbol checks (no NDK in the
  development environment; the workflow steps mirror the previous, working workflow).
- RNNoise compatibility on armeabi-v7a: the C sources are fetched by CI and compiled by Android.mk,
  but no Android binary was produced here; the DSP-side frame assembly is verified with a
  deterministic fake processor instead of the AI model.
- Real-device CPU load, real latency figures, and per-stage ms on ARM (host numbers only).
- Menu layout across different screen sizes / DPI (the reference-space layout logic is unchanged
  from the previous release).
- Hear-myself queue stability on a real device, the exact flush semantics of
  `BASS_ChannelPlay(h, 1)` for push streams, and the audibility of the ±1 % rate trims.
- Actual RubberBand overload recovery on a phone (the controller is unit-tested; the probe uses the
  real RubberBand object only on device).
- Vocoder/vocoder-carrier, PSOLA and FDN reverb sound quality by ear (numerically verified only).
- No user test results are included; none were reported during development.

### Phase 0 - release rename and migration (no DSP / sound changes)
- Renamed the product to **HexedVoiceMod V1.0**: native module `HexedVoiceMod` (`libHexedVoiceMod.so`),
  AML metadata `MYMOD(com.hexed.voicemod, HexedVoiceMod, 1.0.0, brruham)`, logger tag `HexedVoiceMod`,
  menu script `menu/HexedVoiceMod_Menu.lua` (`script_name('HexedVoiceMod')`, `script_version('1.0')`).
- Library API version is now **9** (`vfx_stat(8) == 9`), separate from the product version
  (`vfx_stat(17) == 100` = 1.0.0 x 100). All `vfx_*` exports, parameter ids `0..10` and the `vc_api`
  table are unchanged, so old menus and integrations keep working.
- Double-load protection: if the old `libvoicefx.so` is already loaded in the process (checked with
  `dlopen(..., RTLD_NOW | RTLD_NOLOAD)`, which never loads a second copy), the new library does not
  install its BASS hook, logs a warning, sets `vfx_stat(18) == 1` and the Status page shows a red
  "Old libvoicefx.so found in the mods folder - delete it" warning.
- Native configuration migration: settings are now stored in `/storage/emulated/0/HexedVoiceMod.cfg`,
  the log in `/storage/emulated/0/HexedVoiceMod_log.txt`. If the new config does not exist, the old
  `/storage/emulated/0/voicefx.cfg` is imported once (temp-file + rename, robust against interrupted
  copies). The old file is never deleted or modified.
- Menu-side migration: `config/VoiceFX_settings.cfg` and `config/VoiceFX_favorites.txt` are copied to
  `config/HexedVoiceMod_settings.cfg` / `config/HexedVoiceMod_favorites.txt` once, only when the
  destination does not exist yet. Originals are preserved.
- Menu loader: tries `libHexedVoiceMod.so` first, then scans the loaded libraries for
  `hexedvoicemod` / `voicefx` (case-insensitive). The `voicefx` fallback is required for detection and
  compatibility. Requires library API >= 9; an older library shows "Update libHexedVoiceMod.so".
- New chat commands: `/hvm` and `/hexedvoice` (aliases of `/vfx` and `/voicefx`, which are kept).
  Chat prefix is now `[HexedVoiceMod]`.
- GitHub Actions workflow renamed to `build-hexedvoicemod`, artifact `HexedVoiceMod`, all paths and
  `readelf` / `nm` validation steps updated to `libHexedVoiceMod.so`; host tests now run in both
  configurations (RubberBand stand-in and Lite-only) plus a prohibited-term check
  (`tools/check_terms.sh`: "beta" / "VoiceFX v8" may appear only in CHANGELOG.md and the upgrade
  section of README.md).
- Icon and font asset paths are unchanged (`HEXED/Icons/VoiceFX/`, `HEXED/Fonts/`) - existing assets
  keep working without moving anything.

### Phase 1 - signal conditioning and dynamics
- **Mandatory input conditioning** (always on in V1 mode, before the noise suppressor and tuner): DC blocker
  (one-pole, R = 0.995) + fixed 2nd-order 75 Hz high-pass, with a ~28 ms fade-in after every (re)start so
  enabling never clicks. Expected CPU cost: about 2 biquad-style ops per sample (< 0.5% of a phone core).
- **Reworked gate** (ids 44 `gate_hold_ms` 0-150 def 20, 45 `gate_range_db` 0-60 def 24, 46 `gate_hyst_db`
  0-18 def 6): separate open/close thresholds (hysteresis), hold time, smoothed attack (1.5 ms) / release
  (60 ms), soft 2:1 downward expansion with a depth floor instead of a hard mute, and ~5 ms of lookahead
  (audio delayed, gain leads it) so word onsets survive. Adds 5 ms latency only while the gate is on
  (`gate_db > -79.5`). The original gate is restored bit-exactly by legacy mode.
- **Compressor** (ids 47-53: `comp_on` def off, `comp_thresh_db` -60..0 def -24, `comp_ratio` 1-20 def 3,
  `comp_attack_ms` 0.5-100 def 10, `comp_release_ms` 10-1000 def 120, `comp_makeup_db` 0-24 def 0,
  `comp_auto_mkup` def on): feed-forward, RMS-style detector, 6 dB soft knee, gain curve evaluated at a
  32-sample control rate (no per-sample transcendental calls), automatic make-up derived from the threshold.
  Placed between the gate and the pitch processing. Off by default = transparent.
- **De-esser** (ids 54-56: `deess_on` def off, `deess_amt` 0-1 def 0.5, `deess_freq` 3000-9000 def 5500):
  split-band, the sibilance band (about 4-9 kHz at the default) is band-passed and its content subtracted,
  up to `deess_amt` * 15 dB, fast attack / ~50 ms release. Amount 0 or `deess_on` 0 is exactly transparent
  (identity, not an approximation). Placed after pitch/chords, before the effect rack.
- **Lookahead limiter** (id 57 `limit_ceil_db` -12..0 def -0.5): replaces the end-of-chain tanh soft knee in
  V1 mode. ~3 ms true-peak lookahead (audio delayed 144 samples at 48 kHz, gain computed from the input so
  the reduction is in place before the peak), fast attack (0.3 ms), smooth release (80 ms). The delay is
  included in `vfx_statf(2)` latency; the original tanh knee remains in legacy mode. Adds 3 ms latency and
  about 0.3% CPU in V1 mode.
- **V1 / legacy comparison**: id 43 `legacy_mode` (0 = V1 processing, 1 = original engine), Dynamics-page
  button "Compare: V1 / v8" and command `/hvm ab`. The comparison state is never saved to any config file
  (native or menu). Switching fades out over 12 ms, flips the mode between chunks (so the whole next chunk
  already runs the new mode - no timeline jump), and fades back in over 12 ms; the gate/limiter delay lines
  stay primed in legacy mode so switching back is seamless. The fade-through-dip transition is bounded,
  real-time safe and click-free (verified by test); it is not a parallel crossfade of both paths.
- New Status/debug figure: `vfx_statf(10)` = current V1 gate gain (0..1).
- Total added latency in V1 mode with defaults: 3 ms (limiter) + 5 ms while the gate is on. With all new
  stages off and the gate off, V1 differs from legacy only by the conditioning and the 3 ms limiter delay
  (verified: best alignment 144 samples ±4, correlation 0.79-0.86 on speech-shaped test signals, level
  within 0.7 dB; in legacy mode the passthrough is bit-exact).
- Tests: new [1b] block (DC blocker, 75 Hz HPF response, gate hysteresis/hold, compressor curve, de-esser
  band reduction + transparency, limiter ceiling 29387 <= -1 dBFS +1% tol, V1-vs-legacy offset, chunk-size
  independence including 1-sample chunks, extreme/NaN parameter values, startup and live-toggle click
  checks); [1]/[2]/[3]/[7]/[12] updated for the conditioning + limiter delay. All tests pass in both the
  RubberBand stand-in and Lite-only configurations.

### Phase 2 - Auto-Tune timing and vocal quality
- **Pitch-analysis lookahead** (id 58 `tune_lookahead_ms`, 0-60 ms, default 0): the lead pitch engine and the chord
  voices are fed from a preallocated delayed copy of the input while the YIN tracker keeps analysing the undelayed
  signal, so the correction is aligned with the audio it corrects instead of arriving ~25-50 ms late. The dry path
  is delayed by the same amount, dry/wet stay sample-aligned, and the extra latency is reported in `vfx_statf(2)`
  (only while the pitch engine is actually running; the direct path never pays it). Default 0 preserves the
  legacy timing exactly.
- **Adaptive YIN window**: the analysis window follows the last reliable pitch (about two periods, clamped to
  20-40 ms; falls back to the fixed 40 ms window when unvoiced). Measured effect: a 69->74 note change is reported
  ~49 ms after the note change vs ~59 ms with the legacy fixed window (stand-in build numbers).
- **Vibrato preservation** (id 59 `tune_keep_vibrato`, 0-1, default 0): the correction is split with a ~200 ms
  slow tracker - the slow drift is fully corrected toward the scale note while a configurable fraction of the fast
  (4-8 Hz) deviation stays audible. Measured on a continuous 6 Hz / ±0.5 semitone vibrato with hard tune +
  40 ms lookahead: depth 0.079 st with keep = 0 vs 0.423 st with keep = 1 (raw 0.5 st), mean on the note in both
  cases. keep = 0 reduces exactly to the original full correction. Octave-error protection is unchanged.
- Legacy mode restores the old pitch-analysis timing completely (fixed window, no lookahead, no vibrato keep) -
  verified by impulse-alignment probes: V1 with 30 ms lookahead = engine + 1440 + 144 samples, legacy = engine
  latency only.
- Menu: Tune page gains "Lookahead ms (0 = old timing)" and "Keep vibrato" sliders (api 9).
- Tests: new [13b] block (note-switch lag adaptive vs legacy, impulse-alignment with/without lookahead and in
  legacy mode, vibrato depth keep 0 vs 1, chunk-size independence 960/random/1-sample, no-extra-latency when
  tuning is off). All tests pass in both configurations.

### Phase 3 - hear-myself monitoring
- The monitor queue no longer drops audio in normal operation. A **queue servo** (BASS-free, host-testable
  `MonServo` in voicefx_dsp.h) holds the queued audio near **50 ms** by adjusting the monitor stream playback
  rate via `BASS_ATTRIB_FREQ`, hard-limited to **±1 %** and smoothed over ~20 push cycles so the correction
  can neither be heard nor oscillate (simulated-queue tests: rate factor never left ±1 % in 60 s; a 100 ms
  surplus drained back under 70 ms in ~3.4 s; ±3 ms push jitter stays bounded with no growth).
- **Hard reset** only when the queue passes ~200 ms (device stall / suspended output): the stream is restarted
  (buffer cleared), the 80 ms priming gate re-arms, and the event is logged; the reset counter is capped to
  avoid a flush storm. BASS_DATA_AVAILABLE errors skip the push instead of dropping chunks.
- Legacy comparison mode restores the original v8 behaviour exactly: drop new chunks while > 0.25 s is queued
  (no servo, no rate changes).
- New figure `vfx_statf(9)` = actual monitor queue latency in ms; the Status page "Hear myself" row shows
  "playing, queue N ms".
- NOT verified on device (needs the Nezuko client): actual BASS restart-flush semantics of
  `BASS_ChannelPlay(h, 1)` for push streams, audible quality of the ±1 % rate changes on real hardware, and
  servo behaviour across app background/foreground cycles. The 80 ms priming behaviour itself is unchanged.
- Tests: [15] simulated-queue block (deterministic, clearly labelled as NOT a BASS/device test).

### Phase 4 - audio quality upgrades (all new effects default OFF / transparent; legacy mode keeps the old sound)
- **Optional AI noise suppression** (id 60 `ns_engine`, 0 = classic, 1 = AI): RNNoise runs on 48 kHz mono
  480-sample frames. The DSP core only does the frame assembly in preallocated rings (partial frames survive
  between callbacks - behaviour is chunk-size independent, verified with a deterministic fake processor and
  1/480/960/random-sample chunks producing identical output). The AI model is fetched by the build workflow
  (`jni/rnnoise-src`, xiph/rnnoise 0.2, BSD-3-Clause) and never stored in the repository; without it the
  project still builds (`VFX_NO_RNNOISE`) and ns_engine=1 falls back to the classic Wiener engine.
  Switching engines cross-fades (~10 ms); the AI path adds exactly 480 samples (10 ms) of latency, reported
  in `vfx_statf(2)` while active. RNNoise is created in `Engine::rebuild` (hook thread), never in the audio
  callback. **Uncertainty:** an actual armeabi-v7a NDK build of the RNNoise C sources has not been run here
  (no NDK in this environment); the sources are plain C99 and the Android.mk wiring compiles src/*.c +
  src/celt/*.c with include dirs jni/rnnoise-src{,/include,/src}.
- **Formant-preserving chord voices** (id 61 `chord_formant`, 0-1, default 0): V1 chord voices use a
  pitch-synchronous TD-PSOLA-style shifter (two-period Hann grains from a precomputed LUT, placed at
  pitch-synchronous output epochs, fixed 32 ms internal read latency, at most 12 active grains, bounded) that
  keeps the vocal formants while shifting pitch. The parameter blends between the old LitePitch voice sound
  (0, default, bit-identical to before) and PSOLA (1); when the input is unvoiced or the period estimate is
  unreliable the voices fall back to LitePitch (measured: unvoiced input with formant 1 stays within
  -0.7 dB of the input level). PSOLA pitch accuracy with Magic Chords: C#4/E4/A2 voices land within 1.6 Hz
  of the exact scale notes. The PSOLA voice sits up to ~1 ms behind the lead voice (documented trade-off of
  the fixed-latency design; the LitePitch path keeps its exact legacy alignment).
- **Anti-aliased drive** (V1 mode): 2x oversampling with a 1025-point tanh LUT (no per-sample tanhf).
  Measured level match vs the legacy drive at 500 Hz: within 0.0 dB. Legacy drive = original tanhf path.
- **V1 reverb: modulated 8-line FDN** replacing Freeverb in V1 mode: 8 incommensurate delay lines,
  Hadamard feedback mixing scaled to unit row norm (loop gain <= rev_size-derived T60 of 0.3-3.5 s, stable
  and bounded - verified peaks < 16k at mix 1 with the wildest settings), one-pole damping per line,
  a slow triangle-swept modulation on two lines, and a predelay line (id 62 `rev_predelay_ms` 0-100,
  id 63 `rev_damp` 0-1). `rev_size` keeps its meaning (now tail length), `rev_mix` unchanged. Legacy mode
  keeps the original Freeverb bit-exactly (verified: predelay/damp changes produce 0 differing samples).
- **Echo damping** (id 64 `echo_damp` 0-1): a one-pole low-pass (9 kHz -> 1.5 kHz) plus a fixed 75 Hz
  rumble high-pass inside the echo feedback loop only. Measured: the 3rd repeat of a 5 kHz burst loses
  19.5 dB with damping at 1 vs 0. Repeat level semantics unchanged.
- **Vocoder upgrades** (V1 mode): 20 analysis/synthesis bands (the original 12-band path is untouched for
  legacy) and id 65 `voc_carrier`: 0 = the cyber carrier follows the detected vocal pitch, 1 = it snaps to
  the selected scale via the Tuner (verified: sung C#4 in fixed C major -> carrier 261.6/293.7 Hz, not
  277.2; follow mode -> 277.5 Hz). Whisper mode stays noise-carried. The pitch tracker now runs when the
  cyber vocoder needs a carrier pitch.
- Menu: Clean page gains the engine selector (Classic / AI), Chords page the PSOLA slider, Space page echo
  damping + reverb predelay/damping, Robot page the cyber carrier mode.
- Tests: [16] block (frame assembly + latency + classic fallback, PSOLA accuracy + unvoiced fallback,
  drive match, FDN stability at extremes + legacy bit-exactness, echo damping, vocoder carrier modes,
  legacy-ignores-new-knobs bit-exact pairs). All tests pass in both configurations.

### Phase 5 - CPU headroom and performance
- **Lookup-table oscillators**: every per-sample `sinf`/`cosf` in the DSP is now served from precomputed
  1024-step LUTs with linear interpolation - LitePitch grain windows, the robot ring modulator, tremolo and
  the chorus/flanger/vibrato LFOs. Measured numerical difference vs the reference math: **4.7e-6** (sine) and
  **2.4e-6** (Hann) of full scale - far below the 16-bit quantisation step (3e-5). Phase accumulators are
  unchanged, so phase continuity on frequency changes is preserved (tremolo speed-change test: largest step
  470 vs the 235 natural maximum, no click). The tables are built once at load time, never in the callback.
- **Automatic Lite recovery**: after an overload fallback the engine probes RubberBand with a short bounded
  probe (one 512-sample block through the already-constructed object - never an initialisation in the
  callback) every 20 s of audio and switches back only when the measured probe load stays under 55 % of real
  time; the decision logic is a separate host-testable struct (`LiteRecovery`), failures are counted and
  logged, missing RubberBand is handled (no probing when the object does not exist / VFX_NO_RUBBERBAND).
  The switch back is handled by the existing cross-fade state machine, so it is click-free.
- **Per-stage CPU timing** (compile-time optional with `VFX_NO_PROFILING`): average and worst-case ms per
  DSP call for the input stage, the pitch engine and the output stage, measured with the monotonic clock at
  chunk granularity (3 clock reads per call, no per-chunk logging) and shown on the Status page as
  "Stage ms avg (in/eng/out)"; `vfx_statf(11..16)`.
- Menu: Status page gains the stage-timing row.
- Tests: [17] block (LUT accuracy, oscillator phase continuity on parameter changes, recovery-controller
  decisions with simulated timings, stage counters finite/positive/peak>=avg). All tests pass in both
  configurations. Real-device CPU benchmarks are NOT fabricated - they need the Nezuko client.

### v8 beta (historical, previous product name)
- v8: Auto-Tune styles + Magic Chords (Tune / Chords effect pages, 10 new voices, `/vfx tune`,
  `/vfx chord`), library API 8.
- v7: noise suppression (Wiener STFT with minimum-statistics noise tracking + keyboard-click guard),
  quick Noise toggle, `/vfx noise`, library API 7.
- v6: full menu restyle (icon sidebar, voice grid, side panel sliders), effect rack pages, CPU /
  dropout meters, pitch engine choice (Auto / High / Lite), hear-myself pre-buffer, library API 6.
- Earlier: voice presets, pitch/formant shifting, robot, monitor.
