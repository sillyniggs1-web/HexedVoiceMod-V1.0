// Host tests for the HexedVoiceMod V1.0 engine. Build: g++ -std=c++17 -O2 -ffast-math -fno-finite-math-only -I <rubberband or fake> host_test.cpp
// With the fake RubberBand stand-in (pure delay) the plumbing is checked sample-exact; effects are checked numerically.
// NOTE: this is a stand-in for the real RubberBand 4.0 - latency/plumbing is exact, pitch quality is not validated here.
#define VFX_HOST_TEST 1
#define VFX_CFG_PATH        "/tmp/hvm_test/HexedVoiceMod.cfg"
#define VFX_LOG_PATH        "/tmp/hvm_test/HexedVoiceMod_log.txt"
#define VFX_LEGACY_CFG_PATH "/tmp/hvm_test/voicefx.cfg"
#include "../jni/voicefx_dsp.h"
#include <sys/stat.h>
#include <cstdlib>
#include <vector>
#include <cstdio>
#include <thread>
#include <chrono>
static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("  FAIL: "); printf(__VA_ARGS__); printf("\n"); } else { printf("  ok:   "); printf(__VA_ARGS__); printf("\n"); } } while (0)
static std::vector<int16_t> tone(int total, double f, double amp) { std::vector<int16_t> v(total); for (int i = 0; i < total; i++) v[i] = (int16_t)(amp * sin(2 * M_PI * f * i / 48000.0)); return v; }
static std::vector<int16_t> voiceish(int total) { std::vector<int16_t> v(total); for (int i = 0; i < total; i++) { double t = i / 48000.0, e = 0.5 + 0.5 * sin(2 * M_PI * 3 * t);
    v[i] = (int16_t)(e * (9000 * sin(2 * M_PI * 180 * t) + 5000 * sin(2 * M_PI * 540 * t) + 3000 * sin(2 * M_PI * 1500 * t) + 1500 * sin(2 * M_PI * 3100 * t))); } return v; }
static void reset_all() { params_default(); vfx_prepare(48000); vfx_set(P_ENABLED, 1.f); }
static void reset_all_legacy() { params_default(); vfx_set(P_LEGACY, 1.f); vfx_prepare(48000); vfx_set(P_ENABLED, 1.f); }   // legacy set BEFORE prepare: effective immediately, no transition fade
static void run(std::vector<int16_t>& d, int chunk, unsigned seed = 0) { int p = 0, total = (int)d.size(); while (p < total) { int n = chunk; if (seed) { seed = seed * 1664525u + 1013904223u; n = 100 + (seed >> 16) % 1900; } if (p + n > total) n = total - p; vfx_process_i16(d.data() + p, n); p += n; } }
static double rms(const std::vector<int16_t>& d, int a, int b) { double e = 0; for (int i = a; i < b; i++) e += (double)d[i] * d[i]; return sqrt(e / (b - a)) / 32768.0; }
static double db(double x) { return 20 * log10(x + 1e-12); }
static double gainAt(double f, const char* what) { auto in = tone(48000, f, 8000); auto out = in; run(out, 960); (void)what; return db(rms(out, 24000, 48000)) - db(rms(in, 24000, 48000)); }
static int maxJump(const std::vector<int16_t>& d, int a, int b) { int m = 0; for (int i = a + 1; i < b; i++) m = std::max(m, abs((int)d[i] - (int)d[i - 1])); return m; }
static double zcFreq(const std::vector<int16_t>& d, int a, int b) { int z = 0; for (int i = a + 1; i < b; i++) if ((d[i - 1] < 0) != (d[i] < 0)) z++; return z / 2.0 / ((b - a) / 48000.0); }
static int bestLag(const std::vector<int16_t>& a, const std::vector<int16_t>& b, int lo, int hi, int from, int len) {
    int best = lo; double bc = -1;
    for (int lag = lo; lag <= hi; lag++) { double c = 0; for (int i = from; i < from + len; i++) c += (double)a[i] * b[i - lag]; if (c > bc) { bc = c; best = lag; } }
    return best; }
static double corrAt(const std::vector<int16_t>& a, const std::vector<int16_t>& b, int lag, int from, int len) {
    double c = 0, aa = 0, bb = 0;
    for (int i = from; i < from + len; i++) { double x = a[i], y = b[i - lag]; c += x * y; aa += x * x; bb += y * y; }
    return c / sqrt(aa * bb + 1e-9); }


// ---- helpers for the Auto-Tune / chord tests
struct Note { double midi; double dur; };
static std::vector<int16_t> sing(const std::vector<Note>& notes, double amp = 0.22) {
    int total = 0; for (auto& n : notes) total += (int)(n.dur * 48000); std::vector<int16_t> v(total); double ph = 0; int i = 0;
    for (auto& n : notes) { int len = (int)(n.dur * 48000); double f = 440.0 * pow(2.0, (n.midi - 69.0) / 12.0);
        for (int k = 0; k < len; k++, i++) { double s = 0, ps = 0; for (int h = 1; h * f < 6000; h++) { double fh = h * f; double a = (1.0 / h) * (1.0 + 1.5 * exp(-pow((fh - 700) / 250, 2)) + 1.0 * exp(-pow((fh - 1200) / 300, 2))); s += a * sin(h * ph); ps += a; }
            double env = std::min(1.0, k / 400.0) * std::min(1.0, (len - k) / 400.0); v[i] = (int16_t)(amp * 32768.0 * env * s / 2.6); ph += 2 * M_PI * f / 48000.0; } }
    return v; }
static double measureMidi(const std::vector<int16_t>& x, int start, int len) {      // normalised autocorrelation, parabolic peak
    int lagMin = 48000 / 1000, lagMax = 48000 / 70; std::vector<double> r(lagMax + 2, 0.0); double e0 = 0; for (int i = 0; i < len; i++) e0 += (double)x[start + i] * x[start + i];
    if (e0 < 1e3) return -1;
    for (int lag = lagMin; lag <= lagMax + 1; lag++) { double c = 0, e1 = 0; for (int i = 0; i < len - lagMax - 2; i++) { c += (double)x[start + i] * x[start + i + lag]; e1 += (double)x[start + i + lag] * x[start + i + lag]; } r[lag] = c / sqrt(e0 * e1 + 1e-9); }
    int bl = lagMin; double bv = -2; for (int lag = lagMin + 1; lag <= lagMax; lag++) if (r[lag] > bv && r[lag] >= r[lag - 1] && r[lag] >= r[lag + 1]) { bv = r[lag]; bl = lag; }
    for (int lag = lagMin + 1; lag <= lagMax; lag++) if (r[lag] > 0.9 * bv && r[lag] >= r[lag - 1] && r[lag] >= r[lag + 1]) { bl = lag; break; }     // first strong peak (avoid sub-octaves)
    double a = r[bl - 1], b = r[bl], c2 = r[bl + 1], den = a - 2 * b + c2, sh = fabs(den) > 1e-12 ? 0.5 * (a - c2) / den : 0; double lag = bl + sh;
    return 69.0 + 12.0 * log2((48000.0 / lag) / 440.0); }
static double goertzelDb(const std::vector<int16_t>& x, int start, int len, double f) {
    double re = 0, im = 0; for (int i = 0; i < len; i++) { double w = 0.5 - 0.5 * cos(2 * M_PI * i / len), a = 2 * M_PI * f * (start + i) / 48000.0; re += w * x[start + i] * cos(a); im += w * x[start + i] * sin(a); }
    return 10 * log10((re * re + im * im) / ((double)len * len) + 1e-3); }
static double mf(double midi) { return 440.0 * pow(2.0, (midi - 69.0) / 12.0); }
static std::vector<int16_t> singVib(double midi, double depthSt, double rateHz, double dur) {   // one continuous note with vibrato
    int total = (int)(dur * 48000); std::vector<int16_t> v(total); double ph = 0;
    for (int i = 0; i < total; i++) { double t = i / 48000.0; double m = midi + depthSt * sin(2 * M_PI * rateHz * t);
        double f = 440.0 * pow(2.0, (m - 69.0) / 12.0); double s = 0, ps = 0;
        for (int h = 1; h * f < 6000; h++) { double fh = h * f; double a = (1.0 / h) * (1.0 + 1.5 * exp(-pow((fh - 700) / 250, 2)) + 1.0 * exp(-pow((fh - 1200) / 300, 2))); s += a * sin(h * ph); ps += a; }
        double env = std::min(1.0, i / 400.0) * std::min(1.0, (total - i) / 400.0);
        v[i] = (int16_t)(0.22 * 32768.0 * env * s / 2.6); ph += 2 * M_PI * f / 48000.0; }
    return v; }
static void tuneSetup(int key, int scale, double timeMs, double amt, int chord) { reset_all(); vfx_set(P_QUALITY, 2.f); vfx_set(P_TUNE_ON, 1.f); vfx_set(P_TUNE_KEY, (float)key); vfx_set(P_TUNE_SCALE, (float)scale);
    vfx_set(P_TUNE_TIME, (float)timeMs); vfx_set(P_TUNE_AMT, (float)amt); vfx_set(P_CHORD, (float)chord); }

// deterministic fake RNNoise processor used by test [16]: out = 0.5 * in per 480-sample frame
static void* fakeRnCreate(int) { return (void*)(intptr_t)1; }
static void fakeRnFree(void*) {}
static float fakeRnProcess(void*, float* out, const float* in) { for (int i = 0; i < 480; i++) out[i] = in[i] * 0.5f; return 0.3f; }

int main()
{
    printf("[0] HexedVoiceMod V1.0 identity: library API 9, product version stat, config import\n");
    { reset_all();
      CHECK(vfx_stat(8) == 9, "vfx_stat(8) library API version = %d (want 9)", vfx_stat(8));
      CHECK(vfx_stat(17) == 100, "vfx_stat(17) product version * 100 = %d (want 100)", vfx_stat(17));
      CHECK(vfx_stat(18) == 0, "old-library flag on host = %d (the dlopen guard itself lives in main.cpp)", vfx_stat(18)); }
    { system("mkdir -p /tmp/hvm_test; rm -f /tmp/hvm_test/HexedVoiceMod.cfg /tmp/hvm_test/HexedVoiceMod.cfg.tmp /tmp/hvm_test/voicefx.cfg");
      FILE* f = fopen(VFX_LEGACY_CFG_PATH, "w"); if (f) { fprintf(f, "pitch=1.5\ngain_db=-6\nenabled=1\nbogus=7\n"); fclose(f); }
      params_default(); vfx_load();
      CHECK(fabs(vfx_get(P_PITCH) - 1.5f) < 1e-4, "legacy config imported: pitch %.4f (want 1.5)", vfx_get(P_PITCH));
      CHECK(fabs(vfx_get(P_GAIN_DB) + 6.f) < 1e-4, "legacy config imported: gain_db %.2f (want -6)", vfx_get(P_GAIN_DB));
      struct stat st;
      CHECK(stat(VFX_CFG_PATH, &st) == 0, "new config file created by the import");
      CHECK(stat(VFX_LEGACY_CFG_PATH, &st) == 0, "legacy config still present (never deleted)");
      vfx_set(P_PITCH, 2.f); vfx_save();
      f = fopen(VFX_LEGACY_CFG_PATH, "r"); char buf[64] = { 0 }; int has15 = 0; if (f) { while (fgets(buf, sizeof(buf), f)) if (!strncmp(buf, "pitch=1.5", 9)) has15 = 1; fclose(f); }
      CHECK(has15 == 1, "legacy file content untouched by the new save");
      params_default(); vfx_load();
      CHECK(fabs(vfx_get(P_PITCH) - 2.f) < 1e-4, "after migration the new config wins, not the legacy one: pitch %.4f", vfx_get(P_PITCH));
      vfx_load();  // second call: migration must not re-import over the new settings
      CHECK(fabs(vfx_get(P_PITCH) - 2.f) < 1e-4, "no re-import on subsequent loads: pitch %.4f", vfx_get(P_PITCH));
      reset_all(); }

    printf("[1] Normal preset (enabled, no pitch shift): legacy = bit exact, V1 = conditioning + limiter delay only\n");
    for (unsigned sd : { 0u, 4242u }) { reset_all_legacy(); auto in = voiceish(96000), out = in; run(out, 960, sd);
        int bad = 0; for (size_t i = 0; i < in.size(); i++) if (abs(out[i] - in[i]) > 1) bad++;
        CHECK(bad == 0, "legacy %s chunks: %d mismatching samples (bit exact)", sd ? "random" : "960", bad); }
    { reset_all(); auto in = voiceish(96000), out = in; run(out, 960);
      int lag = bestLag(out, in, 60, 240, 48000, 47000);
      double cr = corrAt(out, in, lag, 48000, 47000);
      CHECK(lag >= 140 && lag <= 148, "V1 passthrough alignment: best lag %d (want 144 = limiter lookahead, speech tolerance ±4)", lag);
      CHECK(cr > 0.75, "V1 passthrough correlation at that offset: %.3f (HPF phase turns the harmonics differently)", cr);
      double lv = db(rms(out, 48000, 96000)) - db(rms(in, 48000, 96000));
      CHECK(fabs(lv) < 0.6, "V1 passthrough level %+.2f dB (conditioning only)", lv);
      reset_all(); auto o2 = voiceish(96000); run(o2, 0, 555);
      int bad = 0; for (size_t i = 0; i < o2.size(); i++) if (o2[i] != out[i]) bad++;
      CHECK(bad == 0, "V1 passthrough: chunk size does not change the output (%d differ)", bad); }

    printf("[1b] Phase 1 dynamics: DC/HPF, gate, compressor, de-esser, limiter, V1/legacy compare\n");
    // (a) DC blocker
    { std::vector<int16_t> dc(48000, 8000);
      reset_all(); auto out = dc; run(out, 960);
      CHECK(rms(out, 24000, 48000) < 0.005, "V1 conditioning removes DC: rms %.4f", rms(out, 24000, 48000));
      reset_all(); vfx_set(P_LEGACY, 1.f); out = dc; run(out, 960);
      int bad = 0; for (size_t i = 4000; i < 48000; i++) if (abs(out[i] - 8000) > 1) bad++;
      CHECK(bad == 0, "legacy keeps DC unchanged (%d mismatching samples)", bad); }
    // (b) fixed 75 Hz high-pass
    { reset_all(); double g30 = gainAt(30, ""), g500 = gainAt(500, "");
      CHECK(g30 < -14, "75 Hz HPF: 30 Hz %.1f dB (strong cut)", g30);
      CHECK(fabs(g500) < 1.0, "75 Hz HPF: 500 Hz %+.2f dB (transparent)", g500); }
    // (c) gate hysteresis: between close and open thresholds the gate stays open in V1, closes in legacy
    { auto loud = tone(24000, 440, 2000), mid = tone(24000, 440, 180); std::vector<int16_t> sig(24000, 0);
      sig.insert(sig.end(), loud.begin(), loud.end()); sig.insert(sig.end(), mid.begin(), mid.end());
      double midRms = rms(mid, 0, 24000) ;
      reset_all(); vfx_set(P_GATE_DB, -40.f); vfx_set(P_GATE_HYST_DB, 12.f); vfx_set(P_GATE_HOLD_MS, 10.f);
      auto o1 = sig; run(o1, 960);
      double v1 = rms(o1, 48000, 71000) / (midRms + 1e-9);
      reset_all(); vfx_set(P_LEGACY, 1.f); vfx_set(P_GATE_DB, -40.f);
      auto o2 = sig; run(o2, 960);
      double lg = rms(o2, 48000, 71000) / (midRms + 1e-9);
      CHECK(v1 > 0.7, "V1 gate hysteresis: level between close/open stays open (%.2f of unity)", v1);
      CHECK(lg < 0.5, "legacy gate: same level is below its single threshold (%.2f of unity)", lg); }
    // (d) gate hold: after the envelope falls below the threshold the gate stays open for the hold time (read via vfx_statf(10))
    { std::vector<int16_t> sig; auto loud = tone(9600, 440, 1200); sig.insert(sig.end(), loud.begin(), loud.end());   // 200 ms above threshold
      std::vector<int16_t> qu(5760, 0); sig.insert(sig.end(), qu.begin(), qu.end());                                  // then 120 ms of quiet below it
      auto probe = [&](float holdMs) {
        reset_all(); vfx_set(P_GATE_DB, -40.f); vfx_set(P_GATE_HYST_DB, 0.f); vfx_set(P_GATE_HOLD_MS, holdMs);
        vfx_process_i16(sig.data(), 9600 + 4800);                    // 200 ms tone + 100 ms into the quiet part
        return vfx_statf(10);
      };
      float gHold = probe(40.f), gNo = probe(0.f);
      CHECK(gHold > 0.85f && gNo < 0.8f, "gate hold: gain 100 ms below threshold: hold 40 ms %.3f (still open) vs hold 0 %.3f (released)", gHold, gNo); }
    // (e) compressor gain curve (RMS 6.99 dB over threshold, ratio 2 -> ~3.5 dB gain reduction)
    { reset_all(); vfx_set(P_COMP_ON, 1.f); vfx_set(P_COMP_THRESH_DB, -20.f); vfx_set(P_COMP_RATIO, 2.f);
      vfx_set(P_COMP_ATTACK_MS, 5.f); vfx_set(P_COMP_RELEASE_MS, 100.f); vfx_set(P_COMP_MAKEUP_DB, 0.f); vfx_set(P_COMP_AUTO, 0.f);
      auto in = tone(48000, 440, 10362), out = in; run(out, 960);
      double g1 = db(rms(out, 24000, 48000)) - db(rms(in, 24000, 48000));
      CHECK(g1 < -2.0 && g1 > -5.0, "compressor: 6.9 dB RMS over threshold, ratio 2 -> %+.2f dB (want about -3.5)", g1);
      auto in2 = tone(48000, 440, 1036), out2 = in2; run(out2, 960);
      double g2 = db(rms(out2, 24000, 48000)) - db(rms(in2, 24000, 48000));
      CHECK(fabs(g2) < 0.8, "compressor: below threshold %+.2f dB (want 0)", g2); }
    // (f) de-esser: 6 kHz content is reduced, 1 kHz content is not
    { reset_all(); vfx_set(P_DEESS_ON, 1.f); vfx_set(P_DEESS_AMT, 1.f); vfx_set(P_DEESS_FREQ, 5500.f);
      double g6k = gainAt(6000, ""), g1k = gainAt(1000, "");
      CHECK(g6k < -6, "de-esser amt 1: 6 kHz %+.1f dB (in band, reduced)", g6k);
      CHECK(fabs(g1k) < 1.5, "de-esser amt 1: 1 kHz %+.2f dB (untouched)", g1k);
      reset_all(); vfx_set(P_DEESS_ON, 1.f); vfx_set(P_DEESS_AMT, 0.f); double g6b = gainAt(6000, "");
      CHECK(fabs(g6b) < 1.5, "de-esser amt 0: 6 kHz %+.2f dB (transparent)", g6b); }
    // (g) limiter ceiling enforcement + unity below the ceiling
    { reset_all(); vfx_set(P_LIMIT_CEIL_DB, -1.f);
      auto in = tone(48000, 300, 32767), out = in; run(out, 960);
      int pk = 0; for (int i = 4800; i < 48000; i++) pk = std::max(pk, abs((int)out[i]));
      CHECK(pk <= 32767.0 * 0.8913 * 1.01 + 1, "limiter: full-scale tone peak %d <= -1 dBFS ceiling (+1%% tol)", pk);
      reset_all(); auto in2 = tone(48000, 300, 9000), out2 = in2; run(out2, 960);
      int pk2 = 0; for (int i = 4800; i < 48000; i++) pk2 = std::max(pk2, abs((int)out2[i]));
      CHECK(fabs(db(pk2 / 9000.0)) < 0.4, "limiter: unity below the ceiling (%+.2f dB)", db(pk2 / 9000.0)); }
    // (h) V1 vs legacy defaults: only conditioning + the 144-sample limiter delay differ
    { auto in = voiceish(96000);
      reset_all(); auto v1 = in; run(v1, 960);
      reset_all_legacy(); auto lg = in; run(lg, 960);
      int lag = bestLag(v1, lg, 60, 260, 48000, 47000);
      CHECK(lag >= 140 && lag <= 148, "V1 vs legacy best offset %d (want 144 ±4)", lag);
      double cr = corrAt(v1, lg, lag, 48000, 47000);
      CHECK(cr > 0.75, "V1 vs legacy correlation %.3f", cr);
      double lv = db(rms(v1, 48000, 96000)) - db(rms(lg, 48000, 96000));
      CHECK(fabs(lv) < 0.7, "V1 vs legacy level difference %+.2f dB", lv); }
    // (i) chunk-size independence with every dynamics stage active
    { auto in = voiceish(96000), o1 = in, o2 = in, o3 = in;
      reset_all(); vfx_set(P_GATE_DB, -50.f); vfx_set(P_COMP_ON, 1.f); vfx_set(P_DEESS_ON, 1.f); run(o1, 960);
      reset_all(); vfx_set(P_GATE_DB, -50.f); vfx_set(P_COMP_ON, 1.f); vfx_set(P_DEESS_ON, 1.f); run(o2, 0, 4242);
      int bad = 0; for (size_t i = 0; i < in.size(); i++) if (o1[i] != o2[i]) bad++;
      CHECK(bad == 0, "gate+comp+de-esser+limiter: 960 vs random chunks: %d differing samples", bad);
      reset_all(); vfx_set(P_GATE_DB, -50.f); vfx_set(P_COMP_ON, 1.f); vfx_set(P_DEESS_ON, 1.f); run(o3, 1);
      bad = 0; for (size_t i = 0; i < in.size(); i++) if (o1[i] != o3[i]) bad++;
      CHECK(bad == 0, "same, 1-sample chunks: %d differing samples", bad); }
    // (j) extreme / NaN parameter values on the new ids stay sane
    { reset_all(); for (int id = P_LEGACY; id < P_COUNT; id++) { vfx_set(id, 1e9f); vfx_set(id, -1e9f); vfx_set(id, 0.f / 0.f); }
      auto in = voiceish(48000), out = in; run(out, 960);
      int bad = 0; for (auto x : out) if (x == -32768 || x == 32767) bad++;
      CHECK(bad < 2000, "extreme/NaN parameters: railed samples %d", bad);
      reset_all(); }
    // (k) no startup click
    { reset_all(); auto in = tone(48000, 300, 12000), out = in; run(out, 960);
      CHECK(maxJump(out, 0, 2400) < 1200, "startup: largest step %d (natural ~471)", maxJump(out, 0, 2400)); }
    // (l) live V1 <-> legacy toggle: click-free (12 ms fade-out / 12 ms fade-in through a short dip)
    { reset_all(); auto in = tone(96000, 300, 9000), out = in; int p = 0;
      while (p < (int)in.size()) { if (p == 48000) vfx_set(P_LEGACY, 1.f); if (p == 72000) vfx_set(P_LEGACY, 0.f); vfx_process_i16(out.data() + p, 960); p += 960; }
      CHECK(maxJump(out, 20000, 94000) < 900, "legacy toggle: largest step %d (natural 353)", maxJump(out, 20000, 94000)); }

    printf("[2] pitch path alignment (fake RubberBand = pure delay): mix 1 and 0.5, constant and random chunks\n");
#ifndef VFX_NO_RUBBERBAND
    for (float mix : { 1.f, 0.5f }) for (unsigned sd : { 0u, 777u }) { reset_all(); vfx_set(P_PITCH, 2.f); vfx_set(P_MIX, mix);
        auto in = voiceish(144000), out = in; run(out, 960, sd);
        int lag = bestLag(out, in, 2048 - 100, 2048 + 300, 80000, 48000);
        CHECK(lag >= 2188 && lag <= 2196, "mix %.1f %s chunks: alignment lag %d (want 2192 = 2048 engine + 144 limiter, ±4)", mix, sd ? "random" : "960", lag); }

#endif
    printf("[3] switching presets while talking: no click on the fades, no dead gap on the direct path\n");
    { reset_all(); auto in = tone(144000, 300, 9000), out = in; vfx_set(P_PITCH, 2.f);
      int p = 0; int jumpMax = 0; int minAfter = 99999;
      while (p < (int)in.size()) { int n = 960; if (p == 48000) vfx_set(P_PITCH, 1.f); if (p == 96000) vfx_set(P_PITCH, 2.f); vfx_process_i16(out.data() + p, n); p += n; }
      // NOTE: the block-based pitch engine refills for ~45 ms after a mid-speech start; that pre-existing warm-up gap
      // (present in the original engine as well) is excluded here. The fades themselves must be click-free.
      int j1 = maxJump(out, 40000, 95900);   // 300 Hz @ 9000 amp: natural max step = 353; covers the on->direct fade
      int j2 = maxJump(out, 99500, 140000);  // steady pitch path after the warm-up
      CHECK(j1 < 800 && j2 < 800, "largest fade steps: %d / %d (natural max 353)", j1, j2);
      double r = rms(out, 60000, 90000); CHECK(r > 0.15, "level after switching to direct: %.3f", r); }

    printf("[4] mic burst: stale audio from the previous burst must not replay\n");
    { reset_all(); vfx_set(P_PITCH, 2.f); auto a = tone(24000, 400, 12000); run(a, 960);
      std::this_thread::sleep_for(std::chrono::milliseconds(200));            // pause > 120 ms
      std::vector<int16_t> sil(9600, 0); run(sil, 960);
      CHECK(rms(sil, 0, 9600) < 1e-4, "first 200 ms of the next burst (silent input) is silent: rms %.5f", rms(sil, 0, 9600)); }

    printf("[5] filters (frequency response through the full engine)\n");
    { reset_all(); vfx_set(P_HP_HZ, 400.f); double g1 = gainAt(100, ""), g2 = gainAt(1000, ""); CHECK(g1 < -20 && fabs(g2) < 1.0, "high-pass 400 Hz: 100 Hz %.1f dB, 1 kHz %.1f dB", g1, g2);
      reset_all(); vfx_set(P_LP_HZ, 2000.f); g1 = gainAt(6000, ""); g2 = gainAt(500, ""); CHECK(g1 < -20 && fabs(g2) < 1.0, "low-pass 2 kHz: 6 kHz %.1f dB, 500 Hz %.1f dB", g1, g2);
      reset_all(); vfx_set(P_BASS_DB, 9.f); g1 = gainAt(90, ""); g2 = gainAt(2000, ""); CHECK(g1 > 5 && fabs(g2) < 1.0, "bass +9 dB: 90 Hz %+.1f dB (75 Hz HPF takes ~2 dB there), 2 kHz %+.1f dB", g1, g2);
      reset_all(); vfx_set(P_TREBLE_DB, -9.f); g1 = gainAt(9000, ""); g2 = gainAt(500, ""); CHECK(g1 < -7 && fabs(g2) < 1.0, "treble -9 dB: 9 kHz %+.1f dB, 500 Hz %+.1f dB", g1, g2); }

    printf("[6] drive keeps a sane level; bit-crusher/tremolo/ring-mod run and stay finite\n");
    { for (float d : { 0.2f, 0.6f, 1.f }) { reset_all(); vfx_set(P_DRIVE, d); double g = gainAt(500, ""); CHECK(g > -6 && g < 8, "drive %.1f: level change %+.1f dB", d, g); }
      reset_all(); vfx_set(P_BITS, 6.f); vfx_set(P_DOWN, 6.f); double g = gainAt(500, ""); CHECK(g > -6 && g < 3, "8-bit style (6 bit, /6): %+.1f dB", g);
      reset_all(); vfx_set(P_TREM_DEPTH, 1.f); vfx_set(P_TREM_HZ, 5.f); auto in = tone(48000, 400, 8000), out = in; run(out, 960);
      double lo = 1e9, hi = 0; for (int i = 12000; i < 48000; i += 480) { double r = rms(out, i, i + 480); lo = std::min(lo, r); hi = std::max(hi, r); }
      CHECK(lo < hi * 0.2, "tremolo depth 1: envelope min/max %.3f/%.3f", lo, hi);
      reset_all(); vfx_set(P_ROBOT, 1.f); vfx_set(P_ROBOT_HZ, 50.f); vfx_set(P_ROBOT_AMT, 1.f); in = tone(48000, 400, 8000); out = in; run(out, 960);
      lo = 1e9; hi = 0; for (int i = 12000; i < 48000; i += 120) { double r = rms(out, i, i + 120); lo = std::min(lo, r); hi = std::max(hi, r); }
      CHECK(lo < hi * 0.7, "ring modulator envelope varies: %.3f/%.3f", lo, hi); }

    printf("[7] echo / reverb / chorus / flanger / vibrato\n");
    { reset_all(); vfx_set(P_ECHO_MS, 250.f); vfx_set(P_ECHO_FB, 0.5f); vfx_set(P_ECHO_MIX, 1.f); std::vector<int16_t> d(48000, 0);
      for (int i = 0; i < 100; i++) d[4800 + i] = (int16_t)(16000 * sin(2 * M_PI * 3000 * i / 48000.0));   // tone burst after the conditioner fade-in
      run(d, 960);
      int e1 = 4800 + 100 + 12000; double a1 = 0, a2 = 0;                   // +144: the V1 limiter delay shifts the whole output
      for (int i = -60; i < 60; i++) { a1 = std::max(a1, (double)abs(d[e1 + 144 + i])); a2 = std::max(a2, (double)abs(d[e1 + 12000 + 144 + i])); }
      CHECK(a1 > 8000 && a2 > a1 * 0.3 && a2 < a1 * 0.8, "echo: 1st repeat at 250 ms (%.0f), 2nd at 500 ms is ~half (%.0f)", a1, a2);
      reset_all(); vfx_set(P_REV_MIX, 0.8f); vfx_set(P_REV_SIZE, 0.8f); std::vector<int16_t> r(96000, 0);
      for (int i = 0; i < 480; i++) r[i] = (int16_t)(12000 * sin(2 * M_PI * 3000 * i / 48000.0));   // 10 ms tone burst (conditioning removes DC pulses)
      run(r, 960);
      double e_early = rms(r, 4800, 14400), e_late = rms(r, 72000, 96000); CHECK(e_early > 0.0015 && e_late < e_early * 0.5, "reverb tail decays (FDN level differs from Freeverb): %.4f -> %.4f", e_early, e_late);
      std::vector<int16_t> nz(96000); unsigned s = 1; for (auto& x : nz) { s = s * 1664525u + 1013904223u; x = (int16_t)(((int)(s >> 16) - 32768) / 4); }
      reset_all(); vfx_set(P_REV_MIX, 0.5f); auto nin = nz; run(nin, 960); double g = db(rms(nin, 48000, 96000)) - db(rms(nz, 48000, 96000)); CHECK(g > -3 && g < 8, "reverb 50%% on noise: level change %+.1f dB", g);
      for (int mode = 1; mode <= 3; mode++) { reset_all(); vfx_set(P_MOD_MODE, (float)mode); vfx_set(P_MOD_DEPTH, 1.f); vfx_set(P_MOD_RATE, 4.f); auto v = voiceish(96000), o = v; run(o, 960);
        double gg = db(rms(o, 48000, 96000)) - db(rms(v, 48000, 96000)); CHECK(gg > -8 && gg < 6, "mod mode %d: level change %+.1f dB", mode, gg); } }

    printf("[8] vocoder (whisper / cyber): stable, level matched, voice is gone\n");
    for (int mode = 1; mode <= 2; mode++) { reset_all(); vfx_set(P_VOC_MODE, (float)mode); auto v = voiceish(144000), o = v; run(o, 960);
        double gg = db(rms(o, 96000, 144000)) - db(rms(v, 96000, 144000)); CHECK(gg > -8 && gg < 6 && maxJump(o, 100000, 144000) < 30000, "voc mode %d: level change %+.1f dB", mode, gg); }

    printf("[9] lite pitch shifter (built-in): measured pitch of a 300 Hz tone\n");
    for (float ratio : { 0.7071f, 1.26f, 2.f }) { reset_all(); vfx_set(P_QUALITY, 2.f); vfx_set(P_PITCH, ratio); auto in = tone(96000, 300, 9000), o = in; run(o, 960);
        double f = zcFreq(o, 48000, 96000); CHECK(fabs(f / 300.0 - ratio) < 0.06, "ratio %.3f -> %.1f Hz (want %.1f), latency %.1f ms", ratio, f, 300 * ratio, vfx_statf(2)); }

#ifndef VFX_NO_RUBBERBAND
    printf("[10] auto fallback to lite when the CPU is overloaded\n");
    { reset_all(); vfx_set(P_PITCH, 1.5f); auto in = tone(48000, 300, 9000), o = in; run(o, 960); CHECK(vfx_stat(13) == 0 && vfx_stat(12) == 1, "idle host: RubberBand engine, auto-lite %d", vfx_stat(13)); }

#endif
    printf("[11] robustness: random parameters + random chunks for 20 s of audio, no NaN/overflow/crash\n");
    { reset_all(); unsigned s = 99; auto v = voiceish(48000 * 20); int bad = 0; size_t p = 0;
      while (p < v.size()) { s = s * 1664525u + 1013904223u; int id = 1 + (s >> 8) % (P_COUNT - 1); if (id == P_MONITOR) id = P_PITCH; s = s * 1664525u + 1013904223u;
        float lo = kPar[id].lo, hi = kPar[id].hi; vfx_set(id, lo + (hi - lo) * ((s >> 8) % 1000) / 1000.f); if (id == P_ENABLED) vfx_set(P_ENABLED, 1.f);
        s = s * 1664525u + 1013904223u; int n = 50 + (s >> 16) % 4000; if (p + n > v.size()) n = (int)(v.size() - p); vfx_process_i16(v.data() + p, n); p += n; }
      for (auto x : v) if (x == -32768) bad++; CHECK(bad < 50, "samples hitting the rail: %d", bad); }


    printf("[12] noise suppression\n");
    {
        auto fan = [](int total, double rmsTarget, unsigned seed) { std::vector<double> v(total); double lp = 0, lp2 = 0; unsigned r = seed;
            for (int i = 0; i < total; i++) { r = r * 1664525u + 1013904223u; double w = ((int)(r >> 8) - 8388608) / 8388608.0; lp += (w - lp) * 0.08; lp2 += (lp - lp2) * 0.3; v[i] = lp2 + 0.3 * w; }
            double e = 0; for (double x : v) e += x * x; double g = rmsTarget / sqrt(e / total); for (auto& x : v) x *= g; return v; };
        auto speech = [](int total) { std::vector<double> v(total, 0.0); for (int i = 0; i < total; i++) { double t = i / 48000.0; double ph = fmod(t, 1.0);
            double env = (ph > 0.25 && ph < 0.85) ? 0.5 + 0.5 * sin(2 * M_PI * 4 * t) : 0.0; env *= 0.5 * (1 + sin(2 * M_PI * 1.7 * t));
            double f0 = 140 + 20 * sin(2 * M_PI * 0.7 * t), s = 0; for (int h = 1; h <= 14; h++) s += sin(2 * M_PI * f0 * h * t + h) / h; v[i] = 0.12 * env * s; } return v; };
        auto toI16 = [](const std::vector<double>& a) { std::vector<int16_t> o(a.size()); for (size_t i = 0; i < a.size(); i++) o[i] = (int16_t)std::max(-32767.0, std::min(32767.0, a[i] * 32768.0)); return o; };
        const int T = 48000 * 8;
        // (a) fan only, three strengths
        for (float amt : { 0.3f, 0.6f, 1.0f }) { reset_all(); vfx_set(P_NS_ON, 1.f); vfx_set(P_NS_AMT, amt); auto in = toI16(fan(T, 0.012, 5)), out = in; run(out, 960);
            double red = db(rms(in, 48000 * 4, T)) - db(rms(out, 48000 * 4, T));
            CHECK(red > (amt < 0.5f ? 8 : (amt < 0.9f ? 12 : 18)), "fan noise only, strength %.1f: reduced by %.1f dB (floor est %.1f dBFS)", amt, red, vfx_statf(5)); }
        // (b) clean speech must survive
        { reset_all(); vfx_set(P_NS_ON, 1.f); vfx_set(P_NS_AMT, 0.6f); auto in = toI16(speech(T)), out = in; run(out, 960);
          int bl = bestLag(out, in, 600, 720, 48000 * 2, T - 48000 * 2);
          double lv = db(rms(out, 48000 * 2, T)) - db(rms(in, 48000 * 2 - bl, T - bl)); double c = 0, a = 0, b2 = 0;
          for (int i = 48000 * 2; i < T; i++) { c += (double)out[i] * in[i - bl]; a += (double)out[i] * out[i]; b2 += (double)in[i - bl] * in[i - bl]; }
          CHECK(fabs(lv) < 1.5 && c / sqrt(a * b2) > 0.7 && abs(bl - 656) <= 4,
                "clean speech: level %+.2f dB, best alignment %d (want 656±4), correlation there %.3f", lv, bl, c / sqrt(a * b2)); }
        // (c) speech over fan: gaps get quiet, speech keeps its level, SNR improves
        { reset_all(); vfx_set(P_NS_ON, 1.f); vfx_set(P_NS_AMT, 0.7f); auto sp = speech(T), fn = fan(T, 0.012, 9); std::vector<double> mixd(T); for (int i = 0; i < T; i++) mixd[i] = sp[i] + fn[i];
          auto in = toI16(mixd), out = in; run(out, 960);
          // gaps: t mod 1 in [0.9,1.0) ; speech: [0.4,0.8)
          double gi = 0, go = 0, si = 0, so = 0; int gn = 0, sn = 0;
          for (int i = 48000 * 3; i < T; i++) { double ph = fmod(i / 48000.0, 1.0); double xi = in[i] / 32768.0, xo = out[i + 0] / 32768.0;
              if (ph > 0.9) { gi += xi * xi; go += xo * xo; gn++; } else if (ph > 0.45 && ph < 0.75) { si += xi * xi; so += xo * xo; sn++; } }
          double gap = 10 * log10(gi / go), sl = 10 * log10(so / si);
          CHECK(gap > 10 && sl > -4, "speech over fan: noise between words -%.1f dB, speech level %+.1f dB", gap, sl); }
        // (d) keyboard clicks over a quiet room
        { std::vector<double> sig = fan(T, 0.004, 3); for (int c = 0; c < 18; c++) { int at = 48000 + c * 19000; unsigned r = 77 + c; for (int i = 0; i < 160; i++) { r = r * 1664525u + 1013904223u; double w = ((int)(r >> 8) - 8388608) / 8388608.0; sig[at + i] += 0.25 * w * exp(-i / 40.0); } }
          double pk[2];
          for (int ci = 0; ci < 2; ci++) { reset_all(); vfx_set(P_NS_ON, 1.f); vfx_set(P_NS_AMT, 0.6f); vfx_set(P_NS_CLICK, ci ? 1.f : 0.f); auto in = toI16(sig), out = in; run(out, 960);
              double m = 0; for (int c = 4; c < 18; c++) { int at = 48000 + c * 19000 + 512; int pkc = 0; for (int i = 0; i < 400; i++) pkc = std::max(pkc, (int)abs(out[at + i])); m += pkc; } pk[ci] = m / 14.0; }
          CHECK(pk[1] < pk[0] * 0.6 && pk[1] < 0.25 * 32768 * 0.5, "clicks: avg peak %.0f with click guard 0, %.0f with guard 1 (%.1f dB lower)", pk[0], pk[1], db(pk[0]) - db(pk[1])); }
        // (e) deterministic: chunk size does not change the result
        { reset_all(); vfx_set(P_NS_ON, 1.f); auto in = toI16(speech(96000)), o1 = in, o2 = in; run(o1, 960); reset_all(); vfx_set(P_NS_ON, 1.f); run(o2, 0, 31337); int bad = 0; for (size_t i = 0; i < in.size(); i++) if (o1[i] != o2[i]) bad++;
          CHECK(bad == 0, "960-sample chunks vs random chunks: %d differing samples", bad); }
        // (f) switching it on and off while talking: no click
        { reset_all(); auto in = tone(48000 * 4, 300, 9000), out = in; int p = 0; while (p < (int)in.size()) { if (p == 48000) vfx_set(P_NS_ON, 1.f); if (p == 144000) vfx_set(P_NS_ON, 0.f); vfx_process_i16(out.data() + p, 960); p += 960; }
          int j = maxJump(out, 20000, 190000); CHECK(j < 900, "largest step while toggling noise suppression: %d (natural max 353)", j); }
        // (g) latency of the suppressor path
        { reset_all(); vfx_set(P_NS_ON, 1.f); vfx_set(P_NS_AMT, 0.2f); auto in = toI16(speech(48000 * 3)), out = in; run(out, 960); int best = 0; double bc = -1;
          for (int lag = 450; lag < 850; lag++) { double c = 0; for (int i = 60000; i < 100000; i++) c += (double)out[i] * in[i - lag]; if (c > bc) { bc = c; best = lag; } }
          CHECK(abs(best - 656) <= 4, "added latency %d samples (%.1f ms, want 656 = 512 NS + 144 limiter, speech tolerance ±4)", best, best / 48.0); }
    }

    printf("[13] Auto-Tune: pitch tracker, scale snapping, retune speed (Lite pitch engine, real pitch shifting)\n");
    printf("[13b] Phase 2 tuner timing: transition lag, lookahead alignment, vibrato keep\n");
    {
        // (a) note-transition response: how long after a note change the tracker reports the new note
        auto switchLagMs = [&](double from, double to, int adaptive) {
            tuneSetup(0, 0, 0, 0.0, 0); if (!adaptive) vfx_set(P_LEGACY, 1.f);
            int at = 48000;
            std::vector<int16_t> sig = sing({ { from, 1.2 } }); sig.resize(at + 24000, 0);
            auto b = sing({ { to, 0.5 } });
            for (size_t i = 0; i < b.size(); i++) sig[at + i] = b[i];
            vfx_process_i16(sig.data(), 960);                            // warm-up: 2 tuner hops flush the previous test's stat
            int first = -1;
            for (int i = 960; i < (int)sig.size(); i += 48) {
                vfx_process_i16(sig.data() + i, std::min(48, (int)sig.size() - i));
                if (first < 0 && fabs(vfx_statf(7) - to) < 0.3) first = i;
            }
            return first < 0 ? 9999 : (first - at) / 48.0;
        };
        double lagAdapt = switchLagMs(69, 74, 1), lagOld = switchLagMs(69, 74, 0);
        printf("        note switch 69->74: adaptive %.1f ms, legacy timing %.1f ms\n", lagAdapt, lagOld);
        CHECK(lagAdapt < lagOld && lagAdapt < 60.0 && lagOld < 110.0, "adaptive YIN window: note-switch lag %.1f ms < legacy %.1f ms", lagAdapt, lagOld);
        // (b) lookahead: impulse probes give the exact engine-path delay (periodic signals have congruent correlation peaks)
#ifdef VFX_NO_RUBBERBAND
        const int engLat = 770;          // built-in Lite shifter: impulse input keeps the grain at its default (768 + 2)
#else
        const int engLat = 2048;         // fake RubberBand stand-in: 512 block + 1536 start delay
#endif
        auto impulseLag = [&](int lookMs, int legacy) {
            if (legacy) reset_all_legacy(); else reset_all();
            vfx_set(P_TUNE_ON, 1.f); vfx_set(P_TUNE_TIME, 0.f); vfx_set(P_TUNE_AMT, 0.f); vfx_set(P_TUNE_LOOKAHEAD, (float)lookMs); vfx_set(P_PITCH, 1.f);
            std::vector<int16_t> sig(96000, 0); sig[20000] = 16000; sig[28000] = 12000;
            run(sig, 960);
            int lag = 0, best = 0;
            for (int i = 20000 + 300; i < 20000 + 4600; i++) if (abs((int)sig[i]) > best) { best = abs((int)sig[i]); lag = i - 20000; }
            return lag;
        };
        { int lag = impulseLag(30, 0);
          CHECK(abs(lag - (engLat + 1440 + 144)) <= 6, "lookahead 30 ms: pitch path alignment %d (want %d = engine + 1440 look + 144 limiter, ±6)", lag, engLat + 1440 + 144);
          reset_all(); vfx_set(P_TUNE_ON, 0.f); vfx_set(P_TUNE_LOOKAHEAD, 30.f);        // feature disabled: engine direct, no extra latency
          auto in2 = voiceish(48000), out2 = in2; run(out2, 960);
          int lag2 = bestLag(out2, in2, 100, 260, 24000, 23000);
          CHECK(lag2 >= 140 && lag2 <= 148, "lookahead set but tuning off: direct alignment %d (no unnecessary latency)", lag2); }
        // (c) vibrato: 6 Hz, about ±0.5 st. With lookahead aligning the correction, keep 0 flattens the vibrato,
        //     keep 1 passes most of it; in both cases the mean stays on the note (60.0)
        { auto vib = [&](double keep) {
            tuneSetup(0, 0, 0, 1.0, 0); vfx_set(P_TUNE_LOOKAHEAD, 40.f); vfx_set(P_TUNE_KEEP_VIB, (float)keep);
            auto in = singVib(60.0, 0.5, 6.0, 1.6), out = in; run(out, 960);
            double lo = 999, hi = -999;
            for (int s = 48000; s + 2048 < 72000; s += 1200) { double mm = measureMidi(out, s, 2048); if (mm > 0) { lo = std::min(lo, mm); hi = std::max(hi, mm); } }
            return (hi - lo) / 2.0;                                       // short-window pitch excursion = vibrato depth
          };
          double flat = vib(0.0), kept = vib(1.0);
          printf("        vibrato depth after hard tune (lookahead 40 ms): keep 0 -> %.3f st, keep 1 -> %.3f st (raw 0.5 st)\n", flat, kept);
          CHECK(flat < 0.15 && kept > 0.25, "vibrato keep: corrected %.3f st vs kept %.3f st", flat, kept); }
        // (d) chunk independence with lookahead + vibrato keep active
        { tuneSetup(2, 1, 40, 0.9, 1); vfx_set(P_TUNE_LOOKAHEAD, 25.f); vfx_set(P_TUNE_KEEP_VIB, 0.5f);
          auto in = sing({ { 62.3, 0.8 }, { 66.4, 0.8 } }), o1 = in, o2 = in, o3 = in; run(o1, 960);
          tuneSetup(2, 1, 40, 0.9, 1); vfx_set(P_TUNE_LOOKAHEAD, 25.f); vfx_set(P_TUNE_KEEP_VIB, 0.5f); run(o2, 0, 4711);
          tuneSetup(2, 1, 40, 0.9, 1); vfx_set(P_TUNE_LOOKAHEAD, 25.f); vfx_set(P_TUNE_KEEP_VIB, 0.5f); run(o3, 1);
          int bad = 0; for (size_t i = 0; i < in.size(); i++) if (o1[i] != o2[i] || o1[i] != o3[i]) bad++;
          CHECK(bad == 0, "tune lookahead + vibrato keep: 960 vs random vs 1-sample chunks: %d differ", bad); }
        // (e) legacy timing: lookahead and vibrato keep have no effect in legacy mode
        { int lag = impulseLag(60, 1);
          CHECK(abs(lag - engLat) <= 6, "legacy ignores lookahead and limiter: alignment %d (want %d)", lag, engLat); }
    }
    {
        // (a) tracker accuracy over the singing range, notes off pitch by up to 30 cents
        for (double m : { 45.0, 55.0, 60.3, 69.0, 76.2 }) { tuneSetup(0, 0, 0, 0.0, 0); auto in = sing({ { m, 1.2 } }), out = in; run(out, 960);
            double det = vfx_statf(7); CHECK(fabs(det - m) < 0.15, "tracker: sung midi %.2f -> detected %.2f (%.0f Hz)", m, det, mf(m)); }
        // (b) hard tune in C major: every sung note ends up on a scale note
        struct Case { double sung, want; };
        for (Case c : { Case{ 61.3, 62 }, Case{ 64.4, 64 }, Case{ 65.8, 65 }, Case{ 58.6, 59 }, Case{ 70.0, 69.0 + 0 } }) {
            if (c.sung == 70.0) c.want = 71 - 1 + 0;           // A#4 sits halfway: either neighbour is fine (A or B), checked below
            tuneSetup(0, 1, 0, 1.0, 0); auto in = sing({ { c.sung, 1.4 } }), out = in; run(out, 960); double mo = measureMidi(out, 48000 * 6 / 10, 24000);
            bool ok = c.sung == 70.0 ? (fabs(mo - 69) < 0.25 || fabs(mo - 71) < 0.25) : fabs(mo - c.want) < 0.25;
            CHECK(ok, "C major hard tune: sung %.1f -> output %.2f (want %.0f)", c.sung, mo, c.sung == 70.0 ? 69.0 : c.want); }
        // (c) amount scales the correction, chromatic snap
        { tuneSetup(0, 0, 0, 1.0, 0); auto in = sing({ { 60.35, 1.4 } }), out = in; run(out, 960); double mo = measureMidi(out, 28800, 24000); CHECK(fabs(mo - 60.0) < 0.2, "chromatic, amount 1: 60.35 -> %.2f", mo);
          tuneSetup(0, 0, 0, 0.5, 0); in = sing({ { 60.35, 1.4 } }); out = in; run(out, 960); mo = measureMidi(out, 28800, 24000); CHECK(fabs(mo - 60.175) < 0.2, "chromatic, amount 0.5: 60.35 -> %.2f (halfway)", mo); }
        // (d) retune time: a note sung 0.4 semitone flat of E4 (63.6 -> target 64): instant tune is on the note within 100 ms, 300 ms smoothing glides
        for (double tm : { 0.0, 300.0 }) { tuneSetup(0, 1, tm, 1.0, 0); auto in = sing({ { 62.0, 0.8 }, { 63.6, 1.6 } }), out = in; run(out, 960);
            double at100 = measureMidi(out, 38400 + 4800, 1440), at600 = measureMidi(out, 38400 + 28800, 1440);
            printf("        retune time %3.0f ms: 100 ms after the note starts %.2f, after 600 ms %.2f (target 64.00)\n", tm, at100, at600);
            if (tm == 0) CHECK(fabs(at100 - 64.0) < 0.12, "instant retune: on the note after 100 ms (%.2f)", at100); else CHECK(at100 < 63.9 && at100 > 63.55 && at600 > at100 + 0.1, "300 ms retune glides (%.2f -> %.2f)", at100, at600); }
        // (e) speech-like sound without a pitch (noise) must not be tuned / must not explode
        { tuneSetup(0, 1, 0, 1.0, 1); std::vector<int16_t> nz(48000); unsigned r = 5; for (auto& x : nz) { r = r * 1664525u + 1013904223u; x = (int16_t)(((int)(r >> 16) - 32768) / 8); } auto out = nz; run(out, 960);
          CHECK(db(rms(out, 9600, 48000)) < db(rms(nz, 9600, 48000)) + 4, "noise in: output level %+.1f dB vs input", db(rms(out, 9600, 48000)) - db(rms(nz, 9600, 48000))); }
        { tuneSetup(0, 1, 0, 1.0, 1); std::vector<int16_t> z(48000, 0); run(z, 960); int mx = 0; for (auto v : z) mx = std::max(mx, (int)abs(v)); CHECK(mx < 3, "silence in, Magic Chords on: peak %d", mx); }
        // (f) chunk size independence
        { tuneSetup(2, 1, 40, 0.9, 1); auto in = sing({ { 62.3, 0.8 }, { 66.4, 0.8 }, { 69.2, 0.8 } }), o1 = in, o2 = in; run(o1, 960); tuneSetup(2, 1, 40, 0.9, 1); run(o2, 0, 4711); int bad = 0; for (size_t i = 0; i < in.size(); i++) if (o1[i] != o2[i]) bad++;
          CHECK(bad == 0, "tune + chords: 960-sample vs random chunks: %d differing samples", bad); }
    }
    printf("[14] Magic Chords / chord modes (Lite engine)\n");
    {
        // (a) Magic in A major, sung A3: lead 220 Hz + 3rd C#4 + 5th E4 + bass A2, each as a clean single line at the right frequency
        { tuneSetup(9, 1, 0, 1.0, 1); auto in = sing({ { 57.0, 1.8 } }), out = in; run(out, 960);
          auto peak = [&](double lo, double hi, double& at) { double bm = -999; for (double f = lo; f <= hi; f += 0.5) { double v = goertzelDb(out, 28800, 38400, f); if (v > bm) { bm = v; at = f; } } return bm; };
          double fl, f3, f5, fb; double pl = peak(214, 226, fl), p3 = peak(266, 290, f3), p5 = peak(316, 343, f5), pb = peak(104, 117, fb);
          printf("        lead %.1f Hz %.0f dB | 3rd %.1f Hz (want 277.2) %.0f dB | 5th %.1f Hz (want 329.6) %.0f dB | bass %.1f Hz (want 110.0) %.0f dB\n", fl, pl, f3, p3, f5, p5, fb, pb);
          CHECK(fabs(f3 - mf(61)) < 3.0 && fabs(f5 - mf(64)) < 3.0 && fabs(fb - mf(45)) < 2.0, "voices sit on C#4 / E4 / A2 (errors %.1f, %.1f, %.1f Hz)", f3 - mf(61), f5 - mf(64), fb - mf(45));
          CHECK(p3 > pl - 16 && p5 > pl - 16 && pb > pl - 18, "voices are strong single lines: %.0f / %.0f / %.0f dB below the lead", pl - p3, pl - p5, pl - pb);
          double fo1, fo2, fo3; double q1 = peak(293, 305, fo1), q2 = peak(366, 400, fo2), q3 = peak(240, 252, fo3);
          CHECK(q1 < p3 - 8 && q2 < p3 - 8 && q3 < p3 - 8, "notes outside the chord stay down: D4-ish %.0f, G4-ish %.0f, B3-ish %.0f dB vs 3rd %.0f dB", q1, q2, q3, p3);
          tuneSetup(9, 1, 0, 1.0, 0); auto base = in; run(base, 960); double lv = db(rms(out, 24000, 62400)) - db(rms(base, 24000, 62400)); CHECK(lv > 0.5 && lv < 6.0, "level with chords vs without: %+.1f dB", lv); }
        // (b) diatonic: in D major a sung B3 gets a MINOR third (D4) and a fifth (F#4), not D#4
        { tuneSetup(2, 1, 0, 1.0, 2); auto in = sing({ { 59.0, 1.6 } }), out = in; run(out, 960);
          double d4 = goertzelDb(out, 24000, 38400, mf(62)), ds4 = goertzelDb(out, 24000, 38400, mf(63)), fs4 = goertzelDb(out, 24000, 38400, mf(66));
          CHECK(d4 > ds4 + 12 && fs4 > ds4 + 8, "D major, sing B: D4 %.0f dB, F#4 %.0f dB, D#4 %.0f dB (minor chord)", d4, fs4, ds4); }
        // (c) every mode produces its added voices and stays finite
        const char* names[9] = { "off", "Magic", "Triad", "Power", "Seventh", "Sus", "Angel", "Octaves", "Duet" };
        for (int mode = 1; mode <= 8; mode++) { tuneSetup(0, 1, 20, 1.0, mode); auto in = sing({ { 60.0, 1.4 } }), base = in; run(base, 960); tuneSetup(0, 1, 20, 1.0, 0); base = in; run(base, 960);
            tuneSetup(0, 1, 20, 1.0, mode); auto out = in; run(out, 960); double lv = db(rms(out, 24000, 62400)) - db(rms(base, 24000, 62400)); int mx = 0; for (auto v : out) mx = std::max(mx, (int)abs(v));
            CHECK(lv > 0.3 && lv < 6.5 && mx < 32000, "mode %d %-8s: level %+.1f dB, peak %d", mode, names[mode], lv, mx); }
        // (d) auto key: a D-major melody must be recognised as D
        { tuneSetup(12, 1, 40, 1.0, 0); std::vector<Note> mel; unsigned r = 9; const int deg[12] = { 62, 62, 62, 69, 69, 66, 66, 64, 67, 71, 73, 62 }; for (int i = 0; i < 40; i++) { r = r * 1664525u + 1013904223u; mel.push_back({ (double)deg[(r >> 16) % 12] + ((int)((r >> 8) % 21) - 10) * 0.02, 0.33 }); }      // tonic-heavy D-major tune
          auto in = sing(mel), out = in; run(out, 960); CHECK(vfx_stat(15) == 2, "auto key from a D-major melody: root %d (2 = D)", vfx_stat(15));
          tuneSetup(12, 2, 40, 1.0, 0); mel.clear(); const int amin[12] = { 57, 57, 57, 64, 64, 60, 60, 62, 65, 67, 59, 57 }; for (int i = 0; i < 40; i++) { r = r * 1664525u + 1013904223u; mel.push_back({ (double)amin[(r >> 16) % 12], 0.33 }); }      // tonic-heavy A-minor tune
          in = sing(mel); out = in; run(out, 960); CHECK(vfx_stat(15) == 9, "auto key, minor scale, A-minor melody: root %d (9 = A)", vfx_stat(15)); }
        // (e) cost
        { tuneSetup(0, 1, 20, 1.0, 1); auto in = sing({ { 60.0, 4.0 } }), out = in; auto t0 = std::chrono::steady_clock::now(); run(out, 960); double sec = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
          CHECK(sec / 4.0 < 0.30, "CPU on this PC: %.1f%% of real time for tune + 3 chord voices", 100 * sec / 4.0); }
    }
    printf("[15] Phase 3 monitor queue servo (deterministic simulation, NOT a BASS/device test)\n");
    {
        MonServo s; s.init(48000);
        const double pushMs = 20.0, bytesPerMs = 96.0;                 // 20 ms chunks, 16-bit mono at 48 kHz
        const double inPerPush = pushMs * bytesPerMs;
        double q = 50 * bytesPerMs;                                    // start at the 50 ms target
        double maxQ = 0; int overLimit = 0;
        for (int t = 0; t < 3000; t++) {                               // 60 s: first 0.5 s has 1.2x input (net buildup)
            double in = inPerPush * (t < 25 ? 1.2 : 1.0);
            q += in;
            float sp = s.update((float)q, true);
            q -= inPerPush * (double)sp; if (q < 0) q = 0;
            if (q > maxQ) maxQ = q;
            if (fabs((double)sp - 1.0) > 0.0101) overLimit++;
        }
        double mean = s.ms;
        CHECK(overLimit == 0, "servo rate factor never exceeded ±1%% over 60 s (%d violations)", overLimit);
        CHECK(maxQ / bytesPerMs < 200.0, "queue never reached the hard-reset threshold (max %.1f ms)", maxQ / bytesPerMs);
        CHECK(fabs(mean - 50.0) < 15.0, "queue settled near the 50 ms target (measured %.1f ms)", mean);
        // recovery: 100 ms of extra audio queued -> the servo drains it back below 70 ms (slow, smooth, no drops)
        MonServo s2; s2.init(48000);
        double q2 = 100 * bytesPerMs; int t2 = 0;
        for (; t2 < 20000; t2++) { q2 += inPerPush; float sp = s2.update((float)q2, true); q2 -= inPerPush * (double)sp; if (q2 < 0) q2 = 0; if (q2 < 70 * bytesPerMs) break; }
        CHECK(t2 < 20000, "servo drained a 100 ms surplus back under 70 ms (took %.1f s)", t2 * 0.02);
        // hard reset decision + priming gate
        MonServo s3; s3.init(48000);
        CHECK(!s3.needHardReset(150 * bytesPerMs), "150 ms queued: no hard reset");
        CHECK(s3.needHardReset(220 * bytesPerMs), "220 ms queued: hard reset requested");
        CHECK(s3.update(40 * bytesPerMs, false) == 1.f, "not playing: no rate correction, speed stays 1");
        // stability: realistic push jitter around the target must not grow (no servo oscillation)
        MonServo s4; s4.init(48000);
        double q4 = 50 * bytesPerMs; double worst = 0;
        for (int t = 0; t < 4000; t++) { q4 += inPerPush + 3 * bytesPerMs * sin(2 * M_PI * t / 40.0);    // ±3 ms chunk jitter, 0.8 s period
            float sp = s4.update((float)q4, true); q4 -= inPerPush * (double)sp; if (q4 < 0) q4 = 0;
            worst = std::max(worst, fabs(q4 / bytesPerMs - 50.0)); }
        CHECK(worst < 45.0, "jitter stays bounded (max deviation %.1f ms, inherent ripple of the ±3 ms jitter is ~19 ms) - no servo oscillation", worst);
    }

    printf("[16] Phase 4: RNNoise frame assembly, PSOLA chords, drive AA, FDN reverb, echo damp, vocoder carrier\n");
    {
        // (a) RNNoise frame assembly with a deterministic fake processor (no AI model on host):
        //     output must be the input delayed exactly one 480-sample frame, gain 0.5, for ANY chunking
        g_rnCreate = fakeRnCreate; g_rnFree = fakeRnFree; g_rnProcess = fakeRnProcess;
        auto rnRun = [&](int chunk, unsigned seed) {
            reset_all(); vfx_set(P_NS_ON, 1.f); vfx_set(P_NS_ENGINE, 1.f); vfx_set(P_NS_AMT, 1.f);
            auto in = voiceish(96000), out = in; run(out, chunk, seed); return out;
        };
        { auto o1 = rnRun(960, 0), o2 = rnRun(1, 0), o3 = rnRun(0, 31337), o4 = rnRun(480, 0), o5 = rnRun(0, 777);
          int bad = 0; for (size_t i = 0; i < o1.size(); i++) if (o1[i] != o2[i] || o1[i] != o3[i] || o1[i] != o4[i] || o1[i] != o5[i]) bad++;
          CHECK(bad == 0, "RNNoise frame assembly: 960 vs 1 vs random vs 480 vs random2: %d differing samples", bad);
          reset_all(); vfx_set(P_NS_ON, 1.f); vfx_set(P_NS_ENGINE, 1.f); vfx_set(P_NS_AMT, 1.f);
          std::vector<int16_t> sig(96000, 0); sig[20000] = 16000; run(sig, 960);
          int pk = 0, at = 0; for (int i = 20000 + 300; i < 20000 + 1200; i++) if (abs((int)sig[i]) > pk) { pk = abs((int)sig[i]); at = i; }
          CHECK(abs(at - 20000 - 480 - 144) <= 4, "RNNoise latency: impulse peak at %d (want %d = 480 frame + 144 limiter)", at, 20000 + 624);
          // engine 0 (classic) still works alongside
          reset_all(); vfx_set(P_NS_ON, 1.f); vfx_set(P_NS_ENGINE, 0.f); auto in2 = voiceish(48000), o6 = in2; run(o6, 960);
          CHECK(fabs(db(rms(o6, 24000, 48000)) - db(rms(in2, 24000, 48000))) < 3.0, "classic engine still selectable: level %+.2f dB", db(rms(o6, 24000, 48000)) - db(rms(in2, 24000, 48000))); }
        g_rnCreate = nullptr; g_rnFree = nullptr; g_rnProcess = nullptr;
        // with the hooks gone, ns_engine=1 must fall back to the classic engine (no crash, sane level)
        { reset_all(); vfx_set(P_NS_ON, 1.f); vfx_set(P_NS_ENGINE, 1.f); auto in = voiceish(48000), out = in; run(out, 960);
          CHECK(fabs(db(rms(out, 24000, 48000)) - db(rms(in, 24000, 48000))) < 3.0, "ns_engine=1 without RNNoise hooks: classic fallback, level %+.2f dB", db(rms(out, 24000, 48000)) - db(rms(in, 24000, 48000))); }
        // (b) PSOLA chord voices: Magic chords with formant 1 keeps the same scale-aware harmony
        { tuneSetup(9, 1, 0, 1.0, 1); vfx_set(P_CHORD_FORMANT, 1.f); auto in = sing({ { 57.0, 1.8 } }), out = in; run(out, 960);
          auto peak = [&](double lo, double hi, double& at) { double bm = -999; for (double f = lo; f <= hi; f += 0.5) { double v = goertzelDb(out, 28800, 38400, f); if (v > bm) { bm = v; at = f; } } return bm; };
          double f3, f5, fb; peak(266, 290, f3); peak(316, 343, f5); peak(104, 117, fb);
          CHECK(fabs(f3 - mf(61)) < 3.5 && fabs(f5 - mf(64)) < 3.5 && fabs(fb - mf(45)) < 3.0, "PSOLA Magic chords: C#4 %.1f Hz, E4 %.1f Hz, A2 %.1f Hz (errors %.1f/%.1f/%.1f)", f3, f5, fb, f3 - mf(61), f5 - mf(64), fb - mf(45));
          // unvoiced input with PSOLA requested: falls back to LitePitch voices, stays sane
          std::vector<int16_t> nz(48000); unsigned r = 5; for (auto& x : nz) { r = r * 1664525u + 1013904223u; x = (int16_t)(((int)(r >> 16) - 32768) / 8); }
          tuneSetup(0, 1, 0, 1.0, 1); vfx_set(P_CHORD_FORMANT, 1.f); auto o = nz; run(o, 960);
          CHECK(db(rms(o, 9600, 48000)) < db(rms(nz, 9600, 48000)) + 4, "PSOLA with unvoiced input: falls back, level %+.1f dB", db(rms(o, 9600, 48000)) - db(rms(nz, 9600, 48000))); }
        // (c) V1 drive (2x oversampled LUT tanh) matches the legacy drive level within 1.5 dB
        { reset_all(); vfx_set(P_DRIVE, 0.5f); double g1 = gainAt(500, "");
          reset_all(); vfx_set(P_DRIVE, 0.5f); vfx_set(P_LEGACY, 1.f); double g2 = gainAt(500, "");
          CHECK(fabs(g1 - g2) < 1.5, "drive 0.5: V1 %+.2f dB vs legacy %+.2f dB", g1, g2); }
        // (d) FDN reverb stays stable and bounded at the extremes; legacy reverb ignores the new knobs
        { for (float pd : { 0.f, 100.f }) for (float dmp : { 0.f, 1.f }) {
            reset_all(); vfx_set(P_REV_MIX, 1.f); vfx_set(P_REV_SIZE, 1.f); vfx_set(P_REV_PREDELAY_MS, pd); vfx_set(P_REV_DAMP, dmp);
            std::vector<int16_t> nz(96000); unsigned s = 7; for (auto& x : nz) { s = s * 1664525u + 1013904223u; x = (int16_t)(((int)(s >> 16) - 32768) / 3); }
            auto o = nz; run(o, 960); int mx = 0; for (auto v : o) mx = std::max(mx, (int)abs(v));
            CHECK(mx < 32000, "FDN reverb stable (predelay %.0f, damp %.0f): peak %d", pd, dmp, mx); }
          std::vector<int16_t> a1, a2;
          reset_all_legacy(); vfx_set(P_REV_MIX, 0.8f); vfx_set(P_REV_SIZE, 0.8f); vfx_set(P_REV_PREDELAY_MS, 50.f); vfx_set(P_REV_DAMP, 1.f);
          auto in = voiceish(48000); a1 = in; run(a1, 960);
          reset_all_legacy(); vfx_set(P_REV_MIX, 0.8f); vfx_set(P_REV_SIZE, 0.8f);
          a2 = in; run(a2, 960);
          int bad = 0; for (size_t i = 0; i < a1.size(); i++) if (a1[i] != a2[i]) bad++;
          CHECK(bad == 0, "legacy reverb ignores predelay/damp (%d differing samples)", bad); }
        // (e) echo damping: high-frequency content of later repeats drops faster with damping on
        { reset_all(); vfx_set(P_ECHO_MS, 250.f); vfx_set(P_ECHO_FB, 0.6f); vfx_set(P_ECHO_MIX, 1.f);
          auto echoTest = [&](float damp) { reset_all(); vfx_set(P_ECHO_MS, 250.f); vfx_set(P_ECHO_FB, 0.6f); vfx_set(P_ECHO_MIX, 1.f); vfx_set(P_ECHO_DAMP, damp);
            std::vector<int16_t> d(96000, 0); for (int i = 0; i < 480; i++) d[4800 + i] = (int16_t)(12000 * sin(2 * M_PI * 5000 * i / 48000.0));
            run(d, 960); double e1 = goertzelDb(d, 4800 + 480 + 3 * 12000 + 144 - 60, 120, 5000); return e1; };
          double e0 = echoTest(0.f), e1 = echoTest(1.f);
          CHECK(e1 < e0 - 3.0, "echo damping: 3rd repeat 5 kHz content %.1f dB (damp 0) vs %.1f dB (damp 1)", e0, e1); }
        // (f) vocoder carrier follows the detected voice, or snaps to the scale
        { auto vocPeak = [&](int carrier) { reset_all(); vfx_set(P_VOC_MODE, 2.f); vfx_set(P_VOC_CARRIER, (float)carrier); vfx_set(P_TUNE_KEY, 0.f);   // fixed key C: auto-key would make the single sung note the tonic
            auto in = sing({ { 61.0, 1.5 } }), out = in; run(out, 960);       // C#4: outside C major
            double at, bm = -999; for (double f = 240; f <= 310; f += 0.5) { double v = goertzelDb(out, 48000, 38400, f); if (v > bm) { bm = v; at = f; } }
            return at; };
          double fFollow = vocPeak(0), fSnap = vocPeak(1);
          CHECK(fabs(fFollow - mf(61)) < 4.0, "vocoder carrier follow: %.1f Hz (sung C#4 = %.1f)", fFollow, mf(61));
          CHECK(fabs(fSnap - mf(60)) < 4.0 || fabs(fSnap - mf(62)) < 4.0, "vocoder carrier snap: %.1f Hz (C4 %.1f or D4 %.1f)", fSnap, mf(60), mf(62)); }
        // (g) legacy mode ignores every Phase 4 knob (bit-exact pairs)
        { auto legacyPair = [&](int id, float v1v, float v2v) {
            reset_all_legacy(); vfx_set(id, v1v); auto a = voiceish(48000); run(a, 960);
            reset_all_legacy(); vfx_set(id, v2v); auto b = voiceish(48000); run(b, 960);
            int bad = 0; for (size_t i = 0; i < a.size(); i++) if (a[i] != b[i]) bad++;
            return bad; };
          CHECK(legacyPair(P_NS_ENGINE, 0.f, 1.f) == 0, "legacy ignores ns_engine");
          CHECK(legacyPair(P_CHORD_FORMANT, 0.f, 1.f) == 0, "legacy ignores chord_formant");
          CHECK(legacyPair(P_ECHO_DAMP, 0.f, 1.f) == 0, "legacy ignores echo_damp");
          CHECK(legacyPair(P_VOC_CARRIER, 0.f, 1.f) == 0, "legacy ignores voc_carrier"); }
    }

    printf("[17] Phase 5: LUT oscillators, Lite recovery controller, per-stage CPU timing\n");
    {
        // (a) LUT accuracy vs the reference math (the numerical difference the oscillator swap introduces)
        { double maxS = 0, maxH = 0;
          for (int i = 0; i < 2000; i++) { double ph = i * 0.0005;
              maxS = std::max(maxS, fabs((double)sinLut((float)ph) - sin(2 * M_PI * ph)));
              maxH = std::max(maxH, fabs((double)hannLut((float)ph) - (0.5 - 0.5 * cos(2 * M_PI * ph)))); }
          printf("        LUT max errors: sin %.2e, hann %.2e (of full scale)\n", maxS, maxH);
          CHECK(maxS < 1e-3 && maxH < 1e-3, "oscillator LUTs match the reference within 1e-3 (sin %.1e, hann %.1e)", maxS, maxH); }
        // (b) phase continuity: changing tremolo speed mid-tone must not click
        { reset_all(); vfx_set(P_TREM_DEPTH, 1.f); vfx_set(P_TREM_HZ, 5.f);
          auto in = tone(48000, 400, 9000), out = in; int p = 0;
          while (p < (int)in.size()) { if (p == 24000) vfx_set(P_TREM_HZ, 13.f); vfx_process_i16(out.data() + p, 960); p += 960; }
          CHECK(maxJump(out, 20000, 44000) < 900, "tremolo speed change: largest step %d (natural 235)", maxJump(out, 20000, 44000)); }
        // (c) LiteRecovery controller decisions (deterministic, no RubberBand involvement)
        { LiteRecovery rc;
          bool probed = false; for (int i = 0; i < 999; i++) probed |= rc.wantProbe(0.02f);   // 19.98 s of audio
          CHECK(probed == false, "no probe before 20 s of audio");
          probed = rc.wantProbe(0.02f);                                                       // crosses 20 s
          CHECK(probed == true, "probe fires at 20 s");
          CHECK(LiteRecovery::loadOk(0.54f) && !LiteRecovery::loadOk(0.56f), "probe load threshold: below 55%% passes, above fails");
          rc.onResult(false); rc.onResult(false);
          CHECK(rc.failStreak == 2, "failed probes are counted (%d)", rc.failStreak);
          rc.onResult(true);
          CHECK(rc.failStreak == 0, "a passing probe clears the streak"); }
        // (d) per-stage CPU counters run, are finite, and the peak is at least the average
        { reset_all(); vfx_set(P_PITCH, 1.5f); auto in = voiceish(48000), out = in; run(out, 960);
          float aIn = vfx_statf(11), aEng = vfx_statf(12), aOut = vfx_statf(13);
          float pIn = vfx_statf(14), pEng = vfx_statf(15), pOut = vfx_statf(16);
          CHECK(aIn > 0.f && aEng > 0.f && aOut > 0.f, "stage averages positive: in %.3f, engine %.3f, out %.3f ms", aIn, aEng, aOut);
          CHECK(pIn >= aIn && pEng >= aEng && pOut >= aOut, "stage peaks >= averages: %.3f/%.3f/%.3f", pIn, pEng, pOut);
          CHECK(aIn == aIn && aEng == aEng && aOut == aOut, "stage counters are finite (no NaN)"); }
    }

    printf("\n%s (%d failed)\n", fails ? "SOME TESTS FAILED" : "ALL TESTS PASSED", fails);
    return fails ? 1 : 0;
}
