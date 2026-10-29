# Third-party notices for HexedVoiceMod V1.0

This project combines several third-party components. They are listed here with
their licenses and attribution. RubberBand and RNNoise are **not stored in this
repository** - the GitHub Actions workflow downloads them at build time; the
folders `jni/rubberband-src/` and `jni/rnnoise-src/` are build inputs only.

## RubberBand 4.0 (optional pitch engine, fetched by the build workflow)

- Source: https://github.com/breakfastquay/rubberband (tag v4.0.0), single-file
  build `single/RubberBandSingle.cpp` plus `rubberband/RubberBandLiveShifter.h`.
- License: **GNU General Public License v2.0 or later** - Copyright (C) Chris
  Cannam and breakfastquay. This license is the reason HexedVoiceMod as a
  combined work is distributed under GPL-2.0-or-later (see `LICENSE`).
- Used for: the "High"/"Auto" pitch engine (RubberBandLiveShifter with
  OptionWindowShort / OptionFormantShifted / OptionChannelsApart).
- When RubberBand is absent the project builds with the built-in Lite pitch
  shifter only (`VFX_NO_RUBBERBAND`).

## RNNoise 0.2 (optional AI noise suppression, fetched by the build workflow)

- Source: https://github.com/xiph/rnnoise (tag v0.2), files under `src/` and
  `include/rnnoise.h`.
- License: **BSD 3-Clause** - Copyright (c) 2017, Jean-Marc Valin, Xiph.Org
  Foundation. All rights reserved. Redistribution and use in source and binary
  forms, with or without modification, are permitted under the BSD 3-Clause
  terms (see the upstream COPYING file that ships with the RNNoise sources).
- Used for: the Noise > Engine "AI" mode (480-sample frames at 48 kHz mono).
- When RNNoise is absent the project builds with the classic Wiener engine only
  (`VFX_NO_RNNOISE`).

## AML mod headers (`jni/mod/`)

- Source: https://github.com/RusJJ/AndroidModLoader.git (AML - Android Mod
  Loader), bundled as the mod's build dependency (`dep.1` in
  `project.properties`), untouched except for this notice.
- Copyright (C) RusJJ / AndroidModLoader contributors.
- **License:** the bundled headers carry no explicit license file. Verify the
  applicable license in the upstream repository before redistribution. If the
  upstream license turns out to be incompatible with GPL-2.0-or-later, the AML
  headers must be treated as a separately linked system library (they form the
  mod loader interface, not the audio engine).

## stb_sprintf (`jni/mod/thirdparty/stb_sprintf.h` + `STB_LICENSE`)

- Source: https://github.com/nothings/stb (stb_sprintf v1.10), originally by
  Jeff Roberts / RAD Game Tools, 2015.
- License: **Public domain (Unlicense) or MIT, at your choice** - the full dual
  license text is bundled unchanged in `jni/mod/thirdparty/STB_LICENSE`.

## Built-in DSP code

Everything under `jni/voicefx_dsp.h`, `jni/main.cpp`, `menu/` and `test/` that
is not listed above is part of HexedVoiceMod itself and is licensed by `LICENSE`
(GPL-2.0-or-later). The classical DSP building blocks (Freeverb topology,
RBJ biquad cookbook formulas, YIN pitch tracking, Krumhansl-Schmuckler key
profiles) implement published, unpatented algorithms; the implementation code
is original to this project.
