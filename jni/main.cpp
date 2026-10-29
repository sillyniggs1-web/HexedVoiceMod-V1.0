// HexedVoiceMod V1.0 - AML mod for SA-MP Nezuko. Voice changer + effect rack + noise suppression + Auto-Tune / Magic Chords.
// Hooks libBASS BASS_RecordStart (AML hook engine / Dobby), attaches a BASS DSP to the mic channel and runs the DSP core
// (voicefx_dsp.h) on it. The menu script (HexedVoiceMod_Menu.lua) talks to this library through the exported vfx_* functions.
#include <mod/amlmod.h>
#include <mod/logger.h>
#include <dlfcn.h>
#include <android/log.h>

MYMOD(com.hexed.voicemod, HexedVoiceMod, 1.0.0, brruham)

#ifndef VFX_NO_RNNOISE
#include "rnnoise/rnnoise.h"
#endif
#include "voicefx_dsp.h"

// RNNoise glue (Phase 4): only compiled when jni/rnnoise-src is present; otherwise the classic Wiener
// engine is used and ns_engine=1 silently falls back to it. The state is created in Engine::rebuild
// (hook thread / OnModLoad), never inside the audio callback.
#ifndef VFX_NO_RNNOISE
static void* rn_create(int rate) { (void)rate; return (void*)rnnoise_create(nullptr); }
static void  rn_free(void* st) { if (st) rnnoise_destroy((DenoiseState*)st); }
static float rn_process(void* st, float* out, const float* in) { return rnnoise_process_frame((DenoiseState*)st, out, in); }
static struct RnHookInstall { RnHookInstall() { g_rnCreate = rn_create; g_rnFree = rn_free; g_rnProcess = rn_process; } } g_rnHookInstall;
#endif

// logging sink used by voicefx_dsp.h: AML logger + log file the menu tells the user to send
void vfx_platform_log(const char* msg)
{
    logger->Info("%s", msg);
    FILE* f = fopen(LOG_PATH, "a");
    if (f) { fprintf(f, "[HexedVoiceMod] %s\n", msg); fclose(f); }
}

typedef uint32_t DWORD;
typedef void (*DSPPROC)(DWORD handle, DWORD channel, void* buffer, DWORD length, void* user);
typedef DWORD (*RecordStartFn)(DWORD freq, DWORD chans, DWORD flags, void* proc, void* user);
typedef DWORD (*SetDSPFn)(DWORD handle, DSPPROC proc, void* user, int priority);
typedef DWORD (*StreamCreateFn)(DWORD freq, DWORD chans, DWORD flags, void* proc, void* user);
typedef DWORD (*StreamPutFn)(DWORD handle, const void* buf, DWORD len);
typedef int   (*PlayFn)(DWORD handle, int restart);
typedef int   (*SetAttrFn)(DWORD handle, DWORD attr, float value);
typedef int   (*FreeFn)(DWORD handle);
typedef DWORD (*GetDataFn)(DWORD handle, void* buf, DWORD len);
typedef int   (*ErrFn)(void);
typedef int   (*DobbyHookFn)(void* addr, void* replace, void** origin);
typedef void* (*DobbyResolveFn)(const char* image, const char* sym);

static RecordStartFn g_origRecStart = nullptr;
static SetDSPFn      g_setDSP = nullptr;
static std::atomic<int> g_hookBusy{0};
static StreamCreateFn g_streamCreate = nullptr; static StreamPutFn g_streamPut = nullptr; static PlayFn g_chPlay = nullptr;
static SetAttrFn g_setAttr = nullptr; static FreeFn g_streamFree = nullptr; static GetDataFn g_getData = nullptr; static ErrFn g_errCode = nullptr;
static std::atomic<DWORD> g_monStream{0};
static std::atomic<int>   g_monPrimed{0};
static std::atomic<int>   g_monResets{0};
static std::mutex g_monMtx;
static MonServo g_servo;                  // hear-myself queue servo (Phase 3), BASS-free and host-testable

// "Hear myself": a push stream on the default BASS output, fed from the DSP callback (after the effect).
// The stream is NOT started when created; it starts once 80 ms are queued, so the output buffer never runs dry between chunks.
static void mon_update(int id)
{
    if (id != P_MONITOR && id != P_MON_VOL) return;
    std::lock_guard<std::mutex> lk(g_monMtx);
    const bool want = g_par[P_MONITOR] >= 0.5f;
    DWORD h = g_monStream.load();
    if (want && !h) {
        if (!g_streamCreate || !g_streamPut || !g_chPlay) { g_monState = -1; vlog("monitor: BASS stream functions missing"); return; }
        int rate = g_rate.load() ? g_rate.load() : 48000;
        h = g_streamCreate((DWORD)rate, 1, 0, (void*)-1 /* STREAMPROC_PUSH */, nullptr);
        if (!h) { g_monState = -1; vlog("monitor: BASS_StreamCreate failed, BASS error %d", g_errCode ? g_errCode() : -1); return; }
        g_monPrimed = 0; g_monStream = h; g_monState = 1;
        g_servo.init(rate);
        vlog("monitor: stream %u created at %d Hz (starts after 80 ms are queued, servo target 50 ms)", h, rate);
    } else if (!want && h) {
        g_monStream = 0; g_monPrimed = 0;
        if (g_streamFree) g_streamFree(h);
        g_monState = 0; vlog("monitor: stopped");
        return;
    }
    if (h && g_setAttr) g_setAttr(h, 2 /* BASS_ATTRIB_VOL */, g_par[P_MON_VOL]);
}

static void mon_push(void* buf, DWORD len)
{
    DWORD h = g_monStream.load(); if (!h) return;
    DWORD rate = (DWORD)(g_rate.load() ? g_rate.load() : 48000);
    DWORD avail = g_getData ? g_getData(h, nullptr, 0 /* BASS_DATA_AVAILABLE */) : 0;
    if (avail == (DWORD)-1) return;                                // BASS error this round: skip, do not drop anything
    const bool legacy = g_par[P_LEGACY] >= 0.5f;
    if (legacy) {                                                  // original v8 behaviour
        if (avail > rate / 2) return;                              // already >0.25 s queued (rate/2 bytes): drop
    } else {
        float qBytes = (float)avail + (float)len;
        g_monLat = g_servo.update(qBytes, g_monPrimed.load() != 0);
        if (g_monPrimed.load() && g_servo.needHardReset(qBytes)) { // runaway queue (device stall, suspended output): flush it
            if (g_chPlay && g_monResets.fetch_add(1) < 100) {
                g_chPlay(h, 1);                                    // restart clears the buffered audio; re-prime below
                g_monPrimed = 0;
                vlog("monitor: queue passed %.0f ms - hard reset #%d", (double)g_servo.ms, g_monResets.load());
            }
            return;
        }
        if (g_setAttr && g_monPrimed.load()) g_setAttr(h, 1 /* BASS_ATTRIB_FREQ */, (float)rate * g_servo.speed);
    }
    g_streamPut(h, buf, len);
    if (!g_monPrimed.load() && avail + len >= rate * 2 * 8 / 100) { // 80 ms of 16-bit mono
        std::lock_guard<std::mutex> lk(g_monMtx);
        if (g_monStream.load() == h && !g_monPrimed.load()) { g_chPlay(h, 0); g_monPrimed = 1; vlog("monitor: playing"); }
    }
}

static void dsp_proc(DWORD, DWORD, void* buffer, DWORD length, void*)
{
    g_dspCalls++; g_lastBytes = (int)length;
    if (!g_fmtOK.load() || length < 2 || length > 65536) return;
    if (g_par[P_ENABLED] >= 0.5f) g_eng.process((int16_t*)buffer, (int)(length / 2));
    if (g_par[P_MONITOR] >= 0.5f) mon_push(buffer, length);       // hear the processed audio (or the raw mic when the effect is off)
}

static DWORD hook_RecordStart(DWORD freq, DWORD chans, DWORD flags, void* proc, void* user)
{
    DWORD h = g_origRecStart(freq, chans, flags, proc, user);
    int n = ++g_recStarts;
    bool ok = (chans == 1) && !(flags & 0x101);              // mono, 16-bit (no BASS_SAMPLE_8BITS / FLOAT)
    g_fmtOK = ok ? 1 : 0;
    if (n <= 20) vlog("RecordStart #%d freq=%u chans=%u flags=0x%x proc=%p -> handle=%u fmtOK=%d", n, freq, chans, flags, proc, h, (int)ok);
    if (ok && (int)freq != g_rate.load()) g_eng.rebuild(freq);
    else if (ok) g_eng.reset();
    if (h && g_setDSP) {
        DWORD d = g_setDSP(h, dsp_proc, nullptr, 1);
        if (n <= 20) vlog("BASS_ChannelSetDSP(handle=%u) -> %u", h, d);
    }
    return h;
}

// Double-load protection: if the old library is still resident in this process, do NOT install a second BASS
// hook (the mic audio would then be processed twice). RTLD_NOLOAD only reports an already mapped library and
// never loads a new copy; the reference it returns is released again with dlclose, and the reference AML holds
// keeps the old library loaded, so nothing the other mod owns is torn down.
static bool legacy_voicefx_loaded()
{
    void* h = dlopen("libvoicefx.so", RTLD_NOW | RTLD_NOLOAD);
    if (!h) return false;
    dlclose(h);
    return true;
}

static void install_hook()
{
    if (legacy_voicefx_loaded()) {
        if (g_oldLibFound.exchange(1) == 0)
            vlog("WARNING: old libvoicefx.so found in the mods folder - delete it. Not installing the BASS hook to avoid processing the microphone twice.");
        g_hookBusy = 0;
        return;
    }
    if (g_hookState.load() == 1 || g_hookBusy.exchange(1)) return;
    // Use the same path Nezuko uses so we get the very same loaded copy of libBASS.
    void* bass = dlopen("/data/data/com.nezukosamp.game/lib/libBASS.so", RTLD_NOW);
    const char* how = "absolute path";
    if (!bass) { bass = dlopen("libBASS.so", RTLD_NOW); how = "soname"; }
    if (!bass && aml) { bass = aml->GetLibHandle("libBASS.so"); how = "AML"; }
    if (!bass) { vlog("ERROR: libBASS.so not found"); g_hookState = -1; g_hookBusy = 0; return; }
    g_setDSP = (SetDSPFn)dlsym(bass, "BASS_ChannelSetDSP");
    g_streamCreate = (StreamCreateFn)dlsym(bass, "BASS_StreamCreate"); g_streamPut = (StreamPutFn)dlsym(bass, "BASS_StreamPutData");
    g_chPlay = (PlayFn)dlsym(bass, "BASS_ChannelPlay"); g_setAttr = (SetAttrFn)dlsym(bass, "BASS_ChannelSetAttribute");
    g_streamFree = (FreeFn)dlsym(bass, "BASS_StreamFree"); g_getData = (GetDataFn)dlsym(bass, "BASS_ChannelGetData");
    g_errCode = (ErrFn)dlsym(bass, "BASS_ErrorGetCode");
    g_onParam = mon_update;
    void* target = dlsym(bass, "BASS_RecordStart");
    vlog("libBASS via %s, BASS_RecordStart=%p BASS_ChannelSetDSP=%p", how, target, (void*)g_setDSP);
    if (!target || !g_setDSP) { g_hookState = -1; g_hookBusy = 0; return; }

    // 1) AML's own hook engine (no extra library needed)
    if (aml && aml->Hook(target, (void*)hook_RecordStart, (void**)&g_origRecStart) && g_origRecStart) {
        g_hookState = 1; g_hookBusy = 0; vlog("RecordStart hook installed (AML)"); return;
    }
    vlog("AML hook failed, trying libdobby.so");
    // 2) fallback: Dobby, if the device has it
    void* dob = dlopen("libdobby.so", RTLD_NOW);
    DobbyHookFn hookFn = dob ? (DobbyHookFn)dlsym(dob, "DobbyHook") : nullptr;
    if (!hookFn) { vlog("ERROR: no usable hook engine (AML Hook failed, libdobby.so %s)", dob ? "has no DobbyHook" : "not found"); g_hookState = -1; g_hookBusy = 0; return; }
    g_origRecStart = nullptr;
    int r = hookFn(target, (void*)hook_RecordStart, (void**)&g_origRecStart);
    if (r != 0 || !g_origRecStart) { vlog("ERROR: DobbyHook returned %d", r); g_hookState = -1; g_hookBusy = 0; return; }
    g_hookState = 1; g_hookBusy = 0;
    vlog("RecordStart hook installed (Dobby)");
}
VFX_EXPORT int vfx_hook_retry(void) { install_hook(); return g_hookState; }

ON_MOD_PRELOAD() { remove(LOG_PATH); vlog("OnModPreLoad HexedVoiceMod 1.0.0"); }
ON_MOD_LOAD()
{
    logger->SetTag("HexedVoiceMod");
    params_default(); cfg_load();
    vlog("OnModLoad HexedVoiceMod V1.0 (enabled=%.0f pitch=%.3f formant=%.3f quality=%.0f noise=%.0f tune=%.0f chord=%.0f)", (double)g_par[P_ENABLED].load(), (double)g_par[P_PITCH].load(), (double)g_par[P_FORMANT].load(), (double)g_par[P_QUALITY].load(), (double)g_par[P_NS_ON].load(), (double)g_par[P_TUNE_ON].load(), (double)g_par[P_CHORD].load());
    g_eng.rebuild(48000);            // Nezuko records at 48000 Hz mono; rebuilt automatically if that ever changes
    install_hook();
}
ON_ALL_MODS_LOAD() { if (g_hookState.load() != 1 && !g_oldLibFound.load()) { vlog("retrying hook after all mods loaded"); install_hook(); } }
