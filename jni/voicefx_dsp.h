// HexedVoiceMod V1.0 DSP core (header-only, no Android / AML dependency, so it can also be unit-tested on a PC).
// Included once by main.cpp, which contains the AML glue (hook, monitor stream, entry points).
#pragma once
// Voice changer + filter/effect rack: hooks libBASS BASS_RecordStart (AML hook engine / Dobby) and runs a BASS DSP on the mic channel.
//
// Features: pitch shifting (RubberBand high-quality engine with a built-in light shifter as fallback and automatic CPU
// fallback), formant control, noise suppression (spectral Wiener filter with minimum-statistics noise tracking +
// keyboard-click guard, 512 samples = 10.7 ms at 48 kHz of added latency while it is on), Auto-Tune (YIN pitch tracker,
// scales, auto key) + chord voices, effect rack (high/low-pass, bass/treble shelves, drive, bit-crusher, tremolo,
// chorus/flanger/vibrato, echo, reverb, robot blend, noise/saw vocoder), hear-myself monitoring.
// Robustness: one fixed-length dry delay line for exact dry/wet alignment, cross-faded preset/pitch switching,
// pipeline reset when a mic burst has a > 120 ms gap, denormal flushing in every recursive filter, NaN guards,
// 80 ms monitor pre-buffer, CPU meter. All parameters are plain floats behind vfx_get/vfx_set.
// Library API version 9 (menu asks vfx_stat(8); product version itself is vfx_stat(17) = 100 = 1.0.0 * 100).
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdarg.h>
#include <math.h>
#include <atomic>
#include <mutex>
#include <vector>
#include <memory>
#include <chrono>
#include <algorithm>
// RubberBand is optional: define VFX_NO_RUBBERBAND (Android.mk does it when jni/rubberband-src is missing) to build with the
// built-in Lite pitch shifter only (pitch works, formant control and the high-quality engine do not).
#ifndef VFX_NO_RUBBERBAND
#include "rubberband/RubberBandLiveShifter.h"
#endif


#define VFX_EXPORT extern "C" __attribute__((visibility("default")))
#define VFX_API_VERSION 9
// Product identity: the library API version (VFX_API_VERSION) is separate from the product version.
// The Lua menu requires API >= 9; vfx_stat(17) returns the product version * 100.
#ifndef VFX_CFG_PATH
#define VFX_CFG_PATH "/storage/emulated/0/HexedVoiceMod.cfg"
#endif
#ifndef VFX_LOG_PATH
#define VFX_LOG_PATH "/storage/emulated/0/HexedVoiceMod_log.txt"
#endif
// First-run import source (read-only; it is never deleted or written).
#ifndef VFX_LEGACY_CFG_PATH
#define VFX_LEGACY_CFG_PATH "/storage/emulated/0/voicefx.cfg"
#endif
#define CFG_PATH VFX_CFG_PATH
#define LOG_PATH VFX_LOG_PATH

// ------------------------------------------------------------------ logging
#ifndef VFX_HOST_TEST
void vfx_platform_log(const char* msg);
#endif
static std::atomic<int> g_logBudget{400};
static void vlog(const char* fmt, ...)
{
    if (g_logBudget.fetch_sub(1) <= 0) return;
    char buf[512];
    va_list ap; va_start(ap, fmt); vsnprintf(buf, sizeof(buf), fmt, ap); va_end(ap);
#ifndef VFX_HOST_TEST
    vfx_platform_log(buf);                       // defined in main.cpp: AML logger + /storage/emulated/0/voicefx_log.txt
#else
    fprintf(stderr, "[VFX] %s\n", buf);
#endif
}

// ------------------------------------------------------------------ parameters (written by menu, read by DSP thread)
// ids 0..10 are identical to v5 (old menus keep working); new ones are appended.
enum {
    P_ENABLED, P_PITCH, P_FORMANT, P_PRESERVE, P_GAIN_DB, P_GATE_DB, P_MIX, P_ROBOT, P_ROBOT_HZ, P_MON_VOL, P_MONITOR,
    P_HP_HZ, P_LP_HZ, P_BASS_DB, P_TREBLE_DB, P_DRIVE, P_BITS, P_DOWN, P_TREM_HZ, P_TREM_DEPTH,
    P_MOD_MODE, P_MOD_RATE, P_MOD_DEPTH, P_ECHO_MS, P_ECHO_FB, P_ECHO_MIX, P_REV_SIZE, P_REV_MIX,
    P_ROBOT_AMT, P_VOC_MODE, P_VOC_HZ, P_QUALITY, P_NS_ON, P_NS_AMT, P_NS_CLICK,
    P_TUNE_ON, P_TUNE_KEY, P_TUNE_SCALE, P_TUNE_TIME, P_TUNE_AMT, P_CHORD, P_CHORD_MIX, P_CHORD_SPREAD,
    // ---- V1.0 additions (appended; ids 0..42 keep their v8 meanings) ----
    P_LEGACY,                                                            // 43 V1 / legacy comparison (never saved)
    P_GATE_HOLD_MS, P_GATE_RANGE_DB, P_GATE_HYST_DB,                     // 44..46 reworked gate
    P_COMP_ON, P_COMP_THRESH_DB, P_COMP_RATIO, P_COMP_ATTACK_MS, P_COMP_RELEASE_MS, P_COMP_MAKEUP_DB, P_COMP_AUTO,  // 47..53
    P_DEESS_ON, P_DEESS_AMT, P_DEESS_FREQ,                               // 54..56
    P_LIMIT_CEIL_DB,                                                     // 57
    P_TUNE_LOOKAHEAD, P_TUNE_KEEP_VIB,                                   // 58..59 (Phase 2)
    P_NS_ENGINE,                                                         // 60: 0 classic Wiener, 1 RNNoise AI (Phase 4)
    P_CHORD_FORMANT,                                                     // 61: 0 old chord voices, 1 PSOLA formant-preserving
    P_REV_PREDELAY_MS, P_REV_DAMP,                                       // 62..63 FDN reverb controls
    P_ECHO_DAMP,                                                         // 64
    P_VOC_CARRIER,                                                       // 65: 0 follow vocal pitch, 1 snap to scale
    P_COUNT
};
struct ParDef { const char* name; float def, lo, hi; int kind; };   // kind 0 float, 1 switch (0/1), 2 integer
static const ParDef kPar[P_COUNT] = {
    {"enabled",   0.f,   0.f,    1.f,   1}, {"pitch",    1.f,   0.25f,  4.f,   0}, {"formant",   1.f,  0.25f, 4.f,  0},
    {"preserve",  0.f,   0.f,    1.f,   1}, {"gain_db",  0.f,  -24.f,  24.f,   0}, {"gate_db", -80.f, -80.f, -20.f, 0},
    {"mix",       1.f,   0.f,    1.f,   0}, {"robot",    0.f,   0.f,    1.f,   1}, {"robot_hz",  80.f, 20.f, 500.f, 0},
    {"mon_vol",   1.f,   0.f,    2.f,   0}, {"monitor",  0.f,   0.f,    1.f,   1},   // monitor is never saved (feedback safety)
    {"hp_hz",     20.f,  20.f,   2000.f,0}, {"lp_hz",    20000.f, 1000.f, 20000.f, 0}, {"bass_db", 0.f, -12.f, 12.f, 0},
    {"treble_db", 0.f,  -12.f,   12.f,  0}, {"drive",    0.f,   0.f,    1.f,   0}, {"bits",     16.f,  4.f,   16.f, 0},
    {"down",      1.f,   1.f,    32.f,  0}, {"trem_hz",  5.f,   0.5f,   20.f,  0}, {"trem_depth",0.f,  0.f,   1.f,  0},
    {"mod_mode",  0.f,   0.f,    3.f,   2}, {"mod_rate", 1.f,   0.1f,   10.f,  0}, {"mod_depth", 0.5f, 0.f,   1.f,  0},
    {"echo_ms",   300.f, 40.f,   1000.f,0}, {"echo_fb",  0.35f, 0.f,    0.85f, 0}, {"echo_mix",  0.f,  0.f,   1.f,  0},
    {"rev_size",  0.5f,  0.f,    1.f,   0}, {"rev_mix",  0.f,   0.f,    1.f,   0}, {"robot_amt", 1.f,  0.f,   1.f,  0},
    {"voc_mode",  0.f,   0.f,    2.f,   2}, {"voc_hz",   110.f, 50.f,   400.f, 0}, {"quality",   0.f,  0.f,   2.f,  2},
    {"ns_on",     0.f,   0.f,    1.f,   1}, {"ns_amt",   0.6f,  0.f,    1.f,   0}, {"ns_click", 0.5f,  0.f,   1.f,  0},
    {"tune_on",   0.f,   0.f,    1.f,   1}, {"tune_key", 12.f,  0.f,    12.f,  2}, {"tune_scale", 1.f, 0.f,   7.f,  2},
    {"tune_time", 40.f,  0.f,    400.f, 0}, {"tune_amt", 0.9f,  0.f,    1.f,   0}, {"chord",    0.f,   0.f,   8.f,  2},
    {"chord_mix", 0.65f, 0.f,    1.f,   0}, {"chord_spread", 8.f, 0.f,  30.f,  0},
    // ---- V1.0 additions (defaults are OFF / transparent: the default sound stays on the legacy path) ----
    {"legacy_mode", 0.f,  0.f,    1.f,   1},                              // 0 = V1 processing, 1 = legacy (not saved)
    {"gate_hold_ms", 20.f, 0.f,   150.f, 0}, {"gate_range_db", 24.f, 0.f, 60.f, 0}, {"gate_hyst_db", 6.f, 0.f, 18.f, 0},
    {"comp_on",   0.f,   0.f,    1.f,   1}, {"comp_thresh_db", -24.f, -60.f, 0.f, 0}, {"comp_ratio", 3.f, 1.f, 20.f, 0},
    {"comp_attack_ms", 10.f, 0.5f, 100.f, 0}, {"comp_release_ms", 120.f, 10.f, 1000.f, 0}, {"comp_makeup_db", 0.f, 0.f, 24.f, 0},
    {"comp_auto_mkup", 1.f, 0.f,   1.f,   1},
    {"deess_on",  0.f,   0.f,    1.f,   1}, {"deess_amt", 0.5f,  0.f,    1.f,   0}, {"deess_freq", 5500.f, 3000.f, 9000.f, 0},
    {"limit_ceil_db", -0.5f, -12.f, 0.f, 0},
    {"tune_lookahead_ms", 0.f, 0.f,  60.f,  0}, {"tune_keep_vibrato", 0.f, 0.f, 1.f, 0},
    {"ns_engine", 0.f,   0.f,    1.f,   1},
    {"chord_formant", 0.f, 0.f,  1.f,   0},
    {"rev_predelay_ms", 0.f, 0.f, 100.f, 0}, {"rev_damp", 0.5f, 0.f, 1.f, 0},
    {"echo_damp", 0.f,   0.f,    1.f,   0},
    {"voc_carrier", 0.f, 0.f,    1.f,   1},
};
static std::atomic<float> g_par[P_COUNT];
static std::atomic<int>   g_qualityEpoch{0};
static void params_default() { for (int i = 0; i < P_COUNT; i++) g_par[i] = kPar[i].def; }
static struct ParamInit { ParamInit() { params_default(); } } g_paramInit;   // sane defaults even before OnModLoad
static float clampf(float v, float a, float b) { return v < a ? a : (v > b ? b : v); }
static float sanitize(int id, float v)
{
    if (id < 0 || id >= P_COUNT) return 0.f;
    const ParDef& d = kPar[id];
    if (!(v == v) || v > 1e12f || v < -1e12f) v = d.def;              // NaN / inf
    if (d.kind == 1) return v >= 0.5f ? 1.f : 0.f;
    if (d.kind == 2) v = floorf(v + 0.5f);
    return clampf(v, d.lo, d.hi);
}

// ------------------------------------------------------------------ stats
static std::atomic<int>   g_monState{0};       // 0 off, 1 playing, -1 stream creation failed
static std::atomic<int>   g_hookState{0};      // 0 none, 1 installed, -1 failed
static std::atomic<int>   g_recStarts{0}, g_dspCalls{0}, g_lastBytes{0}, g_rate{0}, g_fmtOK{0}, g_block{0};
static std::atomic<int>   g_underruns{0}, g_gapResets{0}, g_overruns{0}, g_engineMode{0}, g_autoLite{0};
static std::atomic<int>   g_oldLibFound{0};      // set by main.cpp when the old libvoicefx.so is already loaded
static std::atomic<float> g_peakIn{0.f}, g_peakOut{0.f}, g_latMs{0.f}, g_load{0.f}, g_loadPeak{0.f}, g_nsFloor{-90.f}, g_nsRed{0.f}, g_tuneDet{-1.f}, g_tuneTgt{-1.f};
static std::atomic<float> g_gateGain{1.f};       // current V1 gate gain (0..1), for the Status page / tests
static std::atomic<float> g_monLat{0.f};         // hear-myself monitor queue latency in ms (set by main.cpp)
// per-stage CPU timing (Phase 5): average / worst ms per DSP call for input stage, pitch engine, output stage
static std::atomic<float> g_stageMs[3] = { {0.f}, {0.f}, {0.f} };
static std::atomic<float> g_stagePk[3] = { {0.f}, {0.f}, {0.f} };
static std::atomic<int>   g_tuneRoot{0}, g_tuneVoiced{0};

// ------------------------------------------------------------------ DSP building blocks
namespace {
const float kPi = 3.14159265358979f, kTwoPi = 6.28318530717959f;
static inline float fz(float v) { return (v > -1e-20f && v < 1e-20f) ? 0.f : v; }   // flush denormals
static inline float wrap01(float v) { return v >= 1.f ? v - 1.f : v; }

struct Ring {                         // float FIFO (overwrites oldest when full)
    std::vector<float> b; size_t r = 0, w = 0, n = 0;
    void init(size_t cap) { b.assign(cap, 0.f); r = w = n = 0; }
    void clear() { r = w = n = 0; }
    void push(float v) { if (n == b.size()) { r = (r + 1) % b.size(); n--; } b[w] = v; w = (w + 1) % b.size(); n++; }
    float pop() { if (!n) return 0.f; float v = b[r]; r = (r + 1) % b.size(); n--; return v; }
};

struct Biquad {                       // RBJ cookbook, transposed direct form II
    float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    inline float proc(float x) { float y = b0 * x + z1; z1 = fz(b1 * x - a1 * y + z2); z2 = fz(b2 * x - a2 * y); return y; }
    void clear() { z1 = z2 = 0.f; }
    void set(double B0, double B1, double B2, double A0, double A1, double A2)
    { b0 = (float)(B0 / A0); b1 = (float)(B1 / A0); b2 = (float)(B2 / A0); a1 = (float)(A1 / A0); a2 = (float)(A2 / A0); }
    static double fclamp(double fs, double f) { return f < 10.0 ? 10.0 : (f > fs * 0.45 ? fs * 0.45 : f); }
    void lowpass(double fs, double f, double q)  { double w = 2 * kPi * fclamp(fs, f) / fs, c = cos(w), al = sin(w) / (2 * q);
        set((1 - c) / 2, 1 - c, (1 - c) / 2, 1 + al, -2 * c, 1 - al); }
    void highpass(double fs, double f, double q) { double w = 2 * kPi * fclamp(fs, f) / fs, c = cos(w), al = sin(w) / (2 * q);
        set((1 + c) / 2, -(1 + c), (1 + c) / 2, 1 + al, -2 * c, 1 - al); }
    void bandpass(double fs, double f, double q) { double w = 2 * kPi * fclamp(fs, f) / fs, c = cos(w), al = sin(w) / (2 * q);
        set(al, 0, -al, 1 + al, -2 * c, 1 - al); }
    void lowshelf(double fs, double f, double dB) { double A = pow(10.0, dB / 40), w = 2 * kPi * fclamp(fs, f) / fs, c = cos(w);
        double be = 2 * sqrt(A) * (sin(w) / 2 * sqrt(2.0));
        set(A * ((A + 1) - (A - 1) * c + be), 2 * A * ((A - 1) - (A + 1) * c), A * ((A + 1) - (A - 1) * c - be),
            (A + 1) + (A - 1) * c + be, -2 * ((A - 1) + (A + 1) * c), (A + 1) + (A - 1) * c - be); }
    void highshelf(double fs, double f, double dB) { double A = pow(10.0, dB / 40), w = 2 * kPi * fclamp(fs, f) / fs, c = cos(w);
        double be = 2 * sqrt(A) * (sin(w) / 2 * sqrt(2.0));
        set(A * ((A + 1) + (A - 1) * c + be), -2 * A * ((A - 1) + (A + 1) * c), A * ((A + 1) + (A - 1) * c - be),
            (A + 1) - (A - 1) * c + be, 2 * ((A - 1) - (A + 1) * c), (A + 1) - (A - 1) * c - be); }
};


// Precomputed window / oscillator tables (built once at load time, so the real-time path never calls
// cosf/sinf for grain windows, LFOs or ring modulation). 1024 steps + linear interpolation: max error
// about 4.7e-5 of full scale; phase continuity is exact because the phase accumulators are unchanged.
static float g_hannLut[1025];
static struct HannLutInit { HannLutInit() { for (int i = 0; i <= 1024; i++) g_hannLut[i] = 0.5f - 0.5f * cosf(kTwoPi * i / 1024.f); } } g_hannLutInit;
static inline float hannLut(float ph) {                 // ph in [0, 1)
    ph = ph - floorf(ph);
    float x = ph * 1024.f; int i = (int)x; if (i > 1023) i = 1023;
    return g_hannLut[i] + (g_hannLut[i + 1] - g_hannLut[i]) * (x - (float)i);
}
static float g_sinLut[1025];
static struct SinLutInit { SinLutInit() { for (int i = 0; i <= 1024; i++) g_sinLut[i] = sinf(kTwoPi * i / 1024.f); } } g_sinLutInit;
static inline float sinLut(float ph) {                  // ph in turns [0, 1)
    ph = ph - floorf(ph);
    float x = ph * 1024.f; int i = (int)x; if (i > 1023) i = 1023;
    return g_sinLut[i] + (g_sinLut[i + 1] - g_sinLut[i]) * (x - (float)i);
}

// Light built-in pitch shifter: two crossfaded read heads on a delay line. Cheap (no FFT), ~15 ms latency, no formant control.
// Pitch-adaptive: when the caller knows the voice period T0 (setPeriod) the grain length is 2*k*T0, so the two heads read the same
// waveform phase and add up coherently - without it a voice splits into two side lines ~30 Hz apart (the usual "warble").
struct LitePitch {
    std::vector<float> buf; size_t mask = 0, w = 0; float W = 1536.f, Wdef = 1536.f, Wtgt = 1536.f, d1 = 0.f, ratio = 1.f, fs = 48000.f;
    void init(float f) { fs = f; size_t n = 1; while (n < (size_t)(fs * 0.12f)) n <<= 1; buf.assign(n, 0.f); mask = n - 1; w = 0;
                         Wdef = floorf(fs * 0.032f); W = Wtgt = Wdef; d1 = 0.f; }
    void reset() { std::fill(buf.begin(), buf.end(), 0.f); w = 0; d1 = 0.f; W = Wtgt = Wdef; }
    void setPeriod(float t0) {                                  // t0 = voice period in samples; <= 0 = unknown
        if (t0 <= 8.f) { Wtgt = Wdef; return; }
        int k = std::max(1, (int)floorf(0.016f * fs / t0 + 0.5f)); Wtgt = clampf(2.f * (float)k * t0, 0.010f * fs, 0.055f * fs);
    }
    size_t latency() const { return (size_t)(W * 0.5f) + 2; }
    inline float rd(float d) const { float rp = (float)w - d; long i0 = (long)floorf(rp); float fr = rp - (float)i0;
        return buf[(size_t)i0 & mask] * (1.f - fr) + buf[(size_t)(i0 + 1) & mask] * fr; }
    float proc(float x) {
        buf[w & mask] = x; w++;
        W += (Wtgt - W) * 0.002f;
        d1 += 1.f - ratio;
        if (d1 >= W) d1 -= W; else if (d1 < 0.f) d1 += W;
        float d2 = d1 + 0.5f * W; if (d2 >= W) d2 -= W;
        float g1 = hannLut(d1 / W), g2 = hannLut(d2 / W);   // grain windows from the precomputed LUT (Phase 5)
        return g1 * rd(2.f + d1) + g2 * rd(2.f + d2);
    }
};

struct Comb { std::vector<float> b; size_t i = 0; float store = 0.f;
    void init(size_t n) { b.assign(n < 1 ? 1 : n, 0.f); i = 0; store = 0.f; }
    inline float proc(float in, float fb, float d1, float d2) { float o = b[i]; store = fz(o * d2 + store * d1); b[i] = fz(in + store * fb); if (++i >= b.size()) i = 0; return o; } };
struct Allpass { std::vector<float> b; size_t i = 0;
    void init(size_t n) { b.assign(n < 1 ? 1 : n, 0.f); i = 0; }
    inline float proc(float in) { float bo = b[i]; float o = bo - in; b[i] = fz(in + bo * 0.5f); if (++i >= b.size()) i = 0; return o; } };

struct Dly {                                  // fixed delay line
    std::vector<float> b; size_t mask = 0, w = 0, d = 0;
    void init(size_t n) { size_t k = 1; while (k < n) k <<= 1; b.assign(k, 0.f); mask = k - 1; w = 0; d = 0; }
    void clear() { std::fill(b.begin(), b.end(), 0.f); w = 0; }
    void set(size_t dd) { d = dd > mask ? mask : dd; }
    inline float proc(float x) { b[w & mask] = x; float y = b[(w - d) & mask]; w++; return y; }
};

struct Snap {        // one consistent read of the parameters per DSP call
    float hp, lp, bass, treble, drive, bits, down, tremHz, tremDepth, modRate, modDepth, echoMs, echoFb, echoMix, revSize, revMix,
          robotHz, robotAmt, vocHz; int modMode, vocMode; bool robot;
    // ---- V1.0 additions (Phase 4)
    bool v1 = false;                 // false = legacy path (original effects), true = V1 effects
    int vocCarrier = 0; float vocF0 = 0.f; float vocTarget = -1.f; bool vocF0ok = false;
    float revPredMs = 0.f, revDamp = 0.5f, echoDamp = 0.f;
};

// effect rack, runs sample by sample after the pitch stage
// tanh lookup table for the V1 drive stage (2x oversampled, no per-sample tanhf). Domain [-8, 8].
static float g_drLut[1025];
static struct DrLutInit { DrLutInit() { for (int i = 0; i <= 1024; i++) g_drLut[i] = tanhf(-8.f + 16.f * i / 1024.f); } } g_drLutInit;
static inline float tanhLut(float x) {                  // x clamped to [-8, 8]
    if (x > 8.f) return 1.f; if (x < -8.f) return -1.f;
    float t = (x + 8.f) * 64.f; int i = (int)t; if (i > 1023) i = 1023;
    return g_drLut[i] + (g_drLut[i + 1] - g_drLut[i]) * (t - (float)i);
}

struct FxChain {
    static const int NB = 12, NBV = 20;                 // NB: legacy vocoder bands, NBV: V1 bands
    float fs = 48000.f; Snap p{};
    Biquad hp[2], lp[2], bass, treb; bool hpOn = false, lpOn = false, bassOn = false, trebOn = false;
    float cHp = -1, cLp = -1, cBass = -99, cTreb = -99;
    float phR = 0, phT = 0, hold = 0, hcnt = 0;
    std::vector<float> mbuf; size_t mmask = 0, mw = 0; float lfo1 = 0, lfo2 = 0, fbState = 0;
    std::vector<float> ebuf; size_t ew = 0; float eCur = 0, eTgt = 0; bool eInit = false;
    Comb comb[8]; Allpass ap[4]; float revFb = 0.84f;
    Biquad an[NB], sy[NB]; float venv[NB]; float vph = 0, inP = 0, outP = 0, vAgc = 1.f; uint32_t rng = 22222u;
    float xp1 = 0.f;                                    // previous sample for the 2x oversampled drive
    Biquad anV[NBV], syV[NBV]; float venvV[NBV];        // V1 vocoder: 20 bands
    float vphV = 0.f, inPV = 0.f, outPV = 0.f, vAgcV = 1.f;
    // ---- V1 echo damping (one-pole LP + fixed HP inside the feedback loop)
    float eLp = 0.f, eLpC = 0.5f, eHp = 0.f, eHpC = 0.998f, eDampLast = -1.f;
    float fdnFb = 0.999f;
    // ---- V1 reverb: modulated 8-line feedback delay network with predelay and damping
    std::vector<float> rbuf[8]; size_t rm[8], rw8 = 0; float rmod[8], rLp[8], rLpC[8];
    std::vector<float> pdbuf; size_t pdmask = 0, pdw = 0; float pdLast = -1.f;
    float fdnPh = 0.f;

    void init(float f) {
        fs = f; reset();
        size_t n = 1; while (n < (size_t)(fs * 0.1f)) n <<= 1; mbuf.assign(n, 0.f); mmask = n - 1;
        ebuf.assign((size_t)(fs * 1.05f) + 16, 0.f);
        const int ct[8] = { 1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617 }, at[4] = { 556, 441, 341, 225 };
        for (int i = 0; i < 8; i++) comb[i].init((size_t)(ct[i] * fs / 44100.f));
        for (int i = 0; i < 4; i++) ap[i].init((size_t)(at[i] * fs / 44100.f));
        for (int i = 0; i < NB; i++) { double c = 140.0 * pow(7500.0 / 140.0, i / (double)(NB - 1)); an[i].bandpass(fs, c, 3.0); sy[i].bandpass(fs, c, 3.0); }
        for (int i = 0; i < NBV; i++) { double c = 140.0 * pow(7500.0 / 140.0, i / (double)(NBV - 1)); anV[i].bandpass(fs, c, 3.0); syV[i].bandpass(fs, c, 3.0); }
        // V1 reverb: 8 incommensurate delay lines (scaled to the sample rate)
        static const int rlen[8] = { 1063, 1499, 1733, 2029, 2371, 2777, 3187, 3571 };
        for (int i = 0; i < 8; i++) {
            size_t n = (size_t)(rlen[i] * fs / 44100.f); if (n < 32) n = 32;
            rbuf[i].assign(n, 0.f); rm[i] = 0; rmod[i] = 0.f; rLp[i] = 0.f; rLpC[i] = 0.4f;
        }
        size_t pn = 1; while (pn < (size_t)(fs * 0.12f)) pn <<= 1; pdbuf.assign(pn, 0.f); pdmask = pn - 1; pdw = 0;
        eLp = eHp = 0.f; xp1 = 0.f; fdnPh = 0.f; vphV = 0.f; inPV = outPV = 0.f; vAgcV = 1.f;
        for (int i = 0; i < NBV; i++) venvV[i] = 0.f;
        cHp = cLp = -1; cBass = cTreb = -99;
    }
    void reset() {
        for (auto& b : hp) b.clear();
        for (auto& b : lp) b.clear();
        bass.clear(); treb.clear();
        std::fill(mbuf.begin(), mbuf.end(), 0.f); std::fill(ebuf.begin(), ebuf.end(), 0.f);
        for (auto& c : comb) init_clear(c);
        for (auto& a : ap) std::fill(a.b.begin(), a.b.end(), 0.f);
        for (auto& b : an) b.clear();
        for (auto& b : sy) b.clear();
        for (int i = 0; i < NB; i++) venv[i] = 0.f;
        phR = phT = hold = hcnt = 0.f; lfo1 = lfo2 = fbState = 0.f; mw = ew = 0; eInit = false; vph = 0.f; inP = outP = 0.f; vAgc = 1.f;
        for (int i = 0; i < 8; i++) { std::fill(rbuf[i].begin(), rbuf[i].end(), 0.f); rm[i] = 0; rmod[i] = 0.f; rLp[i] = 0.f; }
        std::fill(pdbuf.begin(), pdbuf.end(), 0.f); pdw = 0; fdnPh = 0.f;
        eLp = eHp = 0.f; xp1 = 0.f; eDampLast = -1.f; pdLast = -1.f;
        for (int i = 0; i < NBV; i++) venvV[i] = 0.f;
        vphV = 0.f; inPV = outPV = 0.f; vAgcV = 1.f;
    }
    static void init_clear(Comb& c) { std::fill(c.b.begin(), c.b.end(), 0.f); c.store = 0.f; c.i = 0; }

    void update(const Snap& s) {
        p = s;
        bool on = s.hp > 25.f; if (on != hpOn) { hp[0].clear(); hp[1].clear(); cHp = -1; } hpOn = on;
        if (on && s.hp != cHp) { hp[0].highpass(fs, s.hp, 0.5412); hp[1].highpass(fs, s.hp, 1.3066); cHp = s.hp; }
        on = s.lp < 19000.f; if (on != lpOn) { lp[0].clear(); lp[1].clear(); cLp = -1; } lpOn = on;
        if (on && s.lp != cLp) { lp[0].lowpass(fs, s.lp, 0.5412); lp[1].lowpass(fs, s.lp, 1.3066); cLp = s.lp; }
        on = fabsf(s.bass) > 0.05f; if (on != bassOn) { bass.clear(); cBass = -99; } bassOn = on;
        if (on && s.bass != cBass) { bass.lowshelf(fs, 220.0, s.bass); cBass = s.bass; }
        on = fabsf(s.treble) > 0.05f; if (on != trebOn) { treb.clear(); cTreb = -99; } trebOn = on;
        if (on && s.treble != cTreb) { treb.highshelf(fs, 3500.0, s.treble); cTreb = s.treble; }
        eTgt = clampf(s.echoMs * 0.001f * fs, 8.f, (float)ebuf.size() - 8.f);
        if (!eInit) { eCur = eTgt; eInit = true; }
        revFb = 0.70f + 0.28f * s.revSize;
        {                                                         // V1 FDN: rev_size -> T60 0.3..3.5 s -> per-sample loop gain
            double t60 = 0.3 + 3.2 * clampf(s.revSize, 0.f, 1.f);
            double g = pow(10.0, -3.0 / ((double)fs * t60));
            fdnFb = (float)(g > 0.9995 ? 0.9995 : g);
        }
        if (s.echoDamp != eDampLast) {                            // V1: echo feedback damping 0 = off, 1 = dark
            eDampLast = s.echoDamp;
            double fc = 9000.0 - 7500.0 * clampf(s.echoDamp, 0.f, 1.f);   // 9 kHz .. 1.5 kHz
            eLpC = (float)(1.0 - exp(-2.0 * kPi * fc / fs));
            eHpC = (float)exp(-2.0 * kPi * 75.0 / fs);            // fixed 75 Hz rumble filter in the loop
        }
    }

    inline float noise() { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return (float)(int32_t)rng * (1.f / 2147483648.f); }
    inline float mrd(float d) const { float rp = (float)mw - d; long i0 = (long)floorf(rp); float fr = rp - (float)i0;
        return mbuf[(size_t)i0 & mmask] * (1.f - fr) + mbuf[(size_t)(i0 + 1) & mmask] * fr; }

    float process(float x) {
        // 1) vocoder (whisper = noise carrier, cyber = saw carrier): keeps the spoken envelope, replaces the voice source
        if (p.vocMode) {
            float car;
            if (p.vocMode == 1) car = noise();
            else {
                float f0 = p.vocHz;
                if (p.v1 && p.vocF0ok) {                          // V1: carrier follows the detected voice (or its scale note)
                    f0 = (p.vocCarrier == 1 && p.vocTarget > 0.f)
                       ? clampf(440.f * exp2f((p.vocTarget - 69.f) * (1.f / 12.f)), 50.f, 400.f)
                       : clampf(p.vocF0, 50.f, 400.f);
                }
                float dt = f0 / fs;
                if (p.v1) { vphV += dt; vphV -= floorf(vphV); float t = vphV, bl = 0.f;
                   if (t < dt) { float u = t / dt; bl = u + u - u * u - 1.f; } else if (t > 1.f - dt) { float u = (t - 1.f) / dt; bl = u * u + u + u + 1.f; }
                   car = 2.f * t - 1.f - bl;
                } else { vph += dt; vph -= floorf(vph); float t = vph, bl = 0.f;
                   if (t < dt) { float u = t / dt; bl = u + u - u * u - 1.f; } else if (t > 1.f - dt) { float u = (t - 1.f) / dt; bl = u * u + u + u + 1.f; }
                   car = 2.f * t - 1.f - bl; }
            }
            float acc = 0.f;
            if (p.v1) {
                for (int i = 0; i < NBV; i++) {
                    float a = fabsf(anV[i].proc(x));
                    venvV[i] = fz(venvV[i] + (a - venvV[i]) * (a > venvV[i] ? 0.02f : 0.0035f));
                    acc += venvV[i] * syV[i].proc(car);
                }
                inPV = fz(inPV + (x * x - inPV) * 0.0008f); outPV = fz(outPV + (acc * acc - outPV) * 0.0008f);
                float tg = sqrtf((inPV + 1e-9f) / (outPV + 1e-9f)); tg = clampf(tg, 0.05f, 60.f);
                vAgcV += (tg - vAgcV) * 0.002f;
                x = acc * vAgcV;
            } else {
                for (int i = 0; i < NB; i++) {
                    float a = fabsf(an[i].proc(x));
                    venv[i] = fz(venv[i] + (a - venv[i]) * (a > venv[i] ? 0.02f : 0.0035f));
                    acc += venv[i] * sy[i].proc(car);
                }
                inP = fz(inP + (x * x - inP) * 0.0008f); outP = fz(outP + (acc * acc - outP) * 0.0008f);
                float tg = sqrtf((inP + 1e-9f) / (outP + 1e-9f)); tg = clampf(tg, 0.05f, 60.f);
                vAgc += (tg - vAgc) * 0.002f;
                x = acc * vAgc;
            }
        }
        // 2) drive: legacy = original tanhf; V1 = 2x oversampled tanh from a LUT (anti-aliased, no per-sample tanhf)
        if (p.drive > 0.001f) {
            float g = 1.f + 20.f * p.drive;
            if (p.v1) {
                float m = 0.5f * (x + xp1); xp1 = x;
                x = (0.5f * tanhLut(g * m) + 0.5f * tanhLut(g * x)) * (0.85f / sqrtf(g));
            } else {
                x = tanhf(g * x) * (0.85f / sqrtf(g));
            }
        }
        // 3) bit crusher / sample-rate reducer
        if (p.bits < 15.5f || p.down > 1.01f) {
            if (p.down > 1.01f) { hcnt += 1.f; if (hcnt >= p.down) { hcnt -= p.down; hold = x; } x = hold; }
            if (p.bits < 15.5f) { float lv = exp2f(p.bits - 1.f); x = floorf(x * lv + 0.5f) / lv; }
        }
        // 4) tone: high-pass / low-pass (24 dB/oct) and shelves
        if (hpOn) x = hp[1].proc(hp[0].proc(x));
        if (lpOn) x = lp[1].proc(lp[0].proc(x));
        if (bassOn) x = bass.proc(x);
        if (trebOn) x = treb.proc(x);
        // 5) robot ring modulator
        if (p.robot) { phR += p.robotHz / fs; phR -= floorf(phR); x *= (1.f - p.robotAmt) + p.robotAmt * sinLut(phR); }
        // 6) tremolo
        if (p.tremDepth > 0.001f) { phT += p.tremHz / fs; phT -= floorf(phT); x *= 1.f - p.tremDepth * (0.5f - 0.5f * sinLut(phT)); }
        // 7) modulated delay: chorus / flanger / vibrato
        if (p.modMode) {
            float rate = p.modRate / fs; lfo1 = wrap01(lfo1 + rate); lfo2 = wrap01(lfo2 + rate * 1.13f);
            float s1 = sinLut(lfo1), s2 = sinLut(lfo2 + 1.7f / kTwoPi), out;   // 1.7 rad phase offset folded into turns
            if (p.modMode == 2) {                       // flanger
                mbuf[mw & mmask] = fz(x + fbState * 0.55f);
                float t = mrd(fs * (0.0025f + 0.0020f * p.modDepth * s1)); fbState = t; out = (x + t) * 0.7f;
            } else {
                mbuf[mw & mmask] = x;
                if (p.modMode == 1) { float t1 = mrd(fs * (0.018f + 0.006f * p.modDepth * s1)), t2 = mrd(fs * (0.026f + 0.008f * p.modDepth * s2)); out = x * 0.7f + (t1 + t2) * 0.35f; }
                else out = mrd(fs * (0.004f + 0.003f * p.modDepth * s1));   // vibrato
            }
            mw++; x = out;
        }
        // 8) echo (legacy: raw feedback; V1: damping LP + rumble HP inside the feedback loop only)
        if (p.echoMix > 0.001f) {
            eCur += (eTgt - eCur) * 0.0007f;
            float rp = (float)ew - eCur; if (rp < 0.f) rp += (float)ebuf.size();
            size_t i0 = (size_t)rp, i1 = i0 + 1 >= ebuf.size() ? 0 : i0 + 1; float fr = rp - (float)i0;
            float d = ebuf[i0] * (1.f - fr) + ebuf[i1] * fr;
            if (p.v1) {
                float dHp = fz(d - eHp); eHp = fz(d * (1.f - eHpC) + eHp * eHpC);     // fixed 75 Hz HP (state variable form)
                eLp = fz(eLp + (dHp - eLp) * eLpC);                                   // damping LP in the loop
                ebuf[ew] = fz(x + eLp * p.echoFb);
            } else {
                ebuf[ew] = fz(x + d * p.echoFb);
            }
            if (++ew >= ebuf.size()) ew = 0;
            x += d * p.echoMix;
        }
        // 9) reverb: legacy = Freeverb topology (original); V1 = modulated 8-line FDN with predelay + damping
        if (p.revMix > 0.001f) {
            if (!p.v1) {
                float in = x * 0.015f, sum = 0.f;
                for (int i = 0; i < 8; i++) sum += comb[i].proc(in, revFb, 0.3f, 0.7f);
                for (int i = 0; i < 4; i++) sum = ap[i].proc(sum);
                x = x * (1.f - 0.25f * p.revMix) + sum * 2.2f * p.revMix;
            } else {
                // predelay line
                float pdSmp = (float)clampf(p.revPredMs * 0.001f * fs, 0.f, (float)(pdmask - 4));
                if (pdSmp != pdLast) pdLast = pdSmp;
                pdbuf[pdw & pdmask] = x;
                float rp = (float)pdw - pdSmp; if (rp < 0.f) rp += (float)pdmask + 1.f;
                size_t p0 = (size_t)rp, p1 = (p0 + 1) & pdmask; float pfr = rp - (float)p0;
                float pd = pdbuf[p0 & pdmask] * (1.f - pfr) + pdbuf[p1] * pfr;
                pdw++;
                // 8-line FDN: damping LP in every feedback path, Hadamard (±0.5) sign mixing, 2 modulated lines
                float y[8];
                float lc = (float)(1.0 - exp(-2.0 * kPi * (9000.0 - 7500.0 * clampf(p.revDamp, 0.f, 1.f)) / fs));
                fdnPh += 0.31f / fs; if (fdnPh >= 1.f) fdnPh -= 1.f;              // slow triangle LFO, no trig per sample
                float tri = fdnPh < 0.5f ? fdnPh * 4.f - 1.f : 3.f - fdnPh * 4.f;
                float modS = tri * 0.0006f * fs;                                  // ±0.6 ms sweep
                for (int i = 0; i < 8; i++) {
                    size_t n = rbuf[i].size();
                    float rd = (float)rm[i] + ((i == 2 || i == 5) ? modS * (i == 2 ? 1.f : -0.7f) : 0.f);
                    while (rd >= (float)n) rd -= (float)n; while (rd < 0.f) rd += (float)n;
                    size_t q0 = (size_t)rd, q1 = (q0 + 1) % n; float fr = rd - (float)q0;
                    float v = rbuf[i][q0] * (1.f - fr) + rbuf[i][q1] * fr;
                    rLp[i] = fz(rLp[i] + (v - rLp[i]) * lc);                      // damping in the loop
                    y[i] = rLp[i];
                }
                static const float H[8][8] = {                                    // Hadamard rows scaled by 1/sqrt(8): unit norm
                    { .35355f, .35355f, .35355f, .35355f, .35355f, .35355f, .35355f, .35355f },
                    { .35355f, -.35355f, .35355f, -.35355f, .35355f, -.35355f, .35355f, -.35355f },
                    { .35355f, .35355f, -.35355f, -.35355f, .35355f, .35355f, -.35355f, -.35355f },
                    { .35355f, -.35355f, -.35355f, .35355f, .35355f, -.35355f, -.35355f, .35355f },
                    { .35355f, .35355f, .35355f, .35355f, -.35355f, -.35355f, -.35355f, -.35355f },
                    { .35355f, -.35355f, .35355f, -.35355f, -.35355f, .35355f, -.35355f, .35355f },
                    { .35355f, .35355f, -.35355f, -.35355f, -.35355f, -.35355f, .35355f, .35355f },
                    { .35355f, -.35355f, -.35355f, .35355f, -.35355f, .35355f, .35355f, -.35355f } };
                                for (int i = 0; i < 8; i++) {
                    float acc = 0.f; for (int j = 0; j < 8; j++) acc += H[i][j] * y[j];
                    rbuf[i][rm[i]] = fz(pd * 0.1f + acc * fdnFb);
                    if (++rm[i] >= rbuf[i].size()) rm[i] = 0;
                }
                float sum = 0.35355f * (y[0] + y[1] + y[2] + y[3] + y[4] + y[5] + y[6] + y[7]);
                x = x * (1.f - 0.25f * p.revMix) + sum * 1.5f * p.revMix;
            }
        }
        return x;
    }
};


// Noise suppressor: 512-point STFT (hop 256, sqrt-Hann), noise floor from minimum statistics over ~1 s of smoothed bin powers,
// decision-directed Wiener gain with a floor set by the strength, plus a short-burst guard for keyboard clicks.
struct NoiseSuppressor {
    static const int N = 512, HOP = 256, NB = 257, NSUB = 6, SUBLEN = 32;
    float fs = 48000.f, win[N], cosT[N / 2], sinT[N / 2]; int rev[N];
    float hist[N], hop[HOP], acc[N], re[N], im[N];
    float S[NB], minCur[NB], minHist[NSUB][NB], Nest[NB], Gprev[NB], gamPrev[NB], Gs[NB], P[NB], Graw[NB];
    int hopN = 0, frames = 0, subCount = 0, histPos = 0, histFill = 0, hfRun = 0, clickHold = 0;
    float avgHF = 0.f, avgLF = 0.f, floorDb = -90.f, redDb = 0.f;
    Ring outq;

    void init(float f) {
        fs = f;
        for (int n = 0; n < N; n++) win[n] = sqrtf(0.5f - 0.5f * cosf(kTwoPi * n / N));
        for (int k = 0; k < N / 2; k++) { cosT[k] = cosf(kTwoPi * k / N); sinT[k] = sinf(kTwoPi * k / N); }
        for (int a = 0; a < N; a++) { int r = 0, v = a; for (int b = 0; b < 9; b++) { r = (r << 1) | (v & 1); v >>= 1; } rev[a] = r; }
        outq.init(2048); reset();
    }
    void reset() { flush(); frames = 0; subCount = 0; histPos = 0; histFill = 0; avgHF = avgLF = 0.f; floorDb = -90.f;
                   for (int k = 0; k < NB; k++) { S[k] = minCur[k] = Nest[k] = 0.f; for (int j = 0; j < NSUB; j++) minHist[j][k] = 0.f; } }
    void flush() {                       // drop audio state, keep what was learned about the noise (a mic burst after a pause starts clean)
        std::fill(hist, hist + N, 0.f); std::fill(hop, hop + HOP, 0.f); std::fill(acc, acc + N, 0.f);
        for (int k = 0; k < NB; k++) { Gprev[k] = 1.f; gamPrev[k] = 0.f; Gs[k] = 1.f; }
        hopN = 0; hfRun = 0; clickHold = 0; redDb = 0.f; outq.clear(); for (int i = 0; i < HOP; i++) outq.push(0.f);
    }
    void fft(float* r, float* i) const {
        for (int a = 0; a < N; a++) { int b = rev[a]; if (b > a) { std::swap(r[a], r[b]); std::swap(i[a], i[b]); } }
        for (int len = 2; len <= N; len <<= 1) {
            int half = len >> 1, step = N / len;
            for (int s0 = 0; s0 < N; s0 += len) for (int j = 0; j < half; j++) {
                float wr = cosT[j * step], wi = -sinT[j * step]; int a = s0 + j, b = a + half;
                float tr = r[b] * wr - i[b] * wi, ti = r[b] * wi + i[b] * wr;
                r[b] = r[a] - tr; i[b] = i[a] - ti; r[a] += tr; i[a] += ti;
            }
        }
    }
    void frame(float amt, float click) {
        memmove(hist, hist + HOP, HOP * sizeof(float)); memcpy(hist + HOP, hop, HOP * sizeof(float));
        for (int n = 0; n < N; n++) { re[n] = hist[n] * win[n]; im[n] = 0.f; }
        fft(re, im);
        for (int k = 0; k < NB; k++) P[k] = re[k] * re[k] + im[k] * im[k];
        // ---- noise floor tracking
        if (frames == 0) for (int k = 0; k < NB; k++) { S[k] = P[k]; minCur[k] = P[k]; }
        for (int k = 0; k < NB; k++) { S[k] = 0.85f * S[k] + 0.15f * P[k]; if (S[k] < minCur[k]) minCur[k] = S[k]; }
        if (++subCount >= SUBLEN) {
            subCount = 0; for (int k = 0; k < NB; k++) { minHist[histPos][k] = minCur[k]; minCur[k] = S[k]; }
            histPos = (histPos + 1) % NSUB; if (histFill < NSUB) histFill++;
        }
        double mn = 0.0;
        for (int k = 0; k < NB; k++) {
            float m = minCur[k]; for (int j = 0; j < histFill; j++) if (minHist[j][k] < m) m = minHist[j][k];
            if (frames < 8) m = S[k];
            Nest[k] = m * 1.8f + 1e-12f; if (k > 0 && k < NB - 1) mn += m;
        }
        floorDb = 10.f * log10f((float)(mn / (NB - 2)) / (N / 2) + 1e-12f);
        // ---- click guard: short, broadband, high-frequency-only burst that is far above the learned noise
        const float binHz = fs / N; int kLF = std::max(3, (int)(1000.f / binHz)), kHF = std::max(kLF + 1, (int)(2000.f / binHz)), kC = std::max(2, (int)(600.f / binHz));
        float EH = 0.f, EL = 0.f, NH = 0.f, NL = 0.f;
        for (int k = kHF; k < NB; k++) { EH += P[k]; NH += Nest[k]; }
        for (int k = 3; k <= kLF; k++) { EL += P[k]; NL += Nest[k]; }
        if (frames == 0) { avgHF = EH; avgLF = EL; }
        bool burst = frames > 20 && EH > 8.f * avgHF + 6.f * NH && EL < 4.f * avgLF + 4.f * NL;
        if (!burst) { avgHF += (EH - avgHF) * 0.05f; avgLF += (EL - avgLF) * 0.05f; hfRun = 0; }
        else { hfRun++; if (hfRun <= 2) clickHold = 3; else clickHold = 0; }        // lasts longer than ~10 ms -> not a click (s, sh, fan puff)
        // ---- gains
        const float gMin = powf(10.f, -(8.f + 22.f * amt) / 20.f), ovf = 1.f + 1.5f * amt;   // over-subtraction factor
        for (int k = 0; k < NB; k++) {
            float gam = P[k] / (Nest[k] * ovf);
            float xi = 0.96f * Gprev[k] * Gprev[k] * gamPrev[k] + 0.04f * std::max(gam - 1.f, 0.f);
            if (xi < 0.0032f) xi = 0.0032f;
            float g = xi / (1.f + xi); if (g < gMin) g = gMin; if (g > 1.f) g = 1.f;
            Graw[k] = g; Gprev[k] = g; gamPrev[k] = gam;
        }
        const float clickLin = powf(10.f, -(12.f + 20.f * click) / 20.f); const bool clk = clickHold > 0 && click > 0.01f;
        float sumG = 0.f;
        for (int k = 0; k < NB; k++) {
            float gl = Graw[k > 0 ? k - 1 : 1], gr = Graw[k < NB - 1 ? k + 1 : NB - 2];
            float gf = 0.25f * gl + 0.5f * Graw[k] + 0.25f * gr;
            Gs[k] += (gf - Gs[k]) * (gf > Gs[k] ? 0.8f : 0.5f);
            float ga = Gs[k]; if (clk && k >= kC) ga *= clickLin;
            re[k] *= ga; im[k] *= ga; if (k > 0 && k < NB - 1) { re[N - k] *= ga; im[N - k] *= ga; }
            sumG += ga;
        }
        if (clickHold > 0) clickHold--;
        redDb += (-20.f * log10f(sumG / NB + 1e-6f) - redDb) * 0.1f;
        fft(im, re);                                       // inverse via swapped real/imag
        const float inv = 1.f / N;
        for (int n = 0; n < N; n++) acc[n] += re[n] * inv * win[n];
        for (int n = 0; n < HOP; n++) outq.push(acc[n]);
        memmove(acc, acc + HOP, HOP * sizeof(float)); std::fill(acc + HOP, acc + N, 0.f);
        frames++;
    }
    inline float proc(float x, float amt, float click) {
        hop[hopN++] = x; if (hopN >= HOP) { hopN = 0; frame(amt, click); }
        return outq.pop();
    }
};

// ---- RNNoise (Phase 4, optional): the AI model itself is NOT part of the DSP core. When the build has
// jni/rnnoise-src (fetched by the workflow), main.cpp sets these three hooks before the engine starts;
// the core only does the frame assembly / latency bookkeeping, so host tests can inject a deterministic
// fake processor and verify the buffering across arbitrary chunk sizes.
static void*  (*g_rnCreate)(int rate) = nullptr;                      // create a DenoiseState for this rate
static void   (*g_rnFree)(void* st) = nullptr;                        // free it
static float  (*g_rnProcess)(void* st, float* out, const float* in) = nullptr;   // 480-sample frames, returns VAD 0..1

// RNNoise front end (Phase 4, optional): assembles the incoming audio into 480-sample frames in preallocated
// rings, runs the AI model through the g_rnProcess hook and paces the denoised frames out. Partial frames
// survive between BASS callbacks, so behaviour is independent of the chunk size (verified by test with a
// deterministic fake processor). Adds exactly 480 samples (10 ms at 48 kHz) of latency while active.
struct RnDenoiser {
    static const int FRM = 480;
    Ring inq, outq;
    float fin[FRM], fout[FRM];
    void* st = nullptr;
    bool active = false, stFailed = false;
    float lastVad = 0.f;
    bool init(int rate, bool want) {
        inq.init(FRM * 8); outq.init(FRM * 8);
        std::fill(fin, fin + FRM, 0.f); std::fill(fout, fout + FRM, 0.f);
        if (want && !st && g_rnCreate) { st = g_rnCreate(rate); if (!st) stFailed = true; }
        active = want && st != nullptr && g_rnProcess != nullptr;
        if (!active) { inq.clear(); outq.clear(); for (int i = 0; i < FRM; i++) outq.push(0.f); }   // keep the output paced
        return active;
    }
    void clear() { inq.clear(); outq.clear(); for (int i = 0; i < FRM; i++) { fin[i] = 0.f; fout[i] = 0.f; outq.push(0.f); } }
    inline float proc(float x) {                        // active path: frame-aligned processing
        inq.push(x);
        while (inq.n >= FRM) {
            for (int i = 0; i < FRM; i++) fin[i] = inq.pop();
            lastVad = g_rnProcess(st, fout, fin);
            for (int i = 0; i < FRM; i++) outq.push(fout[i]);
        }
        return outq.n ? outq.pop() : 0.f;
    }
    inline float passthrough(float x) {                 // inactive path: keep the rings in step so a switch is glitch-free
        inq.push(x);
        while (inq.n >= FRM) { for (int i = 0; i < FRM; i++) inq.pop(); for (int i = 0; i < FRM; i++) outq.push(0.f); }
        if (outq.n) outq.pop();
        return x;
    }
    size_t latency() const { return FRM; }
};

// ================================================================== V1.0 dynamics / conditioning blocks
// All of these are real-time safe: preallocated buffers, no locks, no file I/O, no per-sample sinf/cosf/tanhf/powf.
// Except the conditioner (mandatory in V1 mode) every stage defaults to OFF so the default sound stays on the legacy path.

// Mandatory input conditioning: DC blocker (one-pole, R = 0.995) + fixed 2nd-order 75 Hz high-pass.
// Runs at the very top of the chain, before the noise suppressor and the tuner. A slow ramp on enable
// prevents a startup click.
struct Conditioner {
    float x1 = 0.f, y1 = 0.f, fade = 0.f;
    Biquad hp;
    void init(float f) { hp.highpass(f, 75.0, 0.7071); clear(); }
    void clear() { x1 = y1 = 0.f; hp.clear(); fade = 0.f; }
    inline float proc(float x) {
        if (fade < 1.f) fade = std::min(1.f, fade + 0.0006f);       // ~28 ms fade-in after (re)start: no click
        float y = fz(x - x1 + 0.995f * y1); x1 = x; y1 = y;         // DC blocker
        return fade * hp.proc(y);                                   // fixed 75 Hz high-pass
    }
};

// Reworked gate: separate open/close thresholds (hysteresis), hold time, smoothed attack/release,
// soft 2:1 downward expansion (no hard mute) and ~5 ms of lookahead so word onsets survive.
// The detector sees the input directly; the audio passes through a 5 ms delay line, so the smoothed
// gain change always leads the audio it multiplies.
struct DynGate {
    float fs = 48000.f, env = 0.f, gain = 1.f; int holdLeft = 0;
    float atkA = 0.f, relA = 0.f;
    Dly look; size_t lat = 0;
    void init(float f) { fs = f; lat = (size_t)(f * 0.005f); look.init(lat + 4); look.set(lat);
                         atkA = 1.f - expf(-1.f / (0.0015f * f)); relA = 1.f - expf(-1.f / (0.060f * f)); clear(); }
    void clear() { env = 0.f; gain = 1.f; holdLeft = 0; look.clear(); look.set(lat); }
    // thrOpen/hystLin/rangeLin are linear; holdSmp in samples. Returns the gated sample.
    inline float proc(float x, float thrOpen, float hystLin, float rangeLin, int holdSmp) {
        float a = fabsf(x);
        env = a > env ? a : fz(env * 0.9995f);                      // same fast-attack peak detector as the legacy gate
        const float thr = gain > 0.5f ? thrOpen : thrOpen * hystLin; // hysteresis: close level sits below the open level
        if (env >= thr) holdLeft = holdSmp; else if (holdLeft > 0) holdLeft--;
        float tgt = 1.f;
        if (holdLeft <= 0) {                                        // closed side: soft 2:1 expansion with a floor
            const float thrClose = std::max(thrOpen * hystLin, 1e-7f);
            float over = env / thrClose;                            // 0..1 just below the close threshold, < 1 deeper
            tgt = clampf(over * over, rangeLin, 1.f);
        }
        gain += (tgt - gain) * (tgt > gain ? atkA : relA);          // open fast, close slow
        return look.proc(x) * gain;                                 // audio is delayed, the gain leads it (lookahead)
    }
};

// Feed-forward compressor with an RMS-style detector and a 6 dB soft knee. The gain curve is evaluated at a
// 32-sample control rate (cheap, no per-sample powf); the per-sample path only smooths and applies the gain.
struct Compressor {
    float fs = 48000.f, env2 = 0.f, gr = 1.f, mk = 1.f, atkA = 0.f, relA = 0.f;
    int cnt = 0;
    float gTgt = 1.f;
    void init(float f) { fs = f; setTimes(10.f, 120.f); clear(); }
    void setTimes(float atkMs, float relMs) {
        atkA = 1.f - expf(-1.f / (std::max(0.5f, atkMs) * 0.001f * fs));
        relA = 1.f - expf(-1.f / (std::max(10.f, relMs) * 0.001f * fs));
    }
    void setMakeup(float mkLin) { mk = clampf(mkLin, 0.03f, 16.f); }
    void clear() { env2 = 0.f; gr = 1.f; gTgt = 1.f; cnt = 0; mk = 1.f; }   // mk reset: the first control update re-derives it deterministically
    // thrLin, mkLin are linear; autoMk overrides mkLin with a threshold-derived make-up gain.
    inline float proc(float x, float thrLin, float ratio, float mkLin, bool autoMk) {
        env2 = fz(env2 + (x * x - env2) * 0.02f);                   // RMS-like envelope (~few ms at 48 kHz)
        if (++cnt >= 32) {                                          // control rate: ~0.67 ms
            cnt = 0;
            float lev = sqrtf(env2);
            float r = clampf(ratio, 1.f, 20.f);
            float g = 1.f;
            const float knee = 6.f;
            if (lev > thrLin * 1.001f) {
                float overDb = 20.f * log10f(lev / thrLin + 1e-12f);            // dB above threshold
                if (overDb > knee) g = powf(10.f, -overDb * (1.f - 1.f / r) / 20.f);
                else { float t = overDb / knee; g = powf(10.f, -(t * t) * knee * (1.f - 1.f / r) / 20.f); }   // soft knee
            }
            gTgt = clampf(g, 0.03f, 1.f);
            float m = mkLin;
            if (autoMk) m = clampf(powf(10.f, -20.f * log10f(std::max(thrLin, 1e-4f)) / 40.f), 0.5f, 4.f);
            m = autoMk ? m : mkLin; setMakeup(m);
        }
        gr = fz(gr + (gTgt - gr) * (gTgt < gr ? atkA : relA));      // attack = gain dropping, release = recovering
        return x * gr * mk;
    }
};

// Split-band de-esser: the sibilance band (about 4-9 kHz at the default 5.5 kHz setting) is detected with a band-pass
// and reduced by subtracting a fraction of the band content, by up to deess_amt * 12 dB, with fast attack and a smooth
// release. With amount = 0 (g = 1) nothing is subtracted, so the stage is exactly transparent. Placed after the pitch /
// chord mixing and before the effect rack.
struct DeEsser {
    float fs = 48000.f, fLast = -1.f, env = 0.f, g = 1.f, gTgt = 1.f;
    int cnt = 0;
    Biquad bp;                                                      // sibilance band copy (the part that gets reduced)
    void init(float f) { fs = f; clear(); }
    void setFreq(float fc) {                                        // stable at the configured sample rate
        fc = Biquad::fclamp(fs, fc);
        if (fabsf(fc - fLast) < 1.f) return;
        fLast = (float)fc;
        bp.bandpass(fs, fc, 0.9);
    }
    void clear() { bp.clear(); env = 0.f; g = 1.f; gTgt = 1.f; cnt = 0; }
    inline float proc(float x, float amt) {
        float bd = bp.proc(x);
        float a = fabsf(bd);
        env = a > env ? a : fz(env * 0.9993f);
        if (++cnt >= 32) {                                          // control-rate gain decision
            cnt = 0;
            const float thr = 0.12f;                                // ~ -18 dBFS in-band peak: sibilance territory
            float red = 15.f * clampf(amt, 0.f, 1.f);               // max reduction in dB (amt 1 -> 15 dB)
            float gLin = env > thr ? powf(10.f, -red * (1.f - thr / env) / 20.f) : 1.f;
            gTgt = clampf(gLin, powf(10.f, -red / 20.f), 1.f);
        }
        g += (gTgt - g) * (gTgt < g ? 0.004f : 0.0006f);            // fast attack, ~50 ms release
        return x - bd * (1.f - g);                                  // subtract the band content; g == 1 -> exactly x
    }
};

// End-of-chain true-peak lookahead limiter (~3 ms) with a configurable output ceiling. Replaces the old
// tanh soft knee in V1 mode; the original knee stays available in legacy mode. The audio is delayed by
// `lat` samples while the smoothed gain is computed from the input, so the gain reduction is already in
// place before the peak leaves the delay line. Latency is reported in g_latMs.
struct LookLimiter {
    float fs = 48000.f, gain = 1.f, atkA = 0.f, relA = 0.f;
    Dly look; size_t lat = 0;
    void init(float f) { fs = f; lat = (size_t)(f * 0.003f); look.init(lat + 4); look.set(lat);
                         atkA = 1.f - expf(-1.f / (0.0003f * f)); relA = 1.f - expf(-1.f / (0.080f * f)); clear(); }
    void clear() { look.clear(); look.set(lat); gain = 1.f; }
    inline float proc(float x, float ceilLin) {
        float peak = fabsf(x);
        float tgt = (peak > ceilLin && peak > 1e-9f) ? ceilLin / peak : 1.f;
        gain += (tgt - gain) * (tgt < gain ? atkA : relA);          // fast attack (within the lookahead window), smooth release
        return look.proc(x) * gain;
    }
};


// Pitch-synchronous overlap-add (TD-PSOLA style) shifter for the chord voices (Phase 4). Grains are Hann
// windows of two input periods placed at pitch-synchronous output epochs, so the spectral envelope
// (formants) of the input stays intact while the pitch moves. All buffers preallocated; at most 12 grains
// are ever active (bounded); a fixed 32 ms internal read latency keeps every grain causal. Unvoiced input
// (no period estimate) is handled by the engine: it falls back to the LitePitch voice.
struct Psola {
    float fs = 48000.f, T0 = 200.f, ratio = 1.f;
    std::vector<float> in; size_t imask = 0, iw = 0;
    static const size_t LAT = 1536;                   // fixed internal latency (samples), >= 2 * max period
    struct Grain { float start = 0.f, read = 0.f, len = 1.f, idx = 0.f; };
    Grain g[12]; int ng = 0;
    double epochOut = 0.f;                            // absolute output position of the next synthesis epoch
    float norm = 1.f;
    size_t lat = LAT + 344;                           // reported latency (grain centred read adds ~half a max period)
    void init(float f) {
        fs = f;
        size_t n = 1; while (n < 8192) n <<= 1; in.assign(n, 0.f); imask = n - 1; iw = 0;
        clear();
    }
    void clear() { std::fill(in.begin(), in.end(), 0.f); iw = 0; ng = 0; epochOut = (double)(LAT + 1); T0 = 200.f; }
    void setPeriod(float t0) { if (t0 > 8.f && t0 < 1500.f) T0 = t0; }
    float proc(float x) {
        in[iw & imask] = x; iw++;
        const float Ta = clampf(T0, 24.f, 700.f);                       // analysis period (input samples)
        const float Ts = clampf(T0 / clampf(ratio, 0.25f, 4.f), 24.f, 1400.f);   // synthesis period (output samples)
        // spawn grains on the synthesis epoch grid; each grain reads the input around (epoch - LAT)
        while ((double)iw >= epochOut) {
            if (ng < 12) {
                const double readC = epochOut - (double)LAT;            // grain centre in input time
                double a = floor(readC / Ta + 0.5) * Ta;                // snap to the nearest input epoch
                if (a < Ta) a = Ta;
                if (a + Ta <= (double)iw) {                             // fully causal: input exists
                    g[ng].start = (float)epochOut;
                    g[ng].read = (float)(a - Ta);
                    g[ng].len = 2.f * Ta;
                    g[ng].idx = 0.f;
                    ng++;
                }
            }
            epochOut += Ts;
            norm = Ta / Ts;                                             // overlap-add sum is ratio -> normalise by it
        }
        // sum the active grains at this output position
        float y = 0.f;
        const double pos = (double)iw;
        for (int i = 0; i < ng; i++) {
            float o = pos - g[i].start;
            if (o >= g[i].len) {                                        // grain finished: compact
                g[i] = g[ng - 1]; ng--; i--; continue;
            }
            if (o >= 0.f) {
                float w = hannLut(o / g[i].len);                        // Hann from the precomputed LUT
                y += in[(size_t)(g[i].read + o) & imask] * w;
            }
        }
        return y / std::max(norm, 0.25f);
    }
};

// Automatic Lite -> RubberBand recovery controller (Phase 5). The decision logic is kept free of BASS /
// RubberBand calls so the host tests can drive it deterministically; the Engine runs the actual bounded
// probe (one 512-sample block through the existing RubberBand object - never an initialisation) every
// ~20 s of audio and switches back only when the measured probe load stays under 55 %.
#ifndef VFX_NO_PROFILING
#define VFX_PROFILE_STAGE(idx, ...) { auto t_vfx_##idx = std::chrono::steady_clock::now(); __VA_ARGS__; \
    float ms_vfx_##idx = std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - t_vfx_##idx).count(); \
    float a_vfx_##idx = g_stageMs[idx].load(); g_stageMs[idx] = a_vfx_##idx * 0.9f + ms_vfx_##idx * 0.1f; \
    float pk_vfx_##idx = g_stagePk[idx].load(); g_stagePk[idx] = ms_vfx_##idx > pk_vfx_##idx ? ms_vfx_##idx : pk_vfx_##idx * 0.999f; }
#else
#define VFX_PROFILE_STAGE(idx, ...) { __VA_ARGS__; }
#endif

struct LiteRecovery {
    float probeT = 0.f;           // audio seconds since the last probe
    int failStreak = 0;
    bool wantProbe(float dtAudioSec) {
        probeT += dtAudioSec;
        if (probeT < 20.f) return false;
        probeT = 0.f;
        return true;
    }
    static bool loadOk(float probeLoad) { return probeLoad < 0.55f; }   // measured fraction of real time
    void onResult(bool ok) { failStreak = ok ? 0 : failStreak + 1; }
};

// Hear-myself monitor queue servo (Phase 3). Instead of dropping chunks when the monitor queue grows, the
// monitor stream plays slightly faster or slower (BASS_ATTRIB_FREQ, limited to +/-1 %, smoothed so it can
// never be heard) to hold the queued audio near 50 ms. Only a queue beyond ~200 ms triggers a hard reset.
// This struct is deliberately free of BASS calls so the host tests can simulate the queue deterministically;
// main.cpp feeds it the queued byte count and applies the returned rate factor.
struct MonServo {
    float targetMs = 50.f, hardMs = 200.f;
    int rate = 48000;
    float speed = 1.f;            // current playback rate factor (1 = nominal)
    float ms = 0.f;               // last measured queue latency in ms
    void init(int r) { rate = r > 8000 ? r : 48000; speed = 1.f; ms = 0.f; }
    inline float bytesToMs(float qBytes) const { return 1000.f * qBytes / (2.f * (float)rate); }
    // qBytes: bytes currently queued (16-bit mono) including the chunk about to be pushed.
    // playing: the stream has been started. Returns the rate factor to apply (1 = nominal).
    float update(float qBytes, bool playing) {
        ms = bytesToMs(qBytes);
        if (!playing) { speed = 1.f; return 1.f; }
        float err = ms - targetMs;                       // + = too much queued -> play faster
        float corr = err / std::max(targetMs, 1.f) * 0.02f;
        if (corr > 0.01f) corr = 0.01f; else if (corr < -0.01f) corr = -0.01f;   // hard +/-1 % limit
        speed += (1.f + corr - speed) * 0.05f;           // smooth: ~20 push cycles time constant, no audible glide
        if (speed > 1.01f) speed = 1.01f; else if (speed < 0.99f) speed = 0.99f;
        return speed;
    }
    bool needHardReset(float qBytes) const { return bytesToMs(qBytes) > hardMs; }
};

// ================================================================== Auto-Tune / harmony helpers
// Scales as semitone sets relative to the root. 0 chromatic, 1 major, 2 minor, 3 pentatonic major, 4 pentatonic minor, 5 blues, 6 dorian, 7 harmonic minor
static const signed char kScaleDeg[8][12] = {
    { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 }, { 0, 2, 4, 5, 7, 9, 11, -1, -1, -1, -1, -1 }, { 0, 2, 3, 5, 7, 8, 10, -1, -1, -1, -1, -1 },
    { 0, 2, 4, 7, 9, -1, -1, -1, -1, -1, -1, -1 }, { 0, 3, 5, 7, 10, -1, -1, -1, -1, -1, -1, -1 }, { 0, 3, 5, 6, 7, 10, -1, -1, -1, -1, -1, -1 },
    { 0, 2, 3, 5, 7, 9, 10, -1, -1, -1, -1, -1 }, { 0, 2, 3, 5, 7, 8, 11, -1, -1, -1, -1, -1 } };
static const int kHarmScale[8] = { 1, 1, 2, 1, 2, 2, 6, 7 };       // scale used to build chords (chromatic / pentatonic / blues borrow the full scale)
static const int kRootOffset[8] = { 0, 0, 9, 0, 9, 9, 2, 9 };      // auto key finds the major tonic; minor-type scales sit 9 semitones above it
static uint16_t scaleMask(int sc) {
    static uint16_t m[8]; static bool init = false;
    if (!init) { for (int i = 0; i < 8; i++) { m[i] = 0; for (int j = 0; j < 12 && kScaleDeg[i][j] >= 0; j++) m[i] |= (uint16_t)(1u << kScaleDeg[i][j]); } init = true; }
    return m[sc < 0 ? 0 : (sc > 7 ? 7 : sc)];
}
static inline bool inScale(int n, int root, uint16_t mask) { int pc = ((n - root) % 12 + 12) % 12; return (mask >> pc) & 1; }
// nearest scale note to a (fractional) midi pitch; keeps the previous note while it is almost as close (no flicker at the boundary)
static float nearestNote(float midi, int root, uint16_t mask, float prev) {
    int c = (int)floorf(midi + 0.5f); float best = 1e9f; int bn = c;
    for (int d = -3; d <= 3; d++) { int n = c + d; if (!inScale(n, root, mask)) continue; float dist = fabsf(midi - (float)n); if (dist < best) { best = dist; bn = n; } }
    if (prev > 0.f && inScale((int)prev, root, mask)) { float dp = fabsf(midi - prev); if (dp < best + 0.15f && dp < 1.6f) return prev; }
    return (float)bn;
}
static int stepUp(int n, int steps, int root, uint16_t hm) {         // n moved up `steps` scale degrees (n is snapped into the scale first)
    if (!inScale(n, root, hm)) { int a = n, b = n; while (!inScale(a, root, hm)) a--; while (!inScale(b, root, hm)) b++; n = (n - a <= b - n) ? a : b; }
    while (steps > 0) { n++; while (!inScale(n, root, hm)) n++; steps--; }
    return n;
}
struct ChordVoice { signed char steps, oct; float gain; };
struct ChordDef { int n; ChordVoice v[3]; };
// 1 Magic (3rd + 5th + bass root), 2 Triad, 3 Power (5th + octave), 4 Seventh, 5 Sus (4th + 5th), 6 Angel (airy, an octave up), 7 Octaves, 8 Duet (3rd)
static const ChordDef kChord[9] = {
    { 0, { { 0, 0, 0.f }, { 0, 0, 0.f }, { 0, 0, 0.f } } },
    { 3, { { 2, 0, 0.85f }, { 4, 0, 0.75f }, { 0, -1, 0.70f } } },
    { 2, { { 2, 0, 0.90f }, { 4, 0, 0.85f }, { 0, 0, 0.f } } },
    { 2, { { 4, 0, 0.90f }, { 0, 1, 0.65f }, { 0, 0, 0.f } } },
    { 3, { { 2, 0, 0.80f }, { 4, 0, 0.75f }, { 6, 0, 0.70f } } },
    { 2, { { 3, 0, 0.85f }, { 4, 0, 0.85f }, { 0, 0, 0.f } } },
    { 3, { { 0, 1, 0.75f }, { 4, 1, 0.65f }, { 2, 1, 0.55f } } },
    { 2, { { 0, -1, 0.90f }, { 0, 1, 0.60f }, { 0, 0, 0.f } } },
    { 1, { { 2, 0, 1.00f }, { 0, 0, 0.f }, { 0, 0, 0.f } } } };

// Pitch tracker (YIN on a 12 kHz copy, 10 ms hop) + scale/key logic + Auto-Tune correction + chord-voice pitch shifts.
// V1.0: the analysis window adapts to the last reliable pitch (about two periods, at least 20 ms) so high voices react
// about twice as fast, and the correction can keep a configurable fraction of the fast vibrato while still pulling the
// slow pitch drift onto the scale note.
struct Tuner {
    static const int W = 480, NB = 2048;
    float fs = 48000.f, fsd = 12000.f; int dec = 4, hopS = 480, hopLeft = 480, decCnt = 0, tauMin = 12, tauMax = 171, rw = 0;
    float ring[NB], win[1024], d[260], cm[260]; Biquad lp;
    // parameters (written by Engine before each call)
    bool tuneOn = false; int key = 12, scale = 1, chord = 0; float timeMs = 40.f, amt = 0.9f, presetSemi = 0.f;
    bool adaptive = false;          // V1: adaptive analysis window (legacy timing when false)
    float keepVib = 0.f;            // V1: fraction of fast vibrato that stays audible (0 = fully corrected = legacy behaviour)
    // state / results
    bool voiced = false; int voicedRun = 0, unvoicedRun = 0, jumpRun = 0, root = 0, autoTonic = 0, hops = 0;
    int Weff = W;                   // effective analysis window in decimated samples (adaptive, clamped to [20 ms .. W])
    float midi = 0.f, conf = 0.f, lastMidi = 0.f, med[3], target = -1.f, desired = 0.f, corr = 0.f, hist[12], shiftV[3], devS = 0.f;

    void init(float f) {
        fs = f; dec = std::max(1, (int)floorf(fs / 12000.f + 0.5f)); fsd = fs / (float)dec; hopS = std::max(64, (int)floorf(fs * 0.010f + 0.5f));
        tauMin = std::max(2, (int)(fsd / 1000.f)); tauMax = std::min(255, (int)(fsd / 70.f)); lp.lowpass(fs, std::min(3500.0, fsd * 0.4), 0.7071);
        reset();
    }
    void reset() { flush(); for (int k = 0; k < 12; k++) hist[k] = 0.f; autoTonic = 0; hops = 0; }
    void flush() {
        std::fill(ring, ring + NB, 0.f); lp.clear(); rw = 0; decCnt = 0; hopLeft = hopS; voiced = false; voicedRun = unvoicedRun = jumpRun = 0;
        midi = lastMidi = 0.f; conf = 0.f; med[0] = med[1] = med[2] = 0.f; target = -1.f; desired = corr = 0.f; shiftV[0] = shiftV[1] = shiftV[2] = 0.f;
        Weff = W; devS = 0.f;
    }
    inline void push(float x) {
        float y = lp.proc(x);
        if (++decCnt >= dec) { decCnt = 0; ring[rw & (NB - 1)] = y; rw++; }
        if (--hopLeft <= 0) { hopLeft = hopS; analyse(); }
    }
    bool detect(float& m, float& cf) {                      // YIN: returns true and the midi pitch when the frame is voiced
        const int w = Weff;                                  // adaptive window (V1), fixed W in legacy timing
        const int L = w + tauMax + 1;
        for (int i = 0; i < L; i++) win[i] = ring[(rw - L + i) & (NB - 1)];
        double e = 0; for (int i = L - w; i < L; i++) e += (double)win[i] * win[i];
        if (sqrt(e / w) < 0.004) return false;
        float run = 0.f; cm[0] = 1.f;
        for (int tau = 1; tau <= tauMax; tau++) {
            float sum = 0.f; for (int j = 0; j < w; j++) { float df = win[j] - win[j + tau]; sum += df * df; }
            d[tau] = sum; run += sum; cm[tau] = run > 1e-12f ? sum * tau / run : 1.f;
        }
        int best = -1;
        for (int tau = tauMin; tau < tauMax; tau++) if (cm[tau] < 0.15f) { while (tau + 1 < tauMax && cm[tau + 1] < cm[tau]) tau++; best = tau; break; }
        if (best < 0) { float mn = 1e9f; for (int tau = tauMin; tau < tauMax; tau++) if (cm[tau] < mn) { mn = cm[tau]; best = tau; } if (mn > 0.30f) return false; }
        float tp = (float)best;
        if (best > 1 && best < tauMax) { float s0 = cm[best - 1], s1 = cm[best], s2 = cm[best + 1], den = 2.f * (2.f * s1 - s2 - s0); if (fabsf(den) > 1e-9f) tp += clampf((s2 - s0) / den, -1.f, 1.f); }
        float f0 = fsd / tp; if (f0 < 60.f || f0 > 1100.f) return false;
        m = 69.f + 12.f * log2f(f0 / 440.f); cf = clampf(1.f - cm[best], 0.f, 1.f); return true;
    }
    void pickKey() {                                         // Krumhansl-Schmuckler: correlate the note histogram with 12 major + 12 minor key profiles
        static const float pmaj[12] = { 6.35f, 2.23f, 3.48f, 2.33f, 4.38f, 4.09f, 2.52f, 5.19f, 2.39f, 3.66f, 2.29f, 2.88f };
        static const float pmin[12] = { 6.33f, 2.68f, 3.52f, 5.38f, 2.60f, 3.53f, 2.54f, 4.75f, 3.98f, 2.69f, 3.34f, 3.17f };
        float tot = 0.f; for (int k = 0; k < 12; k++) tot += hist[k];
        if (tot < 1.2f) return;
        const float hm = tot / 12.f; float hv = 0.f; for (int k = 0; k < 12; k++) hv += (hist[k] - hm) * (hist[k] - hm);
        if (hv < 1e-6f) return;
        float best = -2.f, cur = -2.f; int bt = autoTonic;
        for (int mode = 0; mode < 2; mode++) {
            const float* pr = mode ? pmin : pmaj; float pm = 0.f; for (int k = 0; k < 12; k++) pm += pr[k]; pm /= 12.f; float pv = 0.f; for (int k = 0; k < 12; k++) pv += (pr[k] - pm) * (pr[k] - pm);
            for (int t = 0; t < 12; t++) {
                float c = 0.f; for (int pc = 0; pc < 12; pc++) c += (hist[(t + pc) % 12] - hm) * (pr[pc] - pm);
                c /= sqrtf(hv * pv);
                const int maj = mode ? (t + 3) % 12 : t;              // minor keys are stored as their relative major (same notes)
                if (maj == autoTonic && c > cur) cur = c;
                if (c > best) { best = c; bt = maj; }
            }
        }
        if (bt != autoTonic && best > cur + 0.03f) autoTonic = bt;
    }
    void analyse() {
        float m = 0.f, cf = 0.f; bool ok = detect(m, cf);
        if (ok) {                                           // reject one-frame octave jumps, then median-of-3
            if (voiced && fabsf(m - lastMidi) > 6.5f) { if (++jumpRun < 3) ok = false; else jumpRun = 0; } else jumpRun = 0;
        }
        if (ok) {
            if (!voiced) { med[0] = med[1] = med[2] = m; } else { med[0] = med[1]; med[1] = med[2]; med[2] = m; }
            float a = med[0], b = med[1], c = med[2]; midi = std::max(std::min(a, b), std::min(std::max(a, b), c)); conf = cf;
            lastMidi = midi; unvoicedRun = 0; if (++voicedRun >= 2) voiced = true;
        } else { voicedRun = 0; if (++unvoicedRun >= 4) { voiced = false; if (unvoicedRun > 25) target = -1.f; } }
        // adaptive window: about two pitch periods, at least 20 ms, never larger than the fixed W; falls back to W when unvoiced
        if (adaptive) {
            int wt = W;
            if (voiced && midi > 0.f) {
                float f0 = 440.f * exp2f((midi - 69.f) * (1.f / 12.f));
                float period = fsd / std::max(f0, 1.f);
                wt = (int)clampf(2.f * period, 0.020f * fsd, (float)W);
            }
            Weff += (wt - Weff) * 0.25f;                    // move smoothly, never destabilise the frame
        } else Weff = W;
        for (int k = 0; k < 12; k++) hist[k] *= 0.9985f;
        if (ok && voiced) { int pc = ((int)floorf(midi + 0.5f) % 12 + 12) % 12; hist[pc] += conf; }
        if ((++hops % 50) == 0) pickKey();
        const int sc = scale < 0 ? 0 : (scale > 7 ? 7 : scale);
        root = key < 12 ? key : (autoTonic + kRootOffset[sc]) % 12;
        const uint16_t mask = scaleMask(sc);
        if (voiced && ok) {
            const float outMidi = midi + presetSemi;
            const float cand = nearestNote(outMidi, root, mask, target); target = cand;
            if (tuneOn) {
                // V1 vibrato preservation: correct the slow pitch drift fully, pass a configurable fraction of the
                // fast (4-8 Hz) deviation. keepVib = 0 reduces exactly to the original full correction.
                const float dev = outMidi - cand;                       // + = singing above the scale note
                devS += (dev - devS) * 0.05f;                           // slow tracker (~200 ms) separates drift from vibrato
                const float devF = dev - devS;
                desired = clampf(-amt * (devS + (1.f - clampf(keepVib, 0.f, 1.f)) * devF), -3.f, 3.f);
            } else desired = 0.f;
            if (chord > 0) {
                const ChordDef& cd = kChord[chord]; const uint16_t hm = scaleMask(kHarmScale[sc]);
                for (int v = 0; v < cd.n; v++) { int note = stepUp((int)cand, cd.v[v].steps, root, hm) + 12 * cd.v[v].oct; shiftV[v] = (float)note - midi; }
            }
        } else desired = tuneOn ? desired * 0.9f : 0.f;
        if (timeMs <= 1.f) corr = desired; else corr += (desired - corr) * (1.f - expf(-10.f / timeMs));
        if (!tuneOn) devS = 0.f;
        g_tuneDet = voiced ? midi : -1.f; g_tuneTgt = (voiced && target > 0.f) ? target + 0.f : -1.f; g_tuneRoot = root; g_tuneVoiced = voiced ? 1 : 0;
    }
};

const size_t kSub = 2048;

struct Engine {
    std::mutex mtx;
#ifndef VFX_NO_RUBBERBAND
    std::unique_ptr<RubberBand::RubberBandLiveShifter> rb;
#endif
    bool ready = false;
    size_t B = 0, rate = 48000, rbDelay = 0;
    Ring inq, outq;
    std::vector<float> inBlk, outBlk, xin, wet, dryv, dline; size_t dmask = 0, dw = 0, dryDelay = 0;
    LitePitch lite; FxChain fx; NoiseSuppressor ns;
    bool nsActive = false; float nsM = 0.f; int nsWarm = 0;
    Tuner tuner; LitePitch harm[3]; Dly cdelay; std::vector<float> chordv;
    int chordCur = 0; bool chordFresh = false; float chG = 0.f, vg = 0.f, hr[3] = { 1.f, 1.f, 1.f };
    // ---- V1.0 stage state
    Conditioner cond; DynGate gate; Compressor comp; DeEsser deess; LookLimiter lim;
    Dly llook; int lookSmp = 0;                      // pitch lookahead: the tuner runs ahead of the delayed engine input
    std::vector<float> xeng;                         // per-sub-block delayed copy feeding the lead engine + chord voices
    RnDenoiser rn; float rnM = 0.f;                  // optional AI noise suppression + its cross-fade
    Psola psola[3];                                  // V1 formant-preserving chord voices
    LiteRecovery rec;                                // automatic Lite -> RubberBand recovery (Phase 5)
    float compAtkLast = -1.f, compRelLast = -1.f, deessFLast = -1.f;
    bool effLegacy = false, pendLegacy = false;      // effective / pending legacy-comparison mode
    int mfDir = 0; float mf = 1.f;                   // mode transition fade (1 = settled)
    int engine = 0;                 // running pitch engine: 0 none (zero latency), 1 RubberBand, 2 lite
    float xf = 0.f;                 // 0 = direct path, 1 = pitch path (cross-faded)
    bool instant = true, autoLite = false;
    float lastPitch = -1.f, lastForm = -1.f, lastW = 0.f, env = 0.f, gateG = 1.f, load = 0.f;
    int overStreak = 0, epoch = 0;
    std::chrono::steady_clock::time_point last; bool haveLast = false;

    bool rebuild(size_t sr)         // called from the RecordStart hook thread, never from the DSP path
    {
        std::lock_guard<std::mutex> lk(mtx);
#ifndef VFX_NO_RUBBERBAND
        rb.reset();
        rb.reset(new RubberBand::RubberBandLiveShifter(sr, 1,
                 RubberBand::RubberBandLiveShifter::OptionWindowShort |
                 RubberBand::RubberBandLiveShifter::OptionFormantShifted |
                 RubberBand::RubberBandLiveShifter::OptionChannelsApart));
        rate = sr; B = rb->getBlockSize();
#else
        rate = sr; B = 512;
#endif
        inBlk.assign(B, 0.f); outBlk.assign(B, 0.f);
        xin.assign(kSub, 0.f); wet.assign(kSub, 0.f); dryv.assign(kSub, 0.f); xeng.assign(kSub, 0.f);
        inq.init(65536); outq.init(65536);
        llook.init(3072); llook.set(0);                  // 60 ms max lookahead @ 48 kHz, preallocated
        for (auto& ps : psola) ps.init((float)sr);
        rn.init((int)sr, false);
#ifndef VFX_NO_RUBBERBAND
        size_t sd = rb->getStartDelay(); rbDelay = B + sd; if (rbDelay > 16384) rbDelay = 16384;
#endif
        size_t n = 1; while (n < 32768) n <<= 1; dline.assign(n, 0.f); dmask = n - 1; dw = 0;
        lite.init((float)sr); fx.init((float)sr); ns.init((float)sr); tuner.init((float)sr);
        for (auto& h : harm) h.init((float)sr);
        cdelay.init(16384); chordv.assign(kSub, 0.f);
        cond.init((float)sr); gate.init((float)sr); comp.init((float)sr); deess.init((float)sr); lim.init((float)sr);
        compAtkLast = compRelLast = deessFLast = -1.f;
        ready = true;
        softReset();
        g_block = (int)B; g_rate = (int)sr;
        return true;
    }
    void softReset()                // caller holds mtx (or is single-threaded): flush everything that could replay old audio
    {
#ifndef VFX_NO_RUBBERBAND
        if (rb) rb->reset();
#endif
        inq.clear(); outq.clear(); lite.reset(); fx.reset(); ns.flush(); nsActive = false; nsM = 0.f; nsWarm = 0;
        tuner.flush(); for (auto& h : harm) h.reset(); cdelay.clear(); chordCur = 0; chG = 0.f; vg = 0.f; for (int v = 0; v < 3; v++) hr[v] = 1.f;
        std::fill(dline.begin(), dline.end(), 0.f); dw = 0;
        cond.clear(); gate.clear(); comp.clear(); deess.clear(); lim.clear(); llook.clear(); llook.set(0); lookSmp = 0;
        rn.clear(); rnM = 0.f; for (auto& ps : psola) ps.clear();
        effLegacy = pendLegacy = (g_par[P_LEGACY] >= 0.5f);              // re-sync the comparison mode, no fade needed after a gap
        mfDir = 0; mf = 1.f;
        engine = 0; xf = 0.f; instant = true; lastPitch = lastForm = -1.f; lastW = 0.f; env = 0.f; gateG = 1.f; load = 0.f; overStreak = 0;
        g_engineMode = 0; g_latMs = 0.f;
    }
    void reset() { std::lock_guard<std::mutex> lk(mtx); softReset(); }

    void startEngine(int e)
    {
        engine = e;
#ifndef VFX_NO_RUBBERBAND
        if (e == 1) { rb->reset(); inq.clear(); outq.clear(); for (size_t i = 0; i < B; i++) outq.push(0.f); dryDelay = rbDelay; }
        else
#endif
        { lite.reset(); dryDelay = lite.latency(); }
        lastPitch = lastForm = -1.f;
        if (instant) { xf = 1.f; instant = false; }          // first start after a gap: never let the raw voice through
        g_engineMode = e; g_latMs = 1000.f * (float)dryDelay / (float)rate;
    }

    void switchMode()                // audio thread, at the silent midpoint of the V1 <-> legacy fade
    {
        effLegacy = pendLegacy;
        if (effLegacy) { cond.clear(); gate.clear(); comp.clear(); deess.clear(); lim.clear(); }
        else           { env = 0.f; gateG = 1.f; }                // clear the legacy gate state when leaving it
    }

    void process(int16_t* s, int n)   // in-place 16-bit mono, any n
    {
        std::unique_lock<std::mutex> lk(mtx, std::try_to_lock);
        if (!lk.owns_lock() || !ready || n <= 0) return;            // being rebuilt: pass audio through untouched
        auto t0 = std::chrono::steady_clock::now();
        if (haveLast && std::chrono::duration_cast<std::chrono::milliseconds>(t0 - last).count() > 120) { softReset(); g_gapResets++; }
        int ep = g_qualityEpoch.load(); if (ep != epoch) { epoch = ep; autoLite = false; g_autoLite = 0; }

        const float pitch = g_par[P_PITCH], form = g_par[P_FORMANT];
        const bool preserve = g_par[P_PRESERVE] >= 0.5f;
        const float effForm = clampf(form * (preserve ? 1.f / pitch : 1.f), 0.1f, 8.f);
        const float gainLin = powf(10.f, g_par[P_GAIN_DB] / 20.f);
        const bool gateOn = g_par[P_GATE_DB] > -79.5f;
        const float gateThr = powf(10.f, g_par[P_GATE_DB] / 20.f);
        const float mix = g_par[P_MIX];
        const bool nsOn = g_par[P_NS_ON] >= 0.5f; const float nsAmt = g_par[P_NS_AMT], nsClick = g_par[P_NS_CLICK];
        // ---- Phase 4: optional AI noise suppression (classic stays available; cross-fade between engines)
        const bool wantRN = !effLegacy && nsOn && g_par[P_NS_ENGINE] >= 0.5f && !rn.stFailed;
        if (wantRN && !rn.active) { rn.init((int)rate, true); if (rn.active) vlog("noise: AI engine (RNNoise) active, +10 ms latency"); }
        else if (!wantRN && rn.active) rn.init((int)rate, false);
        const float rnTgt = (nsOn && rn.active) ? 1.f : 0.f;
        if (nsOn && !nsActive && rnTgt < 0.999f) { ns.flush(); nsActive = true; nsWarm = (int)ns.N; nsM = 0.f; }
        const int quality = (int)g_par[P_QUALITY];
        const bool tuneOn = g_par[P_TUNE_ON] >= 0.5f; const int chordWant = (int)g_par[P_CHORD];
        const float chordMix = g_par[P_CHORD_MIX], spread = g_par[P_CHORD_SPREAD];
        tuner.tuneOn = tuneOn; tuner.key = (int)g_par[P_TUNE_KEY]; tuner.scale = (int)g_par[P_TUNE_SCALE];
        tuner.timeMs = g_par[P_TUNE_TIME]; tuner.amt = g_par[P_TUNE_AMT]; tuner.presetSemi = 12.f * log2f(pitch);
        const bool vocWantPitch = !effLegacy && (int)g_par[P_VOC_MODE] == 2;     // cyber vocoder carrier follows the voice
        const bool tunerRun = tuneOn || chordWant > 0 || chordCur > 0 || fabsf(tuner.corr) > 0.01f || vocWantPitch;
        const bool wantsTune = tuneOn || chordWant > 0 || chordCur > 0 || fabsf(tuner.corr) > 0.01f;   // keep the pitch engine alive for tune / chords
        // ---- Phase 2 tuner timing: lookahead (engine input delayed, tuner runs ahead), adaptive window, vibrato keep
        tuner.adaptive = !effLegacy;
        tuner.keepVib = effLegacy ? 0.f : clampf(g_par[P_TUNE_KEEP_VIB], 0.f, 1.f);
        lookSmp = effLegacy ? 0 : std::min((int)(g_par[P_TUNE_LOOKAHEAD] * 0.001f * (float)rate + 0.5f), (int)llook.mask - 8);
        if (lookSmp < 0) lookSmp = 0;
        llook.set((size_t)lookSmp);
        // ---- V1.0 dynamics / comparison parameters (read once per call; the state itself is per sample)
        const bool wantLegacy = g_par[P_LEGACY] >= 0.5f;
        if (mfDir == 0 && wantLegacy != effLegacy) { pendLegacy = wantLegacy; mfDir = -1; }   // start the transition fade
        else if (mfDir == -1 && mf <= 0.f) { switchMode(); mfDir = 1; }   // fade-out finished: flip modes BETWEEN chunks,
        //     so the whole next chunk (input stage included) already runs the new mode - no timeline jump inside a chunk
        const float mfStep = 1.f / (0.012f * (float)rate);          // 12 ms fade-out + 12 ms fade-in, click-free
        const float hystLin = powf(10.f, -g_par[P_GATE_HYST_DB] / 20.f);
        const float rangeLin = powf(10.f, -g_par[P_GATE_RANGE_DB] / 20.f);
        const int holdSmp = (int)(g_par[P_GATE_HOLD_MS] * 0.001f * (float)rate);
        const bool compOn = g_par[P_COMP_ON] >= 0.5f;
        const float compThr = powf(10.f, g_par[P_COMP_THRESH_DB] / 20.f);
        const float compRatio = g_par[P_COMP_RATIO], compMk = powf(10.f, g_par[P_COMP_MAKEUP_DB] / 20.f);
        const bool compAuto = g_par[P_COMP_AUTO] >= 0.5f;
        if (compAtkLast != g_par[P_COMP_ATTACK_MS] || compRelLast != g_par[P_COMP_RELEASE_MS]) {
            compAtkLast = g_par[P_COMP_ATTACK_MS]; compRelLast = g_par[P_COMP_RELEASE_MS];
            comp.setTimes(compAtkLast, compRelLast);
        }
        const bool deessOn = g_par[P_DEESS_ON] >= 0.5f;
        const float deessAmt = g_par[P_DEESS_AMT];
        if (deessFLast != g_par[P_DEESS_FREQ]) { deessFLast = g_par[P_DEESS_FREQ]; deess.setFreq(deessFLast); }
        const float ceilLin = powf(10.f, g_par[P_LIMIT_CEIL_DB] / 20.f);
#ifndef VFX_NO_RUBBERBAND
        const bool needShift = fabsf(pitch - 1.f) > 1e-3f || fabsf(effForm - 1.f) > 1e-3f || wantsTune;
#else
        (void)effForm; const bool needShift = fabsf(pitch - 1.f) > 1e-3f || wantsTune;       // Lite has no formant control
#endif
#ifndef VFX_NO_RUBBERBAND
        const bool useLite = quality == 2 || (quality == 0 && autoLite);
#else
        const bool useLite = true;                          // built without RubberBand: Lite is the only pitch engine
#endif
        const int desired = needShift ? (useLite ? 2 : 1) : 0;

        Snap sn;
        sn.hp = g_par[P_HP_HZ]; sn.lp = g_par[P_LP_HZ]; sn.bass = g_par[P_BASS_DB]; sn.treble = g_par[P_TREBLE_DB];
        sn.drive = g_par[P_DRIVE]; sn.bits = g_par[P_BITS]; sn.down = g_par[P_DOWN]; sn.tremHz = g_par[P_TREM_HZ]; sn.tremDepth = g_par[P_TREM_DEPTH];
        sn.modMode = (int)g_par[P_MOD_MODE]; sn.modRate = g_par[P_MOD_RATE]; sn.modDepth = g_par[P_MOD_DEPTH];
        sn.echoMs = g_par[P_ECHO_MS]; sn.echoFb = g_par[P_ECHO_FB]; sn.echoMix = g_par[P_ECHO_MIX];
        sn.revSize = g_par[P_REV_SIZE]; sn.revMix = g_par[P_REV_MIX];
        sn.robot = g_par[P_ROBOT] >= 0.5f; sn.robotHz = g_par[P_ROBOT_HZ]; sn.robotAmt = g_par[P_ROBOT_AMT];
        sn.vocMode = (int)g_par[P_VOC_MODE]; sn.vocHz = g_par[P_VOC_HZ];
        sn.v1 = !effLegacy;
        sn.vocCarrier = (int)g_par[P_VOC_CARRIER];
        sn.revPredMs = g_par[P_REV_PREDELAY_MS]; sn.revDamp = g_par[P_REV_DAMP]; sn.echoDamp = g_par[P_ECHO_DAMP];
        sn.vocF0ok = tuner.voiced && tuner.midi > 0.f;
        sn.vocF0 = sn.vocF0ok ? 440.f * exp2f((tuner.midi - 69.f) * (1.f / 12.f)) : 0.f;
        sn.vocTarget = (tuner.voiced && tuner.target > 0.f) ? tuner.target : -1.f;   // already scale-snapped
        fx.update(sn);

        const float xfStep = 1.f / (0.010f * (float)rate), nsStep = xfStep;
        float pin = 0.f, pout = 0.f;
        const float hA = 1.f - expf(-1.f / (0.012f * (float)rate)), vgA = 1.f - expf(-1.f / (0.015f * (float)rate));
        for (int base = 0; base < n; ) {
            int m = std::min<int>((int)kSub, n - base); if (tunerRun && tuner.hopLeft < m) m = tuner.hopLeft;   // segments end on pitch-analysis hops: result does not depend on chunk size
            int16_t* sp = s + base;
            if (chordCur != chordWant && chG <= 0.001f) {          // chord mode change: fade out, switch, fade in
                chordCur = chordWant; chordFresh = true; for (auto& h : harm) h.reset(); cdelay.clear(); for (int v = 0; v < 3; v++) hr[v] = 1.f;
            }
            tuner.chord = chordCur;
            const float chT = (chordCur == chordWant && chordCur > 0) ? 1.f : 0.f;
            // results of the pitch analysis as they were when this segment started (a segment never contains an analysis hop boundary)
            const float sCorr = tuner.corr, sMidi = tuner.midi; const bool sVoiced = tuner.voiced; const int sHops = tuner.hops;
            const float sShift[3] = { tuner.shiftV[0], tuner.shiftV[1], tuner.shiftV[2] };
            const float t0s = (sVoiced && tunerRun) ? (float)rate / (440.f * exp2f((sMidi - 69.f) * (1.f / 12.f))) : 0.f;   // voice period in samples
            // engine state machine (once per sub-block): fade the old engine out, then start the wanted one
            if (engine != desired && (engine == 0 || xf <= 0.0005f)) { if (desired == 0) { engine = 0; g_engineMode = 0; g_latMs = 0.f; } else startEngine(desired); }
            const float xfTarget = (engine != 0 && engine == desired) ? 1.f : 0.f;

            // 1) input stage: int16 -> float, conditioning, gate (smoothed), compressor, gain; the dry line always runs so a delayed dry copy is always aligned
            VFX_PROFILE_STAGE(0, {
            for (int i = 0; i < m; i++) {
                float x = sp[i] * (1.f / 32768.f);
                float x_pre_gate = x;
                float ax = fabsf(x); if (ax > pin) pin = ax;
                if (!effLegacy) x = cond.proc(x);            // mandatory DC blocker + 75 Hz high-pass (V1 mode)
                float rnOut = x;
                if (rn.active && rnM > 0.0005f) rnOut = rn.proc(x);          // AI engine (frame aligned)
                else if (rn.active) rnOut = rn.passthrough(x);
                rnM += clampf(rnTgt - rnM, -nsStep, nsStep);
                if (nsActive) {                      // noise suppression first: everything after it works on the cleaned mic
                    float xToClassic = x * (1.f - rnM);
                    float yn = ns.proc(xToClassic, nsAmt, nsClick);
                    float tg = (nsOn && nsWarm == 0) ? 1.f : 0.f; if (nsWarm > 0) nsWarm--;
                    nsM += clampf(tg - nsM, -nsStep, nsStep);
                    x = xToClassic * (1.f - nsM) + yn * nsM;
                    if (!nsOn && nsM <= 0.f) nsActive = false;
                } else if (!nsOn && nsM > 0.f) { nsM += clampf(0.f - nsM, -nsStep, nsStep); if (nsM <= 0.f) nsM = 0.f; }
                if (rn.active) {                     // blend AI engine in/out (cross-fade, no pops)
                    x = x * (1.f - rnM) + rnOut * rnM;
                    ax = fabsf(x);
                }
                if (effLegacy) {
                    if (gateOn) {                            // original gate: single threshold, fixed smoothing
                        env = ax > env ? ax : env * 0.9995f;
                        float tg = env > gateThr ? 1.f : 0.f;
                        gateG += (tg - gateG) * (tg > gateG ? 0.05f : 0.002f);
                        x *= gateG;
                        gate.look.proc(x_pre_gate);          // keep the V1 gate lookahead primed for a seamless switch back
                    }
                } else if (gateOn) {                             // V1 gate: hysteresis + hold + soft expansion + 5 ms lookahead
                    x = gate.proc(x, gateThr, hystLin, rangeLin, holdSmp);
                }
                if (!effLegacy && compOn) x = comp.proc(x, compThr, compRatio, compMk, compAuto);
                x *= gainLin; xin[i] = x;
                if (tunerRun) tuner.push(x);
                xeng[i] = llook.proc(x);                                 // engine/chord feed: delayed by tune_lookahead_ms
                dline[dw & dmask] = x;
                dryv[i] = dline[(dw - dryDelay - (size_t)lookSmp) & dmask]; dw++;   // dry path delayed the same amount: stays aligned
            }
            })
            // 2) pitch engine: preset pitch x Auto-Tune correction (updated every 10 ms)
            VFX_PROFILE_STAGE(1, {
            const float leadRatio = clampf(pitch * exp2f(sCorr * (1.f / 12.f)), 0.25f, 4.f);
#ifndef VFX_NO_RUBBERBAND
            if (engine == 1) {
                if (needShift) {
                    if (lastPitch < 0.f || fabsf(leadRatio - lastPitch) > 1e-5f * lastPitch) { rb->setPitchScale(leadRatio); lastPitch = leadRatio; }
                    if (effForm != lastForm) { rb->setFormantScale(effForm); lastForm = effForm; }
                }
                for (int i = 0; i < m; i++) inq.push(xeng[i]);
                while (inq.n >= B) {
                    for (size_t i = 0; i < B; i++) inBlk[i] = inq.pop();
                    const float* ip[1] = { inBlk.data() }; float* op[1] = { outBlk.data() };
                    rb->shift(ip, op);
                    for (size_t i = 0; i < B; i++) outq.push(outBlk[i]);
                }
                for (int i = 0; i < m; i++) { if (outq.n) lastW = outq.pop(); else g_underruns++; wet[i] = lastW; }
            } else
#endif
            if (engine == 2) {
                lite.ratio = leadRatio; lite.setPeriod(t0s);
                for (int i = 0; i < m; i++) wet[i] = lite.proc(xeng[i]);
            }
            // 2b) chord voices: LitePitch (legacy sound) blended toward pitch-synchronous PSOLA (V1 formant-preserving)
            if (chordCur > 0) {
                const ChordDef& cd = kChord[chordCur]; float tg[3] = { 1.f, 1.f, 1.f };
                const size_t own = (size_t)(0.5f * harm[0].Wdef) + 2, lead = (engine == 1) ? dryDelay : (engine == 2 ? own : 0);   // Lite lead and Lite voices share the same adaptive grain, so they stay aligned by themselves
                cdelay.set(lead > own ? lead - own : 0);           // line the voices up with the lead voice (fixed amount: no zipper noise)
                const float tsec = (float)sHops * 0.010f;
                const float cf = effLegacy ? 0.f : clampf(g_par[P_CHORD_FORMANT], 0.f, 1.f);   // 0 = old sound, 1 = PSOLA
                const bool psolaOk = sVoiced && t0s > 8.f && !effLegacy;   // unvoiced / unreliable period -> LitePitch fallback
                for (int v = 0; v < cd.n; v++) {
                    float det = spread * 0.01f * sinf(kTwoPi * (0.23f + 0.17f * (float)v) * tsec + 2.1f * (float)v);   // slow detune = choir-like width
                    tg[v] = exp2f((sShift[v] + det) * (1.f / 12.f));
                    harm[v].setPeriod(t0s);
                    psola[v].setPeriod(t0s);
                    if (chordFresh) hr[v] = tg[v];
                }
                chordFresh = false;
                const float vgT = sVoiced ? 1.f : 0.f;
                const float psRamp = psolaOk ? cf : 0.f;           // fade to LitePitch when the period is unreliable
                for (int i = 0; i < m; i++) {
                    chG += clampf(chT - chG, -nsStep, nsStep); vg += (vgT - vg) * vgA;
                    float cs = 0.f;
                    for (int v = 0; v < cd.n; v++) {
                        hr[v] += (tg[v] - hr[v]) * hA;
                        harm[v].ratio = clampf(hr[v], 0.25f, 4.f);
                        float lv = harm[v].proc(xeng[i]);
                        if (cf > 0.001f && psolaOk) {
                            psola[v].ratio = clampf(hr[v], 0.25f, 4.f);
                            float pv = psola[v].proc(xeng[i]);
                            float blend = psRamp;
                            if (blend >= 0.999f) cs += cd.v[v].gain * pv;                     // PSOLA only: skip the Lite CPU cost
                            else cs += cd.v[v].gain * ((1.f - blend) * lv + blend * pv);
                        } else {
                            if (cf > 0.001f && !psolaOk) psola[v].proc(xeng[i]);              // keep the PSOLA rings in step
                            cs += cd.v[v].gain * lv;
                        }
                    }
                    chordv[i] = cdelay.proc(cs) * (chG * vg * chordMix);
                }
            }
            })
            // 3) cross-fade direct <-> pitch path, de-esser, effect rack, limiter, mode fade, back to int16
            VFX_PROFILE_STAGE(2, {
            for (int i = 0; i < m; i++) {
                float d = xfTarget - xf; xf += d > xfStep ? xfStep : (d < -xfStep ? -xfStep : d);
                float y = xin[i];
                if (engine != 0) { float pp = dryv[i] * (1.f - mix) + wet[i] * mix; y = y * (1.f - xf) + pp * xf; }
                if (chordCur > 0) y = y * (1.f - 0.12f * chordMix * chG) + chordv[i] * xf;
                if (!effLegacy && deessOn) y = deess.proc(y, deessAmt);   // after pitch/chords, before the rack
                y = fx.process(y);
                if (!(y == y)) y = 0.f;
                float ay = fabsf(y);
                if (effLegacy) {
                    if (ay > 0.9f) y = copysignf(0.9f + 0.1f * tanhf((ay - 0.9f) * 10.f), y);   // original soft limiter
                    lim.look.proc(y);                                    // keep the V1 limiter delay line primed for a seamless switch back
                } else {
                    y = lim.proc(y, ceilLin);                            // V1 lookahead limiter
                }
                if (mfDir != 0) {                                        // V1 <-> legacy comparison transition
                    mf += (float)mfDir * mfStep;
                    if (mf <= 0.f) mf = 0.f;                             // hold silence; the mode flips at the next chunk boundary
                    else if (mf >= 1.f) { mf = 1.f; mfDir = 0; }
                } else mf = 1.f;
                y *= mf;
                if (fabsf(y) > pout) pout = fabsf(y);
                int v = (int)lrintf(y * 32767.f); if (v > 32767) v = 32767; if (v < -32768) v = -32768;
                sp[i] = (int16_t)v;
            }
            })
            base += m;
        }
        if (nsActive) { g_nsFloor = ns.floorDb; g_nsRed = ns.redDb * nsM; } else g_nsRed = 0.f;
        float pi = g_peakIn, po = g_peakOut;
        g_peakIn = pin > pi * 0.85f ? pin : pi * 0.85f;
        g_peakOut = pout > po * 0.85f ? pout : po * 0.85f;
        {   // total algorithmic latency: engine dry/wet alignment + V1 additions (limiter, gate lookahead, tune lookahead)
            float latS = (float)dryDelay;
            if (!effLegacy) {
                latS += (float)lim.lat;
                if (gateOn) latS += (float)gate.lat;
                if (engine != 0) latS += (float)lookSmp;             // the pitch path is fed the delayed copy
                if (rn.active && rnM > 0.999f) latS += (float)RnDenoiser::FRM;   // AI suppression frame latency
            }
            g_latMs = 1000.f * latS / (float)rate;
        }
        g_gateGain = (!effLegacy && gateOn) ? gate.gain : 1.f;

        // CPU meter + automatic fallback
        auto t1 = std::chrono::steady_clock::now(); last = t1; haveLast = true;
        float dt = std::chrono::duration<float>(t1 - t0).count(), dur = (float)n / (float)rate, ratio = dt / dur;
        load = load * 0.9f + ratio * 0.1f; g_load = load * 100.f;
        float lp = g_loadPeak; g_loadPeak = ratio * 100.f > lp * 0.995f ? ratio * 100.f : lp * 0.995f;
        if (ratio > 0.95f) { g_overruns++; overStreak++; } else overStreak = 0;
        if (quality == 0 && engine == 1 && !autoLite && (load > 0.75f || overStreak >= 3)) {
            autoLite = true; g_autoLite = 1; vlog("CPU too high (load %.0f%%) -> switching to the lite pitch shifter", load * 100.f);
        }
        // ---- Phase 5: automatic recovery from Lite back to RubberBand
        if (autoLite && quality == 0 && engine == 2) {
#ifndef VFX_NO_RUBBERBAND
            if (rb && rec.wantProbe(dur)) {
                // short bounded probe: one block through the EXISTING RubberBand object (no initialisation here);
                // the result is discarded, the state is reset by startEngine() if we switch back
                auto tp0 = std::chrono::steady_clock::now();
                for (size_t i = 0; i < B; i++) inBlk[i] = (float)((i & 64) - 32) * (1.f / 64.f);
                rb->setPitchScale(1.0);
                const float* ip2[1] = { inBlk.data() }; float* op2[1] = { outBlk.data() };
                rb->shift(ip2, op2);
                float probeLoad = std::chrono::duration<float>(std::chrono::steady_clock::now() - tp0).count() / ((float)B / (float)rate);
                bool ok = LiteRecovery::loadOk(probeLoad);
                rec.onResult(ok);
                vlog("lite recovery probe: load %.0f%% of real time -> %s", probeLoad * 100.f, ok ? "switching back to RubberBand" : "staying on lite");
                if (ok) { autoLite = false; g_autoLite = 0; }   // the engine state machine starts RubberBand on its own
            }
#endif
        } else if (engine == 1) rec.probeT = 0.f;
    }
};
Engine g_eng;
}  // namespace

// Test hook / used by the BASS DSP callback.
extern "C" __attribute__((visibility("default"))) void vfx_process_i16(int16_t* buf, int n) { g_eng.process(buf, n); }
extern "C" __attribute__((visibility("default"))) void vfx_prepare(int rate) { g_eng.rebuild((size_t)rate); }

// ------------------------------------------------------------------ config file
static void cfg_save()
{
    char tmp[512]; snprintf(tmp, sizeof(tmp), "%s.tmp", CFG_PATH);
    FILE* f = fopen(tmp, "w");
    if (!f) f = fopen(CFG_PATH, "w");              // no temp file possible: write directly (best effort)
    if (!f) return;
    for (int i = 0; i < P_COUNT; i++) if (i != P_MONITOR && i != P_LEGACY) fprintf(f, "%s=%.4f\n", kPar[i].name, (double)g_par[i].load());
    fclose(f);
    FILE* chk = fopen(tmp, "r");                   // atomic-ish commit: rename only when the temp file is really there
    if (chk) { fclose(chk); rename(tmp, CFG_PATH); }   // an interrupted save then leaves the previous file intact
}
static void cfg_parse(FILE* f)                    // apply key=value lines to the parameters
{
    char line[128];
    while (fgets(line, sizeof(line), f)) {
        char* eq = strchr(line, '='); if (!eq) continue; *eq = 0;
        for (int i = 0; i < P_COUNT; i++) if (i != P_MONITOR && i != P_LEGACY && !strcmp(line, kPar[i].name)) g_par[i] = sanitize(i, (float)atof(eq + 1));
    }
}
static bool cfg_migrated = false;                 // the import runs at most once per process
static void cfg_load()
{
    FILE* f = fopen(CFG_PATH, "r");
    if (!f && !cfg_migrated) {
        cfg_migrated = true;
        // First start after the rename: import the legacy configuration. The old file is copied
        // (best effort, via a temp file + rename so an interrupted copy cannot corrupt anything)
        // and is NEVER deleted or modified.
        FILE* lg = fopen(VFX_LEGACY_CFG_PATH, "r");
        if (!lg) return;
        char tmp[512]; snprintf(tmp, sizeof(tmp), "%s.tmp", CFG_PATH);
        FILE* w = fopen(tmp, "w");
        char line[128];
        while (fgets(line, sizeof(line), lg)) if (w) fputs(line, w);
        fclose(lg);
        if (w) { fclose(w); rename(tmp, CFG_PATH); }   // if this fails we still import the values below
        f = fopen(CFG_PATH, "r");
        if (!f) { f = fopen(VFX_LEGACY_CFG_PATH, "r"); if (f) { cfg_parse(f); fclose(f); vlog("config: imported legacy settings (values applied, copy to %s failed)", CFG_PATH); } return; }
        vlog("config: imported legacy settings into %s", CFG_PATH);
    }
    if (!f) return;
    cfg_parse(f);
    fclose(f);
}

// ------------------------------------------------------------------ public C API for the menu (Lua FFI)
VFX_EXPORT float vfx_get(int id)  { return (id >= 0 && id < P_COUNT) ? g_par[id].load() : 0.f; }
static void (*g_onParam)(int id) = nullptr;   // set on Android: (re)creates / frees the monitor stream
VFX_EXPORT void  vfx_set(int id, float v)
{
    if (id < 0 || id >= P_COUNT) return;
    g_par[id] = sanitize(id, v);
    if (id == P_QUALITY) g_qualityEpoch++;
    if (g_onParam) g_onParam(id);
}
VFX_EXPORT void  vfx_save(void) { cfg_save(); }
VFX_EXPORT void  vfx_load(void) { cfg_load(); }
// stat ids: 0 hook(1/0/-1) 1 recordStarts 2 dspCalls 3 lastChunkBytes 4 rate 5 formatOK 6 blockSize 7 monitor(1/0/-1)
//           8 api version (9)  9 underruns 10 gap resets 11 overruns (call slower than real time) 12 engine (0 none/1 RubberBand/2 lite) 13 auto-lite active 14 RubberBand compiled in (1/0) 15 key root in use (0..11) 16 voice detected (1/0)
//           17 product version * 100 (= 100 for V1.0)  18 old libvoicefx.so detected in the process (1 = delete it)
VFX_EXPORT int   vfx_stat(int id)
{
    switch (id) { case 0: return g_hookState; case 1: return g_recStarts; case 2: return g_dspCalls;
                  case 3: return g_lastBytes; case 4: return g_rate; case 5: return g_fmtOK; case 6: return g_block; case 7: return g_monState;
                  case 8: return VFX_API_VERSION; case 9: return g_underruns; case 10: return g_gapResets; case 11: return g_overruns;
                  case 12: return g_engineMode; case 13: return g_autoLite; case 15: return g_tuneRoot; case 16: return g_tuneVoiced;
                  case 17: return 100; case 18: return g_oldLibFound;
#ifndef VFX_NO_RUBBERBAND
                  case 14: return 1;
#endif
                }
    return 0;
}
// float stat ids: 0 peakIn 1 peakOut 2 latencyMs 3 cpu load % (average) 4 cpu load % (peak) 5 noise floor dBFS 6 noise reduction dB 7 detected note (midi, -1 = none) 8 tuned note (midi) 9 monitor queue latency ms 10 V1 gate gain (0..1)
VFX_EXPORT float vfx_statf(int id)
{
    switch (id) { case 0: return g_peakIn.load(); case 1: return g_peakOut.load(); case 2: return g_latMs.load();
                  case 3: return g_load.load(); case 4: return g_loadPeak.load();
                  case 5: return g_nsFloor.load(); case 6: return g_nsRed.load();
                  case 7: return g_tuneDet.load(); case 8: return g_tuneTgt.load();
                  case 9: return g_monLat.load(); case 10: return g_gateGain.load();
                  case 11: return g_stageMs[0].load(); case 12: return g_stageMs[1].load(); case 13: return g_stageMs[2].load();
                  case 14: return g_stagePk[0].load(); case 15: return g_stagePk[1].load(); case 16: return g_stagePk[2].load(); }
    return 0.f;
}

// Backwards-compatible table used by the original v4 menus.
struct vc_api_t {
    void (*set_pitch)(float); void (*enable)(void); void (*disable)(void); int (*is_enabled)(void);
    float (*get_pitch)(void); void (*set_formant)(float); float (*get_formant)(void);
};
static void  _set_pitch(float v)   { vfx_set(P_PITCH, v); }
static void  _enable(void)         { vfx_set(P_ENABLED, 1.f); }
static void  _disable(void)        { vfx_set(P_ENABLED, 0.f); }
static int   _is_enabled(void)     { return g_par[P_ENABLED] >= 0.5f; }
static float _get_pitch(void)      { return g_par[P_PITCH]; }
static void  _set_formant(float v) { vfx_set(P_FORMANT, v); }
static float _get_formant(void)    { return g_par[P_FORMANT]; }
VFX_EXPORT vc_api_t vc_api = { _set_pitch, _enable, _disable, _is_enabled, _get_pitch, _set_formant, _get_formant };

