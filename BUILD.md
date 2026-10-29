# Building HexedVoiceMod

The build instructions moved to [README.md](README.md) - see **"Building with GitHub Actions"**
and **"Local build notes"** there. This file is kept only as a pointer so old links do not break.

Quick reference:

* CI (recommended): push to GitHub, run `build-hexedvoicemod`, download the `HexedVoiceMod`
  artifact (`libHexedVoiceMod.so`, armeabi-v7a). RubberBand and RNNoise are fetched by the workflow.
* Host tests: `g++ -std=c++17 -O2 -ffast-math -fno-finite-math-only -I test/fake test/host_test.cpp`
  (add `-DVFX_NO_RUBBERBAND` for the Lite-only configuration).
* Local ndk-build: `$ANDROID_NDK_HOME/ndk-build NDK_PROJECT_PATH=. APP_BUILD_SCRIPT=jni/Android.mk
  NDK_APPLICATION_MK=jni/Application.mk` with `jni/rubberband-src/` and/or `jni/rnnoise-src/`
  present for the optional engines.
