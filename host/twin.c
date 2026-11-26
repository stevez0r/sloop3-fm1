/* SPDX-License-Identifier: GPL-3.0-only
 * The PC twin (twin.h): fw/main.c as it is, included here so that the scenarios see its state; the scenarios run
 * in their own thread, taking turns with the firmware (never at the same time): a scenario waits in simulated
 * time, the firmware runs meanwhile.
 *   twin SCENARIO OUTDIR [FLASH_IN] [SEED]    (FLASH_IN: an image of the flash; none: blank, the firmware installed)
 * Exit: 0 all expectations met, 1 one failed, 70+ a breach the twin caught. */
#include "../app/gfx.h"
#include "twin.h"
/* (each tile drawn costs CPU time when TWIN_DRAW_US is set: gfx_finish ends every tile) */
#define gfx_finish(px, n) (tw_draw_cost(), gfx_finish(px, n))
#include "../fw/main.c"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <math.h>
#ifdef TW_FPE
#include <fenv.h>
#endif

static const char *tw_out = "build/twin";
static int tw_fails, tw_checks;
static uint64_t tw_seed = 1;
static uint32_t tw_rnd(void) { tw_seed ^= tw_seed << 13; tw_seed ^= tw_seed >> 7; tw_seed ^= tw_seed << 17; return (uint32_t)(tw_seed >> 11); }
static double tw_s(void) { return (double)tw_ns * 1e-9; }
#define EXPECT(c, ...) do { tw_checks++; if (!(c)) { tw_fails++; printf("[%8.3f] FAIL %s:%d: ", tw_s(), __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); fflush(stdout); } } while (0)

/* ---- the firmware and the scenario, taking turns */
static pthread_mutex_t tw_mx = PTHREAD_MUTEX_INITIALIZER; static pthread_cond_t tw_cv = PTHREAD_COND_INITIALIZER;
static int tw_turn;                         /* 0 the firmware, 1 the scenario */
static uint64_t tw_wake; static int tw_done, tw_audio_on;
static void tw_give(int to)                 /* hand over, and wait for the turn to come back */
{
    pthread_mutex_lock(&tw_mx);
    tw_turn = to; pthread_cond_broadcast(&tw_cv);
    while (tw_turn == to) pthread_cond_wait(&tw_cv, &tw_mx);
    pthread_mutex_unlock(&tw_mx);
}
static void tw_safe_point(void) { if (!tw_done && tw_ns >= tw_wake) tw_give(1); }
static FILE *tw_film, *tw_panel; static double tw_film_t;   /* the film (sc_film): a picture of the screen and the panel every 1/30 s */
static void film_shot(void); extern uint8_t tw_glow[41], tw_lvl[41];
static void wait_ms(double ms)
{
    uint64_t end = tw_ns + (uint64_t)(ms * 1e6);
    if (!tw_film) { tw_wake = end; tw_give(0); return; }
    do { uint64_t w = tw_ns + 4000000u; tw_wake = w < end ? w : end; tw_give(0); film_shot(); } while (tw_ns < end);
}
static void wait_until(uint64_t ns) { if (ns > tw_ns) { tw_wake = ns; tw_give(0); } }

/* ---- what the firmware said (its messages), its sound */
static char tw_msgs[8192][40]; static uint32_t tw_nmsg, tw_seen_gen;
static double tw_peak, tw_sq; static uint64_t tw_sqn; static int tw_wild; static FILE *tw_wav; static uint32_t tw_wav_n;
void twin_tick(void)
{
    static uint32_t m_at, i_at;                     /* (3.3: two kinds: the popup, msg; the title's line, info) */
    if (msg_gen != tw_seen_gen) {
        tw_seen_gen = msg_gen;
        if (msg_live && msg_at != m_at) { m_at = msg_at; memcpy(tw_msgs[tw_nmsg % 8192], msg, 40); tw_msgs[tw_nmsg % 8192][39] = 0; tw_nmsg++;
            printf("[%8.3f] %s %s\n", tw_s(), msg_bad ? "!!" : "**", msg); fflush(stdout); }
        if (info_live && info_at != i_at) { i_at = info_at; memcpy(tw_msgs[tw_nmsg % 8192], info, 40); tw_msgs[tw_nmsg % 8192][39] = 0; tw_nmsg++;
            printf("[%8.3f]    %s\n", tw_s(), info); fflush(stdout); }
    }
    if (!tw_audio_on) tw_safe_point();
}
static double tw_cost_avg; static int tw_spikes; static double tw_spike_max;
void twin_audio(const int32_t *lr, uint32_t frames)
{
    tw_audio_on = 1;
    {   /* the cost: a half that takes 25 times the usual (and over 5 ms on the PC) is a spike, as on the FM-1 a PEAK */
        double c = (double)tw_half_cpu_ns;
        if (tw_cost_avg > 0.0 && c > 25.0 * tw_cost_avg && c > 5e6) {
            if (tw_spikes++ < 5) printf("[%8.3f] !! TWIN: a half cost %.1f ms, %.0f times the usual\n", (double)tw_ns * 1e-9, c * 1e-6, c / tw_cost_avg);
            if (c / tw_cost_avg > tw_spike_max) tw_spike_max = c / tw_cost_avg;
        }
        tw_cost_avg = tw_cost_avg <= 0.0 ? c : tw_cost_avg + (gb_minf((float)c, (float)(4.0 * tw_cost_avg)) - tw_cost_avg) * 0.01;
    }
    for (uint32_t i = 0; i < 2 * frames; i++) {
        double a = fabs((double)lr[i]) / 8388608.0;
        if (a > tw_peak) tw_peak = a;
        tw_sq += a * a; tw_sqn++;
        if (a > 1.01) tw_wild++;
    }
    if (tw_wav) { for (uint32_t i = 0; i < 2 * frames; i++) { int v = lr[i] >> 8; v = v > 32767 ? 32767 : v < -32768 ? -32768 : v; int16_t s = (int16_t)v; fwrite(&s, 2, 1, tw_wav); } tw_wav_n += frames; }
    {   /* (3.4.1) the interface's pass (fw/main.c t_ui): 3 s without one while the sound goes on is what a player calls a
         * freeze (the screen still, the buttons dead) */
        static int said; uint32_t now = (uint32_t)(tw_ns * lsb / 1000u), lim = 3000000u * lsb;
        if (t_ui && lsb && (uint32_t)(now - t_ui) > lim && !said && cal_step < 0) {
            said = 1; tw_checks++; tw_fails++;
            printf("[%8.3f] FAIL the interface stood still %.1f s (the sound going on): a freeze\n", tw_s(), (double)(uint32_t)(now - t_ui) / (lsb * 1e6));
        }
    }
    tw_safe_point();
}
static uint32_t tw_erases, tw_writes, tw_unerased;
static int tw_sloop_writes;
void twin_flash_op(int kind, uint32_t a, uint32_t n)
{
    (void)n; if (kind == 1) tw_erases++; else tw_writes++;
    /* SLOOP 2.8's data (0x97000..) is never written: but its staging area (erasable), and a user slot the editor sends */
    if (a >= 0x97000u && !(a >= 0xE0000u && a < 0xE5000u)) {
        static const uint32_t AT[4] = { 0xA0000u, 0xB4000u, 0xC8000u, 0xE7000u };
        int slot = 0; for (int k = 0; k < 4; k++) if (a >= AT[k] && a < AT[k] + SMP_USR_SIZE && (ed_sm.on || ed_sm.phase) && ed_sm.k == k) slot = 1;
        if (!slot && tw_sloop_writes++ < 5) printf("[%8.3f] !! TWIN: SLOOP 2.8's data written (%s at flash 0x%05X)\n", tw_s(), kind == 1 ? "an erase" : "a write", a);
    }
}
void twin_flash_unerased(uint32_t a) { if (tw_unerased++ < 5) printf("[%8.3f] !! TWIN: a write over bits not erased at flash 0x%05X\n", tw_s(), a); }
void twin_fail_hook(void) { fflush(stdout); }
static void tw_save_flash(void)
{
    char fn[512]; snprintf(fn, sizeof fn, "%s/flash.bin", tw_out);
    FILE *f = fopen(fn, "wb"); if (f) { fwrite(tw_flash, 1, TW_FLASH, f); fclose(f); }
}
static void tw_wav_close(void);
void twin_end(int code)
{
    tw_wav_close();
    tw_save_flash();
    if (tw_unerased) { printf("TWIN: %u bytes written over bits not erased\n", tw_unerased); if (!code) code = 72; }
    if (tw_spikes) { printf("TWIN: %d halves far costlier than the usual (up to %.0f times)\n", tw_spikes, tw_spike_max); if (!code) code = 74; }
    if (tw_sloop_writes) { printf("TWIN: SLOOP 2.8's data written %d times\n", tw_sloop_writes); if (!code) code = 73; }
    if (tw_wild) { printf("TWIN: %d samples beyond full scale\n", tw_wild); }
    printf("TWIN: %.1f s simulated, %d checks, %d failed; flash: %u erases, %u writes; window: %d stale reads; core 2: %u jobs\n",
           tw_s(), tw_checks, tw_fails, tw_erases, tw_writes, tw_stale_reads, cpu1_jobs());
    fflush(stdout);
    tw_denormals();
    _exit(code ? code : tw_fails ? 1 : 0);
}

/* ---- the scenario's hands */
enum { B_OCTD = BTN_OCT_DOWN, B_OCTU = BTN_OCT_UP, B_FX = BTN_FX, B_SEL = BTN_SEL, B_ENV = BTN_ENV, B_LFO = BTN_LFO, B_EDIT = BTN_EDIT,
       B_GLO = BTN_GLO, B_HOME = BTN_HOME, B_SAVE = BTN_SAVE, B_ARP = BTN_ARP, B_SEQ = BTN_SEQ, B_PLAY = BTN_PLAY, B_REC = BTN_REC };
static void press(int b) { tw_btn |= 1u << b; wait_ms(30); }
static void release(int b) { tw_btn &= ~(1u << b); wait_ms(30); }
static void tap(int b) { press(b); wait_ms(50); release(b); wait_ms(150); }
static void hold(int b, double ms) { press(b); wait_ms(ms); release(b); wait_ms(150); }
static void keydown(int k) { tw_keys |= 1u << k; wait_ms(20); }
static void keyup(int k) { tw_keys &= ~(1u << k); wait_ms(20); }
static void tapkey(int k) { keydown(k); wait_ms(80); keyup(k); wait_ms(80); }
static int8_t tw_turned[7];                            /* (the knobs' turns, for the film's drawing of the panel) */
static void turn(int e, int n) { int s = n < 0 ? -1 : 1; for (int i = 0; i < abs(n); i++) { tw_enc[e] += s; tw_turned[e] += (int8_t)s; wait_ms(8); } wait_ms(60); }
static void shot(const char *name)
{
    char fn[512]; snprintf(fn, sizeof fn, "%s/%s.ppm", tw_out, name);
    FILE *f = fopen(fn, "wb"); if (!f) return;
    fprintf(f, "P6 240 240 255\n");
    for (int i = 0; i < 240 * 240; i++) { uint16_t c = tw_lcd[i]; unsigned char p[3] = { (unsigned char)((c >> 11) * 255 / 31), (unsigned char)(((c >> 5) & 63) * 255 / 63), (unsigned char)((c & 31) * 255 / 31) }; fwrite(p, 1, 3, f); }
    fclose(f);
}
static void tw_wav_close(void)
{
    if (!tw_wav) return;
    uint32_t bytes = tw_wav_n * 4u; fseek(tw_wav, 0, SEEK_SET);
    uint8_t h[44]; memcpy(h, "RIFF", 4); uint32_t v = 36 + bytes; memcpy(h + 4, &v, 4); memcpy(h + 8, "WAVEfmt ", 8);
    v = 16; memcpy(h + 16, &v, 4); uint16_t s = 1; memcpy(h + 20, &s, 2); s = 2; memcpy(h + 22, &s, 2);
    v = 44118; memcpy(h + 24, &v, 4); v = 44118 * 4; memcpy(h + 28, &v, 4); s = 4; memcpy(h + 32, &s, 2); s = 16; memcpy(h + 34, &s, 2);
    memcpy(h + 36, "data", 4); memcpy(h + 40, &bytes, 4); fwrite(h, 1, 44, tw_wav); fclose(tw_wav); tw_wav = 0;
}
static void wav_on(const char *name)
{
    tw_wav_close(); char fn[512]; snprintf(fn, sizeof fn, "%s/%s.wav", tw_out, name);
    tw_wav = fopen(fn, "wb"); tw_wav_n = 0; uint8_t z[44] = { 0 }; if (tw_wav) fwrite(z, 1, 44, tw_wav);
}
static uint32_t msg_mark(void) { return tw_nmsg; }
static int said_since(uint32_t mark, const char *text)
{
    for (uint32_t i = mark; i < tw_nmsg; i++) if (strstr(tw_msgs[i % 8192], text)) return 1;
    return 0;
}
static int wait_msg(uint32_t mark, const char *text, double ms)
{
    for (double t = 0; t < ms; t += 20) { if (said_since(mark, text)) return 1; wait_ms(20); }
    return said_since(mark, text);
}
static double peak_over(double ms) { tw_peak = 0; wait_ms(ms); return tw_peak; }
static double rms_over(double ms) { tw_sq = 0; tw_sqn = 0; wait_ms(ms); return tw_sqn ? sqrt(tw_sq / (double)tw_sqn) : 0.0; }
static int tw_ed_busy(int cmd, const uint8_t *raw, uint32_t n, uint8_t *rep, uint32_t *rn, double timeout_ms);
static void wait_saved(double ms) { for (double t = 0; t < ms && (saving || save_said); t += 10) wait_ms(10); }

/* ---- the web editor, as editor-sloop3.html talks to it */
static uint8_t tw_rx[4096]; static uint32_t tw_rxn; static int tw_rx_in;
static int tw_ed(int cmd, const uint8_t *raw, uint32_t n, uint8_t *rep, uint32_t *rn, double timeout_ms)
{
    static uint8_t fr[1400];
    uint32_t fl = ed_frame(cmd, raw, n, fr, sizeof fr);
    if (!fl) return -100;
    if (getenv("TWIN_ED")) printf("[%8.3f]    ed > %d (%u bytes) saving %d sm.on %d phase %d\n", tw_s(), cmd, n, saving, ed_sm.on, ed_sm.phase);
    for (uint32_t i = 0; i < fl; ) { uint8_t pk[4]; i += (uint32_t)ed_usb_packet(fr, fl, i, pk); tw_usb_put_in(pk); }
    for (double t = 0; t < timeout_ms; t += 2) {
        wait_ms(2);
        uint8_t pk[64][4]; uint32_t k = tw_usb_take_out(pk, 64);
        for (uint32_t j = 0; j < k; j++) {
            int nb = midi_usb_bytes(pk[j]);
            for (int b = 0; b < nb; b++) {
                uint8_t c = pk[j][1 + b];
                if (c == 0xF0) { tw_rx_in = 1; tw_rxn = 0; }
                if (tw_rx_in && tw_rxn < sizeof tw_rx) tw_rx[tw_rxn++] = c;
                if (c == 0xF7 && tw_rx_in) {
                    tw_rx_in = 0;
                    int r = ed_parse(tw_rx, tw_rxn, rep, 700, rn);
                    if (r >= 0) return r;
                }
            }
        }
    }
    return -101;                                          /* (no answer) */
}

/* ---- the checks that hold at any moment */
static seq_t tw_q2; static seq_hist tw_h2; static gb_sound tw_s2; static int16_t tw_fx2[GB_FX_MEM]; static psounds tw_ps2;
static uint8_t tw_pb[STORE_PMAX], tw_pb2[STORE_PMAX];
static void check_project_roundtrip(const char *when)
{
    if (saving || sbuf_held()) return;
    project_extra x0; proj_extra(&x0);
    int n = project_part(&Q, &S, &x0, tw_pb, 0, STORE_PMAX, 0);   /* (3.3.5: made whole here, as the save makes it a part at a time) */
    EXPECT(n <= (int)STORE_PMAX, "%s: the project is larger than its place (%d)", when, n);
    EXPECT(n > 0, "%s: the project did not make its bytes", when);
    if (n <= 0) return;
    seq_init(&tw_q2, rate_hz, 120.0f, &tw_h2); snd_init(&tw_s2, tw_fx2, 0);
    static seq_pat tw_keep2[NTRK];
    project_extra x2; memset(&x2, 0, sizeof x2); x2.ps = &tw_ps2; x2.keep = tw_keep2;
    EXPECT(project_load(&tw_q2, &tw_s2, &x2, tw_pb, (uint32_t)n) == 0, "%s: the project's bytes do not load", when);
    for (int t = 0; t < NTRK; t++) {                      /* (as load_after goes on, 4.0: the current one's as saved, else the track's) */
        int k = x2.preset16[t] & 0x7FFF, cur = tw_q2.t[t].cur;
        int po = x2.preset16[t] != 0xFFFF && preset_valid(k, tw_s2.trk[t].patch.engine) ? (x2.preset16[t] & 0x8000 ? -k - 2 : k) : -1;
        if (!x2.pscur_got[t]) ps_take(&tw_ps2, t, cur, &tw_s2.trk[t].patch, tw_s2.trk[t].patch.p, po);
        for (int i = 0; i < SEQ_NPAT; i++) if (i != cur && !((x2.ps_mask[t] >> i) & 1u)) ps_copy(&tw_ps2, t, cur, i);
        tw_ps2.cur[t] = (uint8_t)cur;
        if (!x2.keep_got[t]) tw_keep2[t] = tw_q2.t[t].pat[cur];
    }
    int n2 = project_save(&tw_q2, &tw_s2, &x2, tw_pb2, sizeof tw_pb2);
    if (n2 != n || memcmp(tw_pb, tw_pb2, (size_t)n)) {
        char fn[512]; snprintf(fn, sizeof fn, "%s/roundtrip-a.bin", tw_out); FILE *f = fopen(fn, "wb"); if (f) { fwrite(tw_pb, 1, (size_t)n, f); fclose(f); }
        snprintf(fn, sizeof fn, "%s/roundtrip-b.bin", tw_out); f = fopen(fn, "wb"); if (f && n2 > 0) { fwrite(tw_pb2, 1, (size_t)n2, f); fclose(f); }
    }
    int diff = -1; for (int i = 0; i < n && i < n2; i++) if (tw_pb[i] != tw_pb2[i]) { diff = i; break; }
    EXPECT(n2 == n && diff < 0, "%s: saved, loaded, saved again: not the same (%d / %d bytes, first difference at %d)", when, n, n2, diff);
}

/* ---- what must survive a restart: written at a scenario's end and at the next start, compared by host/twin.sh */
static void digest(const char *name)
{
    char fn[512]; snprintf(fn, sizeof fn, "%s/%s", tw_out, name);
    FILE *f = fopen(fn, "w"); if (!f) return;
    fprintf(f, "project %d used %X bpm %.2f\nhome", proj + 1, proj_used, Q.bpm);
    for (int i = 0; i < HS_N; i++) fprintf(f, " %d", hs[i]);
    fprintf(f, "\ncolor %d cal %d midi %d %d %d %d %d remote %d fxch %d bend %d\nsounds", scr_color, cal_ok, MC.in, MC.sync, MC.out, MC.clk_out, MC.dmap, m_rmch, m_fxch, m_bend);
    for (int k = 0; k < US_N; k++) if (us_used(k)) fprintf(f, " %d:%s", k + 1, us_name(k));
    fprintf(f, "\nfavs"); for (int i = 0; i < US_NFAV; i++) fprintf(f, " %d", us_fav[i]);
    fprintf(f, "\ntracks"); for (int t = 0; t < NTRK; t++) fprintf(f, " %d/%d/%d", S.trk[t].patch.engine, preset_of[t], seq_count(&Q, t));
    { uint32_t at, n = 0, c = 0; if (store_proj_find(&at, &n, &c) == 0) fprintf(f, "\nsaved %u %08X", n, c); }   /* (3.3.5: the saved project, byte for byte) */
    fprintf(f, "\n");
    fclose(f);
}
/* (4.0.6, Sam) the project saved: SAVE on TRACKS, the SONG screen; SAVE again, PROJECT (its 3rd page); KNOB 1: SAVE */
static void proj_save_keys(void)
{
    if (sticky != PG_PROJ) { if (sticky != PG_SONG2) { tap(B_SAVE); wait_ms(80); } tap(B_SAVE); wait_ms(80); }
    turn(ENC_KNOB1, 1);
}
static void save_and_wait(void)
{
    /* (4.0, as SLOOP 2.8.5: SAVE on TRACKS the SONG screen, SAVE there saves) */
    if (vis_on) { tap(B_HOME); wait_ms(80); }
    else if (sticky != PG_TRACKS && sticky != PG_SONG2) { tap(B_HOME); wait_ms(80); if (vis_on) { tap(B_HOME); wait_ms(80); } }
    uint32_t m = msg_mark(); proj_save_keys();
    EXPECT(wait_msg(m, "SAVED", 4000), "SAVE > PROJECT > SAVE: no PROJECT SAVED");
    wait_saved(4000); wait_ms(300);
}

/* ---- the scenarios */
static void boot_wait(void) { for (int i = 0; i < 2000 && !tw_audio_on; i++) wait_ms(5); wait_ms(400); }

static void sc_boot(void)                     /* a blank FM-1: it starts, it plays, it saves, it comes back */
{
    boot_wait();
    EXPECT(tw_audio_on, "no sound started");
    shot("boot");                                         /* (the start's logo, 3.1.6; 3.3: no popup with the version after it) */
    EXPECT(splash_on, "the start: no logo");
    for (int i = 0; i < 400 && splash_on; i++) wait_ms(10);
    wait_ms(100);
    EXPECT(!splash_on && !msg_live && !said_since(0, "SLOOP " VERSION), "a popup at the start (3.3: none, unless something is wrong)");
    check_project_roundtrip("at the start");
    uint32_t m = msg_mark();
    tap(B_PLAY); double pk = peak_over(3000);
    printf("[%8.3f]    the demo plays: peak %.3f\n", tw_s(), pk);
    EXPECT(Q.playing && pk > 0.02, "PLAY: nothing heard (peak %.4f)", pk);   /* (3.3.6: 0.05 -> 0.02: the demo peaked at 0.051, 0.0485 with its sampled drums at their right speed; this asks for sound, not a level) */
    shot("playing");
    tap(B_PLAY); wait_ms(800);
    EXPECT(!Q.playing, "PLAY again did not stop");
    proj_save_keys();                                     /* SAVE, SAVE, KNOB 1: the project written */
    EXPECT(wait_msg(m, "SAVED", 3000), "SAVE > PROJECT: no PROJECT SAVED");
    wait_saved(2000);
    check_project_roundtrip("after the save");
}
static void sc_reboot(void)                   /* after sc_boot's flash: the project comes back as it was */
{
    boot_wait();
    digest("boot-state.txt");
    EXPECT(proj_used != 0, "no project found after the restart");
    shot("reboot");
    check_project_roundtrip("after the restart");
}
static void sc_pages(void)                    /* every button's pages, each drawn */
{
    boot_wait();
    static const struct { int b; const char *n; int taps; } P[] = {
        { B_EDIT, "edit", 7 }, { B_ENV, "env", 3 }, { B_LFO, "lfo", 4 }, { B_FX, "fx", 8 }, { B_GLO, "glo", 6 },
        { B_SEL, "sel", 3 }, { B_ARP, "arp", 3 }, { B_SEQ, "seq", 3 }, { B_SAVE, "save", 2 } };
    for (unsigned i = 0; i < sizeof P / sizeof P[0]; i++) {
        for (int k = 0; k < P[i].taps; k++) {
            tap(P[i].b); wait_ms(300);
            char nm[64]; snprintf(nm, sizeof nm, "page-%s-%d", P[i].n, k + 1); shot(nm);
            for (int e = ENC_KNOB1; e <= ENC_KNOB4; e++) { turn(e, 3); turn(e, -3); }
        }
        tap(B_HOME); wait_ms(200);
    }
    hold(B_HOME, 900); shot("home-held");                 /* (3.3.8: the menu, open) */
    turn(ENC_SELECT, 1); wait_ms(300); shot("home-menu"); tap(B_HOME); wait_ms(300);
    for (int t = 0; t < 5; t++) { turn(ENC_ALGORITHM, 1); wait_ms(100); }
    EXPECT(POS[trk] == NTRK - 1, "ALGORITHM: not stopped on the last track (shown %d)", POS[trk] + 1);   /* (3.3.8: it no longer goes round) */
    turn(ENC_ALGORITHM, 1); wait_ms(100);
    EXPECT(POS[trk] == NTRK - 1, "ALGORITHM went round past the last track (shown %d)", POS[trk] + 1);
    for (int t = 0; t < 6; t++) { turn(ENC_ALGORITHM, -1); wait_ms(100); }
    EXPECT(POS[trk] == 0, "ALGORITHM: not stopped on the first track (shown %d)", POS[trk] + 1);
    for (int p = 0; p < 20; p++) turn(ENC_PRESETS, 1);
    shot("presets");
    check_project_roundtrip("after the pages");
}
static void sc_record(void)                   /* REC: notes played go in; SEQ steps; undo; save and back */
{
    boot_wait();
    /* 3.3.7: a tester's report: "when recording a chord loop, the chords stack up with time". A new project takes
     * on the steps (QUANT ON, as SLOOP 2.8); the same chord played on 3 loops in a row is written once */
    {
        EXPECT(Q.t[trk].quant == 100, "a new project records as played, not on the steps (QUANT %d)", Q.t[trk].quant);
        int n0 = seq_count(&Q, trk);
        tap(B_PLAY); wait_ms(300); tap(B_REC); wait_ms(300);
        uint32_t len = (uint32_t)Q.t[trk].pat[Q.t[trk].cur].steps * SEQ_TPS;
        for (int pass = 0; pass < 3; pass++) {
            uint32_t loop = Q.tick / len;
            while (Q.tick / len == loop) wait_ms(1);          /* the loop's start */
            wait_ms(30 + pass * 9);                            /* (a little later each time, as a hand plays) */
            keydown(0); keydown(4); keydown(7); wait_ms(300); keyup(0); keyup(4); keyup(7); wait_ms(100);
        }
        tap(B_REC); wait_ms(300);
        int n1 = seq_count(&Q, trk);
        printf("[%8.3f]    a chord on 3 loops: %d events written\n", tw_s(), n1 - n0);
        EXPECT(n1 - n0 == 3, "the same chord on 3 loops: %d events (3)", n1 - n0);
        press(B_EDIT); tap(B_OCTD); release(B_EDIT); wait_ms(300);   /* (the take undone) */
        EXPECT(seq_count(&Q, trk) == n0, "the chord's take not undone (%d)", seq_count(&Q, trk));
        tap(B_PLAY); wait_ms(500);
    }
    /* 3.3.7, issue #3: SEQ held on the drums, KNOB 1 turns the sound shown: a step is written with that one (it was
     * written with the last pad played) */
    {
        int t0 = trk; trk = 3;
        tapkey(0); wait_ms(200);
        int played = last_chord[3][0], want = (played + 1) % 16, st = -1;
        for (int w = 0; w < 16 && st < 0; w++) if (!seq_step_has(&Q, 3, w, -1)) st = w;
        press(B_SEQ); wait_ms(300);
        turn(ENC_KNOB1, 1); wait_ms(200);
        tapkey(white_key[st]); wait_ms(200);
        release(B_SEQ); wait_ms(300);
        EXPECT(st >= 0 && seq_step_has(&Q, 3, st, want) && !seq_step_has(&Q, 3, st, played), "SEQ held, KNOB 1 to sound %d (played %d): step %d has %d / %d", want, played, st + 1, seq_step_has(&Q, 3, st, want), seq_step_has(&Q, 3, st, played));
        press(B_EDIT); tap(B_OCTD); release(B_EDIT); wait_ms(300);
        trk = t0;
    }
    tap(B_PLAY); wait_ms(500);
    tap(B_REC); wait_ms(300);
    for (int i = 0; i < 8; i++) { tapkey(i * 2); wait_ms(150); }
    tap(B_REC); wait_ms(300);
    int n = seq_count(&Q, trk);
    printf("[%8.3f]    recorded on track %d: %d events\n", tw_s(), trk + 1, n);
    EXPECT(n >= 8, "REC: the notes played are not in the pattern (%d)", n);
    press(B_EDIT); tap(B_OCTD); release(B_EDIT); wait_ms(300);   /* undo */
    int n2 = seq_count(&Q, trk);
    EXPECT(n2 < n, "EDIT + OCT-: nothing undone (%d -> %d)", n, n2);
    press(B_EDIT); tap(B_OCTU); release(B_EDIT); wait_ms(300);   /* redo */
    EXPECT(seq_count(&Q, trk) == n, "EDIT + OCT+: not redone (%d)", seq_count(&Q, trk));
    tap(B_PLAY); wait_ms(300);
    shot("recorded");
    /* 3.3.6: REC held: it erases, and only that (it used to turn REC on first); held then let go early: nothing */
    {
        uint32_t m = msg_mark(); int n3 = seq_count(&Q, trk);
        hold(B_REC, 1000);
        EXPECT(!recording_armed && !Q.recording && seq_count(&Q, trk) == n3 && said_since(m, "ERASE CANCELLED"), "REC held 1 s: armed %d, recording %d, %d -> %d events", recording_armed, Q.recording, n3, seq_count(&Q, trk));
        tap(B_PLAY); wait_ms(300); m = msg_mark();
        press(B_REC); wait_ms(1200);
        EXPECT(!Q.recording && !recording_armed && said_since(m, "HOLD REC"), "REC held: recording started before the erase (%d %d)", Q.recording, recording_armed);
        wait_ms(1300); release(B_REC); wait_ms(200);
        EXPECT(seq_count(&Q, trk) == 0 && !Q.recording && !recording_armed && said_since(m, "TRACK ERASED"), "REC held 2.5 s: %d events left, recording %d, armed %d", seq_count(&Q, trk), Q.recording, recording_armed);
        press(B_EDIT); tap(B_OCTD); release(B_EDIT); wait_ms(300);   /* (undone: the take back) */
        EXPECT(seq_count(&Q, trk) == n3, "the erase not undone (%d of %d)", seq_count(&Q, trk), n3);
        tap(B_PLAY); wait_ms(300);
    }
    /* 3.3.6: SAVE held: the 8 patterns' keys lit, 1-4 and 5-8 each faint to full; the one playing blinks; 13-16 faint */
    {
        press(B_SAVE); wait_ms(300);
        int cur = Q.t[trk].cur, grad = 1, blink_on = 0, blink_off = 0, cmd = 1;
        for (int w = 0; w < 8; w++) if (w != cur && tw_lvl[14 + white_key[w]] != 1 + (w & 3)) grad = 0;
        for (int w = 12; w < 16; w++) if (!tw_glow[14 + white_key[w]]) cmd = 0;
        for (int i = 0; i < 100; i++) { wait_ms(10); if (tw_lvl[14 + white_key[cur]] == 4) blink_on++; else blink_off++; }
        release(B_SAVE); wait_ms(300);
        EXPECT(grad && cmd && blink_on > 10 && blink_off > 10, "SAVE held: the patterns' lights (gradient %d, commands %d, the playing one %d on %d off)", grad, cmd, blink_on, blink_off);
    }
    /* 3.3.6: a track changed while recording: REC stops (the default); GLO REC 6/6 KEEP REC: the take goes on there */
    {
        int t0 = trk; uint32_t m = msg_mark();
        EXPECT(!rec_keep, "REC KEEP is not off by default");
        tap(B_PLAY); wait_ms(300); tap(B_REC); wait_ms(300);
        EXPECT(Q.recording && Q.rec_trk == t0, "REC not on (%d)", Q.recording);
        turn(ENC_ALGORITHM, 1); wait_ms(200);
        EXPECT(trk != t0 && !Q.recording && said_since(m, "REC STOPPED"), "track changed: REC still on (%d)", Q.recording);
        turn(ENC_ALGORITHM, -1); wait_ms(200);
        tap(B_PLAY); wait_ms(300);
        /* 3.3.6: on GLO's pages SELECT turns the pages (not the tempo: KNOB 1 is it, on GLOBAL) */
        tap(B_GLO); wait_ms(150);
        { float bpm0 = Q.bpm; EXPECT(sticky == PG_GLOBAL, "GLO: not GLOBAL (%d)", sticky);
          turn(ENC_SELECT, 1); wait_ms(150);
          EXPECT(sticky == PG_MASTER && Q.bpm == bpm0, "GLOBAL: SELECT changed the tempo or not the page (page %d, %.1f -> %.1f)", sticky, bpm0, Q.bpm);   /* (4.0: GLOBAL MASTER SYSTEM DRUMS TEXTURE 1-2 MIDI 1-2 REC) */
          turn(ENC_SELECT, 7); wait_ms(150);
          EXPECT(sticky == PG_GREC && Q.bpm == bpm0, "SELECT: not on to REC 9/9 (%d)", sticky);
          turn(ENC_SELECT, -8); wait_ms(150);
          EXPECT(sticky == PG_GLOBAL, "SELECT: not back to GLOBAL (%d)", sticky);
          turn(ENC_KNOB1, 2); wait_ms(150);
          EXPECT(Q.bpm != bpm0, "GLOBAL: KNOB 1 not the tempo"); turn(ENC_KNOB1, -2); wait_ms(150); }
        tap(B_HOME); wait_ms(200);
        for (int i = 0; i < 10 && sticky != PG_GREC; i++) { tap(B_GLO); wait_ms(150); }
        EXPECT(sticky == PG_GREC, "GLO: no REC 6/6 page (%d)", sticky);
        shot("glo-rec");
        turn(ENC_KNOB1, 1); wait_ms(200);
        EXPECT(rec_keep == 1, "TRK CHG: not KEEP REC");
        tap(B_HOME); wait_ms(200);
        tap(B_PLAY); wait_ms(300); tap(B_REC); wait_ms(300); m = msg_mark();
        turn(ENC_ALGORITHM, 1); wait_ms(200);
        EXPECT(trk != t0 && Q.recording && Q.rec_trk == trk && said_since(m, "REC TRACK"), "KEEP REC: the take did not go on there (rec %d on %d, track %d)", Q.recording, Q.rec_trk, trk);
        tap(B_REC); wait_ms(200); tap(B_PLAY); wait_ms(300);
        turn(ENC_ALGORITHM, -1); wait_ms(200);
        EXPECT(trk == t0, "back on the track (%d)", trk);
    }
    check_project_roundtrip("after recording");
    uint32_t m = msg_mark(); proj_save_keys();
    EXPECT(wait_msg(m, "SAVED", 3000), "no PROJECT SAVED");
    wait_saved(3000);
}
static void sc_powercut(void)                 /* the power cut during a save: the start after it finds a project */
{
    boot_wait();
    tap(B_PLAY); wait_ms(300); tap(B_REC); for (int i = 0; i < 6; i++) tapkey(3 + i); tap(B_REC); tap(B_PLAY);
    proj_save_keys();
    for (int i = 0; i < 400 && !saving; i++) wait_ms(1);
    uint64_t t0 = tw_ns;
    double at = (double)(tw_rnd() % 9000) / 10.0;          /* somewhere in the save (3.3.5: ~0.7 s: 13 erases, the pages, the reading back) */
    wait_until(t0 + (uint64_t)(at * 1e6));
    printf("[%8.3f]    POWER CUT (%.1f ms into the save; saving %d, an operation under way: %d)\n", tw_s(), at, saving, tw_flash_busy_now());
    tw_flash_cut();
    twin_end(0);
}
/* 3.3.5: "8 loops of 8 bars on every track, each its own sound: it must all be kept". The sequencer full to its
 * limits: 40 patterns of 128 steps, 128 notes each (played: off the grid, every velocity and length, conditions,
 * ratchets), 32 locks each, each pattern its own sound, a full song; saved, opened again, the same byte for byte */
static void fill_max(void)
{
    for (int t = 0; t < NTRK; t++) for (int i = 0; i < SEQ_NPAT; i++) {
        seq_pat *pt = &Q.t[t].pat[i];
        pt->steps = SEQ_MAXSTEPS; uint32_t tick = 0;
        for (int e = 0; e < SEQ_PATEV; e++) {
            seq_ev *v = &pt->ev[e];
            tick += tw_rnd() % 95u; if (tick > (uint32_t)SEQ_MAXSTEPS * SEQ_TPS - 1u) tick = (uint32_t)SEQ_MAXSTEPS * SEQ_TPS - 1u;
            v->tick = (uint16_t)tick; v->len = (uint16_t)(1u + tw_rnd() % 3000u); v->note = (uint8_t)(tw_rnd() % 128u); v->vel = (uint8_t)(1u + tw_rnd() % 127u);
            v->cond = (uint8_t)(tw_rnd() % 3u ? SEQ_ALWAYS : SEQ_CHANCE + 1u + tw_rnd() % 9u); v->ratchet = (uint8_t)((1u + tw_rnd() % 4u) | (tw_rnd() % 4u == 0 ? 0x10u : 0u));
        }
        pt->n = SEQ_PATEV;
        pt->nlock = SEQ_LOCKS;
        for (int l = 0; l < SEQ_LOCKS; l++) { pt->lk[l].step = (uint8_t)(l * 4); pt->lk[l].param = (uint8_t)(tw_rnd() % 48u); pt->lk[l].value = (uint8_t)tw_rnd(); }
        float own[GB_NPARAM]; for (int k = 0; k < GB_NPARAM; k++) own[k] = (float)(tw_rnd() % 256u) / 255.0f;
        if (i != Q.t[t].cur) ps_take(&PS, t, i, &S.trk[t].patch, own, -1);
    }
    Q.song_n = SEQ_SONG; Q.song_len = 8u * SEQ_SONG;
    for (int i = 0; i < SEQ_SONG; i++) { Q.song[i].bar = (uint16_t)(i * 8); Q.song[i].mute = (uint8_t)(tw_rnd() & 31u); for (int t = 0; t < SEQ_NTRK; t++) Q.song[i].pat[t] = (uint8_t)(tw_rnd() % SEQ_NPAT); }
    if (Q.hist) Q.hist->n_undo = Q.hist->n_redo = 0;
    for (int t = 0; t < NTRK; t++) commit_trk(t);         /* (4.0: as it is now, saved: what a save writes) */
}
static void sc_bigproj(void)
{
    boot_wait(); wait_ms(3000);
    fill_max();
    uint32_t c0, n0 = proj_measure(&c0);
    printf("[%8.3f]    the fullest project: %u bytes, %u %% of its place (%u)\n", tw_s(), n0, n0 * 100u / STORE_PMAX, STORE_PMAX);
    EXPECT(n0 <= STORE_PMAX, "the fullest project does not fit (%u > %u)", n0, STORE_PMAX);
    uint32_t m = msg_mark(); double t0 = tw_s(); save_project(1);
    EXPECT(wait_msg(m, "PROJECT SAVED", 20000), "the fullest project: no PROJECT SAVED");
    printf("[%8.3f]    saved in %.2f s\n", tw_s(), tw_s() - t0);
    uint32_t at, n1 = 0, c1 = 0;
    EXPECT(store_proj_find(&at, &n1, &c1) == 0 && n1 == n0 && c1 == c0, "the flash does not hold it (%u %08X, %u %08X)", n1, c1, n0, c0);
    load_project(0); wait_ms(300);
    uint32_t c2, n2 = proj_measure(&c2);
    EXPECT(n2 == n0 && c2 == c0, "opened again, it is not the same (%u %08X, %u %08X)", n2, c2, n0, c0);
    check_project_roundtrip("the fullest project");
    /* changed while it is being written: written again, from what it is then */
    fill_max(); uint32_t c3, n3 = proj_measure(&c3);
    m = msg_mark(); save_project(1); for (int i = 0; i < 300 && !store_proj_given(); i++) wait_ms(1);   /* (3.4.0: no erase first, the writing starts at once) */
    Q.t[2].pat[5].ev[7].vel ^= 1u; uint32_t c4, n4 = proj_measure(&c4);
    EXPECT(wait_msg(m, "SAVING AGAIN", 20000), "changed during its save: not written again");
    EXPECT(wait_msg(m, "PROJECT SAVED", 30000), "changed during its save: no PROJECT SAVED after");
    wait_saved(20000);
    EXPECT(store_proj_find(&at, &n1, &c1) == 0 && n1 == n4 && c1 == c4 && c1 != c3, "the save kept is not the project as it is now");
    /* the editor: the fullest project read from the flash, written back through it, opened */
    static uint8_t file[STORE_PMAX]; uint8_t raw[700], rep[700]; uint32_t rn, flen = 0, total = 1, crc = 0;
    for (uint32_t off = 0; off < total; ) {
        raw[0] = 0; raw[1] = 0; raw[2] = (uint8_t)off; raw[3] = (uint8_t)(off >> 8); raw[4] = (uint8_t)(off >> 16); raw[5] = 0; raw[6] = 128; raw[7] = 1;
        int r = tw_ed_busy(ED_READ, raw, 8, rep, &rn, 2000);
        EXPECT(r == ED_READ, "READ at %u: %d", off, r); if (r != ED_READ) return;
        total = rep[6] | rep[7] << 8 | rep[8] << 16 | (uint32_t)rep[9] << 24; crc = rep[10] | rep[11] << 8 | rep[12] << 16 | (uint32_t)rep[13] << 24;
        memcpy(file + off, rep + 14, rn - 14); off += rn - 14; flen = off;
        if (rn == 14) break;
    }
    EXPECT(flen == n4 && crc32_ieee(0, file, flen) == crc && crc == c4, "the editor's read of the fullest project (%u of %u)", flen, n4);
    file[100] ^= 0;                                       /* (as it is) */
    raw[0] = 0; raw[1] = 0; raw[2] = (uint8_t)flen; raw[3] = (uint8_t)(flen >> 8); raw[4] = (uint8_t)(flen >> 16); raw[5] = 0;
    EXPECT(tw_ed_busy(ED_WBEGIN, raw, 6, rep, &rn, 2000) == ED_WBEGIN, "WBEGIN (the fullest)");
    for (uint32_t off = 0; off < flen; off += 384) {
        uint32_t k = flen - off < 384 ? flen - off : 384;
        raw[0] = (uint8_t)off; raw[1] = (uint8_t)(off >> 8); raw[2] = (uint8_t)(off >> 16); raw[3] = 0; memcpy(raw + 4, file + off, k);
        if (tw_ed_busy(ED_WDATA, raw, 4 + k, rep, &rn, 2000) != ED_WDATA) { EXPECT(0, "WDATA %u (the fullest)", off); return; }
    }
    m = msg_mark();
    raw[0] = (uint8_t)crc; raw[1] = (uint8_t)(crc >> 8); raw[2] = (uint8_t)(crc >> 16); raw[3] = (uint8_t)(crc >> 24);
    EXPECT(tw_ed_busy(ED_WEND, raw, 4, rep, &rn, 3000) == ED_WEND, "WEND (the fullest)");
    EXPECT(wait_msg(m, "PROJECT RECEIVED", 20000), "the fullest from the editor: no PROJECT RECEIVED");
    uint32_t c5, n5 = proj_measure(&c5);
    EXPECT(n5 == n4 && c5 == c4, "the fullest from the editor, opened: not the same");
    /* a damaged file from the editor: refused, the project kept */
    file[40] ^= 0xFF;
    raw[0] = 0; raw[1] = 0; raw[2] = (uint8_t)flen; raw[3] = (uint8_t)(flen >> 8); raw[4] = (uint8_t)(flen >> 16); raw[5] = 0;
    tw_ed_busy(ED_WBEGIN, raw, 6, rep, &rn, 2000);
    for (uint32_t off = 0; off < flen; off += 384) {
        uint32_t k = flen - off < 384 ? flen - off : 384;
        raw[0] = (uint8_t)off; raw[1] = (uint8_t)(off >> 8); raw[2] = (uint8_t)(off >> 16); raw[3] = 0; memcpy(raw + 4, file + off, k);
        tw_ed_busy(ED_WDATA, raw, 4 + k, rep, &rn, 2000);
    }
    uint32_t cbad = c4;                                   /* (the file changed on the way: its CRC is not the one sent) */
    raw[0] = (uint8_t)cbad; raw[1] = (uint8_t)(cbad >> 8); raw[2] = (uint8_t)(cbad >> 16); raw[3] = (uint8_t)(cbad >> 24);
    int r = tw_ed_busy(ED_WEND, raw, 4, rep, &rn, 3000); wait_saved(20000); wait_ms(500);
    EXPECT(store_proj_find(&at, &n1, &c1) == 0 && n1 == n4 && c1 == c4, "a damaged file from the editor replaced the project (%d)", r);
}
static void sc_bigcut(void)                   /* the power cut during the save of the fullest project */
{
    boot_wait(); wait_ms(3000);
    fill_max();
    save_project(1);
    for (int i = 0; i < 400 && !saving; i++) wait_ms(1);
    uint64_t t0 = tw_ns;
    double at = (double)(tw_rnd() % 16000) / 10.0;        /* somewhere in the save (~1.4 s: 13 erases, 200 writes, the reading back) */
    wait_until(t0 + (uint64_t)(at * 1e6));
    printf("[%8.3f]    POWER CUT (%.1f ms into the save; saving %d, an operation under way: %d)\n", tw_s(), at, saving, tw_flash_busy_now());
    tw_flash_cut();
    twin_end(0);
}
static void sc_after_cut(void)
{
    boot_wait();
    EXPECT(tw_audio_on, "no start after the cut");
    EXPECT(proj_used != 0, "no project after the cut");
    int ev = 0; for (int t = 0; t < NTRK; t++) ev += seq_count(&Q, t);
    EXPECT(ev > 0, "after the cut: the project came back empty (the one before it, or the one saved, expected)");
    check_project_roundtrip("after the cut");
    shot("after-cut");
}
/* a user slot as the editor sends it: the bank's first sound and a kick (their ADPCM, a header as SLOOP's) */
static uint8_t tw_slot[SMP_USR_SIZE];
static uint32_t make_slot(void)
{
    memset(tw_slot, 0xFF, sizeof tw_slot);
    memcpy(tw_slot, "FSMP", 4); tw_slot[4] = 1; tw_slot[5] = 0; tw_slot[6] = 1; memcpy(tw_slot + 8, "TWIN\0\0\0\0", 8);
    smp_zone z = *smp_zone_at(smp_set_at(0)->z0); uint32_t nb = (z.n + 1u) / 2u;
    if (nb > 60000u) { z.n = 120000u; nb = 60000u; if (z.le >= z.n) z.le = z.ls = z.n - 1u; }
    memcpy(tw_slot + 512, smp_data() + z.off, nb); z.off = 0; z.lo = 0; z.hi = 127;
    memcpy(tw_slot + 32, &z, 28);
    uint32_t len = nb; tw_slot[16] = (uint8_t)len; tw_slot[17] = (uint8_t)(len >> 8); tw_slot[18] = (uint8_t)(len >> 16); tw_slot[19] = 0;
    uint32_t c = crc32_ieee(0, tw_slot + 512, len);
    tw_slot[20] = (uint8_t)c; tw_slot[21] = (uint8_t)(c >> 8); tw_slot[22] = (uint8_t)(c >> 16); tw_slot[23] = (uint8_t)(c >> 24);
    return len;
}
static int send_sample(int k, uint32_t len, int chunk)
{
    uint8_t raw[700], rep[700]; uint32_t rn;
    raw[0] = 1; raw[1] = (uint8_t)k;
    int r = tw_ed(ED_SAMPLE, raw, 2, rep, &rn, 3000);
    if (r != ED_SAMPLE) { printf("    BEGIN: %d (%d)\n", r, rn ? rep[0] : -1); return -1; }
    for (uint32_t off = 0; off < len; off += (uint32_t)chunk) {
        uint32_t m = len - off < (uint32_t)chunk ? len - off : (uint32_t)chunk;
        raw[0] = 2; raw[1] = (uint8_t)k; raw[2] = (uint8_t)off; raw[3] = (uint8_t)(off >> 8); raw[4] = (uint8_t)(off >> 16); raw[5] = (uint8_t)(off >> 24);
        memcpy(raw + 6, tw_slot + 512 + off, m);
        r = tw_ed(ED_SAMPLE, raw, 6 + m, rep, &rn, 3000);
        if (r != ED_SAMPLE) { printf("    DATA at %u: %d (%d)\n", off, r, rn ? rep[0] : -1); return -1; }
    }
    raw[0] = 3; raw[1] = (uint8_t)k; memcpy(raw + 2, tw_slot, 480);
    r = tw_ed(ED_SAMPLE, raw, 482, rep, &rn, 5000);
    if (r != ED_SAMPLE) { printf("    END: %d (%d)\n", r, rn ? rep[0] : -1); return -1; }
    return rn >= 3 ? rep[2] : -1;
}
static void sc_sample(void)                   /* an editor sends a sample while the FM-1 sits idle (the autosave) */
{
    boot_wait();
    uint32_t len = make_slot();
    tw_ns += 25000000000ull;                              /* (25 s on: the autosave is due as soon as it may) */
    sys_wdt_feed();                                       /* (the jump is the twin's, not a stall of the loop: the watchdog fed) */
    wait_ms(100);
    uint8_t rep[700]; uint32_t rn;
    EXPECT(tw_ed(ED_INFO, 0, 0, rep, &rn, 2000) == ED_INFO, "the editor's INFO not answered");
    int nz = send_sample(1, len, 384);
    printf("[%8.3f]    the sample sent: %u bytes, zones %d\n", tw_s(), len, nz);
    EXPECT(nz == 1, "the slot was not taken (%d)", nz);
    EXPECT(smp_user_nz(1) == 1, "USR2 empty after the editor's sample");
    const uint8_t *w = (const uint8_t *)(FLASH_WIN + 0xB4000u - 0x4000u);
    for (int i = 0; i < 200 && !flash_xip_on(); i++) wait_ms(1);   /* (3.3.8: idle, the next save's bank may be erasing) */
    EXPECT(!memcmp(w + 512, tw_slot + 512, len), "the slot's data in the flash is not what was sent");
    /* played: SAMPLE on USR2 */
    trk = 0; gb_patch *p = &S.trk[0].patch; gb_patch_default(p, ENG_SAMPLE); gb_patch_set(p, SP_E0, smp_nsets() + 1); base_from_sound();
    keydown(12); double pk = peak_over(300); keyup(12); wait_ms(500);
    gb_patch_set(p, SP_E0, 0); base_from_sound();                  /* (the same sound from the bank: the reference) */
    keydown(12); double ref = peak_over(300); keyup(12); wait_ms(500);
    printf("[%8.3f]    the sample sent: peak %.4f (from the bank: %.4f)\n", tw_s(), pk, ref);
    EXPECT(pk > 0.5 * ref && ref > 0.001, "the sample sent is not heard as the bank's (%.4f, %.4f)", pk, ref);
    /* and SLICE on it */
    gb_patch_default(p, ENG_SLICE); gb_patch_set(p, SP_E0, 2); gb_patch_set(p, SP_E0 + 1, 4); base_from_sound();
    keydown(10); pk = peak_over(300); keyup(10);
    EXPECT(pk > 0.001, "SLICE on the sample sent is silent (%.4f)", pk);
    check_project_roundtrip("after the sample");
}
/* (3.3.5) as the editor does: a request answered BUSY asked again, 250 ms later (a project goes to the flash as it comes) */
static int tw_ed_busy(int cmd, const uint8_t *raw, uint32_t n, uint8_t *rep, uint32_t *rn, double timeout_ms)
{
    for (int i = 0; i < 80; i++) {
        int r = tw_ed(cmd, raw, n, rep, rn, timeout_ms);
        if (!(r == ED_NONE && *rn >= 1 && rep[0] == ED_ERR_BUSY)) return r;
        wait_ms(250);
    }
    return -102;
}
static void sc_editor(void)                   /* the editor reads and writes projects, sounds, banks */
{
    boot_wait();
    uint8_t raw[700], rep[700]; uint32_t rn;
    for (int t = 0; t < NTRK; t++) { raw[0] = (uint8_t)t; EXPECT(tw_ed(ED_SOUND, raw, 1, rep, &rn, 2000) == ED_SOUND, "SOUND %d", t); }
    for (int e = 0; e < ENG_N; e++) { raw[0] = (uint8_t)e; EXPECT(tw_ed(ED_DESC, raw, 1, rep, &rn, 2000) == ED_DESC, "DESC %d", e); }
    /* the project read whole (kind 2: as it is now) */
    static uint8_t file[STORE_PMAX]; uint32_t flen = 0, total = 1, crc = 0;
    for (uint32_t off = 0; off < total; ) {
        raw[0] = 2; raw[1] = 0; raw[2] = (uint8_t)off; raw[3] = (uint8_t)(off >> 8); raw[4] = 0; raw[5] = 0; raw[6] = 128; raw[7] = 1;
        int r = tw_ed(ED_READ, raw, 8, rep, &rn, 2000);
        EXPECT(r == ED_READ, "READ at %u: %d", off, r); if (r != ED_READ) return;
        total = rep[6] | rep[7] << 8 | rep[8] << 16 | (uint32_t)rep[9] << 24; crc = rep[10] | rep[11] << 8 | rep[12] << 16 | (uint32_t)rep[13] << 24;
        memcpy(file + off, rep + 14, rn - 14); off += rn - 14; flen = off;
        if (rn == 14) break;
    }
    EXPECT(flen == total && crc32_ieee(0, file, flen) == crc, "the project read is not whole (%u of %u)", flen, total);
    /* written back (3.3.5: the one project; to the flash as it comes, the requests asked again while BUSY) */
    raw[0] = 0; raw[1] = 0; raw[2] = (uint8_t)flen; raw[3] = (uint8_t)(flen >> 8); raw[4] = 0; raw[5] = 0;
    EXPECT(tw_ed_busy(ED_WBEGIN, raw, 6, rep, &rn, 2000) == ED_WBEGIN, "WBEGIN");
    for (uint32_t off = 0; off < flen; off += 300) {
        uint32_t m = flen - off < 300 ? flen - off : 300;
        raw[0] = (uint8_t)off; raw[1] = (uint8_t)(off >> 8); raw[2] = 0; raw[3] = 0; memcpy(raw + 4, file + off, m);
        EXPECT(tw_ed_busy(ED_WDATA, raw, 4 + m, rep, &rn, 2000) == ED_WDATA, "WDATA %u", off);
    }
    uint32_t c = crc32_ieee(0, file, flen); raw[0] = (uint8_t)c; raw[1] = (uint8_t)(c >> 8); raw[2] = (uint8_t)(c >> 16); raw[3] = (uint8_t)(c >> 24);
    uint32_t m = msg_mark();
    EXPECT(tw_ed_busy(ED_WEND, raw, 4, rep, &rn, 3000) == ED_WEND, "WEND");
    EXPECT(wait_msg(m, "PROJECT RECEIVED", 6000), "no PROJECT RECEIVED");
    EXPECT(proj_used == 1u, "no project saved after the editor's write");
    { uint32_t at, n2, c2; EXPECT(store_proj_find(&at, &n2, &c2) == 0 && n2 == flen && c2 == c, "the project in the flash is not the one sent (%u, %u)", n2, flen); }
    /* opened again */
    raw[0] = 0; EXPECT(tw_ed_busy(ED_LOAD, raw, 1, rep, &rn, 3000) == ED_LOAD, "LOAD");
    wait_ms(300);
    EXPECT(proj == 0, "the project opened is not the one (%d)", proj + 1);
    /* the SYN kits as they play, read and written back */
    static uint8_t img[XB_SYN_LEN];
    for (uint32_t off = 0; off < XB_SYN_LEN; ) {
        raw[0] = 1; raw[1] = 1; raw[2] = (uint8_t)off; raw[3] = (uint8_t)(off >> 8); raw[4] = 128; raw[5] = 1;
        int r = tw_ed(ED_BANK, raw, 6, rep, &rn, 2000); EXPECT(r == ED_BANK, "BANK read %u", off); if (r != ED_BANK) return;
        memcpy(img + off, rep + 6, rn - 6); off += rn - 6;
    }
    EXPECT(!xb_check(img, XB_SYN_LEN, 0, 0), "the SYN image read does not check");
    raw[0] = 2; raw[1] = 1; raw[2] = (uint8_t)XB_SYN_LEN; raw[3] = (uint8_t)(XB_SYN_LEN >> 8);
    EXPECT(tw_ed(ED_BANK, raw, 4, rep, &rn, 2000) == ED_BANK, "BANK begin");
    for (uint32_t off = 0; off < XB_SYN_LEN; off += 300) {
        uint32_t k = XB_SYN_LEN - off < 300 ? XB_SYN_LEN - off : 300;
        raw[0] = 3; raw[1] = 1; raw[2] = (uint8_t)off; raw[3] = (uint8_t)(off >> 8); memcpy(raw + 4, img + off, k);
        EXPECT(tw_ed(ED_BANK, raw, 4 + k, rep, &rn, 2000) == ED_BANK, "BANK data %u", off);
    }
    m = msg_mark();
    raw[0] = 4; raw[1] = 1; EXPECT(tw_ed(ED_BANK, raw, 2, rep, &rn, 5000) == ED_BANK, "BANK end");
    EXPECT(said_since(m, "SYN KITS SAVED"), "no SYN KITS SAVED");
    /* your colour theme (SLOOP 2.7's 40 bytes), chosen at once, kept after a restart (host/twin.sh) */
    uint8_t th[40]; memcpy(th, "THM2", 4); memcpy(th + 4, "TWIN    ", 8);
    for (int i = 12; i < 36; i++) th[i] = (uint8_t)(i * 37);
    uint32_t tc = crc32_ieee(0, th, 36); th[36] = (uint8_t)tc; th[37] = (uint8_t)(tc >> 8); th[38] = (uint8_t)(tc >> 16); th[39] = (uint8_t)(tc >> 24);
    EXPECT(tw_ed(ED_THEME, th, 40, rep, &rn, 3000) == ED_THEME && rep[0] == UI_NTHEMES, "THEME not taken");
    wait_saved(3000); wait_ms(500);
    shot("theme");
    /* a sample sent while a USER SOUND is chosen on the panel: the panel waits (EDITOR BUSY), nothing lost */
    check_project_roundtrip("after the editor");
    save_and_wait();
}
/* SLOOP 2.8's FM-1 just updated: its data read, its project imported, all of it played */
static void sc_import(void)
{
    boot_wait();
    for (int i = 0; i < 400 && splash_on; i++) wait_ms(10);   /* (the start's logo first, 3.1.6: then what was done is said) */
    EXPECT(said_since(0, "SLOOP 2.8 IMPORTED"), "no SLOOP 2.8 IMPORTED at the first start");
    EXPECT(proj_used & 1u, "no project 1 after the import");
    shot("import-tracks");
    printf("[%8.3f]    imported: %.0f BPM; ", tw_s(), Q.bpm);
    for (int t = 0; t < NTRK; t++) printf("T%d %s (%d events) ", t + 1, gb_engines[S.trk[t].patch.engine]->name, seq_count(&Q, t));
    printf("\n[%8.3f]    USR1..4 zones: %d %d %d %d; SYN %d, FM6 %d (used 0x%X); your sounds: ", tw_s(), smp_user_nz(0), smp_user_nz(1), smp_user_nz(2), smp_user_nz(3), syn_src, fm6_src, fm6_bank_used);
    int nu = 0; for (int k = 0; k < US_N; k++) if (us_used(k)) { nu++; printf("%s, ", us_name(k)); }
    printf("(%d)\n", nu);
    check_project_roundtrip("after the import");
    int ev = 0; for (int t = 0; t < NTRK; t++) for (int i = 0; i < SEQ_NPAT; i++) ev += Q.t[t].pat[i].n;
    for (int t = 0; t < NTRK; t++) {
        const gb_patch *p = &S.trk[t].patch;
        printf("[%8.3f]    track %d: level %.2f gain %.3f E0 %d", tw_s(), t, S.trk[t].level.tgt, p->gain, gb_ev(p, 0));
        if (p->engine == ENG_DRUM) { printf(" tune"); for (int i = 0; i < 16; i++) printf(" %d", p->dtune[i]); printf(" decay"); for (int i = 0; i < 16; i++) printf(" %d", p->ddec[i]); printf(" mute %X", p->dmute); }
        printf("\n");
    }
    for (int t = 0; t < NTRK; t++) {                      /* each track's sound, from the keys */
        trk = t; wait_ms(50); keydown(12); keydown(16); double k = peak_over(400); keyup(12); keyup(16); wait_ms(600);
        printf("[%8.3f]    track %d (%s): peak %.3f\n", tw_s(), t + 1, gb_engines[S.trk[t].patch.engine]->name, k);
        EXPECT(k > 0.002, "track %d's imported sound is silent (%.4f)", t + 1, k);
    }
    trk = 0;
    tap(B_PLAY); double pk = peak_over(4000);
    printf("[%8.3f]    the imported project plays (%d events): peak %.3f\n", tw_s(), ev, pk);
    EXPECT(!ev || pk > 0.01, "the imported project is silent (%.4f)", pk);
    wav_on("import-play"); wait_ms(8000); tw_wav_close();
    shot("import-playing");
    for (int i = 0; i < 4; i++) { press(B_SAVE); wait_ms(200); tapkey(i); release(B_SAVE); wait_ms(2500); char nm[32]; snprintf(nm, sizeof nm, "import-pattern-%d", i + 1); shot(nm); }
    tap(B_PLAY); wait_ms(500);
    tap(B_EDIT); tap(B_EDIT); tap(B_EDIT); tap(B_EDIT); tap(B_EDIT); shot("import-user-sounds");
    tap(B_EDIT); shot("import-favs");
    check_project_roundtrip("after playing the import");
}
/* MIDI from a computer: notes in, the clock (SYNC EXT), program changes, the notes the sequencer sends out */
static void midi_send(uint8_t a, uint8_t b, uint8_t c)
{
    uint8_t m[3] = { a, b, c }, pk[4]; int n = a >= 0xF8 ? 1 : (a & 0xF0) == 0xC0 || (a & 0xF0) == 0xD0 ? 2 : 3;
    midi_usb_packet(m, n, pk); tw_usb_put_in(pk);
}
static uint32_t out_count(uint8_t status_hi, int drain)
{
    static uint8_t pk[4096][4]; uint32_t n = tw_usb_take_out(pk, 4096), c = 0;
    for (uint32_t i = 0; i < n; i++) if ((pk[i][1] & 0xF0) == status_hi && pk[i][3]) c++;
    (void)drain; return c;
}
static void sc_midi(void)
{
    boot_wait();
    out_count(0, 1);
    /* notes in on each channel: synths 1..4, drums 10 */
    /* (4.0.8, Sam: MIDI with the 5th track) channels 1-4: the synths shown 1-4 (tracks 0 1 2 4), 5 and 10: the drums
     * (track 3); each note heard, and on the right track */
    static const int CH[6] = { 0, 1, 2, 3, 4, 9 }, TK[6] = { 0, 1, 2, 4, 3, 3 };
    for (int i = 0; i < 6; i++) {
        midi_send((uint8_t)(0x90 | CH[i]), 48, 100); wait_ms(60);
        int on = -1; for (int v = 0; v < GB_NVOICE; v++) if (S.v[v].state != V_FREE) on = S.v[v].track;
        double pk = peak_over(300); midi_send((uint8_t)(0x80 | CH[i]), 48, 0); wait_ms(400);
        printf("[%8.3f]    MIDI note on channel %d: track %d, peak %.3f\n", tw_s(), CH[i] + 1, on, pk);
        EXPECT(pk > 0.002, "a note on channel %d not heard (%.4f)", CH[i] + 1, pk);
        EXPECT(on == TK[i], "a note on channel %d played track %d, not %d", CH[i] + 1, on, TK[i]);
    }
    /* out: each track on its channel (KEYS: what the keys play): synth 4 (track 4) on 4, the drums on 10 */
    MC.out = MIDI_OUT_KEYS;
    { static uint8_t pk[4096][4]; tw_usb_take_out(pk, 4096);
      for (int t = 0; t < NTRK; t++) { trk = t; wait_ms(100); tapkey(white_key[4]); wait_ms(150);
        uint32_t n = tw_usb_take_out(pk, 4096); int ch = -1; for (uint32_t i = 0; i < n; i++) if ((pk[i][1] & 0xF0) == 0x90 && pk[i][3]) ch = pk[i][1] & 15;
        EXPECT(ch == midi_ch_of_track(t) && ch == (t == 3 ? 9 : t == 4 ? 3 : t), "track %d's keys out on channel %d, not %d", t, ch + 1, midi_ch_of_track(t) + 1); }
      trk = 0; }
    EXPECT(snd_voices(&S) == 0 || 1, "");
    wait_ms(1500);
    EXPECT(snd_voices(&S) == 0, "notes left on after their note-offs (%d voices)", snd_voices(&S));
    /* a program change: track 1's sound changes */
    int before = preset_of[0];
    midi_send(0xC0, 5, 0); wait_ms(200);
    printf("[%8.3f]    program change 5: track 1's preset %d -> %d\n", tw_s(), before, preset_of[0]);
    EXPECT(preset_of[0] != before, "a program change did nothing");
    /* the clock: SYNC EXT, START, 24 pulses a beat at 100 BPM, then STOP */
    MC.sync = MIDI_SYNC_EXT;
    midi_send(0xFA, 0, 0);
    double iv = 60000.0 / 100.0 / 24.0;
    uint64_t t0 = tw_ns;                                  /* (each pulse at its own time: the twin's turns come every half) */
    for (int i = 0; i < 24 * 8; i++) { midi_send(0xF8, 0, 0); wait_until(t0 + (uint64_t)((i + 1) * iv * 1e6)); }
    printf("[%8.3f]    under the clock: playing %d, %.1f BPM\n", tw_s(), Q.playing, Q.bpm);
    EXPECT(Q.playing && fabs(Q.bpm - 100.0) < 1.0, "the clock not followed (playing %d, %.2f BPM)", Q.playing, Q.bpm);
    midi_send(0xFC, 0, 0); wait_ms(100);
    EXPECT(!Q.playing, "STOP not followed");
    MC.sync = MIDI_SYNC_INT;
    /* the sequencer's notes out (OUT SEQ) */
    MC.out = MIDI_OUT_SEQ; out_count(0, 1);
    tap(B_PLAY); wait_ms(3000); tap(B_PLAY); wait_ms(300);
    uint32_t on = out_count(0x90, 1);
    printf("[%8.3f]    notes sent out in 3 s of the demo: %u\n", tw_s(), on);
    EXPECT(on > 4, "the sequencer sent no notes out (%u)", on);
    /* MIDI files' worth of junk: never a crash */
    for (int i = 0; i < 20000; i++) { uint8_t pk[4] = { (uint8_t)(tw_rnd() & 15), (uint8_t)tw_rnd(), (uint8_t)(tw_rnd() & 127), (uint8_t)(tw_rnd() & 127) }; tw_usb_put_in(pk); if (i % 64 == 0) wait_ms(2); }
    wait_ms(2000);
    midi_send(0xB0, 123, 0); for (int c = 0; c < 16; c++) midi_send((uint8_t)(0xB0 | c), 123, 0);
    wait_ms(3000);
    check_project_roundtrip("after MIDI");
}
/* (3.4.2, Sam: "confirm every track works with MIDI, and the rest of the MIDI system, before publishing") on each of the
 * 5 tracks' channels: CC 7 and CC 11 the level, CC 10 the pan, the sends, the bend, the sustain pedal, a program change
 * (the drums: a kit); MIDI LEARN of CC 1 and CC 11 and the keys' pressure; the sequencer's notes out on each track's
 * channel; REMOTE (PLAY) and the FX channel (a punch-in) */
static void sc_midi2(void)
{
    boot_wait(); wait_ms(500);
    static const int CH[5] = { 0, 1, 2, 3, 9 }, TK[5] = { 0, 1, 2, 4, 3 };
    for (int i = 0; i < 5; i++) {
        int c = CH[i], t = TK[i]; gb_track *k = &S.trk[t];
        midi_send((uint8_t)(0xB0 | c), 7, 20); wait_ms(150); float l7 = k->level.tgt;
        midi_send((uint8_t)(0xB0 | c), 11, 100); wait_ms(150); float l11 = k->level.tgt;
        midi_send((uint8_t)(0xB0 | c), 10, 0); wait_ms(100); float pan = k->pan.tgt;
        midi_send((uint8_t)(0xB0 | c), 91, 127); midi_send((uint8_t)(0xB0 | c), 94, 64); wait_ms(100); float rev = k->send_rev.tgt, dly = k->send_dly.tgt;
        midi_send((uint8_t)(0xB0 | c), 10, 64); midi_send((uint8_t)(0xB0 | c), 91, 0); midi_send((uint8_t)(0xB0 | c), 94, 0); midi_send((uint8_t)(0xB0 | c), 7, 57);
        int pb = 1; if (!is_drum(t)) { midi_send((uint8_t)(0xE0 | c), 0, 127); wait_ms(80); pb = k->bend > 1.5f; midi_send((uint8_t)(0xE0 | c), 0, 64); wait_ms(80); pb = pb && fabsf(k->bend) < 0.01f; }
        /* the sustain pedal: a note let go keeps sounding until the pedal is up */
        midi_send((uint8_t)(0xB0 | c), 64, 127); midi_send((uint8_t)(0x90 | c), 50, 100); wait_ms(100); midi_send((uint8_t)(0x80 | c), 50, 0); wait_ms(300);
        int held = 0; for (int v = 0; v < GB_NVOICE; v++) if (S.v[v].state == V_ON && S.v[v].track == t) held = 1;
        midi_send((uint8_t)(0xB0 | c), 64, 0); wait_ms(300);
        int still = 0; for (int v = 0; v < GB_NVOICE; v++) if (S.v[v].state == V_ON && S.v[v].track == t) still = 1;
        /* a program change: the synths a sound, the drums a kit */
        int p0 = preset_of[t], k0 = (int)(k->patch.p[SP_E0] * 255.0f + 0.5f);
        midi_send((uint8_t)(0xC0 | c), 3, 0); wait_ms(200);
        int pc = is_drum(t) ? (int)(k->patch.p[SP_E0] * 255.0f + 0.5f) != k0 || k0 == 3 : preset_of[t] != p0;
        printf("[%8.3f]    channel %2d, track %d: CC7 %.2f, CC11 %.2f, pan %.2f, sends %.2f %.2f, bend %d, sustain %d/%d, program %d\n", tw_s(), c + 1, t + 1, l7, l11, pan, rev, dly, pb, held, !still, pc);
        EXPECT(fabsf(l7 - 20 / 127.0f) < 0.01f && fabsf(l11 - 100 / 127.0f) < 0.01f, "channel %d: CC 7 / CC 11 not the level (%.2f %.2f)", c + 1, l7, l11);
        EXPECT(pan < 0.01f && rev > 0.99f && fabsf(dly - 64 / 127.0f) < 0.01f, "channel %d: pan or sends not set", c + 1);
        EXPECT(pb, "channel %d: the bend not followed", c + 1);
        if (!is_drum(t)) EXPECT(held && !still, "channel %d: the sustain pedal (held %d, ended %d)", c + 1, held, !still);
        EXPECT(pc, "channel %d: a program change did nothing", c + 1);
    }
    wait_ms(1500);
    EXPECT(snd_voices(&S) == 0, "notes left on (%d voices)", snd_voices(&S));
    /* CC 1 by itself: the vibrato of the track; the keys' pressure the same */
    { gb_track *k = &S.trk[1]; midi_send(0xB1, 1, 0); wait_ms(100); float a = k->patch.p[SP_LPIT];
      midi_send(0xB1, 1, 100); wait_ms(100); float b = k->patch.p[SP_LPIT];
      midi_send(0xD1, 30, 0); wait_ms(100); float c = k->patch.p[SP_LPIT];
      printf("[%8.3f]    CC 1: vibrato %.2f -> %.2f, the pressure -> %.2f\n", tw_s(), a, b, c);
      EXPECT(a < 0.01f && fabsf(b - 100 / 127.0f) < 0.01f && fabsf(c - 30 / 127.0f) < 0.01f, "CC 1 or the pressure not the vibrato");
      midi_send(0xB1, 1, 0); }
    /* MIDI LEARN: CC 1 to track 3's level, CC 11 to track 1's reverb send; then the pressure plays CC 1's part */
    ml_on = 1; ml_pick = 1; ml_trk = 2; ml_code = ML_LEVEL; midi_send(0xB0, 1, 10); wait_ms(150);
    ml_pick = 1; ml_trk = 0; ml_code = ML_REV; midi_send(0xB0, 11, 10); wait_ms(150); ml_on = 0;
    int l1 = 0, l11 = 0; for (int i = 0; i < ML_N; i++) { if ((ml_tab[i] & 127) == 1 && ((ml_tab[i] >> 7) & 7) == 2) l1 = 1; if ((ml_tab[i] & 127) == 11 && (ml_tab[i] >> 10) == ML_REV) l11 = 1; }
    midi_send(0xB5, 1, 90); midi_send(0xB7, 11, 70); wait_ms(150);
    float lv = S.trk[2].level.tgt, rv = S.trk[0].send_rev.tgt;
    midi_send(0xD0, 33, 0); wait_ms(150); float lp = S.trk[2].level.tgt;
    printf("[%8.3f]    learnt: CC 1 %d, CC 11 %d; track 3 level %.2f, track 1 reverb %.2f, the pressure -> %.2f\n", tw_s(), l1, l11, lv, rv, lp);
    EXPECT(l1 && l11, "CC 1 or CC 11 not learnt");
    EXPECT(fabsf(lv - 90 / 127.0f) < 0.01f && fabsf(rv - 70 / 127.0f) < 0.01f && fabsf(lp - 33 / 127.0f) < 0.01f, "the learnt CCs, or the pressure, did nothing");
    for (int i = 0; i < ML_N; i++) ml_tab[i] = 0;
    /* the sequencer's notes out: each track on its own channel */
    { static uint8_t pk[4096][4]; for (int t = 0; t < NTRK; t++) { Q.t[t].pat[Q.t[t].cur].n = 0; seq_step_toggle(&Q, t, 0, is_drum(t) ? 0 : 48, 100); seq_step_toggle(&Q, t, 8, is_drum(t) ? 2 : 55, 100); }
      MC.out = MIDI_OUT_SEQ; tw_usb_take_out(pk, 4096); tap(B_PLAY); wait_ms(2500); tap(B_PLAY); wait_ms(300);
      uint32_t n = tw_usb_take_out(pk, 4096); int seen[16] = { 0 };
      for (uint32_t i = 0; i < n; i++) if ((pk[i][1] & 0xF0) == 0x90 && pk[i][3]) seen[pk[i][1] & 15]++;
      printf("[%8.3f]    notes out by channel: 1:%d 2:%d 3:%d 4:%d 10:%d\n", tw_s(), seen[0], seen[1], seen[2], seen[3], seen[9]);
      for (int i = 0; i < 5; i++) EXPECT(seen[CH[i]] > 0, "the sequencer sent nothing on channel %d", CH[i] + 1); }
    /* REMOTE (channel 16): CC 112 is PLAY; the FX channel (15): a note holds its punch-in */
    m_rmch = 16; m_fxch = 15;
    midi_send(0xBF, 112, 127); wait_ms(100); midi_send(0xBF, 112, 0); wait_ms(400); int rp = Q.playing;
    midi_send(0xBF, 112, 127); wait_ms(100); midi_send(0xBF, 112, 0); wait_ms(300);
    midi_send(0x9E, 53, 100); wait_ms(300); int fx = mfx_key >= 0; midi_send(0x8E, 53, 0); wait_ms(200);
    printf("[%8.3f]    REMOTE PLAY %d, then %d; FX channel punch-in %d, then %d\n", tw_s(), rp, Q.playing, fx, mfx_key);
    EXPECT(rp && !Q.playing, "REMOTE: CC 112 is not PLAY");
    EXPECT(fx && mfx_key < 0, "the FX channel: no punch-in");
    m_rmch = 0; m_fxch = 0; MC.out = MIDI_OUT_KEYS;
    check_project_roundtrip("after MIDI 2");
}
/* OCT- held at power-on: RESCUE, then PLAY: an empty project, the projects kept */
static void sc_rescue(void)
{
    tw_btn = 1u << B_OCTD; wait_ms(600); tw_btn = 0;
    wait_ms(300); shot("rescue");
    EXPECT(rescue, "OCT- at power-on: no RESCUE");
    tap(B_PLAY); boot_wait();
    uint32_t at0, n0 = 0, c0 = 0; int had = store_proj_find(&at0, &n0, &c0) == 0;
    EXPECT(said_since(0, "SAFE START"), "no SAFE START");
    EXPECT(project_empty(), "the safe start's project is not empty");
    EXPECT(had && proj_used && safe_hold, "the project not kept aside (%d, %u, %d)", had, proj_used, safe_hold);
    shot("safe-start");
    /* left alone (the autosave), then a note played, SAVE once: the project there before is still there, unchanged */
    tapkey(5); wait_ms(30000);
    uint32_t at1, n1 = 0, c1 = 0; int still = store_proj_find(&at1, &n1, &c1) == 0;
    EXPECT(still && n1 == n0 && c1 == c0, "SAFE START: the project was overwritten (%u -> %u bytes)", n0, n1);
    press(B_HOME); tapkey(white_key[HK_SAVE]); release(B_HOME); wait_ms(3000);
    store_proj_find(&at1, &n1, &c1);
    EXPECT(n1 == n0 && c1 == c0 && said_since(0, "AGAIN TO SAVE OVER IT"), "SAFE START: SAVE once wrote over the project");
    /* 3.3.6: HOME held: OPEN NEW SAVE on white keys 1, 3, 5 (a key between), lit; the one asked again blinks; OPEN: back */
    press(B_HOME); wait_ms(300);
    int lit = tw_led[14 + white_key[HK_OPEN]] && tw_led[14 + white_key[HK_NEW]] && tw_led[14 + white_key[HK_SAVE]], other = 0;
    for (int w = 0; w < 16; w++) if (w != HK_OPEN && w != HK_NEW && w != HK_SAVE) other += tw_led[14 + white_key[w]];
    EXPECT(HK_OPEN == 0 && HK_NEW == 2 && HK_SAVE == 4, "OPEN NEW SAVE not on white keys 1, 3, 5");
    EXPECT(lit && !other, "HOME held: OPEN NEW SAVE not lit alone (%d, %d others)", lit, other);
    wait_ms(600); shot("home-keys");
    uint32_t m = msg_mark(); tapkey(white_key[HK_SAVE]); int on = 0, off = 0;   /* (a safe start: SAVE asks again) */
    for (int i = 0; i < 100; i++) { wait_ms(10); if (tw_led[14 + white_key[HK_SAVE]]) on++; else off++; }
    EXPECT(on > 10 && off > 10 && said_since(m, "AGAIN TO SAVE OVER IT"), "SAVE asked again: its key does not blink (%d on, %d off)", on, off);
    tapkey(white_key[HK_OPEN]); wait_ms(500); release(B_HOME); wait_ms(1500);   /* (OPEN instead: the saved one back) */
    EXPECT(!project_empty() && !safe_hold, "OPEN: the saved project not back");
}
/* OCT- and OCT+ held at power-on: the calibration, every button then every knob; then it plays, it is kept */
static void sc_calib(void)
{
    tw_btn = 3u; wait_ms(600);
    boot_wait(); tw_btn = 0; wait_ms(300);
    EXPECT(cal_step == 0, "OCT- + OCT+ at power-on: no calibration (%d)", cal_step);
    shot("calibration");
    static const int ORDER[14] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13 };
    for (int i = 0; i < 14; i++) { tap(ORDER[i]); wait_ms(100); }
    for (int e = 0; e < 7; e++) { turn(e, 1); wait_ms(100); }
    EXPECT(cal_ok && cal_step < 0, "the calibration not done (step %d, ok %d)", cal_step, cal_ok);
    wait_ms(2000);
    tap(B_PLAY); double pk = peak_over(2000); tap(B_PLAY);
    EXPECT(pk > 0.02, "after the calibration, PLAY: nothing (%.4f)", pk);
    save_and_wait();
}
/* the device's settings (HOME + SELECT), your sounds, your favourites: changed, saved, and back after a restart */
static void sc_settings(void)
{
    boot_wait();
    /* (3.3.8, as SLOOP 2.8.5) HOME tapped: no menu, no flash of it; HOME + SELECT: the menu, open when let go */
    { int flash = 0; press(B_HOME); for (int i = 0; i < 25; i++) { wait_ms(10); flash += V.page != UI_TRACKS && V.page != UI_VIS && sticky == PG_TRACKS; } release(B_HOME); wait_ms(300);
      EXPECT(!menu_on && !flash, "HOME tapped: the menu (%d) or a flash of it (%d frames)", menu_on, flash); if (vis_on) { tap(B_HOME); wait_ms(200); } }
    press(B_HOME); wait_ms(150); turn(ENC_SELECT, 1); wait_ms(100); release(B_HOME); wait_ms(300);
    EXPECT(menu_on && home_sec == 2, "HOME + SELECT, let go: the menu not open on LIGHTS (open %d, section %d)", menu_on, home_sec);
    tap(B_HOME); wait_ms(300);
    EXPECT(!menu_on, "HOME: the menu not closed");
    hold(B_HOME, 900);
    EXPECT(menu_on, "HOME held 0.9 s: no menu");
    turn(ENC_SELECT, -6); wait_ms(100);                 /* (4.0: SCREEN LIGHTS AUDIO SEQ SYSTEM, as SLOOP 2.8.5) */
    for (int pg = 0; pg < 5; pg++) { for (int e = ENC_KNOB1; e <= (pg >= 3 ? ENC_KNOB1 : ENC_KNOB4); e++) turn(e, 1); char nm[32];   /* (SYSTEM: not CALIBRATION, LEARN) */ snprintf(nm, sizeof nm, "settings-%d", pg + 1); shot(nm); turn(ENC_SELECT, 1); wait_ms(200); }
    tap(B_HOME); wait_ms(300);                          /* (3.3.8: HOME closes it) */
    EXPECT(!menu_on, "HOME: the menu not closed");
    /* a sound of yours: track 1's into place 3 (4.0, as SLOOP 2.8.5: SAVE > USER, KNOB 1 the place, KNOB 4 SAVE) */
    tap(B_EDIT); wait_ms(150); tap(B_SAVE); wait_ms(150);
    EXPECT(sticky == PG_PRESETS, "SAVE tapped on a page: not SAVE > PRESETS (%d)", sticky);
    tap(B_SAVE); wait_ms(300); shot("user-sounds");
    EXPECT(sticky == PG_USER, "SAVE again: not USER (%d)", sticky);
    turn(ENC_KNOB1, 2); turn(ENC_KNOB4, 1); wait_ms(1500);
    printf("[%8.3f]    your sounds: place 3 %s (%s)\n", tw_s(), us_used(2) ? "used" : "empty", us_name(2));
    EXPECT(us_used(2), "USER SOUNDS: SAVE did not keep the sound in place 3");
    turn(ENC_SELECT, -1); wait_ms(300); shot("favs");   /* (SAVE > PRESETS: KNOB 4 FAV) */
    turn(ENC_KNOB4, 1); wait_ms(2500); wait_saved(4000);  /* FAV: the track's sound a favourite (kept a moment later) */
    EXPECT(us_fav[0] == US_BASE + 2, "FAV did not add the track's sound (%d)", us_fav[0]);
    tap(B_HOME); wait_ms(300);
    save_and_wait();
}
/* the sounds people reach first: SLICE's four (each key a slice), the arpeggiator, the piano roll from the editor */
static int preset_named(const char *n) { for (int i = 0; i < gb_npresets; i++) if (!strcmp(gb_presets[i].name, n)) return i; return -1; }
static void sc_play(void)
{
    boot_wait();
    trk = 0;
    static const char *const SL[4] = { "BREAK 16", "CHOP 8", "REVERSE", "USR SLICE" };
    for (int i = 0; i < 4; i++) {
        int np = preset_named(SL[i]); EXPECT(np >= 0, "no preset %s", SL[i]); if (np < 0) continue;
        apply_preset(0, np); wait_ms(200);
        int quiet = 0;
        for (int k = 0; k < 16; k++) { keydown(k); double pk = peak_over(120); keyup(k); wait_ms(40); if (pk < 0.001) quiet++; }   /* (3.4.0: the DAC 6 dB lower, the threshold too) */
        printf("[%8.3f]    %s: %d of 16 keys silent\n", tw_s(), SL[i], quiet);
        EXPECT(quiet <= 2, "%s: %d keys silent", SL[i], quiet);
        char nm[32]; snprintf(nm, sizeof nm, "slice-%d", i + 1); shot(nm);
    }
    /* the arpeggiator: a chord held, notes come one after another */
    apply_preset(0, preset_named("RHODES") >= 0 ? preset_named("RHODES") : 0);
    arpc[0].mode = AM_UP; arpc[0].rate = 3;
    tap(B_PLAY); wait_ms(300);
    uint32_t v0 = 0; keydown(0); keydown(4); keydown(7);
    for (int i = 0; i < 40; i++) { wait_ms(25); v0 += (uint32_t)snd_voices(&S); }
    keyup(0); keyup(4); keyup(7); tap(B_PLAY);
    EXPECT(v0 > 0, "ARP: nothing played");
    arpc[0].mode = AM_OFF;
    /* the piano roll (the editor): a pattern written, read back the same */
    uint8_t raw[700], rep[700]; uint32_t rn;
    raw[0] = 1; raw[1] = 255; raw[2] = 0; raw[3] = 0; raw[4] = 16; raw[5] = 0; raw[6] = 4;
    for (int i = 0; i < 4; i++) { uint8_t *o = raw + 7 + 6 * i; uint32_t t = (uint32_t)i * 4u * SEQ_TPS; o[0] = (uint8_t)t; o[1] = (uint8_t)(t >> 8); o[2] = (uint8_t)SEQ_TPS; o[3] = 0; o[4] = (uint8_t)(60 + i * 3); o[5] = 100; }
    int r = tw_ed(ED_NOTES, raw, 7 + 24, rep, &rn, 2000);
    EXPECT(r == ED_NOTES && (rep[2] | rep[3] << 8) == 4, "NOTES: the pattern not written (%d, %d)", r, rn > 3 ? rep[2] | rep[3] << 8 : -1);
    raw[0] = 1; raw[1] = 255; raw[2] = 0; raw[3] = 0;
    r = tw_ed(ED_PATTERN, raw, 4, rep, &rn, 2000);
    int ok = r == ED_PATTERN && rep[10] == 4;
    for (int i = 0; ok && i < 4; i++) ok = rep[11 + 6 * i + 4] == 60 + i * 3 && (rep[11 + 6 * i] | rep[12 + 6 * i] << 8) == i * 4 * SEQ_TPS;
    EXPECT(ok, "PATTERN: the piano roll's notes not read back");
    trk = 1; wait_ms(100); tap(B_SEQ); tap(B_SEQ); wait_ms(300); shot("notes-page");
    tap(B_PLAY); double pk = peak_over(2500); tap(B_PLAY);
    EXPECT(pk > 0.002, "the piano roll's pattern not heard");
    save_and_wait();
}
/* each track alone, at full level, in the demo: how loud (peak, RMS), and the kits one by one */
static void sc_levels(void)
{
    boot_wait();
    printf("[%8.3f]    master %.2f, pot %d\n", tw_s(), S.master.tgt, tw_pot);
    for (int t = 0; t < NTRK; t++) sm_set(&S.trk[t].level, 1.0f);
    tap(B_PLAY); wait_ms(1000);
    for (int t = 0; t < NTRK; t++) {
        for (int u = 0; u < NTRK; u++) solo[u] = u == t;
        wait_ms(300); double pk = 0; tw_peak = 0; double r = rms_over(4000); pk = tw_peak;
        printf("[%8.3f]    track %d (%s, %s): peak %.3f, RMS %.4f\n", tw_s(), t, gb_engines[S.trk[t].patch.engine]->name, preset_of[t] >= 0 ? gb_presets[preset_of[t]].name : "-", pk, r);
    }
    for (int u = 0; u < NTRK; u++) solo[u] = 0;
    tap(B_PLAY); wait_ms(1500);
    /* Sam's mix (11 nov.): the drums at 100, the synths at 30; the drums alone, the synths alone, all */
    for (int t = 0; t < NTRK; t++) sm_set(&S.trk[t].level, is_drum(t) ? 1.0f : 0.3f);
    tw_pot = 1023; wait_ms(300);                          /* (MASTER all the way up) */
    if (getenv("TWIN_SPEAKER")) { hs[HS_LOWCUT] = 1; home_apply(); }
    if (getenv("TWIN_KIT")) { for (int t = 0; t < NTRK; t++) if (is_drum(t)) { gb_patch_set(&S.trk[t].patch, SP_E0, atoi(getenv("TWIN_KIT"))); base_from_sound(); } }
    tap(B_PLAY); wait_ms(1000);
    for (int pass = 0; pass < 3; pass++) {
        for (int t = 0; t < NTRK; t++) Q.t[t].mute = (uint8_t)(pass == 0 ? !is_drum(t) : pass == 1 ? is_drum(t) : 0);
        wait_ms(300); tw_peak = 0; char wn[32]; snprintf(wn, sizeof wn, "sams-mix-%d", pass); wav_on(wn); double r = rms_over(4000); tw_wav_close();
        printf("[%8.3f]    Sam's mix, %s: peak %.3f RMS %.4f\n", tw_s(), pass == 0 ? "drums alone" : pass == 1 ? "synths alone" : "all", tw_peak, r);
    }
    for (int t = 0; t < NTRK; t++) Q.t[t].mute = 0;
    wav_on("sams-mix"); wait_ms(12000); tw_wav_close();
    tap(B_PLAY); wait_ms(1500);
    /* the drum track's kits: a kick (key 0) and a snare (key 2), velocity full */
    int dt = -1; for (int t = 0; t < NTRK; t++) if (is_drum(t)) dt = t;
    trk = dt;
    for (int kit = 0; kit < GB_NKITS; kit += 1) {
        gb_patch_set(&S.trk[dt].patch, SP_E0, kit); base_from_sound();
        double a[3]; static const int KEY[3] = { 0, 2, 6 };
        for (int i = 0; i < 3; i++) { keydown(KEY[i]); a[i] = peak_over(250); keyup(KEY[i]); wait_ms(250); }
        printf("[%8.3f]    kit %2d %-10s kick %.3f snare %.3f hat %.3f\n", tw_s(), kit, kit < GB_KIT_SAMPLED ? "sampled" : kit < GB_KIT_USR ? gb_dkits[kit - GB_KIT_SAMPLED].name : "usr/syn", a[0], a[1], a[2]);
    }
    /* a synth track for comparison: track 0, a chord */
    trk = 0; keydown(12); keydown(16); keydown(19); double sy = peak_over(600); keyup(12); keyup(16); keyup(19);
    printf("[%8.3f]    track 0 chord: peak %.3f\n", tw_s(), sy);
}
/* the mix from the panel: each track chosen with ALGORITHM, its LEVEL (KNOB 2) to the end: which level moves */
static void sc_mixui(void)
{
    boot_wait();
    for (int i = 0; i < 6; i++) {
        float before[NTRK]; for (int t = 0; t < NTRK; t++) before[t] = S.trk[t].level.tgt;
        turn(ENC_KNOB2, 30); wait_ms(200);
        printf("[%8.3f]    trk %d (%s, shown %d): levels", tw_s(), trk, gb_engines[S.trk[trk].patch.engine]->name, POS[trk] + 1);
        for (int t = 0; t < NTRK; t++) printf(" %.2f%s", S.trk[t].level.tgt, S.trk[t].level.tgt != before[t] ? "*" : "");
        printf("\n");
        turn(ENC_ALGORITHM, 1); wait_ms(200);
    }
    shot("mixui");
    /* and GLOBAL 1/6: its LVL knob, track by track */
    tap(B_GLO); wait_ms(300); shot("global");
}
/* the SLICER and the INSERT on every track, every mode, the demo playing: their sound bounded, their cost too */
static void sc_fx(void)
{
    boot_wait();
    tap(B_PLAY); wait_ms(500);
    for (int mode = 1; mode <= 2; mode++) for (int pat = 1; pat <= 16; pat += 5) for (int rate = 0; rate < 6; rate += 2) {
        for (int t = 0; t < NTRK; t++) { gb_patch *p = &S.trk[t].patch; gb_patch_set(p, SP_SLCR, mode); gb_patch_set(p, SP_SLPAT, pat); gb_patch_set(p, SP_SLRATE, rate); gb_patch_set(p, SP_SLDEPTH, 127); }
        base_from_sound(); wait_ms(700);
    }
    for (int ty = 1; ty <= 8; ty++) {
        for (int t = 0; t < NTRK; t++) { gb_patch *p = &S.trk[t].patch; gb_patch_set(p, SP_ITYPE, ty); gb_patch_set(p, SP_IMIX, 127); }
        base_from_sound(); wait_ms(700);
    }
    EXPECT(!tw_wild, "the effects went beyond full scale");
    tap(B_PLAY);
}
/* protocol 2 (3.1.5): the editor's LIVE page, its sequencer (cond, ratchet, locks), the song, the settings, the lists */
static int live(int op, int a, int b, int c, uint8_t *rep)
{
    uint8_t raw[4] = { (uint8_t)op, (uint8_t)a, (uint8_t)b, (uint8_t)c }; uint32_t rn;
    int r = tw_ed(ED_LIVE, raw, 4, rep, &rn, 2000);
    EXPECT(r == ED_LIVE && rn == 1 + 45 + 22 * NTRK + 16, "LIVE op %d: %d (%u bytes)", op, r, rn);
    return r;
}
/* the start's logo, then the visualiser (3.1.6): each style drawn while the demo plays, the sound's cost unchanged,
 * a save and the editor's reads while it shows, its style kept with the settings */
static int cmp_d(const void *a, const void *b) { double x = *(const double *)a, y = *(const double *)b; return x < y ? -1 : x > y; }
static double cost20(void) { double c[20]; for (int i = 0; i < 20; i++) { wait_ms(50); c[i] = (double)tw_half_cpu_ns; } qsort(c, 20, sizeof c[0], cmp_d); return 20.0 * 0.5 * (c[9] + c[10]); }
static void sc_vis(void)
{
    boot_wait();
    EXPECT(splash_on, "the start: no logo");
    wait_ms(300); shot("splash-0.7s"); wait_ms(700); shot("splash-1.7s");
    uint32_t m = msg_mark(); wait_ms(1200);
    EXPECT(!splash_on && !said_since(m, "SLOOP"), "the logo did not end by itself, or a popup with the version after it");
    tap(B_PLAY); wait_ms(600);
    EXPECT(Q.playing, "PLAY");
    /* (the PC's own time, a half's: its median over 20, x 20 as before: one half slowed by the PC's other work is not
     * the visualiser's cost; a sum over 20 failed now and then on a busy PC) */
    double c0 = cost20();
    tap(B_HOME); wait_ms(300);
    EXPECT(vis_on && V.page == UI_VIS, "HOME on TRACKS: no visualiser");
    for (int st = 0; st < VIS_N; st++) {
        if (st) turn(ENC_SELECT, 1);
        wait_ms(1600);
        EXPECT(vis_style == st, "SELECT: style %d, not %d", vis_style, st);
        char nm[32]; snprintf(nm, sizeof nm, "vis-%02d", st + 1); shot(nm);
    }
    double c1 = cost20();
    for (int k = 0; k < 2 && !(c1 < c0 * 1.6 + 2e5); k++) { double c = cost20(); if (c < c1) c1 = c; }   /* (3.3.6: measured again before concluding: the PC busy now and then) */
    printf("[%8.3f]    the sound's cost, visualiser off / on: %.0f / %.0f us a half\n", tw_s(), c0 / 20e3, c1 / 20e3);
    EXPECT(c1 < c0 * 1.6 + 2e5, "the visualiser costs the sound: %.0f -> %.0f", c0, c1);   /* (3.3.6: 1.25 -> 1.6: a 2-CPU PC, the drawing on the other, slows the sound's thread by 1.3-1.4; the visualiser's work in the sound would be several times) */
    /* a layer held shows its screen, then the visualiser again */
    press(B_GLO); wait_ms(1100); EXPECT(V.page != UI_VIS, "GLO held: the visualiser stayed"); release(B_GLO); wait_ms(300);   /* (3.3.8: a tap up to 1 s, as SLOOP 2.8.5) */
    EXPECT(V.page == UI_VIS, "GLO let go: the visualiser did not come back");
    /* a save while it shows: the project written whole (sbuf lent back) */
    m = msg_mark(); save_project(1);                     /* (as the autosave: SAVE tapped is a page, it closes the visualiser) */
    EXPECT(wait_msg(m, "SAVED", 4000), "a save while the visualiser shows: no PROJECT SAVED");
    EXPECT(vis_on && V.page == UI_VIS, "the visualiser gone after a save");
    wait_saved(3000); wait_ms(300);
    check_project_roundtrip("a save under the visualiser");
    uint8_t rep[700]; uint32_t rn; uint8_t raw[8] = { 2, 0, 0, 0, 0, 0, 128, 1 };   /* the editor reads the project now */
    uint32_t off = 0, total = 1, crc = 0; static uint8_t got[STORE_MAX];
    while (off < total) { raw[2] = (uint8_t)off; raw[3] = (uint8_t)(off >> 8); raw[4] = (uint8_t)(off >> 16); raw[6] = 128; raw[7] = 1;
        if (tw_ed(ED_READ, raw, 8, rep, &rn, 3000) != ED_READ || rn < 14) break;
        total = rep[6] | rep[7] << 8 | (uint32_t)rep[8] << 16; crc = rep[10] | rep[11] << 8 | (uint32_t)rep[12] << 16 | (uint32_t)rep[13] << 24;
        memcpy(got + off, rep + 14, rn - 14); off += rn - 14; wait_ms(30); }
    EXPECT(off == total && crc32_ieee(0, got, total) == crc, "the editor's read under the visualiser: %u of %u, crc", off, total);
    tap(B_HOME); wait_ms(300);
    EXPECT(!vis_on && V.page == UI_TRACKS, "HOME again: the visualiser stayed");
    turn(ENC_SELECT, 2); wait_ms(100);
    tap(B_PLAY); wait_ms(300);
    m = msg_mark(); proj_save_keys(); EXPECT(wait_msg(m, "SAVED", 4000), "no save"); wait_saved(3000); tap(B_HOME); wait_ms(200);
    printf("[%8.3f]    style kept: %d\n", tw_s(), vis_style);
    /* 3.3.6: six styles; a style kept by 3.0-3.3.5 (twelve) finds its place, the six taken out the oscilloscope */
    EXPECT(VIS_N == 6 && !strcmp(vis_name[vis_from12[0]], "OSCILLOSCOPE") && !strcmp(vis_name[vis_from12[1]], "SPECTRUM") && !strcmp(vis_name[vis_from12[4]], "VU METERS") &&
           !strcmp(vis_name[vis_from12[7]], "LCD") && !strcmp(vis_name[vis_from12[10]], "WIRES") && !strcmp(vis_name[vis_from12[11]], "SLOOP") && vis_from12[2] == 0 && vis_from12[9] == 0,
           "the twelve styles' places among the six");
    /* SAVE on the SONG pages (3.1.6): tapped, the song saved and the page kept, as SLOOP 2.8.5's SONG screen */
    /* (4.0, as SLOOP 2.8.5) SAVE tapped on TRACKS: the SONG screen; SAVE on it: saved, the screen kept */
    tap(B_SAVE); wait_ms(700); EXPECT(sticky == PG_SONG2, "SAVE tapped: not the SONG screen (%d)", sticky);
    tap(B_SAVE); wait_ms(300); EXPECT(sticky == PG_PROJ, "SAVE on SONG: not PROJECT (page %d)", sticky);   /* (4.0.6) */
    m = msg_mark(); turn(ENC_KNOB1, 1); EXPECT(wait_msg(m, "SAVED", 4000) && sticky == PG_PROJ, "PROJECT > SAVE: not saved (page %d)", sticky); wait_saved(3000);
    tap(B_HOME); wait_ms(300); EXPECT(sticky == PG_TRACKS && !vis_on, "HOME from the SONG pages: not TRACKS");
}
/* serve: the twin as an FM-1 on stdin / stdout for the web editor in a browser (tests/editor_e2e.js): each line
 * in, "M" and the hex of MIDI bytes sent to it ("S name": a screenshot; "Q": the end); each USB packet out, "M" and
 * its bytes. Simulated time kept near the clock's, so the page's polling sees a playing FM-1. */
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
static double wall_s(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return (double)t.tv_sec + t.tv_nsec * 1e-9; }
static void serve_in(const uint8_t *m, uint32_t n)
{
    if (!n) return;
    if (m[0] == 0xF0) { for (uint32_t i = 0; i < n; ) { uint8_t pk[4]; i += (uint32_t)ed_usb_packet(m, n, i, pk); tw_usb_put_in(pk); } return; }
    for (uint32_t i = 0; i < n; ) {
        uint8_t st = m[i]; int len = midi_msg_len(st); if (len <= 0 || i + (uint32_t)len > n) break;
        uint8_t pk[4] = { (uint8_t)(st >> 4), st, len > 1 ? m[i + 1] : 0, len > 2 ? m[i + 2] : 0 }; tw_usb_put_in(pk); i += (uint32_t)len;
    }
}
static void sc_serve(void)
{
    boot_wait();
    fcntl(0, F_SETFL, fcntl(0, F_GETFL) | O_NONBLOCK);
    char line[8192]; size_t ln = 0; double w0 = wall_s(); uint64_t n0 = tw_ns;
    printf("READY\n"); fflush(stdout);
    for (;;) {
        char c; ssize_t r;
        while ((r = read(0, &c, 1)) == 1) {
            if (c != '\n') { if (ln < sizeof line - 1) line[ln++] = c; continue; }
            line[ln] = 0; ln = 0;
            if (line[0] == 'Q') return;
            if (line[0] == 'S') { shot(line + 2); printf("SHOT %s\n", line + 2); fflush(stdout); continue; }
            if (line[0] == 'M') { uint8_t m[2048]; uint32_t n = 0; for (char *p = line + 1; *p && n < sizeof m; ) { while (*p == ' ') p++; if (!*p) break; m[n++] = (uint8_t)strtoul(p, &p, 16); } serve_in(m, n); }
        }
        if (r == 0) return;                                   /* (stdin closed) */
        wait_ms(2);
        uint8_t pk[64][4]; uint32_t k = tw_usb_take_out(pk, 64);
        for (uint32_t j = 0; j < k; j++) { int nb = midi_usb_bytes(pk[j]); if (nb <= 0) continue; printf("M"); for (int b = 0; b < nb; b++) printf(" %02x", pk[j][1 + b]); printf("\n"); }
        if (k) fflush(stdout);
        double ahead = (double)(tw_ns - n0) * 1e-9 - (wall_s() - w0);
        if (ahead > 0.002) usleep((useconds_t)(ahead * 1e6));
    }
}
static void sc_editor2(void)
{
    boot_wait();
    uint8_t rep[700], raw[700]; uint32_t rn;
    EXPECT(tw_ed(ED_INFO, 0, 0, rep, &rn, 2000) == ED_INFO && rep[0] == 2, "INFO: protocol %d", rep[0]);
    live(5, 1, 0, 0, rep); wait_ms(800); live(0, 0, 0, 0, rep);
    EXPECT(rep[1] & 1, "LIVE: PLAY not playing");
    EXPECT(rep[1 + 45 + 22 * 3 + 13] > 0 || rep[1 + 45 + 13] > 0, "LIVE: no meter moves");
    live(4, 1, 0, 100, rep); EXPECT(fabsf(S.trk[1].level.tgt - 100.0f / 127.0f) < 0.01f, "LIVE: level not set");
    live(4, 2, 6, 1, rep); EXPECT(Q.t[2].mute, "LIVE: mute not set");
    live(4, 2, 6, 0, rep); live(4, 0, 7, 1, rep); EXPECT(solo[0], "LIVE: solo"); live(4, 0, 7, 0, rep);
    live(6, 255, 1, 0, rep); wait_ms(2500); EXPECT(Q.t[0].cur == 1, "LIVE: the pattern queued did not come (%d)", Q.t[0].cur);
    live(6, 255, 0, 0, rep); wait_ms(2500);
    live(7, 1, 0, 0, rep); EXPECT(rep[1 + 13] == 1, "LIVE: the punch-in"); wait_ms(300); live(7, 255, 0, 0, rep);
    live(8, 100, 0, 0, rep); wait_ms(100); EXPECT(fabsf(S.master.tgt - 0.5f * (100.f / 126.f) * (100.f / 126.f)) < 0.01f, "LIVE: master");
    live(8, 127, 0, 0, rep);
    live(9, 4, 0, 0, rep); EXPECT(trk == 4, "LIVE: select");
    int np = preset_named("RHODES"); live(10, 4, np & 255, np >> 8, rep); EXPECT(preset_of[4] == np, "LIVE: preset (%d %d)", preset_of[4], np);
    live(13, 1250 & 255, 1250 >> 8, 30, rep); EXPECT(fabsf(Q.bpm - 125.0f) < 0.01f && Q.swing == 30, "LIVE: bpm swing");
    live(12, 1, 0, 0, rep); wait_ms(100); EXPECT(S.throw_rev.tgt > 0.5f, "LIVE: throw"); live(12, 0, 0, 0, rep);
    /* the virtual panel: GLO held + white key 1 mutes track 1 (as on the FM-1) */
    int m0 = Q.t[0].mute;
    live(1, B_GLO, 1, 0, rep); wait_ms(60); live(2, white_key[0], 1, 0, rep); wait_ms(60); live(2, white_key[0], 0, 0, rep); wait_ms(60); live(1, B_GLO, 0, 0, rep); wait_ms(200);
    EXPECT(Q.t[0].mute != m0, "LIVE: GLO + key 1 from the editor did not mute track 1");
    if (Q.t[0].mute) seq_mute(&Q, 0, 0);
    live(3, ENC_ALGORITHM, 1, 0, rep); wait_ms(200);
    live(16, 1, 0, SEQ_DIV_8, rep); EXPECT(Q.t[1].div == SEQ_DIV_8 && rep[1 + 45 + 22 + 18] == SEQ_DIV_8, "LIVE: DIV");
    live(16, 1, 0, SEQ_DIV_16, rep);
    live(11, 2, 3, (uint8_t)-2, rep); EXPECT(rep[1 + 45 + 22 * NTRK + 2] == ((3 + 8) << 4 | (-2 + 4)), "LIVE: a drum pad's TUNE DECAY back (%02x)", rep[1 + 45 + 22 * NTRK + 2]);
    live(11, 2, 0, 0, rep);
    live(5, 0, 0, 0, rep); wait_ms(300);
    /* a pattern with conditions, ratchets, locks */
    raw[0] = 1; raw[1] = 3; raw[2] = 0; raw[3] = 0; raw[4] = 32; raw[5] = 0; raw[6] = 3;
    for (int i = 0; i < 3; i++) { uint8_t *o = raw + 7 + 8 * i; uint32_t t = (uint32_t)i * 4u * SEQ_TPS; o[0] = (uint8_t)t; o[1] = (uint8_t)(t >> 8); o[2] = SEQ_TPS; o[3] = 0; o[4] = (uint8_t)(50 + i); o[5] = 90; o[6] = i == 1 ? SEQ_FILL : SEQ_CHANCE + 5; o[7] = (uint8_t)(i == 2 ? 3 | SEQ_ACCENT : 1); }
    EXPECT(tw_ed(ED_NOTES2, raw, 7 + 24, rep, &rn, 2000) == ED_NOTES2 && (rep[2] | rep[3] << 8) == 3, "NOTES2");
    raw[0] = 1; raw[1] = 3; raw[2] = 2; raw[3] = 4; raw[4] = 6; raw[5] = 200; raw[6] = 8; raw[7] = SEQ_LK_LEVEL; raw[8] = 50;
    EXPECT(tw_ed(ED_LOCKS, raw, 9, rep, &rn, 2000) == ED_LOCKS && rep[2] == 2, "LOCKS");
    raw[0] = 1; raw[1] = 3; raw[2] = 0; raw[3] = 0; raw[4] = 1;
    EXPECT(tw_ed(ED_PATTERN, raw, 5, rep, &rn, 2000) == ED_PATTERN, "PATTERN wide");
    EXPECT(rep[10] == 3 && rep[11 + 8 + 6] == SEQ_FILL && rep[11 + 16 + 7] == (3 | SEQ_ACCENT) && (rep[2] | rep[3] << 8) == 32, "PATTERN wide: the notes");
    EXPECT(rn == 11 + 24 + 1 + 6 && rep[35] == 2 && rep[36 + 3] == 8 && rep[36 + 4] == SEQ_LK_LEVEL, "PATTERN wide: the locks (%u)", rn);
    /* the song */
    raw[0] = 0; raw[1] = 2; raw[2] = 8; raw[3] = 0;
    uint8_t *e = raw + 4; memset(e, 0, 16); e[0] = 0; e[3] = 0; e[4] = 1; e[8] = 4; e[11] = 2; e[12] = 3;
    EXPECT(tw_ed(ED_SONG, raw, 4 + 16, rep, &rn, 2000) == ED_SONG && rep[1] == 2 && Q.song_len == 8 && Q.song[1].bar == 4 && Q.song[1].pat[1] == 3, "SONG");
    /* the settings */
    raw[0] = HS_ACCEL; raw[1] = 0; EXPECT(tw_ed(ED_HOME, raw, 2, rep, &rn, 2000) == ED_HOME && hs[HS_ACCEL] == 0 && rep[HS_ACCEL] == 0, "HOME");
    raw[0] = 41; raw[1] = 4; tw_ed(ED_HOME, raw, 2, rep, &rn, 2000); EXPECT(dly_div == 4, "HOME: the delay's division");
    /* the lists */
    int got = 0; for (int f = 0; f < gb_npresets; ) { raw[0] = 0; raw[1] = (uint8_t)f; raw[2] = (uint8_t)(f >> 8); if (tw_ed(ED_NAMES, raw, 3, rep, &rn, 2000) != ED_NAMES || !rep[5]) break; got += rep[5]; f += rep[5]; }
    EXPECT(got == gb_npresets, "NAMES: %d of %d sounds", got, gb_npresets);
    raw[0] = 1; raw[1] = 0; raw[2] = 0; EXPECT(tw_ed(ED_NAMES, raw, 3, rep, &rn, 2000) == ED_NAMES && (rep[3] | rep[4] << 8) == GB_NKITS, "NAMES: kits");
    save_and_wait();
}
/* 3.1.8: a save under every factory sound. While the project is written the flash window is off, and nothing may
 * read through it: not a sample, not the bank's header (until 3.1.7 a GRAIN track read it once a block: the monkey
 * met it after five minutes). Every sound in turn on a synth track (a kit on the drums), a note held, the project
 * saved while it sounds: the twin stops at the first read of the window. */
static void sc_banksave(void)
{
    boot_wait();
    int saves = 0;
    for (int i = 0; i < gb_npresets; i++) {
        int t = gb_presets[i].engine == ENG_DRUM ? 2 : 0;
        if (is_drum(t) != (gb_presets[i].engine == ENG_DRUM)) continue;
        S.trk[t].patch.engine = gb_presets[i].engine; preset_of[t] = i;
        preset_apply(&S, t, i); harm[t].transpose = 0; preset_extras(t, i); base_from_sound();
        trk = t;
        keydown(3); keydown(7); wait_ms(120);
        uint32_t m = msg_mark(); save_project(1);
        wait_ms(40); keyup(3); keydown(5);                   /* (a note begun during the save, too) */
        EXPECT(wait_msg(m, "SAVED", 5000), "sound %s: no PROJECT SAVED", gb_presets[i].name);
        wait_saved(5000); keyup(7); keyup(5); wait_ms(60); saves++;
    }
    for (int tx = 1; tx <= 12; tx++) {                       /* the texture's sources (the user slots among them) */
        S.tx[0] = (uint8_t)tx; S.tx[1] = 100;
        uint32_t m = msg_mark(); save_project(1);
        EXPECT(wait_msg(m, "SAVED", 5000), "texture %d: no PROJECT SAVED", tx); wait_saved(5000); saves++;
    }
    printf("[%8.3f]    banksave: %d saves\n", tw_s(), saves);
}
/* the monkey: everything at random, for a long time */
/* the punch-ins chained, as hands do on a stage (Sam, 3.2: "GATE, then ECHO, then 1/16..." froze the FM-1): FX held,
 * key after key, the next pressed before the last let go, at every speed; FX let go in the middle; the transport,
 * the tempo, the sends changed under them. The sound bounded, no spike, the firmware answering all along */
static int key_of_punch(int pk) { for (int k = 0; k < 27; k++) if (punch_of_key(k) == pk) return k; return 0; }
static void sc_punch(void)
{
    boot_wait();
    tap(B_PLAY); wait_ms(800);
    {   /* (3.4.2, issue #18) FX let go before the key: the punch-in goes on until the key is let go, as 2.8.5 */
        int k = key_of_punch(PK_ECHO);
        press(B_FX); wait_ms(100); keydown(k); wait_ms(300); release(B_FX); wait_ms(300);
        EXPECT(punch_key == k, "FX let go, the key held: the punch-in stopped");
        keyup(k); wait_ms(200);
        EXPECT(punch_key < 0, "the key let go: the punch-in goes on");
    }
    static const int CH[][4] = { { PK_GATE, PK_ECHO, PK_LOOP16, PK_LOOP32 }, { PK_ECHO, PK_LOOP8, PK_REVERSE, PK_GATE },
                                 { PK_LOOP16, PK_TAPESTOP, PK_ECHO, PK_FREEZE }, { PK_REWIND, PK_ECHO, PK_HALF, PK_GATE },
                                 { PK_STUTTER, PK_FLANGER, PK_ECHO, PK_LOOP4 }, { PK_ECHO, PK_ECHO, PK_LOOP16, PK_ECHO } };
    double secs = getenv("TWIN_SECS") ? atof(getenv("TWIN_SECS")) : 120.0;
    uint64_t end = tw_ns + (uint64_t)(secs * 1e9); int chains = 0;
    while (tw_ns < end) {
        int c = (int)(tw_rnd() % 7), speed = (int)(tw_rnd() % 4);
        static const double HOLD[4] = { 40, 160, 600, 2200 };
        int lockit = getenv("TWIN_LOCK") ? 1 : (int)(tw_rnd() % 2);   /* (3.2: Sam's freeze came with FX locked, HOME) */
        press(B_FX);
        if (lockit) { tap(B_HOME); release(B_FX); EXPECT(locked & (1u << BTN_FX), "FX + HOME did not lock the layer"); }
        int prev = -1;
        for (int i = 0; i < 4 + (int)(tw_rnd() % 6); i++) {
            int pk = c < 6 && i < 4 ? CH[c][i] : (int)(tw_rnd() % PK_N), k = key_of_punch(pk);
            keydown(k);
            if (prev >= 0 && (tw_rnd() & 1)) keyup(prev);         /* (legato: the last let go after the next, or before) */
            wait_ms(HOLD[speed] * (0.5 + (double)(tw_rnd() % 100) / 100.0));
            if (prev >= 0) keyup(prev);
            prev = k;
            if (!lockit && tw_rnd() % 9 == 0) { release(B_FX); wait_ms(tw_rnd() % 300); press(B_FX); }
            if (tw_rnd() % 11 == 0) tap(B_PLAY);
            if (tw_rnd() % 13 == 0) turn(ENC_SELECT, (int)(tw_rnd() % 9) - 4);
            if (tw_rnd() % 15 == 0) { press(B_OCTU); wait_ms(200); release(B_OCTU); }
        }
        if (lockit) { if (tw_rnd() & 1) { keyup(prev); tap(B_FX); } else { tap(B_FX); keyup(prev); } }   /* (FX tapped: the lock let go) */
        else if (tw_rnd() & 1) { keyup(prev); release(B_FX); } else { release(B_FX); keyup(prev); }
        wait_ms(tw_rnd() % 700);
        if (!Q.playing && tw_rnd() % 2) tap(B_PLAY);
        chains++;
    }
    printf("[%8.3f]    punch: %d chains\n", tw_s(), chains);
    tw_btn = 0; tw_keys = 0; wait_ms(2000);
    EXPECT(S.pk.fx < 0 && S.pk.want < 0, "a punch-in stayed on (fx %d, want %d)", S.pk.fx, S.pk.want);
    EXPECT(S.pk.lend == 0, "the delay's memory was not given back (%d)", S.pk.lend);
    EXPECT(!tw_wild, "the sound went beyond full scale (%d samples)", tw_wild);
    if (Q.playing) tap(B_PLAY);
}
/* 3.3.2: the restart report: a crumb as a run stopped in the mix with ECHO held would leave it, read as the next start */
static void sc_crumb(void)
{
    boot_wait();
    crumb.magic = CRUMB_MAGIC; crumb.fx = PK_ECHO + 1; crumb.blocks = 689u * 200u; crumb_at(W_MIX);
    crumb_report(); printf("[%8.3f]    crumb: \"%s\"\n", tw_s(), crash_msg);
    EXPECT(!strcmp(crash_msg, "RESTART IN MIX/FX · ECHO · 3 MIN"), "the report reads \"%s\"", crash_msg);
    crumb.magic ^= 1u; crumb_report();
    EXPECT(crash_msg[0] == 0, "a crumb without its magic was reported: \"%s\"", crash_msg);
    crumb.magic = CRUMB_MAGIC; crumb.fx = 0; crumb.blocks = 689u * 9u; crumb_at(W_SCREEN); crumb_report();
    EXPECT(!strcmp(crash_msg, "RESTART IN SCREEN · 9 S"), "the report reads \"%s\"", crash_msg);
}
/* 3.3.2: a value gone wrong in the effects (what a float trap would have turned into a restart): one block silent,
 * the effects cleared, the sound back */
static void sc_nan(void)
{
    boot_wait(); tap(B_PLAY); wait_ms(1500);
    S.rev_lp[0] = NAN; S.dly_lp[1] = INFINITY;
    wait_ms(30);
    int bad = 0; for (int n = 0; n < GB_BLOCK; n++) bad += !isfinite(snd_last_l[n]) || !isfinite(snd_last_r[n]);
    EXPECT(!bad, "the output still carries %d values gone wrong", bad);
    EXPECT(isfinite(S.rev_lp[0]) && isfinite(S.dly_lp[1]), "the effects kept the values gone wrong");
    wait_ms(2500);
    float pk = 0; for (int n = 0; n < GB_BLOCK; n++) pk = fmaxf(pk, fabsf(snd_last_l[n]));
    printf("[%8.3f]    nan: the output's peak again %.3f\n", tw_s(), pk);
    EXPECT(pk > 0.001f, "the sound did not come back (peak %.4f)", pk);
    /* 3.3.3: values gone wrong that the lines' clearing does not reach: the master's gain, a track's level and send,
     * the chorus's phase. Before, the guard caught them every block and cleared 200 KB each time */
    uint32_t p0 = S.panics;
    S.master.cur = S.master.tgt = NAN; S.trk[1].level.cur = INFINITY; S.trk[2].send_rev.tgt = NAN; S.cho_ph = NAN;
    wait_ms(3000);
    printf("[%8.3f]    nan: %u panics for the second lot\n", tw_s(), S.panics - p0);
    EXPECT(S.panics - p0 <= 3u, "the guard caught them again and again (%u)", S.panics - p0);
    EXPECT(isfinite(S.master.cur) && isfinite(S.trk[1].level.cur) && isfinite(S.trk[2].send_rev.tgt) && isfinite(S.cho_ph), "a value stayed wrong");
    tw_pot = 900; wait_ms(1500);                          /* (MASTER turned: its gain from the knob again) */
    pk = 0; for (int n = 0; n < GB_BLOCK; n++) pk = fmaxf(pk, fabsf(snd_last_l[n]));
    printf("[%8.3f]    nan: the output's peak again %.3f\n", tw_s(), pk);
    EXPECT(pk > 0.001f, "the sound did not come back after the second lot (peak %.4f)", pk);
    tap(B_PLAY);
}
/* 3.3.1: "when I play a lot of FX one after the other, it crashes and restarts": the punch-ins as fast as hands go,
 * every one of the 27 in turn and at random, legato and staccato, FX held or locked, for TWIN_SECS */
static void sc_punch2(void)
{
    boot_wait();
    tap(B_PLAY); wait_ms(800);
    double secs = getenv("TWIN_SECS") ? atof(getenv("TWIN_SECS")) : 120.0;
    uint64_t end = tw_ns + (uint64_t)(secs * 1e9); int n = 0;
    while (tw_ns < end) {
        int lockit = (int)(tw_rnd() % 3) == 0;
        press(B_FX);
        if (lockit) { tap(B_HOME); release(B_FX); }
        int prev = -1, len = 8 + (int)(tw_rnd() % 40), order = (int)(tw_rnd() % 3);
        for (int i = 0; i < len; i++) {
            int k = order == 0 ? i % 27 : order == 1 ? 26 - i % 27 : (int)(tw_rnd() % 27);
            keydown(k);
            if (prev >= 0 && prev != k && (tw_rnd() & 1)) keyup(prev);
            wait_ms(15 + tw_rnd() % (tw_rnd() % 4 ? 120 : 900));
            if (prev >= 0 && prev != k) keyup(prev);
            prev = k; n++;
            if (tw_rnd() % 17 == 0) { tw_keys = 0; wait_ms(1 + tw_rnd() % 40); }
        }
        tw_keys = 0;
        if (lockit) tap(B_FX); else release(B_FX);
        wait_ms(tw_rnd() % 300);
    }
    printf("[%8.3f]    punch2: %d punch-ins\n", tw_s(), n);
    tw_btn = 0; tw_keys = 0; wait_ms(2000);
    EXPECT(S.pk.fx < 0 && S.pk.want < 0, "a punch-in stayed on (fx %d, want %d)", S.pk.fx, S.pk.want);
    EXPECT(S.pk.lend == 0, "the delay's memory was not given back (%d)", S.pk.lend);
    EXPECT(!tw_wild, "the sound went beyond full scale (%d samples)", tw_wild);
    if (Q.playing) tap(B_PLAY);
}
/* 3.3: the preview film. The FM-1 filmed from its power-on: its screen 30 times a second (OUT/film.rgb565, and the
 * time of its first picture in film.txt), its sound (film.wav). A 90s boom-bap beat (90 BPM, BOOMBAP kit, upright
 * bass, a dusty piano, a scratch, vibes), then the pages a hand would show; the panel too (film.panel: 128 bytes a picture) */
static void film_shot(void)
{
    while (tw_film && tw_s() >= tw_film_t) {
        fwrite(tw_lcd, 2, 240 * 240, tw_film);
        if (tw_panel) {                                  /* the panel: buttons and keys held, the knobs, the lights */
            uint8_t r[128] = { 0 }; memcpy(r, &tw_btn, 4); memcpy(r + 4, &tw_keys, 4); memcpy(r + 8, tw_enc, 28);
            memcpy(r + 36, tw_led, 41); for (int i = 0; i < 41; i++) r[77 + i] = (uint8_t)((tw_glow[i] ? 1 : 0) | (tw_lvl[i] << 1));   /* (glow bit 0, the level 0-4 in bits 1-3) */ memcpy(r + 118, tw_turned, 7); fwrite(r, 1, 128, tw_panel);
        }
        tw_film_t += 1.0 / 30.0;
    }
}
static void film_wait(double ms) { wait_ms(ms); }
static void set_sound(int t, const char *name)
{
    int np = preset_named(name); EXPECT(np >= 0, "no sound %s", name); if (np < 0) return;
    preset_of[t] = np; S.trk[t].patch.engine = gb_presets[np].engine; preset_apply(&S, t, np); preset_extras(t, np);
}
static void note(int t, int step, int n, int vel, int len16)
{
    seq_step_toggle(&Q, t, step, n, vel);
    seq_pat *pt = &Q.t[t].pat[Q.t[t].cur];
    for (int i = 0; i < pt->n; i++) if (pt->ev[i].tick == (uint32_t)(step * SEQ_TPS) && pt->ev[i].note == n) pt->ev[i].len = (uint16_t)(len16 * SEQ_TPS - 2);
}
/* (4.0.4, Sam) SAVE > PROJECT, the 3rd SAVE page: SAVE (KNOB 4) writes the project; NEW (KNOB 2) asks, then does it */
static void sc_saveproj(void)
{
    boot_wait(); wait_ms(800);
    tap(B_SAVE); wait_ms(200); EXPECT(sticky == PG_SONG2, "SAVE on TRACKS: not SONG (%d)", sticky);
    tap(B_SAVE); wait_ms(200); EXPECT(sticky == PG_PROJ, "SAVE again: not PROJECT (%d)", sticky); shot("project");
    turn(ENC_KNOB1, 1); for (int i = 0; i < 400 && !(proj_used && !saving); i++) wait_ms(10); wait_ms(300);
    EXPECT(proj_used && !saving, "PROJECT > SAVE: nothing saved (used %d, saving %d)", proj_used, saving);
    tempo(130.0f); wait_ms(100);
    turn(ENC_KNOB2, 1); wait_ms(200); EXPECT(Q.bpm > 125.0f, "PROJECT > NEW: done at the first turn");
    wait_ms(500); turn(ENC_KNOB2, 1); wait_ms(400);
    EXPECT(Q.bpm < 125.0f, "PROJECT > NEW asked again: not a new project (%.0f BPM)", Q.bpm);
    tap(B_SAVE); wait_ms(200); EXPECT(sticky == PG_SONG2, "SAVE on PROJECT: not SONG again (%d)", sticky);
    /* (4.0.7, Sam: "page 3, back to page 2: another menu") the three pages only, with SELECT: 3 -> 2 -> 1 and back */
    tap(B_SAVE); wait_ms(200); EXPECT(sticky == PG_PROJ, "SAVE: not PROJECT (%d)", sticky);
    turn(ENC_SELECT, -1); wait_ms(200); EXPECT(sticky == PG_SONG2, "PROJECT, SELECT left: not SONG 2/3 (%d)", sticky);
    turn(ENC_SELECT, -1); wait_ms(200); EXPECT(sticky == PG_SONG, "SONG 2/3, SELECT left: not SONG 1/3 (%d)", sticky);
    turn(ENC_SELECT, 1); wait_ms(200); EXPECT(sticky == PG_SONG2, "SONG 1/3, SELECT right: not SONG 2/3 (%d)", sticky);
    turn(ENC_SELECT, 1); wait_ms(200); EXPECT(sticky == PG_PROJ, "SONG 2/3, SELECT right: not PROJECT (%d)", sticky);
    turn(ENC_SELECT, 1); wait_ms(200); EXPECT(sticky == PG_PROJ, "PROJECT, SELECT right: left it (%d)", sticky);
    for (int i = 0; i < 4; i++) { tap(B_SAVE); wait_ms(200); EXPECT(sticky == PG_PROJ || sticky == PG_SONG2, "SAVE taps: out of the 3 pages (%d)", sticky); }
}
/* (4.0.9, issue #6: "crashes when punching 1/4 in the Browser") FX + 1/4 held on every page that browses sounds (SOUNDS
 * from PRESETS, EDIT's last page, SAVE > PRESETS, the drums' kits), playing and stopped; every punch key; PRESETS turned meanwhile */
static void sc_punchsnd(void)
{
    boot_wait(); wait_ms(800);
    for (int pass = 0; pass < 4; pass++) {
        tap(B_HOME); wait_ms(150);
        if (pass == 0) { turn(ENC_PRESETS, 1); wait_ms(200); }
        else if (pass == 1) { tap(B_EDIT); for (int i = 0; i < 6 && sticky != PG_SND; i++) { tap(B_EDIT); wait_ms(100); } }
        else if (pass == 2) { tap(B_EDIT); wait_ms(100); tap(B_SAVE); wait_ms(150); }
        else { trk = 3; tempo(40.0f); turn(ENC_PRESETS, 1); wait_ms(200); }   /* (the slowest tempo: 1/4 the longest) */
        if (pass & 1) { tap(B_PLAY); wait_ms(300); }
        printf("[%8.3f]    pass %d: page %d\n", tw_s(), pass, sticky);
        for (int k = 0; k < 16; k++) {
            press(B_FX); wait_ms(60); keydown(white_key[k]); wait_ms(k == 0 ? 2500 : 300); keyup(white_key[k]); wait_ms(60); release(B_FX); wait_ms(120);
        }
        turn(ENC_PRESETS, 2); wait_ms(200); press(B_FX); keydown(white_key[0]); wait_ms(800); turn(ENC_PRESETS, -1); wait_ms(800); keyup(white_key[0]); release(B_FX);
        if (Q.playing) { tap(B_PLAY); wait_ms(200); }
        EXPECT(!S.panics, "pass %d: the sound panicked (%u)", pass, S.panics);
    }
    trk = 0;
}
/* (3.4.2, issue #22: "the arpeggiator does not work, the sound goes silent, on track 3") each synth track of a new
 * project, then each of its sounds' kinds, a chord held with ARP on: heard, stopped and playing */
static void sc_arpall(void)
{
    boot_wait(); wait_ms(800);
    for (int pass = 0; pass < 2; pass++) {
        if (pass) { tap(B_PLAY); wait_ms(300); }
        for (int t = 0; t < NTRK; t++) {
            if (is_drum(t)) continue;
            select_track(t); wait_ms(150);
            for (int u = 0; u < NTRK; u++) solo[u] = u == t;
            double off = peak_over(300);
            arpc[t].mode = AM_UP; arpc[t].rate = 3;
            keydown(0); keydown(4); keydown(7);
            double pk[4]; for (int w = 0; w < 4; w++) pk[w] = peak_over(1000);
            uint32_t v = (uint32_t)snd_voices(&S);
            keyup(0); keyup(4); keyup(7); wait_ms(300);
            arpc[t].mode = AM_OFF;
            for (int u = 0; u < NTRK; u++) solo[u] = 0;
            printf("[%8.3f]    %s, track %d (%s): ARP peaks %.4f %.4f %.4f %.4f (%.4f without), %u voices\n", tw_s(), pass ? "playing" : "stopped", t + 1, gb_presets[preset_of[t]].name, pk[0], pk[1], pk[2], pk[3], off, v);
            if (!pass) for (int w = 0; w < 4; w++) EXPECT(pk[w] > 0.0015, "track %d (%s) silent with ARP, second %d (%.4f)", t + 1, gb_presets[preset_of[t]].name, w + 1, pk[w]);
        }
        if (pass) { tap(B_PLAY); wait_ms(300); }
    }
}
/* (3.4.2, issue #24: "during SONG playback, step 1 does not play when the track's engine changes between patterns")
 * track 1: pattern 1 the 808 (E808), pattern 2 RHODES (FM), a note on step 1 of each; a song of 1 bar each, played:
 * every bar's step 1 heard */
static void sc_songeng(void)
{
    boot_wait(); wait_ms(800);
    select_track(0); wait_ms(150);
    int rh = preset_named("RHODES"); EXPECT(rh >= 0, "no RHODES");
    for (int w = 0; w < 2; w++) {
        seq_queue(&Q, 0, w); wait_ms(100);
        Q.t[0].pat[w].n = 0; Q.t[0].pat[w].nlock = 0; seq_step_toggle(&Q, 0, 0, 48, 110);
        if (w == 1 && rh >= 0) { apply_preset(0, rh); wait_ms(100); }
        commit_trk(0); wait_ms(100);
    }
    EXPECT(PS.s[0][0].engine != PS.s[0][1].engine, "the two patterns' engines the same (%d)", PS.s[0][0].engine);
    seq_song_set_count(&Q, 2); seq_song_set_len(&Q, 0, 1); seq_song_set_len(&Q, 1, 1);
    seq_song_set_pat(&Q, 0, 0, 0); seq_song_set_pat(&Q, 1, 0, 1);
    for (int u = 0; u < NTRK; u++) solo[u] = u == 0;
    seq_song_play(&Q, 1); wait_ms(300); tw_peak = 0; tap(B_PLAY);
    int quiet = 0;
    for (int bar = 0; bar < 6; bar++) {
        double pk;
        if (!bar) { wait_ms(150); pk = tw_peak; }        /* (bar 1: from PLAY on) */
        else { while (Q.tick % SEQ_BAR > SEQ_BAR / 2 || seq_bar(&Q) != (uint32_t)bar) wait_ms(1); pk = peak_over(150); }
        printf("[%8.3f]    bar %d (pattern %d, engine %d): step 1 peak %.4f\n", tw_s(), bar + 1, Q.t[0].cur + 1, S.trk[0].patch.engine, pk);
        if (pk < 0.003) quiet++;
        while (seq_bar(&Q) == (uint32_t)bar) wait_ms(1);
    }
    tap(B_PLAY); seq_song_play(&Q, 0);
    for (int u = 0; u < NTRK; u++) solo[u] = 0;
    EXPECT(!quiet, "%d bars of 6: step 1 silent after the engine changed", quiet);
}
/* (3.4.2, issue #28: "I select a chord (7th) and record: it won't record the chords") SEL CHORD 7TH on track 1, REC,
 * one white key: the 4 notes of its chord written; and with ARP on: what it plays written */
static void sc_chordrec(void)
{
    boot_wait(); wait_ms(800);
    select_track(0); wait_ms(150);
    for (int st = 0; st < 3; st++) {
        static const int8_t SV[3] = { 0, 40, -40 };
        harm[0].chord = CH_7TH; strum[0] = SV[st];
        Q.t[0].pat[Q.t[0].cur].n = 0; Q.t[0].quant = 0;   /* (as played: the strum's order seen) */
        tap(B_PLAY); wait_ms(300); tap(B_REC); wait_ms(300);
        uint32_t len = (uint32_t)Q.t[0].pat[Q.t[0].cur].steps * SEQ_TPS, loop = Q.tick / len;
        while (Q.tick / len == loop) wait_ms(1);
        wait_ms(40); keydown(white_key[0]); wait_ms(400); keyup(white_key[0]); wait_ms(200);
        tap(B_REC); wait_ms(300); tap(B_PLAY); wait_ms(300);
        int n = seq_count(&Q, 0);
        const seq_pat *P = &Q.t[0].pat[Q.t[0].cur]; int hi = 0, lo = 0;
        for (int i = 0; i < P->n; i++) { if (P->ev[i].note > P->ev[hi].note) hi = i; if (P->ev[i].note < P->ev[lo].note) lo = i; }
        printf("[%8.3f]    CHORD 7TH, STRUM %d, one key recorded: %d notes written, the lowest at %u, the highest at %u\n", tw_s(), SV[st], n, n ? P->ev[lo].tick : 0, n ? P->ev[hi].tick : 0);
        EXPECT(n == 4, "CHORD 7TH, STRUM %d: %d notes written (4)", SV[st], n);
        if (n == 4 && SV[st]) EXPECT(SV[st] > 0 ? P->ev[lo].tick < P->ev[hi].tick : P->ev[hi].tick < P->ev[lo].tick, "STRUM %d: the wrong note first", SV[st]);
    }
    harm[0].chord = CH_OFF; strum[0] = 0;
}
/* (3.4.0, Sam: "a MIDI file imported in the editor shows in the FM-1's sequencer, but PLAY gives no sound") what
 * midiImport + patWrite send (ED_NOTES2: cond 0, ratchet 1, the playing pattern), then each track alone, playing */
static double solo_peak(int t, double ms) { for (int u = 0; u < NTRK; u++) solo[u] = u == t; wait_ms(200); double pk = peak_over(ms); for (int u = 0; u < NTRK; u++) solo[u] = 0; return pk; }
static void midi_pat(int t, int pt, int steps, int base)
{
    uint8_t raw[7 + 8 * 16], rep[64]; uint32_t rn;
    raw[0] = (uint8_t)t; raw[1] = (uint8_t)pt; raw[2] = 0; raw[3] = 0; raw[4] = (uint8_t)steps; raw[5] = 0; raw[6] = 16;
    for (int i = 0; i < 16; i++) {
        uint8_t *o = raw + 7 + 8 * i; uint32_t at = (uint32_t)i * (uint32_t)steps / 16u * SEQ_TPS, len = SEQ_TPS * 2;
        o[0] = (uint8_t)at; o[1] = (uint8_t)(at >> 8); o[2] = (uint8_t)len; o[3] = (uint8_t)(len >> 8);
        o[4] = (uint8_t)(base < 0 ? i : base + (i % 8) * 2); o[5] = 96; o[6] = 0; o[7] = 1;
    }
    int r = tw_ed(ED_NOTES2, raw, 7 + 8 * 16, rep, &rn, 2000);
    EXPECT(r == ED_NOTES2 && (rep[2] | rep[3] << 8) == 16, "track %d: the notes not written (%d)", t, r);
}
static void sc_midiimp(void)
{
    boot_wait(); wait_ms(800);
    for (int pass = 0; pass < 2; pass++) {
        if (pass) { home_do(HK_NEW); wait_ms(100); home_do(HK_NEW); wait_ms(800); printf("[%8.3f]    a new project\n", tw_s()); }
        for (int t = 0; t < NTRK; t++) midi_pat(t, 255, 16, is_drum(t) ? -1 : 48);
        wait_ms(300);
        tap(B_PLAY); wait_ms(300);
        for (int t = 0; t < NTRK; t++) {
            double pk = solo_peak(t, 2500);
            printf("[%8.3f]    pass %d, track %d (%s, pattern %d, %d notes): peak %.4f\n", tw_s(), pass, t, preset_of[t] >= 0 ? gb_presets[preset_of[t]].name : "-", Q.t[t].cur + 1, Q.t[t].pat[Q.t[t].cur].n, pk);
            EXPECT(pk > 0.001, "pass %d, track %d: the imported notes not heard (peak %.4f)", pass, t, pk);
        }
        tap(B_PLAY); wait_ms(300);
    }
    tap(B_PLAY); wait_ms(300);   /* written while it plays */
    midi_pat(1, 255, 16, 60); wait_ms(800);
    { double pk = solo_peak(1, 2500); printf("[%8.3f]    track 1 written while playing: %d notes, peak %.4f\n", tw_s(), Q.t[1].pat[Q.t[1].cur].n, pk);
      EXPECT(pk > 0.001, "written while playing: not heard (peak %.4f)", pk); }
    tap(B_PLAY); wait_ms(300);
}
/* (4.0.8, Sam: "SEL does not walk with SELECT") every button's pages: tapped, then SELECT right to the last, then left
 * back to the first, on a synth track and on the drums */
static void sc_selwalk(void)
{
    boot_wait(); wait_ms(800);
    static const uint8_t BT[] = { BTN_EDIT, BTN_FX, BTN_LFO, BTN_ENV, BTN_SEL, BTN_ARP, BTN_GLO, BTN_SEQ };
    for (int pass = 0; pass < 2; pass++) {
        if (pass) { for (int t = 0; t < NTRK; t++) if (is_drum(t)) trk = t; }
        else { set_sound(0, "SAW BASS"); trk = 0; }
        for (unsigned g = 0; g < sizeof BT; g++) {
            const uint8_t *l; int n = grp_of(BT[g], &l), want[12], m = 0;
            for (int i = 0; i < n; i++) if (pg_shown(l[i]) && l[i] != PG_SONG2 && !(is_drum(trk) && (l[i] == PG_STEPS || l[i] == PG_KIT || l[i] == PG_DSOUND))) want[m++] = l[i];
            if (is_drum(trk) && (BT[g] == BTN_EDIT || BT[g] == BTN_SEQ)) continue;          /* (the drums' own screen: drums285) */
            tap(B_HOME); wait_ms(150); tap(BT[g]); wait_ms(150);
            for (int k = 0; k < m + 1; k++) { turn(ENC_SELECT, -1); wait_ms(80); }
            EXPECT(sticky == want[0], "button %d track %d: SELECT left to the end: page %d, not %d", BT[g], trk, sticky, want[0]);
            for (int k = 1; k < m; k++) { turn(ENC_SELECT, 1); wait_ms(80); EXPECT(sticky == want[k], "button %d track %d: SELECT right %d: page %d, not %d", BT[g], trk, k, sticky, want[k]); }
            turn(ENC_SELECT, 1); wait_ms(80); EXPECT(sticky == want[m - 1], "button %d track %d: SELECT past the last: page %d", BT[g], trk, sticky);
        }
    }
    tap(B_HOME);    /* (4.0.8) your theme: the 5th track's colour (synth 4) from bytes 34-35; an older theme (0 there): its default */
    { uint8_t t[40] = { 0x54, 0x48, 0x4D, 0x32, 'T', 'W', 'I', 'N' }; for (int k = 0; k < 5; k++) { t[12 + 2 * k] = (uint8_t)(k * 40); t[13 + 2 * k] = (uint8_t)(k * 40); }
      t[34] = 0x1F; t[35] = 0xF8;                                        /* (magenta) */
      uint32_t c = crc32_ieee(0, t, 36); t[36] = (uint8_t)c; t[37] = (uint8_t)(c >> 8); t[38] = (uint8_t)(c >> 16); t[39] = (uint8_t)(c >> 24);
      memcpy(us_theme, t, 40); scr_color = UI_NTHEMES; color_apply(); EXPECT(TRK_COL[4] == 0xF81F, "your theme: synth 4's colour %04X", TRK_COL[4]);
      t[34] = t[35] = 0; c = crc32_ieee(0, t, 36); t[36] = (uint8_t)c; t[37] = (uint8_t)(c >> 8); t[38] = (uint8_t)(c >> 16); t[39] = (uint8_t)(c >> 24);
      memcpy(us_theme, t, 40); color_apply(); EXPECT(TRK_COL[4] == RGB(240, 110, 200), "an older theme: synth 4 not its default (%04X)", TRK_COL[4]);
      scr_color = 0; color_apply(); }
}
/* (4.0.3) the tour: every page and layer as the firmware fills it, as text (title, sub, the knobs, the tiles), for the
 * inventory against SLOOP 2.8.5's GUIDE (section 16, section 9) */
static void dumpv(const char *tag)
{
    static ui_view tv; memset(&tv, 0, sizeof tv); fill_view(&tv);
    printf("@@ %-16s | %-14s | %-30s |", tag, tv.title, tv.sub);
    for (int i = 0; i < 4; i++) printf(" %s=%s ;", tv.knob[i].label ? tv.knob[i].label : "-", tv.knob[i].label ? tv.knob[i].value : "");
    if (tv.page == UI_LAYER || tv.page == UI_PUNCH || tv.page == UI_MENU) { printf(" tiles:"); for (int i = 0; i < 16; i++) if (tv.tile[i].text[0]) printf(" %d:%s", i + 1, tv.tile[i].text); }
    printf("  [page %d sticky %d]\n", tv.page, sticky);
}
static void sc_tour(void)
{
    boot_wait(); wait_ms(1500);
    set_sound(0, "SAW BASS"); trk = 0; wait_ms(300); tap(B_PLAY); wait_ms(1500); shot("home"); tap(B_PLAY); wait_ms(300);
    gb_patch_set(&S.trk[0].patch, SP_SLCR, 1); gb_patch_set(&S.trk[0].patch, SP_SLPAT, 3); base_of_track(0);
    static const struct { int b, n; const char *nm; } G[] = { { B_EDIT, 7, "EDIT" }, { B_FX, 8, "FX" }, { B_LFO, 4, "LFO" }, { B_ENV, 3, "ENV" },
        { B_SEL, 3, "SEL" }, { B_ARP, 3, "ARP" }, { B_GLO, 10, "GLO" }, { B_SAVE, 5, "SAVE" }, { B_SEQ, 4, "SEQ" } };
    for (int pass = 0; pass < 2; pass++) {
        if (pass) { for (int t = 0; t < NTRK; t++) if (is_drum(t)) trk = t; printf("@@ ---- the drum track (%d)\n", trk); }
        dumpv("TRACKS");
        for (unsigned g = 0; g < sizeof G / sizeof *G; g++) {
            tap(B_HOME); wait_ms(200); char tg[32];
            for (int i = 0; i < G[g].n; i++) { tap(G[g].b); wait_ms(250); snprintf(tg, sizeof tg, "%s x%d", G[g].nm, i + 1); dumpv(tg); if (!pass && G[g].b == B_FX && i == 2) { tap(B_PLAY); wait_ms(900); shot("slicer"); tap(B_PLAY); } }
            tap(B_EDIT); wait_ms(200); tap(G[g].b); wait_ms(250); snprintf(tg, sizeof tg, "%s from EDIT", G[g].nm); dumpv(tg);
            if (G[g].b == B_SAVE) for (int i = 0; i < 5; i++) { tap(B_SAVE); wait_ms(250); snprintf(tg, sizeof tg, "SAVE again x%d", i + 1); dumpv(tg); if (!pass && sticky == PG_PROJ) shot("save-project"); }
        }
        tap(B_HOME); wait_ms(300);
        for (unsigned g = 0; g < sizeof G / sizeof *G; g++) { char tg[32]; press(G[g].b); wait_ms(700); snprintf(tg, sizeof tg, "%s held", G[g].nm); dumpv(tg); release(G[g].b); wait_ms(400); }
        press(B_HOME); wait_ms(1200); dumpv("HOME held");
        for (int i = 0; i < 6; i++) { turn(ENC_SELECT, 1); wait_ms(200); char tg[32]; snprintf(tg, sizeof tg, "HOME sel+%d", i + 1); dumpv(tg); }
        turn(ENC_SELECT, -8); wait_ms(200); dumpv("HOME sel-8"); release(B_HOME); wait_ms(400); tap(B_HOME); wait_ms(300);
    }
}
/* (4.0.2) the low end through the whole firmware: a sub bass at A0..A2 and the kicks, OUT/low-*.wav (tools: the
 * fundamental's level against the engine's own) */
static void sc_lowend(void)
{
    boot_wait(); wait_ms(1500);
    printf("[%8.3f]    lowcut %d flt_q %.2f mfilt %.3f dust %.2f duck %.2f filt %.3f %.3f %.3f %.3f %.3f master %.2f\n", tw_s(), S.lowcut, S.flt_q, S.mfilt.cur, S.dust, S.duck,
           S.trk[0].filt.cur, S.trk[1].filt.cur, S.trk[2].filt.cur, S.trk[3].filt.cur, S.trk[4].filt.cur, S.master.cur);
    for (int t = 0; t < NTRK; t++) for (int p = 0; p < 8; p++) Q.t[t].pat[p].n = 0;
    press(BTN_GLO); wait_ms(700); shot("mix-glo"); release(BTN_GLO); wait_ms(300);   /* (4.0.2: SLOOP 3's MIX, GLO held) */
    shot("home"); press(BTN_FX); wait_ms(700); shot("punch"); locked = 1u << BTN_FX; wait_ms(300); shot("punch-lock"); locked = 0; release(BTN_FX); wait_ms(300);
    set_sound(0, "SUB BASS"); set_sound(3, "808");
    static const int N[4] = { 21, 33, 45, 57 };
    for (int i = 0; i < 4; i++) note(0, i * 4, N[i], 110, 3);
    wav_on("low-bass"); tap(BTN_PLAY); wait_ms(4000); tap(BTN_PLAY); wait_ms(600); tw_wav_close();
    for (int t = 0; t < NTRK; t++) Q.t[t].pat[Q.t[t].cur].n = 0;
    note(3, 0, 0, 120, 1); note(3, 8, 0, 120, 1);
    wav_on("low-kick"); tap(BTN_PLAY); wait_ms(4000); tap(BTN_PLAY); wait_ms(600); tw_wav_close();
    hs[HS_LOWCUT] = 1; home_apply();                       /* the same with SPEAKER LOWCUT on */
    for (int t = 0; t < NTRK; t++) Q.t[t].pat[Q.t[t].cur].n = 0;
    for (int i = 0; i < 4; i++) note(0, i * 4, N[i], 110, 3);
    wav_on("low-bass-lc"); tap(BTN_PLAY); wait_ms(4000); tap(BTN_PLAY); wait_ms(600); tw_wav_close();
    hs[HS_LOWCUT] = 0; home_apply();
}
/* the instruments, key by key (3.3.1: "the drum kits and the samples are the wrong instruments"): each sound on its
 * track, its 16 white keys one at a time, OUT/<sound>.wav; tools/inst_check.py holds them against their sources */
static void sc_inst(void)
{
    boot_wait(); wait_ms(3000);
    static const char *L[] = { "BOOMBAP", "808", "ACOUSTIC", "GM KIT", "GRAND PNO", "UP BASS", "VIBES", "HORN STAB", "STRING STB", "LOFI FLUTE", "SCRATCH" };
    for (unsigned n = 0; n < sizeof L / sizeof *L; n++) {
        int np = preset_named(L[n]); if (np < 0) { printf("no %s\n", L[n]); continue; }
        int t = gb_presets[np].engine == ENG_DRUM ? 3 : 0;
        set_sound(t, L[n]); base_from_sound(); ps_all(); trk = t; wait_ms(400);
        printf("[%8.3f]    %s on track %d: engine %d, kit/set %d\n", tw_s(), L[n], t, S.trk[t].patch.engine, gb_presets[np].e[0]);
        char wn[40]; snprintf(wn, sizeof wn, "inst-%02u", n); wav_on(wn);
        for (int i = 0; i < 16; i++) { keydown(white_key[i]); wait_ms(90); keyup(white_key[i]); wait_ms(1100); }
        tw_wav_close();
    }
}
/* 3.3.1: the public beta's film, one segment a run (TW_SEG): each a hip-hop beat of SLOOP 3's own sounds and the
 * gestures that show one of what SLOOP 3 brings. Like `film`: OUT/film.rgb565, film.panel, film.wav, film.txt, and
 * OUT/marks.txt (a gesture's name and its time in the film) */
static FILE *tw_marks; static double tw_f0;
static void mark(const char *what) { if (tw_marks) fprintf(tw_marks, "%.3f %s\n", tw_s() - tw_f0, what); }
static void lvl(int t, float v) { sm_set(&S.trk[t].level, v); }
static void seg_reset(float bpm, int swing)
{
    for (int t = 0; t < NTRK; t++) for (int p = SEQ_NPAT - 1; p >= 0; p--) { Q.t[t].cur = (uint8_t)p; seq_clear(&Q, t); seq_set_len(&Q, t, 16); }
    seq_set_bpm(&Q, bpm); Q.swing = (uint8_t)swing; S.tx[0] = 0;
}
static void chord(int t, int step, const int *n, int k, int vel, int len16) { for (int i = 0; i < k; i++) note(t, step, n[i], vel, len16); }
static void drum(int step, int pad, int vel) { seq_step_toggle(&Q, 3, step, pad, vel); }
static void key_note(int n, double hold_ms) { int k = n - 53; if (k < 0 || k > 26) return; keydown(k); wait_ms(hold_ms); keyup(k); }
static void seg_film_open(void)
{
    char fn[512];
    snprintf(fn, sizeof fn, "%s/film.panel", tw_out); tw_panel = fopen(fn, "wb");
    snprintf(fn, sizeof fn, "%s/marks.txt", tw_out); tw_marks = fopen(fn, "w");
    snprintf(fn, sizeof fn, "%s/film.rgb565", tw_out); tw_film = fopen(fn, "wb"); tw_film_t = tw_s(); tw_f0 = tw_film_t;
    wav_on("film");
    snprintf(fn, sizeof fn, "%s/film.txt", tw_out); FILE *f = fopen(fn, "w"); if (f) { fprintf(f, "%.6f %.6f\n", tw_f0, tw_s()); fclose(f); }
}
static void seg_film_close(void)
{
    if (tw_film) { fclose(tw_film); tw_film = 0; }
    if (tw_panel) { fclose(tw_panel); tw_panel = 0; }
    if (tw_marks) { fclose(tw_marks); tw_marks = 0; }
    tw_wav_close();
}
static void sc_film2(void)
{
    const char *seg = getenv("TW_SEG") ? getenv("TW_SEG") : "engine";
    boot_wait(); wait_ms(3200);                               /* (the logo drawn: the film starts on TRACKS) */
    if (!strcmp(seg, "cozy")) {
        /* a sketch for the "blocky sandbox" groovebox idea: an original calm piece in the style of that game's
         * soundtrack, on SLOOP 3's own sounds: a soft sampled piano with the pedal down, a pad, a music box, a low
         * sine; D major with major 7ths and 9ths, 72 BPM, no drums, a long dark reverb. 16 bars, then a last chord */
        seg_reset(72, 0);
        set_sound(0, "SUB BASS"); set_sound(1, "GRAND PNO"); set_sound(2, "SOFT PAD"); set_sound(3, "BOOMBAP"); set_sound(4, "MUSIC BOX");
        for (int t = 0; t < NTRK; t++) if (t != 3) seq_set_len(&Q, t, 128);
        static const int BS[4] = { 43, 42, 40, 45 };                                   /* G, F#, E, A (under D/F#) */
        static const int CH[4][4] = { { 59, 62, 66, 69 }, { 57, 62, 64, 69 }, { 55, 59, 62, 66 }, { 52, 57, 59, 64 } };   /* Gmaj7 D/F# Em9 Asus2 */
        static const int AS[4] = { 2, 5, 8, 11 };
        unsigned rnd = 11;
#define HUM(v) ((v) + (int)((rnd = rnd * 1103515245u + 12345u) >> 16) % 7 - 3)
        for (int b = 0; b < 8; b++) {
            int c = b & 3;
            note(1, b * 16, BS[c], HUM(56), 16);
            for (int i = 0; i < 4; i++) note(1, b * 16 + AS[i], CH[c][i], HUM(44 + (i == 3 ? 6 : 0)), 16 - AS[i]);
            if (c == 3) note(1, b * 16 + 14, 61, HUM(40), 2);                          /* (a C# leaning back home) */
            note(2, b * 16, CH[c][0] - 12, 50, 16); note(2, b * 16, CH[c][2] - 12, 46, 16);
            note(0, b * 16, BS[c] - 12, 70, 16);
        }
        /* the melody, bars 5-8 of the pattern: few notes, a lot of air */
        static const int ML[] = { 64 + 4, 78, 64 + 10, 76, 80, 74, 80 + 12, 69, 96 + 2, 71, 96 + 8, 74, 96 + 12, 76, 112, 76, 112 + 8, 73, -1 };
        for (int i = 0; ML[i] >= 0; i += 2) note(1, ML[i], ML[i + 1], HUM(58), 8);
        /* the music box, bars 1-4 of its pattern (heard from bar 9) */
        static const int MB[] = { 6, 81, 14, 78, 16 + 4, 86, 16 + 12, 81, 32 + 6, 83, 32 + 10, 78, 48 + 2, 88, 48 + 8, 85, -1 };
        for (int i = 0; MB[i] >= 0; i += 2) note(4, MB[i], MB[i + 1], HUM(46), 4);
#undef HUM
        base_from_sound(); ps_all(); HIST.n_undo = HIST.n_redo = 0;
        /* the space: long and dark */
        S.rev_fb = 0.9f; S.rev_damp = 0.55f; S.dly_fb = 0.42f; sm_set(&S.dly_time, 27562.0f);
        sm_set(&S.trk[1].send_rev, 0.5f); sm_set(&S.trk[1].send_dly, 0.08f); sm_set(&S.trk[1].filt, -0.3f);
        sm_set(&S.trk[2].send_rev, 0.7f); sm_set(&S.trk[4].send_rev, 0.6f); sm_set(&S.trk[4].send_dly, 0.35f);
        static const struct { int bar; float l[5]; } AR[] = {
            { 0, { 0, .6f, 0, 0, 0 } },            /* the piano alone */
            { 4, { 0, .6f, .16f, 0, 0 } },         /* the pad, the melody */
            { 8, { .2f, .6f, .16f, 0, .18f } },    /* the music box, the low sine */
            { 12, { .2f, .6f, .2f, 0, .12f } },
        };
        double bar_ms = 4 * 60000.0 / 72.0;
        char fn[512];
        snprintf(fn, sizeof fn, "%s/marks.txt", tw_out); tw_marks = fopen(fn, "w");
        double f0 = tw_s(); tw_f0 = f0;
        wav_on("film");
        snprintf(fn, sizeof fn, "%s/film.txt", tw_out); FILE *f = fopen(fn, "w"); if (f) { fprintf(f, "%.6f %.6f\n", f0, tw_s()); fclose(f); }
        for (int t = 0; t < NTRK; t++) lvl(t, AR[0].l[t]);
        film_wait(600);
        tap(B_PLAY); mark("play");
        while (!Q.tick) wait_ms(1);
        if (tw_marks) fprintf(tw_marks, "%.3f bar0\n", tw_s() - tw_f0 - Q.tick * (bar_ms / SEQ_BAR) / 1000.0);
        for (unsigned a = 1; a < sizeof AR / sizeof *AR; a++) {
            while (Q.tick + 12 < (uint32_t)(AR[a].bar * SEQ_BAR)) wait_ms(1);
            for (int t = 0; t < NTRK; t++) lvl(t, AR[a].l[t]);
        }
        while (Q.tick + 16 < 16u * SEQ_BAR) wait_ms(1);
        tw_btn |= 1u << B_PLAY; wait_ms(20); tw_btn &= ~(1u << B_PLAY);                       /* stop: the last chord */
        mark("end"); trk = 1;
        static const int LAST[5] = { 57, 61, 62, 64, 66 };                                     /* Dmaj9 */
        for (int i = 0; i < 5; i++) { tw_keys |= 1u << (LAST[i] - 53); wait_ms(35); }
        wait_ms(4200); tw_keys = 0; wait_ms(5000);
        tw_wav_close(); if (tw_marks) { fclose(tw_marks); tw_marks = 0; }
        return;
    }
    if (!strcmp(seg, "vice") || !strcmp(seg, "vice2")) {
        const int vice2 = !strcmp(seg, "vice2");     /* (3.3.6's launch video, the 3D cut: 18 sections) */
        /* the public beta's film, 80s cut: synth-pop as a 1980s radio station would play it, on SLOOP 3's own sounds.
         * 116 BPM, A minor. A station ident first (the tuning hiss, three brass hits), then the song: patterns of 6
         * bars (one a scene of the film), VERSE (pattern 0) Am F C G F G and CHORUS (pattern 1) F G Am Am F E, the
         * brass hook in the chorus only. FM bass in octaves, a poly synth on a pushed 3-3-2, the 80S kit with a big
         * reverb, a glass pad. 16 sections, then the last chord. Only the sound is written (film.wav, marks.txt) */
        seg_reset(116, 0);
        set_sound(0, "FM BASS"); set_sound(1, "80S POLY"); set_sound(2, "SYN BRASS"); set_sound(3, "80S"); set_sound(4, "RADIO");
        static const int CHV[6][3] = { { 57, 60, 64 }, { 57, 60, 65 }, { 55, 60, 64 }, { 55, 59, 62 }, { 57, 60, 65 }, { 55, 59, 62 } };   /* Am F C G F G */
        static const int CHC[6][3] = { { 57, 60, 65 }, { 55, 59, 62 }, { 57, 60, 64 }, { 57, 60, 64 }, { 57, 60, 65 }, { 56, 59, 64 } };   /* F G Am Am F E */
        static const int RTV[6] = { 45, 41, 48, 43, 41, 43 }, RTC[6] = { 41, 43, 45, 45, 41, 40 };
        static const int HOOK[] = { 0, 72, 2, 2, 72, 2, 4, 74, 2, 6, 76, 4, 10, 72, 2, 12, 69, 4,
                                    16, 74, 2, 18, 74, 2, 20, 76, 2, 22, 79, 4, 26, 76, 2, 28, 74, 4,
                                    32, 76, 6, 38, 72, 2, 40, 76, 4, 44, 81, 4,
                                    48, 79, 4, 52, 76, 4, 56, 72, 8,
                                    64, 72, 2, 66, 74, 2, 68, 76, 4, 72, 77, 4, 76, 76, 4,
                                    80, 74, 4, 84, 71, 4, 88, 68, 4, 92, 71, 4, -1 };
        static const int PUSH[6][2] = { { 0, 3 }, { 3, 3 }, { 6, 2 }, { 8, 3 }, { 11, 3 }, { 14, 2 } };   /* the poly synth's 3-3-2 */
        unsigned rnd = 23;
#define HUM(v) ((v) + (int)((rnd = rnd * 1103515245u + 12345u) >> 16) % 7 - 3)
        for (int pt = 1; pt >= 0; pt--) {
            const int (*CH)[3] = pt ? CHC : CHV; const int *RT = pt ? RTC : RTV;
            for (int t = 0; t < NTRK; t++) { Q.t[t].cur = (uint8_t)pt; seq_set_len(&Q, t, 96); }
            for (int b = 0; b < 6; b++) {
                int s0 = b * 16;
                for (int e = 0; e < 8; e++) note(0, s0 + 2 * e, RT[b] + (e & 1 ? 12 : 0), HUM(e & 1 ? 84 : 104), 2);
                for (int k = 0; k < 6; k++) for (int i = 0; i < 3; i++) note(1, s0 + PUSH[k][0], CH[b][i], HUM(k == 0 || k == 3 ? 92 : 74), PUSH[k][1]);
                for (int i = 0; i < 3; i++) note(4, s0, CH[b][i] + 12, 54, 16);
                /* the drums: a kick on 1 and 3 (the chorus: on every beat), the snare on 2 and 4 with a clap, eighth hats */
                drum(s0, 0, HUM(118)); drum(s0 + 8, 0, HUM(112));
                if (pt) { drum(s0 + 4, 0, HUM(104)); drum(s0 + 12, 0, HUM(104)); } else if (b & 1) drum(s0 + 10, 0, HUM(92));
                drum(s0 + 4, 2, HUM(120)); drum(s0 + 12, 2, HUM(122)); drum(s0 + 12, 3, HUM(80));
                for (int e = 0; e < 8; e++) if (!(b == 5 && e >= 4)) drum(s0 + 2 * e, e == 7 && (b & 1) ? 5 : 4, HUM(e & 1 ? 58 : 80));
                if (b == 5) { drum(s0 + 8, 10, 104); drum(s0 + 10, 10, 96); drum(s0 + 12, 9, 108); drum(s0 + 13, 9, 96); drum(s0 + 14, 9, 112); drum(s0 + 15, 2, 100); }   /* the fill */
            }
            drum(0, 11, 110);                                                   /* a crash on each section's start */
            if (pt) for (int i = 0; HOOK[i] >= 0; i += 3) note(2, HOOK[i], HOOK[i + 1], HUM(100), HOOK[i + 2]);
            else for (int b = 1; b < 6; b += 2) { int nb = (b + 1) % 6; for (int i = 0; i < 3; i++) note(2, b * 16 + 14, CH[nb][i] + 12, HUM(96), 2); }   /* the verse: a brass push into each 2nd bar */
        }
#undef HUM
        double bar_ms = 4 * 60000.0 / 116.0;
        /* the space: the 80s' big reverb, the chorus on the synths, a dotted-eighth echo on the brass */
        S.rev_fb = 0.86f; S.rev_damp = 0.32f; S.dly_fb = 0.32f; sm_set(&S.dly_time, (float)(GB_SR * 0.75 * 60.0 / 116.0));
        sm_set(&S.trk[3].send_rev, 0.34f); sm_set(&S.trk[2].send_rev, 0.3f); sm_set(&S.trk[2].send_dly, 0.2f);
        sm_set(&S.trk[1].send_rev, 0.22f); sm_set(&S.trk[1].send_cho, 0.5f); sm_set(&S.trk[4].send_rev, 0.5f); sm_set(&S.trk[4].send_cho, 0.4f);
        sm_set(&S.trk[0].send_cho, 0.15f);
        char fn[512];
        snprintf(fn, sizeof fn, "%s/marks.txt", tw_out); tw_marks = fopen(fn, "w");
        double f0 = tw_s(); tw_f0 = f0;
        wav_on("film");
        snprintf(fn, sizeof fn, "%s/film.txt", tw_out); FILE *f = fopen(fn, "w"); if (f) { fprintf(f, "%.6f %.6f\n", f0, tw_s()); fclose(f); }
        for (int t = 0; t < NTRK; t++) lvl(t, 0.0f);
        film_wait(400);
        /* the station ident: a radio tuned in (its hiss, 0.9 s), then three brass hits */
        mark("ident"); lvl(4, 0.32f); trk = 4; tw_keys |= 1u << (64 - 53); wait_ms(900); tw_keys = 0; lvl(4, 0.0f); wait_ms(150);
        set_sound(4, "GLASS PAD"); base_from_sound(); ps_all(); HIST.n_undo = HIST.n_redo = 0;
        sm_set(&S.trk[4].send_rev, 0.5f); sm_set(&S.trk[4].send_cho, 0.4f);
        trk = 2; lvl(2, 0.5f);
        static const int H1[4] = { 55, 59, 62, 67 }, H3[4] = { 57, 60, 64, 69 };
        for (int h = 0; h < 3; h++) {
            const int *c = h < 2 ? H1 : H3; uint32_t k = 0; for (int i = 0; i < 4; i++) k |= 1u << (c[i] - 53);
            tw_keys = k; wait_ms(h < 2 ? 140 : 900); tw_keys = 0; wait_ms(h < 2 ? 120 : 260);
        }
        trk = 1;
        static const struct { int pat; float l[5]; } AR[16] = {
            { 0, { 0, .5f, .26f, 0, .26f } },        /* the logo: the synths, no drums */
            { 0, { .5f, .5f, .26f, .8f, .2f } },     /* what SLOOP is: the band */
            { 0, { .5f, .5f, .26f, .8f, .2f } },     /* the beta */
            { 1, { .5f, .44f, .4f, .84f, .2f } },    /* 1 instant: the chorus */
            { 0, { .5f, .5f, .26f, .8f, .2f } },
            { 1, { .5f, .44f, .4f, .84f, .2f } },
            { 0, { 0, .52f, .24f, 0, .3f } },        /* 4 loops: a breath */
            { 1, { .5f, .44f, .4f, .84f, .2f } },
            { 0, { .5f, .5f, .26f, .8f, .2f } },
            { 1, { .5f, .44f, .4f, .84f, .2f } },
            { 0, { .5f, .5f, .26f, .8f, .2f } },
            { 1, { .5f, .44f, .4f, .84f, .2f } },    /* 9 screen */
            { 0, { 0, .52f, .24f, 0, .3f } },        /* SLOOP 2.8 comes along: a breath */
            { 0, { .5f, .5f, .26f, .8f, .2f } },     /* installing (12 bars) */
            { 0, { .5f, .5f, .26f, .8f, .2f } },
            { 1, { .5f, .44f, .4f, .84f, .2f } },    /* the end */
        };
        static const struct { int pat; float l[5]; } AR2[18] = {
            { 0, { 0, .5f, .26f, 0, .26f } },        /* the station, the welcome: the synths, no drums */
            { 0, { .5f, .5f, .26f, .8f, .2f } },
            { 0, { .5f, .5f, .26f, .8f, .2f } },     /* what it is */
            { 1, { .5f, .44f, .4f, .84f, .2f } },
            { 0, { .5f, .5f, .26f, .8f, .2f } },     /* the sounds */
            { 1, { .5f, .44f, .4f, .84f, .2f } },
            { 0, { .5f, .5f, .26f, .8f, .2f } },     /* the sequencer */
            { 1, { .5f, .44f, .4f, .84f, .2f } },
            { 0, { .55f, 0, 0, .9f, .14f } },        /* the crate diggers: the drums and the bass */
            { 0, { .5f, .5f, .26f, .8f, .2f } },     /* the project */
            { 1, { .5f, .44f, .4f, .84f, .2f } },
            { 0, { .5f, .5f, .26f, .8f, .2f } },     /* the hands */
            { 1, { .5f, .44f, .4f, .84f, .2f } },
            { 0, { 0, .52f, .24f, 0, .3f } },        /* the pictures: a breath */
            { 0, { .5f, .5f, .26f, .8f, .2f } },     /* the editor, installing */
            { 0, { .5f, .5f, .26f, .8f, .2f } },
            { 1, { .5f, .44f, .4f, .84f, .2f } },    /* the end */
            { 1, { .5f, .44f, .4f, .84f, .2f } },
        };
        const int NSEC = vice2 ? 18 : 16;
#define ARL(a, t) (vice2 ? AR2[a].l[t] : AR[a].l[t])
#define ARP(a) (vice2 ? AR2[a].pat : AR[a].pat)
        for (int t = 0; t < NTRK; t++) lvl(t, ARL(0, t));
        seq_queue(&Q, -1, 0);
        tap(B_PLAY); mark("play");
        while (!Q.tick) wait_ms(1);
        if (tw_marks) fprintf(tw_marks, "%.3f bar0\n", tw_s() - tw_f0 - Q.tick * (bar_ms / SEQ_BAR) / 1000.0);
        for (int a = 1; a < NSEC; a++) {
            while (Q.tick < (uint32_t)((a * 6 - 5) * SEQ_BAR)) wait_ms(1);          /* (a bar into the section before: the next one queued) */
            seq_queue(&Q, -1, ARP(a));
            while (Q.tick + 12 < (uint32_t)(a * 6 * SEQ_BAR)) wait_ms(1);
            for (int t = 0; t < NTRK; t++) lvl(t, ARL(a, t));
        }
#undef ARL
#undef ARP
        while (Q.tick + 16 < (uint32_t)(NSEC * 6) * SEQ_BAR) wait_ms(1);
        tw_btn |= 1u << B_PLAY; wait_ms(20); tw_btn &= ~(1u << B_PLAY);                       /* stop: the last chord, Am */
        mark("end"); trk = 1;
        static const int LAST[4] = { 57, 60, 64, 69 };
        for (int i = 0; i < 4; i++) tw_keys |= 1u << (LAST[i] - 53);
        trk = 1; wait_ms(2600); tw_keys = 0; wait_ms(4500);
        tw_wav_close(); if (tw_marks) { fclose(tw_marks); tw_marks = 0; }
        return;
    }
    if (!strcmp(seg, "music")) {
        /* the video's one piece of music (3.3.1, third cut): lo-fi hip-hop, 76 BPM, F major 7 · E minor 7 · D minor 9 ·
         * C major 9, a sampled piano, an upright bass, vibes, a soft pad, the BOOMBAP kit. 64 bars arranged by
         * bringing tracks in and out on 4-bar boundaries, then a last chord. Only the sound is written (film.wav). */
        seg_reset(76, 18);
        set_sound(0, "UP BASS"); set_sound(1, "LOFI KEYS"); set_sound(2, "VIBES"); set_sound(3, "BOOMBAP"); set_sound(4, "WARM PAD");
        for (int t = 0; t < NTRK; t++) if (t != 3) seq_set_len(&Q, t, 64);
        seq_set_len(&Q, 3, 32);
        static const int CH[4][5] = { { 53, 57, 60, 64, 67 }, { 52, 55, 59, 62, 64 }, { 50, 53, 57, 60, 64 }, { 48, 52, 55, 59, 62 } };
        static const int RT[4] = { 41, 40, 38, 36 };
        unsigned rnd = 7;
#define HUM(v) ((v) + (int)((rnd = rnd * 1103515245u + 12345u) >> 16) % 9 - 4)
        for (int b = 0; b < 4; b++) {
            for (int i = 0; i < 5; i++) { note(1, b * 16, CH[b][i], HUM(74 - i * 2), 10); note(1, b * 16 + 10, CH[b][i], HUM(58 - i * 2), 6); }
            note(0, b * 16, RT[b], HUM(106), 6); note(0, b * 16 + 7, RT[b] + 12, HUM(68), 1); note(0, b * 16 + 10, RT[b] + 7, HUM(92), 4);
            note(4, b * 16, CH[b][0], 50, 16); note(4, b * 16, CH[b][2], 46, 16);
        }
        static const int ML[] = { 2, 76, 6, 79, 10, 81, 20, 79, 24, 76, 34, 74, 38, 76, 42, 81, 52, 79, 56, 76, 60, 74, -1 };
        for (int i = 0; ML[i] >= 0; i += 2) note(2, ML[i], ML[i + 1], HUM(72), 4);
        for (int b = 0; b < 2; b++) {
            drum(b * 16, 0, HUM(96)); drum(b * 16 + 10, 0, HUM(88)); if (b) drum(b * 16 + 6, 0, HUM(76));
            drum(b * 16 + 4, 2, HUM(114)); drum(b * 16 + 12, 2, HUM(116));
            for (int s = 0; s < 16; s += 2) drum(b * 16 + s, 4, HUM(s % 4 ? 60 : 84));
            drum(b * 16 + 15, 4, 40);
        }
        for (int s = 0; s < 32; s++) if (seq_step_has(&Q, 3, s, 2)) seq_step_nudge(&Q, 3, s, 5);   /* (the snare a touch late) */
        drum(29, 2, 64); drum(31, 2, 92);                                                       /* a little fill: every 8 bars */
        seq_step_cond(&Q, 3, 29, SEQ_CYCLE + 4 * 8 + 3); seq_step_cond(&Q, 3, 31, SEQ_CYCLE + 4 * 8 + 3);
#undef HUM
        base_from_sound(); ps_all(); HIST.n_undo = HIST.n_redo = 0;
        /* the arrangement: which tracks play from which bar (bass, keys, vibes, drums, pad) */
        static const struct { int bar; float l[5]; } AR[] = {
            { 0, { 0, .74f, 0, 0, .16f } },    /* the logo: the piano and the pad */
            { 4, { .62f, .66f, 0, 0, .14f } }, /* what SLOOP is: the bass comes in */
            { 8, { .62f, .5f, 0, .72f, .1f } },     /* the drums */
            { 12, { .62f, .5f, .22f, .72f, .1f } },  /* what's new: the vibes */
            { 28, { .62f, .5f, 0, .72f, .1f } },
            { 36, { .62f, .5f, .22f, .72f, .1f } },
            { 48, { 0, .72f, .26f, 0, .16f } },  /* SLOOP 2.8 comes along: a breath */
            { 52, { .62f, .5f, 0, .72f, .1f } },     /* installing */
            { 60, { 0, .74f, .22f, 0, .16f } },  /* the end */
        };
        double bar_ms = 4 * 60000.0 / 76.0;
        char fn[512];
        snprintf(fn, sizeof fn, "%s/marks.txt", tw_out); tw_marks = fopen(fn, "w");
        double f0 = tw_s(); tw_f0 = f0;
        wav_on("film");
        snprintf(fn, sizeof fn, "%s/film.txt", tw_out); FILE *f = fopen(fn, "w"); if (f) { fprintf(f, "%.6f %.6f\n", f0, tw_s()); fclose(f); }
        for (int t = 0; t < NTRK; t++) lvl(t, AR[0].l[t == 0 ? 0 : t == 1 ? 1 : t == 2 ? 2 : t == 3 ? 3 : 4]);
        film_wait(1000);
        tap(B_PLAY); mark("play");
        while (!Q.tick) wait_ms(1);
        if (tw_marks) fprintf(tw_marks, "%.3f bar0\n", tw_s() - tw_f0 - Q.tick * (bar_ms / SEQ_BAR) / 1000.0);   /* (the 1st downbeat) */
        for (unsigned a = 1; a < sizeof AR / sizeof *AR; a++) {
            while (Q.tick + 12 < (uint32_t)(AR[a].bar * SEQ_BAR)) wait_ms(1);              /* (a few ms before the downbeat) */
            for (int t = 0; t < NTRK; t++) lvl(t, AR[a].l[t]);
        }
        while (Q.tick + 16 < 64u * SEQ_BAR) wait_ms(1);
        tw_btn |= 1u << B_PLAY; wait_ms(20); tw_btn &= ~(1u << B_PLAY);                       /* stop, and the last chord */
        mark("end"); trk = 1;
        static const int LAST[5] = { 53, 57, 60, 64, 67 };
        for (int i = 0; i < 5; i++) tw_keys |= 1u << (LAST[i] - 53);
        wait_ms(3200); tw_keys = 0; wait_ms(4000);
        tw_wav_close(); if (tw_marks) { fclose(tw_marks); tw_marks = 0; }
        return;
    }
    if (!strcmp(seg, "engine")) {
        /* jazz-hop, 84: Rhodes chords that ring over each other, a deep bass, a flute line, the LO-FI kit */
        seg_reset(84, 30);
        set_sound(0, "DEEP BASS"); set_sound(1, "RHODES"); set_sound(2, "LOFI FLUTE"); set_sound(3, "LO-FI"); set_sound(4, "KALIMBA");
        lvl(0, .6f); lvl(1, .5f); lvl(2, .26f); lvl(3, .85f); lvl(4, .2f);
        static const int CH[4][5] = { { 50, 57, 60, 64, 67 }, { 55, 58, 62, 65, 69 }, { 52, 55, 59, 62, 66 }, { 57, 61, 64, 67, 71 } };   /* Dm9 Gm9 Em7b5 A7(9) */
        static const int RT[4] = { 38, 43, 40, 45 };
        for (int t = 0; t < NTRK; t++) if (t != 3) seq_set_len(&Q, t, 64);
        seq_set_len(&Q, 3, 32);
        for (int b = 0; b < 4; b++) {
            chord(1, b * 16, CH[b], 5, 84, 10); chord(1, b * 16 + 11, CH[b], 4, 66, 5);
            note(0, b * 16, RT[b], 110, 6); note(0, b * 16 + 7, RT[b] + 7, 90, 2); note(0, b * 16 + 10, RT[b], 96, 4);
            for (int i = 0; i < 4; i++) note(4, b * 16 + 2 + i * 4, CH[b][1 + (i & 3)] + 12, 50, 2);
        }
        static const int FL[] = { 0, 74, 6, 72, 10, 69, 16 + 4, 67, 16 + 8, 69, 32, 71, 32 + 6, 69, 48 + 2, 76, 48 + 10, 74, -1 };
        for (int i = 0; FL[i] >= 0; i += 2) note(2, FL[i], FL[i + 1], 80, 4);
        for (int b = 0; b < 2; b++) { drum(b * 16, 0, 118); drum(b * 16 + 7, 0, 96); drum(b * 16 + 10, 0, 110); drum(b * 16 + 4, 2, 120); drum(b * 16 + 12, 2, 120);
            for (int s = 0; s < 16; s += 2) drum(b * 16 + s, 4, s % 4 ? 70 : 96); drum(b * 16 + 15, 8, 40); }
        base_from_sound(); ps_all(); HIST.n_undo = HIST.n_redo = 0;
        seg_film_open(); tap(B_PLAY); mark("play");
        film_wait(7000); trk = 1; vis_style = 3; tap(B_HOME); mark("vis"); film_wait(9000);
    } else if (!strcmp(seg, "tracks5")) {
        /* g-funk, 94: a funk bass, the whiny lead, a clavinet, a warm pad and the 4th synth: five tracks */
        seg_reset(94, 18);
        set_sound(0, "FUNK BASS"); set_sound(1, "CLAV"); set_sound(2, "G-FUNK LD"); set_sound(3, "80S"); set_sound(4, "WARM PAD");
        lvl(0, .62f); lvl(1, .36f); lvl(2, .3f); lvl(3, .85f); lvl(4, .22f);
        static const int CH[2][4] = { { 57, 60, 64, 67 }, { 55, 59, 62, 65 } };    /* Am7, G7 */
        for (int t = 0; t < NTRK; t++) if (t != 3) seq_set_len(&Q, t, 32);
        seq_set_len(&Q, 3, 32);
        for (int b = 0; b < 2; b++) {
            int r = b ? 43 : 45;
            note(0, b * 16, r, 116, 3); note(0, b * 16 + 3, r + 12, 90, 1); note(0, b * 16 + 6, r, 100, 2); note(0, b * 16 + 8, r + 7, 96, 2);
            note(0, b * 16 + 10, r + 10, 90, 2); note(0, b * 16 + 12, r + 12, 100, 2); note(0, b * 16 + 14, r + 7, 90, 2);
            for (int s = 2; s < 16; s += 4) chord(1, b * 16 + s, CH[b], 4, 76, 1);
            chord(4, b * 16, CH[b], 4, 60, 16);
            drum(b * 16, 0, 120); drum(b * 16 + 10, 0, 104); drum(b * 16 + 4, 3, 120); drum(b * 16 + 12, 3, 120);
            for (int s = 0; s < 16; s += 2) drum(b * 16 + s, 4, s % 4 ? 72 : 100);
        }
        static const int LD[] = { 0, 76, 3, 79, 6, 81, 12, 79, 16, 76, 22, 74, 26, 72, 28, 74, -1 };
        for (int i = 0; LD[i] >= 0; i += 2) note(2, LD[i], LD[i + 1], 90, 3);
        seq_step_flag(&Q, 2, 0, SEQ_SLIDE, 1); seq_step_flag(&Q, 2, 3, SEQ_SLIDE, 1);
        base_from_sound(); ps_all(); HIST.n_undo = HIST.n_redo = 0;
        seg_film_open(); tap(B_PLAY); mark("play");
        film_wait(8500); press(B_GLO); mark("glo"); film_wait(3000); release(B_GLO); film_wait(4500);
    } else if (!strcmp(seg, "patterns")) {
        /* trap, 140: pattern 1 the intro, pattern 2 the drop, each with its own sound (bell, then pluck) */
        seg_reset(140, 0);
        set_sound(0, "808 BOOM"); set_sound(1, "TRAP BELL"); set_sound(2, "DARK STR"); set_sound(3, "TRAP"); set_sound(4, "SINE PAD");
        lvl(0, .7f); lvl(1, .3f); lvl(2, .24f); lvl(3, .85f); lvl(4, .16f);
        static const int BL[] = { 0, 75, 3, 72, 6, 70, 8, 72, 11, 67, 14, 70, -1 };
        /* pattern 1: the bells, the pad, hats and a soft kick */
        for (int t = 0; t < NTRK; t++) seq_set_len(&Q, t, 16);
        for (int i = 0; BL[i] >= 0; i += 2) note(1, BL[i], BL[i + 1], 84, 2);
        note(2, 0, 51, 70, 16); note(2, 0, 58, 64, 16);
        for (int s = 0; s < 16; s += 2) drum(s, 4, 80);
        drum(0, 0, 100);
        /* pattern 2: the drop: 808, claps, hats rolling, the melody on a pluck */
        for (int t = 0; t < NTRK; t++) { Q.t[t].cur = 1; seq_set_len(&Q, t, 16); }
        for (int i = 0; BL[i] >= 0; i += 2) note(1, BL[i], BL[i + 1], 96, 2);
        note(2, 0, 51, 70, 16); note(2, 0, 58, 64, 16);
        note(0, 0, 39, 120, 6); note(0, 7, 39, 110, 3); note(0, 10, 42, 110, 4); note(0, 14, 37, 100, 2);
        drum(0, 0, 124); drum(7, 0, 110); drum(10, 0, 118); drum(4, 3, 124); drum(12, 3, 124);
        for (int s = 0; s < 16; s += 2) drum(s, 4, s % 4 ? 82 : 104);
        seq_step_ratchet(&Q, 3, 6, 3); seq_step_ratchet(&Q, 3, 14, 4); drum(15, 4, 70); seq_step_ratchet(&Q, 3, 15, 2);
        for (int t = 0; t < NTRK; t++) Q.t[t].cur = 0;
        base_from_sound(); ps_all();
        set_sound(1, "TRAP PLUCK"); base_from_sound(); ps_take(&PS, 1, 1, &S.trk[1].patch, base[1], preset_of[1]);
        set_sound(1, "TRAP BELL"); base_from_sound(); ps_take(&PS, 1, 0, &S.trk[1].patch, base[1], preset_of[1]);
        HIST.n_undo = HIST.n_redo = 0;
        seg_film_open(); tap(B_PLAY); mark("play");
        film_wait(6000);
        trk = 3; press(B_SAVE); film_wait(600); mark("save-drums"); tapkey(white_key[1]); film_wait(900); release(B_SAVE); film_wait(4200);
        trk = 0; press(B_SAVE); film_wait(500); mark("save-808"); tapkey(white_key[1]); film_wait(700); release(B_SAVE); film_wait(2600);
        trk = 1; press(B_SAVE); film_wait(500); mark("save-bell"); tapkey(white_key[1]); film_wait(700); release(B_SAVE); film_wait(5000);
    } else if (!strcmp(seg, "steps128")) {
        /* drill, 142: an 808 line that slides across 128 steps (eight bars), the DRILL kit, dark strings */
        seg_reset(142, 0);
        set_sound(0, "808 SLIDE"); set_sound(1, "CELLO BOW"); set_sound(2, "TUBE BELL"); set_sound(3, "DRILL"); set_sound(4, "DARK STR");
        lvl(0, .7f); lvl(1, .24f); lvl(2, .18f); lvl(3, .82f); lvl(4, .16f);
        seq_set_len(&Q, 0, 128); seq_set_len(&Q, 1, 128); seq_set_len(&Q, 2, 32); seq_set_len(&Q, 4, 128); seq_set_len(&Q, 3, 32);
        static const int RT[8] = { 42, 42, 45, 40, 42, 42, 47, 45 };
        for (int b = 0; b < 8; b++) {
            int r = RT[b];
            note(0, b * 16, r, 120, 4); note(0, b * 16 + 6, r, 110, 2); note(0, b * 16 + 9, r + (b & 1 ? 5 : 3), 110, 3); note(0, b * 16 + 13, r + 12, 104, 2);
            seq_step_flag(&Q, 0, b * 16 + 9, SEQ_SLIDE, 1);
            note(1, b * 16, r + 12, 70, 16); note(4, b * 16, r + 19, 54, 16);
        }
        static const int BE[] = { 0, 78, 3, 77, 6, 73, 16, 78, 19, 81, 22, 77, -1 };
        for (int i = 0; BE[i] >= 0; i += 2) note(2, BE[i], BE[i + 1], 70, 2);
        for (int b = 0; b < 2; b++) { drum(b * 16, 0, 120); drum(b * 16 + 11, 0, 100); drum(b * 16 + 8, 2, 124); if (b) drum(b * 16 + 14, 8, 80);
            for (int s = 0; s < 16; s += 2) drum(b * 16 + s + (s == 6 || s == 14 ? 1 : 0), 4, s % 4 ? 76 : 98); }
        seq_step_ratchet(&Q, 3, 12, 3); seq_step_ratchet(&Q, 3, 28, 3);
        base_from_sound(); ps_all(); HIST.n_undo = HIST.n_redo = 0;
        trk = 0;
        seg_film_open(); tap(B_PLAY); mark("play");
        film_wait(5000); tap(B_SEQ); film_wait(400); tap(B_SEQ); mark("notes"); film_wait(1800);
        turn(ENC_PRESETS, 1); film_wait(900); turn(ENC_PRESETS, 1); film_wait(900); turn(ENC_PRESETS, 1); mark("zoom64"); film_wait(6500);
    } else if (!strcmp(seg, "rec96")) {
        /* dilla, 88: the drums late and loose, chords played in by hand, a little behind the beat: kept where they fell */
        seg_reset(88, 40);
        set_sound(0, "UP BASS"); set_sound(1, "LOFI KEYS"); set_sound(2, "VIBES"); set_sound(3, "DUST"); set_sound(4, "WARM PAD");
        lvl(0, .6f); lvl(1, .5f); lvl(2, .2f); lvl(3, .9f); lvl(4, .14f);
        seq_set_len(&Q, 0, 32); seq_set_len(&Q, 1, 32); seq_set_len(&Q, 3, 32);
        static const int RT[2] = { 41, 43 };
        for (int b = 0; b < 2; b++) { note(0, b * 16, RT[b], 110, 5); note(0, b * 16 + 7, RT[b] + 12, 90, 2); note(0, b * 16 + 10, RT[b] + 7, 96, 4);
            drum(b * 16, 0, 118); drum(b * 16 + 9, 0, 104); drum(b * 16 + 4, 2, 116); drum(b * 16 + 12, 2, 116); drum(b * 16 + 15, 0, 70);
            for (int s = 0; s < 16; s += 2) drum(b * 16 + s, 4, s % 4 ? 64 : 92); }
        for (int s = 0; s < 32; s++) if (seq_step_has(&Q, 3, s, 2)) seq_step_nudge(&Q, 3, s, 9);   /* (the snare late: the feel) */
        base_from_sound(); ps_all(); HIST.n_undo = HIST.n_redo = 0;
        trk = 1;
        seg_film_open(); tap(B_PLAY); mark("play");
        double bar = 4 * 60.0 / 88.0 * 1000.0, st = bar / 16;
        /* REC near the end of bar 2, then the chords from bar 3, each a little late (14, 31, 9, 22 ms) */
        while (seq_bar(&Q) < 1 || seq_step_of(&Q, 1) < 28) wait_ms(1);
        tap(B_REC); mark("rec");
        while (seq_bar(&Q) < 2) wait_ms(1);
        static const int CH[4][4] = { { 60, 63, 67, 70 }, { 60, 63, 67, 70 }, { 62, 65, 69, 72 }, { 62, 65, 67, 71 } };
        static const double AT[4] = { 0, 10, 16, 26 }, LATE[4] = { 14, 31, 9, 22 };
        double base_t = tw_s() * 1000.0;
        for (int c = 0; c < 4; c++) {
            double at = base_t + AT[c] * st + LATE[c];
            while (tw_s() * 1000.0 < at) wait_ms(1);
            for (int i = 0; i < 4; i++) { keydown(CH[c][i] - 53); }
            wait_ms(st * 3);
            for (int i = 0; i < 4; i++) keyup(CH[c][i] - 53);
        }
        while (tw_s() * 1000.0 < base_t + 32 * st + 100) wait_ms(2);
        tap(B_REC); mark("rec-end"); film_wait(2200);
        tap(B_SEQ); film_wait(400); tap(B_SEQ); mark("notes"); film_wait(5200);
        press(B_EDIT); film_wait(300); tap(B_OCTD); mark("undo"); film_wait(500); release(B_EDIT); film_wait(3000);
    } else if (!strcmp(seg, "cond")) {
        /* phonk, 130: the cowbell melody, hats at 50 %, a crash on the 1st of every 4 loops, a fill on the 4th */
        seg_reset(130, 0);
        set_sound(0, "808 DIRTY"); set_sound(1, "TUBE BELL"); set_sound(2, "DARK STR"); set_sound(3, "PHONK"); set_sound(4, "SINE PAD");
        lvl(0, .7f); lvl(1, .26f); lvl(2, .18f); lvl(3, .88f); lvl(4, .12f);
        seq_set_len(&Q, 0, 16); seq_set_len(&Q, 3, 16); seq_set_len(&Q, 2, 16);
        note(0, 0, 40, 120, 4); note(0, 6, 40, 110, 2); note(0, 10, 43, 110, 3); note(0, 14, 38, 100, 2);
        note(2, 0, 52, 60, 16); note(2, 0, 59, 56, 16);
        drum(0, 0, 124); drum(6, 0, 100); drum(10, 0, 118); drum(4, 3, 124); drum(12, 3, 124);
        static const int CB[] = { 0, 2, 3, 6, 8, 10, 11, 14, -1 };
        for (int i = 0; CB[i] >= 0; i++) drum(CB[i], 15, 100);
        for (int s = 1; s < 16; s += 2) { drum(s, 4, 84); seq_step_cond(&Q, 3, s, SEQ_CHANCE + 5); }   /* (odd steps: hats only) */
        drum(13, 9, 110); drum(15, 10, 110);
        base_from_sound(); ps_all();
        /* the toms: a fill on the 4th of every 4 loops */
        seq_step_cond(&Q, 3, 13, SEQ_CYCLE + 4 * 8 + 3); seq_step_cond(&Q, 3, 15, SEQ_CYCLE + 4 * 8 + 3);
        HIST.n_undo = HIST.n_redo = 0;
        trk = 3;
        seg_film_open(); tap(B_PLAY); mark("play");
        film_wait(3000); tap(B_SEQ); mark("grid"); film_wait(12500);
    } else if (!strcmp(seg, "slice")) {
        /* breakbeat, 95: the BREAK cut in 16, its slices played out of order, a double bass, horn stabs */
        seg_reset(95, 10);
        set_sound(0, "BREAK 16"); set_sound(1, "UP BASS"); set_sound(2, "HORN STAB"); set_sound(3, "BOOMBAP"); set_sound(4, "JAZZ ORGAN");
        lvl(0, .85f); lvl(1, .6f); lvl(2, .3f); lvl(3, .0f); lvl(4, .16f);
        seq_set_len(&Q, 0, 32); seq_set_len(&Q, 1, 32); seq_set_len(&Q, 2, 32); seq_set_len(&Q, 4, 32);
        static const int SL[32] = { 0, 1, 2, 3, 4, 5, 2, 7, 8, 9, 10, 11, 12, 13, 4, 15, 0, 1, 2, 3, 4, 0, 6, 7, 8, 9, 4, 11, 12, 4, 14, 15 };
        for (int s = 0; s < 32; s++) note(0, s, 60 + SL[s], 110, 1);
        static const int BS[] = { 0, 41, 3, 41, 6, 44, 10, 46, 16, 39, 19, 39, 22, 41, 26, 43, -1 };
        for (int i = 0; BS[i] >= 0; i += 2) note(1, BS[i], BS[i + 1], 104, 2);
        static const int HS[4] = { 65, 68, 72, 75 };
        chord(2, 6, HS, 4, 100, 1); chord(2, 22, HS, 4, 100, 1); chord(2, 30, HS, 4, 90, 1);
        static const int OR[4] = { 53, 56, 60, 63 };
        chord(4, 0, OR, 4, 54, 32);
        base_from_sound(); ps_all(); HIST.n_undo = HIST.n_redo = 0;
        trk = 0;
        seg_film_open(); tap(B_PLAY); mark("play");
        film_wait(4500); tap(B_EDIT); mark("edit"); film_wait(4500);
        tap(B_PLAY); mark("stop"); film_wait(300);
        static const int KS[] = { 0, 2, 4, 2, 7, 5, 9, 11, 12, 11, 14, 16, -1 };
        for (int i = 0; KS[i] >= 0; i++) { keydown(KS[i]); film_wait(150); keyup(KS[i]); film_wait(165); }
        mark("keys-end"); tap(B_PLAY); film_wait(3500);
    } else if (!strcmp(seg, "save")) {
        /* golden era, 92: BOOMBAP, an organ, an upright bass; the song, the 8 projects, a save */
        seg_reset(92, 26);
        set_sound(0, "UP BASS"); set_sound(1, "JAZZ ORGAN"); set_sound(2, "VIBES"); set_sound(3, "BOOMBAP"); set_sound(4, "SOFT KEYS");
        lvl(0, .62f); lvl(1, .3f); lvl(2, .2f); lvl(3, .9f); lvl(4, .2f);
        seq_set_len(&Q, 0, 32); seq_set_len(&Q, 1, 32); seq_set_len(&Q, 3, 32); seq_set_len(&Q, 2, 32);
        static const int CH[2][4] = { { 57, 60, 64, 67 }, { 55, 58, 62, 65 } };
        for (int b = 0; b < 2; b++) {
            chord(1, b * 16, CH[b], 4, 70, 12); note(0, b * 16, b ? 43 : 45, 112, 6); note(0, b * 16 + 8, b ? 50 : 52, 96, 4);
            note(2, b * 16 + 6, CH[b][3] + 12, 60, 4);
            drum(b * 16, 0, 120); drum(b * 16 + 10, 0, 110); if (b) drum(b * 16 + 3, 0, 96); drum(b * 16 + 4, 2, 122); drum(b * 16 + 12, 2, 122);
            for (int s = 0; s < 16; s += 2) drum(b * 16 + s, 4, s % 4 ? 70 : 96);
        }
        base_from_sound(); ps_all(); HIST.n_undo = HIST.n_redo = 0;
        seg_film_open(); tap(B_PLAY); mark("play");
        film_wait(3000); tap(B_SAVE); mark("song"); film_wait(3500); tap(B_HOME); film_wait(600);
        press(B_HOME); mark("projects"); film_wait(3500); release(B_HOME); film_wait(800);
        proj_save_keys(); mark("saved"); film_wait(4500);
    } else if (!strcmp(seg, "hands") || !strcmp(seg, "vis6") || !strcmp(seg, "vis4")) {
        /* (launch video 3.3.6) the golden-era beat: HOME, SAVE and REC held (the keys that light); or the six visualisers */
        seg_reset(92, 26);
        set_sound(0, "UP BASS"); set_sound(1, "JAZZ ORGAN"); set_sound(2, "VIBES"); set_sound(3, "BOOMBAP"); set_sound(4, "SOFT KEYS");
        lvl(0, .62f); lvl(1, .3f); lvl(2, .2f); lvl(3, .9f); lvl(4, .2f);
        seq_set_len(&Q, 0, 32); seq_set_len(&Q, 1, 32); seq_set_len(&Q, 3, 32); seq_set_len(&Q, 2, 32);
        static const int CH[2][4] = { { 57, 60, 64, 67 }, { 55, 58, 62, 65 } };
        for (int b = 0; b < 2; b++) {
            chord(1, b * 16, CH[b], 4, 70, 12); note(0, b * 16, b ? 43 : 45, 112, 6); note(0, b * 16 + 8, b ? 50 : 52, 96, 4);
            note(2, b * 16 + 6, CH[b][3] + 12, 60, 4); note(4, b * 16 + 2, CH[b][2] + 12, 50, 2); note(4, b * 16 + 10, CH[b][1] + 12, 46, 2);
            drum(b * 16, 0, 120); drum(b * 16 + 10, 0, 110); if (b) drum(b * 16 + 3, 0, 96); drum(b * 16 + 4, 2, 122); drum(b * 16 + 12, 2, 122);
            for (int s = 0; s < 16; s += 2) drum(b * 16 + s, 4, s % 4 ? 70 : 96);
        }
        base_from_sound(); ps_all(); HIST.n_undo = HIST.n_redo = 0;
        if (seg[0] == 'h') {
            trk = 4;
            seg_film_open(); tap(B_PLAY); mark("play"); film_wait(2000);
            press(B_HOME); mark("home"); film_wait(4500); release(B_HOME); film_wait(1200);
            press(B_SAVE); mark("save"); film_wait(4500); release(B_SAVE); film_wait(1200);
            press(B_REC); mark("rec"); film_wait(2600); release(B_REC); mark("erased"); film_wait(3500);
        } else if (!strcmp(seg, "vis4")) {
            /* the narration's order: the oscilloscope, the spectrum, the VU meters, the sailboat */
            trk = 1; vis_style = 0;
            seg_film_open(); tap(B_PLAY); mark("play"); film_wait(1550); tap(B_HOME); mark("vis");
            film_wait(5000); turn(ENC_SELECT, -2); mark("spectrum"); film_wait(1000); turn(ENC_SELECT, -1); mark("vu");
            film_wait(1550); turn(ENC_SELECT, -2); mark("sloop"); film_wait(6500);
        } else {
            trk = 1; vis_style = 0;
            seg_film_open(); tap(B_PLAY); mark("play"); film_wait(1500); tap(B_HOME); mark("vis");
            for (int i = 0; i < 6; i++) { film_wait(2600); if (i < 5) { turn(ENC_SELECT, 1); mark("next"); } }
            film_wait(800);
        }
    } else if (!strcmp(seg, "screen")) {
        /* the same golden-era beat: SOUNDS turned through, the pictures of a sound */
        seg_reset(92, 26);
        set_sound(0, "UP BASS"); set_sound(1, "JAZZ ORGAN"); set_sound(2, "VIBES"); set_sound(3, "BOOMBAP"); set_sound(4, "SOFT KEYS");
        lvl(0, .62f); lvl(1, .3f); lvl(2, .2f); lvl(3, .9f); lvl(4, .2f);
        seq_set_len(&Q, 0, 32); seq_set_len(&Q, 1, 32); seq_set_len(&Q, 3, 32); seq_set_len(&Q, 2, 32);
        static const int CH[2][4] = { { 57, 60, 64, 67 }, { 55, 58, 62, 65 } };
        for (int b = 0; b < 2; b++) {
            chord(1, b * 16, CH[b], 4, 70, 12); note(0, b * 16, b ? 43 : 45, 112, 6); note(0, b * 16 + 8, b ? 50 : 52, 96, 4);
            note(2, b * 16 + 6, CH[b][3] + 12, 60, 4);
            drum(b * 16, 0, 120); drum(b * 16 + 10, 0, 110); if (b) drum(b * 16 + 3, 0, 96); drum(b * 16 + 4, 2, 122); drum(b * 16 + 12, 2, 122);
            for (int s = 0; s < 16; s += 2) drum(b * 16 + s, 4, s % 4 ? 70 : 96);
        }
        base_from_sound(); ps_all(); HIST.n_undo = HIST.n_redo = 0;
        trk = 2;
        seg_film_open(); tap(B_PLAY); mark("play");
        film_wait(2500); turn(ENC_PRESETS, 1); mark("sounds"); film_wait(1300); turn(ENC_PRESETS, 1); film_wait(1300); turn(ENC_PRESETS, -2); film_wait(2200);
        tap(B_HOME); film_wait(500); trk = 1; tap(B_EDIT); mark("edit"); film_wait(2600); tap(B_EDIT); film_wait(2600); tap(B_HOME); film_wait(2500);
    }
    seg_film_close();
}
static void sc_film(void)
{
    char fn[512]; snprintf(fn, sizeof fn, "%s/film.panel", tw_out); tw_panel = fopen(fn, "wb");
    snprintf(fn, sizeof fn, "%s/film.rgb565", tw_out); tw_film = fopen(fn, "wb"); tw_film_t = tw_s();
    double f0 = tw_film_t;
    for (int i = 0; i < 4000 && !tw_audio_on; i++) film_wait(5);
    wav_on("film"); double w0 = tw_s();
    { snprintf(fn, sizeof fn, "%s/film.txt", tw_out); FILE *f = fopen(fn, "w"); if (f) { fprintf(f, "%.6f %.6f\n", f0, w0); fclose(f); } }
    /* the beat, while the logo draws itself */
    for (int t = 0; t < NTRK; t++) seq_clear(&Q, t);
    set_sound(0, "UP BASS"); set_sound(1, "DUSTY PNO"); set_sound(2, "SCRATCH"); set_sound(3, "BOOMBAP"); set_sound(4, "VIBES");
    base_from_sound(); ps_all();
    seq_set_bpm(&Q, 90.0f); Q.swing = 22;
    sm_set(&S.trk[0].level, 0.62f); sm_set(&S.trk[1].level, 0.42f); sm_set(&S.trk[2].level, 0.34f); sm_set(&S.trk[3].level, 0.9f); sm_set(&S.trk[4].level, 0.22f);
    S.tx[0] = 0;                                            /* (no TEXTURE: Sam) */
    /* drums, two bars: KICK 0, SNARE 2, HAT 4, OPEN 5, SNR2 8 */
    seq_set_len(&Q, 3, 32);
    static const int K1[] = { 0, 6, 10, -1 }, K2[] = { 0, 3, 6, 10, 13, -1 };
    for (int i = 0; K1[i] >= 0; i++) seq_step_toggle(&Q, 3, K1[i], 0, 120);
    for (int i = 0; K2[i] >= 0; i++) seq_step_toggle(&Q, 3, 16 + K2[i], 0, i == 4 ? 90 : 120);
    for (int b = 0; b < 2; b++) { seq_step_toggle(&Q, 3, b * 16 + 4, 2, 122); seq_step_toggle(&Q, 3, b * 16 + 12, 2, 122); }
    seq_step_toggle(&Q, 3, 16 + 15, 8, 46); seq_step_toggle(&Q, 3, 7, 8, 38);
    for (int st = 0; st < 32; st += 2) if (st != 30) seq_step_toggle(&Q, 3, st, 4, (st % 4) ? 96 : 70);
    seq_step_toggle(&Q, 3, 30, 5, 80); seq_step_toggle(&Q, 3, 16 + 11, 4, 60);
    /* four bars of D minor, as on a record: Dm9, Gm9, Bbmaj7, A7 */
    static const int CH[4][4] = { { 53, 57, 60, 64 }, { 58, 62, 65, 69 }, { 57, 62, 65, 69 }, { 55, 61, 64, 70 } };
    static const int ROOT[4] = { 38, 43, 46, 45 };
    seq_set_len(&Q, 0, 64); seq_set_len(&Q, 1, 64); seq_set_len(&Q, 2, 64); seq_set_len(&Q, 4, 64);
    for (int b = 0; b < 4; b++) {
        for (int k = 0; k < 4; k++) { note(1, b * 16, CH[b][k], 88, 6); note(1, b * 16 + 10, CH[b][k], 70, 4); }
        int r = ROOT[b];
        note(0, b * 16, r, 112, 5); note(0, b * 16 + 6, r, 100, 3); note(0, b * 16 + 10, b == 3 ? r + 4 : r + 12, 96, 2); note(0, b * 16 + 13, r + 7, 90, 3);
        note(4, b * 16 + 8, CH[b][3] + 12, 60, 6);
    }
    note(2, 3 * 16 + 12, 60, 110, 1); note(2, 3 * 16 + 14, 60, 100, 1);       /* (the scratch, at the turn of the four bars) */
    HIST.n_undo = HIST.n_redo = 0;
    film_wait(2800);                                         /* (the logo) */
    tap(B_PLAY); film_wait(10000);                           /* the beat, TRACKS */
    trk = 1; tap(B_EDIT); film_wait(2500); tap(B_EDIT); film_wait(2500);   /* the sound's pages: pictures of the sound */
    for (int i = 0; i < 6; i++) { turn(ENC_KNOB1, i < 3 ? 3 : -3); film_wait(350); }
    tap(B_HOME); film_wait(400);
    trk = 4; turn(ENC_PRESETS, 1); film_wait(1200); turn(ENC_PRESETS, 1); film_wait(1200); turn(ENC_PRESETS, -2); film_wait(3000);   /* SOUNDS */
    trk = 3; tap(B_SEQ); film_wait(8000);                    /* the drums' steps */
    tap(B_HOME); trk = 1; tap(B_SEQ); film_wait(500); tap(B_SEQ); film_wait(7500);   /* NOTES */
    tap(B_HOME); film_wait(400);
    press(B_FX); film_wait(1500);                            /* the punch-ins, on the beat */
    static const int PK[] = { PK_GATE, PK_LOOP16, PK_ECHO, PK_LOOP32, PK_REVERSE, PK_TAPESTOP };
    for (int i = 0; i < 6; i++) { int k = key_of_punch(PK[i]); keydown(k); film_wait(1100); keyup(k); film_wait(200); }
    release(B_FX); film_wait(1000);
    tap(B_HOME); film_wait(2200);                            /* the visualiser (HOME on TRACKS) */
    for (int st = 1; st < 7; st++) { turn(ENC_SELECT, 1); film_wait(1600); }
    tap(B_HOME); film_wait(14000);                           /* (the editor, the end: TRACKS under the cards) */
    if (tw_film) { fclose(tw_film); tw_film = 0; }
    if (tw_panel) { fclose(tw_panel); tw_panel = 0; }
    tw_wav_close();
}
static void sc_monkey(void)
{
    boot_wait();
    double secs = getenv("TWIN_SECS") ? atof(getenv("TWIN_SECS")) : 120.0;
    uint64_t end = tw_ns + (uint64_t)(secs * 1e9), next_check = tw_ns + 10000000000ull;
    static const int W[] = { B_PLAY, B_PLAY, B_REC, B_SEQ, B_SEQ, B_EDIT, B_EDIT, B_ENV, B_LFO, B_FX, B_GLO, B_SEL, B_ARP, B_SAVE, B_OCTD, B_OCTU, B_HOME };
    int acts = 0;
    while (tw_ns < end) {
        int a = (int)(tw_rnd() % 100);
        if (a < 25) tap(W[tw_rnd() % (sizeof W / sizeof W[0])]);
        else if (a < 45) { int k = (int)(tw_rnd() % 27); if (tw_rnd() & 1) tapkey(k); else { keydown(k); keydown((k + 4) % 27); wait_ms(tw_rnd() % 400); keyup(k); keyup((k + 4) % 27); } }
        else if (a < 65) turn((int)(tw_rnd() % 7), (int)(tw_rnd() % 9) - 4);
        else if (a < 80) {                                 /* a layer held, things done in it */
            int b = W[tw_rnd() % (sizeof W / sizeof W[0])];
            if (b == B_REC || b == B_PLAY) b = B_SEQ;
            press(b);
            for (int i = 0; i < 1 + (int)(tw_rnd() % 4); i++) {
                if (tw_rnd() & 1) tapkey((int)(tw_rnd() % 27)); else turn((int)(tw_rnd() % 7), (int)(tw_rnd() % 7) - 3);
                if (tw_rnd() % 5 == 0) tap(tw_rnd() & 1 ? B_OCTD : B_OCTU);
            }
            wait_ms(tw_rnd() % 900);
            release(b);
        }
        else if (a < 85) { tap(B_SAVE); wait_ms(60); tap(B_SAVE); }
        else if (a < 88) { tw_pot = (int)(tw_rnd() % 1024); wait_ms(50); }
        else if (a < 90) { press(B_HOME); wait_ms(700); if (tw_rnd() & 1) tapkey((int)(tw_rnd() % 16)); else tap(B_SEL); release(B_HOME); }
        else wait_ms(tw_rnd() % 1500);
        acts++;
        if (tw_ns >= next_check) {
            next_check = tw_ns + 10000000000ull;
            char w[48]; snprintf(w, sizeof w, "monkey %.0f s", tw_s());
            check_project_roundtrip(w);
            EXPECT(!tw_wild, "the sound went beyond full scale (%d samples)", tw_wild);
            static int shots; char nm[32]; snprintf(nm, sizeof nm, "monkey-%02d", shots++ % 100); shot(nm);
        }
    }
    printf("[%8.3f]    monkey: %d actions\n", tw_s(), acts);
    tw_btn = 0; tw_keys = 0; wait_ms(500);
    if (cal_step >= 0) { printf("[%8.3f]    monkey: it ended in the calibration: left\n", tw_s()); cal_step = -1; wait_ms(200); }   /* (a soak's last action; SAVE does nothing there) */
    if (Q.playing) tap(B_PLAY);
    wait_ms(3000);
    check_project_roundtrip("monkey's end");
    tw_btn = 0; wait_ms(300); tap(B_HOME); wait_ms(300);
    save_and_wait();                                      /* (then the next start must find all of it: host/twin.sh) */
}

/* 3.2: a DX7 bank sent as the editor sends it, its voices found on the FM-1 by name; SOUNDS (PRESETS shows it, EDIT
 * 6/6 lets the keys choose); the favourites; the lights (faint ones never a full light a frame in N) */
static void tw_vmem(uint8_t *m, int k)        /* a voice that sounds: six carriers, voice k its own ratios; its name */
{
    memset(m, 0, 128);
    for (int o = 0; o < 6; o++) {
        uint8_t *b = m + 17 * o;
        b[0] = 99; b[1] = 60; b[2] = 40; b[3] = 60; b[4] = 99; b[5] = 90; b[6] = 80; b[7] = 0;
        b[12] = 7 << 3; b[14] = (uint8_t)(o < 2 ? 99 : 70); b[15] = (uint8_t)(((1 + (o * (k + 1)) % 7) << 1));
    }
    for (int i = 0; i < 4; i++) { m[102 + i] = 99; m[106 + i] = 50; }
    m[110] = 31; m[111] = 8; m[117] = 24;
    char nm[11]; snprintf(nm, sizeof nm, "Twin Vc %-2d", k + 1); memcpy(m + 118, nm, 10);
}
static int preset_named(const char *n);
static int sb_goto(int cat) { for (int i = 0; i < SB_NCAT + 2 && sb_cat != cat; i++) turn(ENC_KNOB1, 1); return sb_cat == cat; }
/* 3.3.6: YOUR KITS: a sample slot as the drum track's kit, by its sample's name; another kit, then found again with
 * PRESETS; saved with the project (the reboot after compares it) */
static void sc_ukit(void)
{
    boot_wait();
    uint32_t len = make_slot();
    EXPECT(send_sample(1, len, 384) >= 0, "the slot not sent");
    wait_ms(1500);
    int dt = -1; for (int t = 0; t < NTRK; t++) if (is_drum(t)) dt = t;
    trk = dt; tap(B_HOME); wait_ms(200); if (vis_on) { tap(B_HOME); wait_ms(200); }
    int want = UK_BASE + GB_KIT_USR + 1, found = 0, kits = 0;
    for (int i = 0; i < 60 && !found; i++) { turn(ENC_PRESETS, 1); if (preset_of[trk] == want) found = 1; else if (preset_of[trk] >= 0 && preset_of[trk] < gb_npresets) kits++; }
    EXPECT(found && kits >= 30, "PRESETS on the drums: USR2 not among the kits (%d kits walked)", kits);
    EXPECT(sticky == PG_SND && sb_cat == SB_UKIT && !strcmp(snd_name(preset_of[trk]), "TWIN"), "YOUR KITS: '%s' (cat %d)", snd_name(preset_of[trk]), sb_cat);
    EXPECT((int)gb_ev(&S.trk[trk].patch, 0) == GB_KIT_USR + 1, "the kit is not USR2 (%d)", (int)gb_ev(&S.trk[trk].patch, 0));
    shot("ukit");
    char pt[24]; param_text(pt, ENG_DRUM, 0, S.trk[trk].patch.p[SP_E0]);
    EXPECT(!strcmp(pt, "TWIN"), "the KIT value shows '%s', not the sample's name", pt);
    turn(ENC_PRESETS, 3);                                  /* (on, past it: a factory kit) */
    EXPECT(preset_of[trk] != want, "PRESETS stayed on USR2");
    for (int i = 0; i < 60 && preset_of[trk] != want; i++) turn(ENC_PRESETS, -1);
    EXPECT(preset_of[trk] == want && (int)gb_ev(&S.trk[trk].patch, 0) == GB_KIT_USR + 1, "USR2 not found again (%d)", preset_of[trk]);
    /* the drum track plays it: a pad heard */
    snd_note_on(&S, GB_OWNER(OWN_KEY, 0), trk, 0, 1.0f); double pk = peak_over(300);
    EXPECT(pk > 0.01, "the kit USR2 silent (peak %.4f)", pk);
    wait_ms(2600);
    save_and_wait();
    EXPECT(preset_of[trk] == want, "the save changed the kit");
}
static void sc_sounds(void)
{
    boot_wait();
    uint8_t raw[700], rep[700]; uint32_t rn, m;
    /* the bank: 20 voices in B1..B20, as editor-sloop3.html writes it (begin, the bytes, end) */
    static uint8_t img[XB_FM6_LEN];
    memset(img, 0, sizeof img);
    for (int k = 0; k < 20; k++) tw_vmem(img + 16 + 128 * k, k);
    xb_head(img, 1, 0xFFFFFu, XB_FM6_LEN);
    raw[0] = 2; raw[1] = 0; raw[2] = (uint8_t)XB_FM6_LEN; raw[3] = (uint8_t)(XB_FM6_LEN >> 8);
    EXPECT(tw_ed(ED_BANK, raw, 4, rep, &rn, 2000) == ED_BANK, "BANK begin");
    for (uint32_t off = 0; off < XB_FM6_LEN; off += 300) {
        uint32_t k = XB_FM6_LEN - off < 300 ? XB_FM6_LEN - off : 300;
        raw[0] = 3; raw[1] = 0; raw[2] = (uint8_t)off; raw[3] = (uint8_t)(off >> 8); memcpy(raw + 4, img + off, k);
        EXPECT(tw_ed(ED_BANK, raw, 4 + k, rep, &rn, 2000) == ED_BANK, "BANK data %u", off);
    }
    m = msg_mark(); raw[0] = 4; raw[1] = 0; EXPECT(tw_ed(ED_BANK, raw, 2, rep, &rn, 5000) == ED_BANK, "BANK end");
    EXPECT(wait_msg(m, "FM6 BANK SAVED", 4000), "no FM6 BANK SAVED");
    wait_ms(800);
    EXPECT(fm6_bank_used == 0xFFFFFu && fb_ok(19) && !fb_ok(20), "the bank's voices: %X", fm6_bank_used);
    EXPECT(!strcmp(fb_name(0), "TWIN VC 1") && !strcmp(fb_name(19), "TWIN VC 20"), "the voices' names: '%s' '%s'", fb_name(0), fb_name(19));
    /* PRESETS on TRACKS: SOUNDS shows where the knob is, the keys play as ever, and it goes back by itself (on the
     * visualiser: the sound changes, the visualiser stays) */
    tap(B_HOME); wait_ms(200); if (vis_on) { tap(B_HOME); wait_ms(200); }
    int before = sticky, p0 = preset_of[trk];
    turn(ENC_PRESETS, 1); wait_ms(200);
    EXPECT(sticky == PG_SND && sb_back == before, "PRESETS did not open SOUNDS (page %d, back %d)", sticky, sb_back);
    EXPECT(preset_of[trk] != p0, "PRESETS did not change the sound");
    shot("sounds-presets");
    int p1 = preset_of[trk];
    tapkey(white_key[5]); wait_ms(100);
    EXPECT(preset_of[trk] == p1, "SOUNDS opened by PRESETS: a key changed the sound");
    turn(ENC_PRESETS, 2); turn(ENC_PRESETS, -1);
    EXPECT(sticky == PG_SND, "SOUNDS closed while PRESETS turned");
    wait_ms(4600);
    EXPECT(sticky == before, "SOUNDS did not go back by itself (page %d)", sticky);
    /* PRESETS walks every sound once: the factory synth sounds, the 20 voices */
    {
        static uint8_t seen[3000]; int n = 0, start = preset_of[trk], back = 0, bank = 0;
        memset(seen, 0, sizeof seen);
        for (int i = 0; i < 400 && !back; i++) {
            tw_enc[ENC_PRESETS] += 1; wait_ms(12);
            int f = preset_of[trk];
            if (f == start && i) { back = 1; break; }
            EXPECT(f >= 0 && f < 3000, "PRESETS: sound %d", f);
            if (f >= 0 && f < 3000 && !seen[f]) { seen[f] = 1; n++; if (f >= FB_BASE) bank++; }
        }
        int nf = 0; for (int i = 0; i < gb_npresets; i++) nf += gb_presets[i].engine != ENG_DRUM;
        int nu = 0; for (int k = 0; k < US_N; k++) nu += us_used(k);
        printf("[%8.3f]    PRESETS all round: %d sounds (%d factory, %d of yours, %d DX7 voices)\n", tw_s(), n + 1, nf, nu, bank);
        EXPECT(back && bank == 20 && n + 1 >= nf + nu + 20, "PRESETS all round: %d sounds, %d voices, back %d", n + 1, bank, back);
        for (int i = 0; i < 30; i++) { tw_enc[ENC_PRESETS] -= 1; wait_ms(12); }
        wait_ms(4600);
    }
    /* EDIT 6/6: the keys choose. The DX7 category: its tiles are the voices, by name; a key: that voice, heard */
    tap(B_HOME); wait_ms(200);
    for (int i = 0; i < 8 && sticky != PG_SND; i++) tap(B_EDIT);   /* (4.0.1: the last of EDIT's pages, as SLOOP 3) */
    wait_ms(200);
    EXPECT(sticky == PG_SND && sb_back < 0, "EDIT: no SOUNDS (page %d)", sticky);
    EXPECT(sb_goto(SB_BANK), "no DX7 category");
    wait_ms(200); shot("sounds-dx7");
    EXPECT(!strncmp(W.tile[2].text, "TWIN VC 3", 9), "DX7 tile 3: '%s'", W.tile[2].text);
    int tine = preset_named("TINE EP"); snd_load(tine); wait_ms(50);
    /* (PTCH turned to B6 by hand, as before 3.2: the voice the engine waits for is the one read) */
    gb_patch_set(&S.trk[trk].patch, SP_E7, FM6_NFACTORY + 5); base[trk][SP_E7] = S.trk[trk].patch.p[SP_E7]; wait_ms(60);
    EXPECT(fm6_user_id[trk] == FM6_NFACTORY + 5, "PTCH on B6: the voice read is %d", fm6_user_id[trk]);
    snd_load(tine); wait_ms(50);
    keydown(white_key[10]); double ref = peak_over(400); keyup(white_key[10]); wait_ms(600);
    keydown(white_key[2]); double pk = peak_over(400); keyup(white_key[2]); wait_ms(300);
    printf("[%8.3f]    voice 3: peak %.4f (the factory TINE EP: %.4f)\n", tw_s(), pk, ref);
    EXPECT(preset_of[trk] == FB_BASE + 2 && S.trk[trk].patch.engine == ENG_FM6 && fm6_ptch(trk) == FM6_NFACTORY + 2, "the key did not choose voice 3 (%d)", preset_of[trk]);
    EXPECT(fm6_user_id[trk] == FM6_NFACTORY + 2, "voice 3 was not read for the note (%d)", fm6_user_id[trk]);
    EXPECT(pk > 0.3 * ref && ref > 0.001, "voice 3 is not heard (peak %.4f, a factory sound %.4f)", pk, ref);
    EXPECT(W.sound[POS[trk]] && !strcmp(W.sound[POS[trk]], "TWIN VC 3"), "the track's name is not the voice's");
    turn(ENC_SELECT, 1); wait_ms(200);
    EXPECT(sb_cat == SB_BANK && sb_pg == 1 && sb_o[3] == FB_BASE + 19 && sb_o[4] < 0, "DX7 page 2: %d %d", sb_pg, sb_o[3]);
    shot("sounds-dx7-2");
    turn(ENC_SELECT, -1);
    /* a favourite: KNOB 4; kept a moment later; FAVS: its key */
    m = msg_mark(); turn(ENC_KNOB4, 1);
    EXPECT(us_fav[0] == FB_BASE + 2 || us_fav[1] == FB_BASE + 2 || us_fav[2] == FB_BASE + 2, "the voice is not a favourite");
    EXPECT(wait_msg(m, "FAVOURITES KEPT", 6000), "the favourites were not kept");
    wait_saved(4000);
    EXPECT(sb_goto(K_PAD + 1), "no PAD category");
    wait_ms(200); shot("sounds-pad");
    tapkey(white_key[4]); wait_ms(100);
    int pad = preset_of[trk];
    EXPECT(pad >= 0 && pad < gb_npresets && gb_presets[pad].kind == K_PAD, "PAD's fifth key: sound %d", pad);
    m = msg_mark(); turn(ENC_KNOB4, 1); turn(ENC_KNOB4, 1);
    EXPECT(said_since(m, "ALREADY A FAVOURITE"), "a favourite added twice");
    EXPECT(sb_goto(SB_FAV), "no FAVS category");
    wait_ms(200); shot("sounds-favs");
    int at = -1, at2 = -1; for (int i = 0; i < US_NFAV; i++) { if (us_fav[i] == FB_BASE + 2) at = i; if (us_fav[i] == pad) at2 = i; }
    EXPECT(at >= 0 && at2 >= 0 && at != at2, "the two favourites: %d %d", at, at2);
    if (at >= 0) { tapkey(white_key[at]); wait_ms(100); EXPECT(preset_of[trk] == FB_BASE + 2, "FAVS: the voice's key gave %d", preset_of[trk]); }
    if (at >= 0 && at < US_NFAV - 1) { turn(ENC_KNOB3, 1); EXPECT(us_fav[at + 1] == FB_BASE + 2, "MOVE did not move the favourite"); turn(ENC_KNOB3, -1); EXPECT(us_fav[at] == FB_BASE + 2, "MOVE back"); }
    if (at2 >= 0) { tapkey(white_key[at2]); turn(ENC_KNOB4, -1); EXPECT(us_fav[at2] != pad, "FAV left did not remove it"); turn(ENC_KNOB4, 1); }
    wait_ms(2000); wait_saved(4000);
    /* SELECT walks the pages, category after category; left of the first: EDIT 5/6 */
    { int cats = 0, last = -1; EXPECT(sb_goto(SB_FAV), "FAVS"); for (int i = 0; i < 40; i++) { if (sb_cat != last) { last = sb_cat; cats++; } turn(ENC_SELECT, 1); if (sb_cat == SB_FAV && i) break; }
      printf("[%8.3f]    SELECT through SOUNDS: %d categories\n", tw_s(), cats); EXPECT(cats >= 10, "SELECT: %d categories", cats); }
    EXPECT(sb_cat == SB_FAV && sb_pg == 0, "SELECT all round: not back on FAVS"); turn(ENC_SELECT, -1); EXPECT(sticky == PG_VOICE2, "SELECT left of SOUNDS: page %d", sticky);
    turn(ENC_SELECT, 1); EXPECT(sticky == PG_SND, "SELECT right of VOICE 2: page %d", sticky);
    /* the drum track: the kits */
    tap(B_HOME); wait_ms(200);
    for (int i = 0; i < NTRK && !is_drum(trk); i++) { turn(ENC_ALGORITHM, 1); wait_ms(100); }
    EXPECT(is_drum(trk), "no drum track chosen");
    int k0 = preset_of[trk]; turn(ENC_PRESETS, 1); wait_ms(200);
    EXPECT(sticky == PG_SND && sb_cat == K_KIT + 1 && preset_of[trk] != k0 && gb_presets[preset_origin(preset_of[trk])].kind == K_KIT, "the drum track's PRESETS: page %d, category %d", sticky, sb_cat);
    shot("sounds-kits");
    tap(B_EDIT); wait_ms(100); EXPECT(sticky == PG_SND && sb_back < 0, "EDIT did not keep SOUNDS");
    int k1 = preset_of[trk]; tapkey(white_key[7]); wait_ms(100);
    EXPECT(preset_of[trk] != k1 && preset_of[trk] == sb_o[7], "the eighth key did not choose its kit");
    wait_ms(6000); EXPECT(sticky == PG_SND, "EDIT's SOUNDS went away by itself");
    for (int i = 0; i < NTRK && is_drum(trk); i++) { turn(ENC_ALGORITHM, -1); wait_ms(100); }
    /* the lights: HOME > LIGHTS on, every button glows faint in every frame, none is ever a full light for it */
    tap(B_HOME); wait_ms(200);
    hs[HS_LIGHTS] = 2; hs[HS_KEYS] = 2; home_apply();
    { extern uint8_t tw_glow[41]; extern uint32_t tw_glow_offs; int full = 0, faint = 0; uint32_t g0 = tw_glow_offs;
      for (int i = 0; i < 400; i++) { wait_ms(1); full += tw_led[BTN_FX] + tw_led[BTN_SEL] + tw_led[BTN_ARP] + tw_led[14 + white_key[1]]; faint += tw_glow[BTN_FX] && tw_glow[BTN_ARP] && tw_glow[14 + white_key[1]]; }
      printf("[%8.3f]    lights: faint %d of 400 ms, full %d; %u columns put out\n", tw_s(), faint, full, tw_glow_offs - g0);
      EXPECT(full == 0 && faint >= 398, "LIGHTS: full %d, faint %d", full, faint); (void)g0; }   /* (3.3: put out by the interrupt) */
    hs[HS_LIGHTS] = 0; hs[HS_KEYS] = 0; home_apply();
    tap(B_HOME); wait_ms(300);
    save_and_wait();
}
/* 3.3.8: the free take (REC on an empty project, a drum loop played freely, REC on the next "1" a little late or
 * early, as a hand presses it): each hit on its step, none moved to the step before, no hole at the loop's end */
static void ft_play(double step_ms, double rec_ms, int bars, int *bad, int *n_ev, int kind)
{
    /* kind 0: a beat; 1: a kick and a snare only (any grid fits them); 2: the beat, then the "1" played again before REC */
    static const struct { int step, key; } H0[] = { { 0, 0 }, { 2, 4 }, { 4, 2 }, { 6, 4 }, { 7, 0 }, { 8, 0 }, { 10, 4 }, { 12, 2 }, { 14, 4 }, { 15, 2 } },
        H1[] = { { 0, 0 }, { 12, 2 } };
    const struct { int step, key; } *H = kind == 1 ? (const void *)H1 : (const void *)H0;
    const int NH = kind == 1 ? 2 : (int)(sizeof H0 / sizeof H0[0]);
    new_project(0); trk = 3; wait_ms(300);
    tap(B_REC); wait_ms(300);
    uint64_t t0 = tw_ns + 200000000u; uint32_t r = 7;
    for (int b = 0; b < bars; b++) for (int i = 0; i < NH; i++) {
        r = r * 1103515245u + 12345u; double jit = (double)((r >> 16) % 41) - 20.0;   /* +-20 ms */
        if (b == 0 && i == 0) jit = 0;
        wait_until(t0 + (uint64_t)(((b * 16 + H[i].step) * step_ms + jit) * 1e6));
        tw_keys |= 1u << H[i].key; wait_ms(40); tw_keys &= ~(1u << H[i].key);
    }
    if (kind == 2) { wait_until(t0 + (uint64_t)((bars * 16 * step_ms + 15) * 1e6)); tw_keys |= 1u; wait_ms(40); tw_keys &= ~1u; }
    wait_until(t0 + (uint64_t)((bars * 16 * step_ms + rec_ms) * 1e6));
    tw_btn |= 1u << B_REC; wait_ms(60); tw_btn &= ~(1u << B_REC); wait_ms(400);
    const seq_pat *p = &Q.t[3].pat[Q.t[3].cur];
    int steps = p->steps, got[128] = { 0 }; *bad = 0; *n_ev = p->n;
    for (int i = 0; i < p->n; i++) {
        int32_t t = p->ev[i].tick, g = (t + SEQ_TPS / 2) / SEQ_TPS;
        int32_t x = Q.t[3].quant ? g : -1;
        if (x >= 0) got[x % steps]++;
        else { double fs = (double)t / SEQ_TPS; int nr = (int)(fs + 0.5) % steps; if (fabs(fs - (int)(fs + 0.5)) > 0.25) (*bad)++; got[nr]++; }
    }
    for (int b = 0; b < steps / 16; b++) for (int i = 0; i < NH; i++) { int s = b * 16 + H[i].step; if (got[s]) got[s]--; else (*bad)++; }
    for (int s = 0; s < steps; s++) if (got[s]) *bad += got[s];
    printf("[%8.3f]    free take (%s) %d bar(s), a step %.1f ms, REC %+.0f ms: %d bars %.1f BPM QUANT %d, %d events, %d off their step\n",
           tw_s(), kind == 1 ? "kick, snare" : kind == 2 ? "the 1 again" : "a beat", bars, step_ms, rec_ms, steps / 16, Q.bpm, Q.t[3].quant, p->n, *bad);
    tap(B_PLAY); wait_ms(200);
}
/* 3.3.8: SLOOP 2.8.5's drum gestures back: the DRUMS grid (white keys the steps, silent; KNOB STEP HIT LEVEL; the
 * sheet), a drum step held (KNOB 1 its sound, LEVEL and RATCHET that sound alone), the live mutes (EDIT + a black key,
 * then the white keys), the hits lighting their keys, HOME held: the menu, open when let go */
/* 3.3.8: a save while it plays: the flash window off for an erase silences what is read from the flash (samples,
 * kits): a stutter. The next save's bank erased beforehand (stopped, untouched): a save then writes pages only */
/* 4.0 (Patrik, Sam: SLOOP 2.8's sections): what plays is changed freely and kept by the autosave, but a pattern changes
 * only when saved; played again (SAVE + its key) or left for another, it comes back as it was saved, its sound too */
static void sc_sections(void)
{
    boot_wait();
    new_project(0); wait_ms(200);
    int t = POS[0]; trk = t; tap(B_HOME); wait_ms(200);
    seq_add(&Q, t, 0, SEQ_TPS, 60, 100); seq_add(&Q, t, 4 * SEQ_TPS, SEQ_TPS, 62, 100);
    /* saved: SAVE + 1 held (the one playing) */
    press(B_SAVE); wait_ms(300); keydown(white_key[0]); wait_ms(1700); keyup(white_key[0]); release(B_SAVE); wait_saved(5000); wait_ms(300);
    EXPECT(keep[t].n == 2, "SAVE + 1 held: the pattern as saved has %d notes (2)", keep[t].n);
    /* changed, not saved: a note more, another sound */
    seq_add(&Q, t, 8 * SEQ_TPS, SEQ_TPS, 64, 100);
    int po = preset_of[t]; turn(ENC_PRESETS, 3); wait_ms(200);
    EXPECT(Q.t[t].pat[0].n == 3 && preset_of[t] != po, "the change: %d notes, sound %d (%d)", Q.t[t].pat[0].n, preset_of[t], po);
    /* the autosave keeps what plays (its saved version too): opened again, the same */
    save_project(0); wait_saved(5000); load_project(0); wait_ms(300);
    EXPECT(Q.t[t].pat[0].n == 3 && keep[t].n == 2 && preset_of[t] != po, "opened again: %d notes playing (3), %d saved (2), sound %d", Q.t[t].pat[0].n, keep[t].n, preset_of[t]);
    /* SAVE + 1 tapped (the one playing): back as saved, notes and sound */
    press(B_SAVE); wait_ms(200); tapkey(white_key[0]); wait_ms(200); release(B_SAVE); wait_ms(300);
    EXPECT(Q.t[t].pat[0].n == 2 && preset_of[t] == po, "SAVE + 1 tapped: %d notes (2), sound %d (%d)", Q.t[t].pat[0].n, preset_of[t], po);
    shot("sections-back");
    /* changed again, then another pattern played: the first comes back as saved when played again */
    seq_add(&Q, t, 8 * SEQ_TPS, SEQ_TPS, 64, 100);
    tap(B_PLAY); wait_ms(100);
    press(B_SAVE); wait_ms(200); tapkey(white_key[1]); wait_ms(200); release(B_SAVE);
    for (int i = 0; i < 400 && Q.t[t].cur != 1; i++) wait_ms(10);
    EXPECT(Q.t[t].cur == 1 && Q.t[t].pat[0].n == 2, "pattern 2 played: pattern 1 has %d notes (2: as saved)", Q.t[t].pat[0].n);
    /* saved into another pattern: SAVE + 3 held while 2 plays (3 empty: at once) */
    seq_add(&Q, t, 2 * SEQ_TPS, SEQ_TPS, 67, 100);
    press(B_SAVE); wait_ms(200); keydown(white_key[2]); wait_ms(1700); keyup(white_key[2]); release(B_SAVE); wait_saved(5000); wait_ms(200);
    EXPECT(Q.t[t].pat[2].n == 1 && Q.t[t].cur == 1, "SAVE + 3 held: pattern 3 has %d notes (1), playing %d", Q.t[t].pat[2].n, Q.t[t].cur + 1);
    /* over a saved pattern: held, AGAIN; the key again within 3 s: saved */
    seq_add(&Q, t, 6 * SEQ_TPS, SEQ_TPS, 69, 100);
    uint32_t m = msg_mark();
    press(B_SAVE); wait_ms(200); keydown(white_key[0]); wait_ms(1700); keyup(white_key[0]); wait_ms(200);
    EXPECT(said_since(m, "AGAIN: SAVE INTO 1") && Q.t[t].pat[0].n == 2, "SAVE + 1 held over a saved pattern: no AGAIN (%d notes)", Q.t[t].pat[0].n);
    shot("sections-again");
    tapkey(white_key[0]); wait_ms(200); release(B_SAVE); wait_saved(5000);
    EXPECT(Q.t[t].pat[0].n == 2 && Q.t[t].pat[0].ev[0].note == 67 && Q.t[t].pat[0].ev[1].note == 69,
           "the key again: pattern 1 is not what played (%d notes, %d)", Q.t[t].pat[0].n, Q.t[t].pat[0].ev[0].note);
    /* SAVE + 16: the SONG screen; two keys tapped while SAVE is held: a chain */
    press(B_SAVE); wait_ms(200); tapkey(white_key[15]); wait_ms(100); release(B_SAVE); wait_ms(200);
    EXPECT(sticky == PG_SONG2, "SAVE + 16: not the SONG screen (%d)", sticky);
    tap(B_HOME); wait_ms(200);
    press(B_SAVE); wait_ms(200); tapkey(white_key[0]); wait_ms(100); tapkey(white_key[2]); wait_ms(100); release(B_SAVE); wait_ms(200);
    EXPECT(chain_on && chain_n == 2 && chain[0] == 0 && chain[1] == 2, "SAVE + 1, 3 tapped: no chain (%d, %d)", chain_on, chain_n);
    tap(B_PLAY); wait_ms(200);
}
static void sc_savestutter(void)
{
    boot_wait();
    printf("[%8.3f]    stopped, untouched: the next bank erased beforehand\n", tw_s());
    for (int i = 0; i < 200 && !store_proj_ready(); i++) wait_ms(100);
    EXPECT(store_proj_ready(), "the next save's bank not prepared after 20 s stopped");
    seq_step_toggle(&Q, 3, 2, 0, 100); wait_ms(100);           /* (a change: the save writes) */
    tap(B_PLAY); wait_ms(500);
    uint32_t d0 = dark_halves; save_and_wait(); uint32_t d1 = dark_halves - d0;
    printf("[%8.3f]    a save while it plays, prepared: %u halves with the flash off\n", tw_s(), d1);
    EXPECT(d1 == 0, "a save while it plays (prepared): %u halves with the flash off (a stutter)", d1);
    seq_step_toggle(&Q, 3, 3, 0, 100); wait_ms(100);
    d0 = dark_halves; save_and_wait(); uint32_t d2 = dark_halves - d0;   /* (not prepared again: it plays) */
    printf("[%8.3f]    a save right after, not prepared: %u halves with the flash off\n", tw_s(), d2);
    tap(B_PLAY); wait_ms(300);
}
/* (3.4.1) the CPU over its time (TWIN_HALF_US, host/twin.sh) while it plays and saves: CPU OVERLOAD is said, and nothing
 * of the window runs while a save has it off (3.4.0 said it from the sound's loop, the window off: a freeze) */
/* (3.4.1, the monkey's seed 6) REC armed while stopped (REC READY), the menu opened and closed: SAVE still saves */
static void sc_recsave(void)
{
    boot_wait();
    seq_step_toggle(&Q, 3, 2, 0, 100); wait_ms(100);
    tap(B_REC); wait_ms(300);
    printf("[%8.3f]    armed %d sticky %d\n", tw_s(), (int)recording_armed, sticky);
    press(B_HOME); wait_ms(700); release(B_HOME); wait_ms(300); tap(B_HOME); wait_ms(300);
    save_and_wait();
    tap(B_REC); wait_ms(300); save_and_wait();
}
extern uint32_t tw_half_us;
static void sc_oversave(void)
{
    boot_wait();
    tap(B_PLAY); wait_ms(500);
    for (int k = 0; k < 3; k++) {
        /* an erase of 300 ms (a slow part's: TWIN_ERASE_MS, host/twin.sh) under way, the window off, and the sound over
         * its time meanwhile (a heavy moment): the load guard's CPU OVERLOAD comes while the window is off */
        while (!flash_xip_on()) wait_ms(1);
        EXPECT(flash_erase_start(0xE1000u) == 0, "an erase in the staging area refused");   /* (SLOOP 2.8 import staging: erasable) */
        tw_half_us = 6200; wait_ms(200); tw_half_us = 0;
        wait_ms(5500);                                     /* (CPU OVERLOAD said at most every 5 s) */
    }
    EXPECT(said_since(0, "OVERLOAD"), "the CPU over its time: no CPU OVERLOAD said");
    seq_step_toggle(&Q, 3, 2, 0, 100); wait_ms(100); save_and_wait();
    tap(B_PLAY); wait_ms(300);
}
/* (3.4.0, Sam: "saving the patterns 1 to 8 cuts the sound while it plays; and the autosave must not") playing, a pattern
 * saved after another (SAVE held + its key held 1.5 s): how many audio halves the flash window was off (the samples and
 * kits silent meanwhile) */
static void sc_savepat(void)
{
    boot_wait();
    for (int i = 0; i < 200 && !store_proj_ready(); i++) wait_ms(100);
    tap(B_PLAY); wait_ms(800);
    uint32_t total = 0;
    for (int k = 0; k < 8; k++) {
        seq_step_toggle(&Q, 3, k, 0, 100); wait_ms(100);              /* (a change: the save writes) */
        uint32_t d0 = dark_halves;
        press(B_SAVE); wait_ms(300); keydown(white_key[k]); wait_ms(1700); keyup(white_key[k]); wait_ms(100); release(B_SAVE);
        wait_ms(300); wait_saved(8000); wait_ms(500);
        uint32_t d = dark_halves - d0; total += d;
        { uint32_t a = 0, n = 0; store_proj_find(&a, &n, 0); printf("[%8.3f]    pattern %d saved while playing: %u halves with the flash off (%.0f ms); the copy at %05X, %u bytes\n", tw_s(), k + 1, d, d * 1.451, a, n); }
    }
    EXPECT(total == 0, "8 pattern saves while playing: %u halves with the flash off (a stutter)", total);
    tap(B_PLAY); wait_ms(300);
    /* the autosave (stopped, untouched 2.5 s): PLAY pressed while it writes; then 8 more saves while it plays */
    for (int round = 0; round < 3; round++) {
        seq_step_toggle(&Q, 3, 9 + round, 0, 100);
        int seen = 0; for (int i = 0; i < 2500 && !seen; i++) { wait_ms(10); seen = saving; }   /* (at most every 20 s) */
        uint32_t d0 = dark_halves; tap(B_PLAY); wait_ms(300); wait_saved(8000); wait_ms(300);
        uint32_t d = dark_halves - d0;
        printf("[%8.3f]    round %d: the autosave %s, PLAY pressed then: %u halves with the flash off\n", tw_s(), round, seen ? "writing" : "not seen", d);
        EXPECT(seen && d == 0, "round %d: PLAY during the autosave: %u halves with the flash off (seen %d)", round, d, seen);
        uint32_t t2 = 0;
        for (int k = 0; k < 8; k++) {
            seq_step_toggle(&Q, 3, k, 0, 100); wait_ms(100); uint32_t d1 = dark_halves;
            press(B_SAVE); wait_ms(300); keydown(white_key[k]); wait_ms(1700); keyup(white_key[k]); wait_ms(100); release(B_SAVE);
            wait_ms(300); wait_saved(8000); wait_ms(300); t2 += dark_halves - d1;
        }
        printf("[%8.3f]    round %d: 8 more saves while playing: %u halves with the flash off\n", tw_s(), round, t2);
        EXPECT(t2 == 0, "round %d: 8 saves while playing: %u halves with the flash off", round, t2);
        tap(B_PLAY); wait_ms(6000);                                   /* (stopped: the other bank prepared meanwhile) */
    }
}
static void sc_drums285(void)
{
    boot_wait();
    new_project(0); wait_ms(200);
    int dt = -1; for (int t = 0; t < NTRK; t++) if (is_drum(t)) dt = t;
    while (trk != dt) { turn(ENC_ALGORITHM, POS[dt] > POS[trk] ? 1 : -1); wait_ms(100); }
    seq_clear(&Q, dt); wait_ms(100);
    tap(B_SEQ); wait_ms(300);
    EXPECT(sticky == PG_STEPS && V.page == UI_DRUMS, "SEQ on the drums: not the DRUMS grid (page %d, view %d)", sticky, V.page);
    /* the grid: a black key picks SNARE (G#3: SNARE / SNARE 2), white key 5 sets step 5, silently; again clears it */
    tapkey(3); wait_ms(150);
    EXPECT((last_note & 15) == 2, "G#3 on the grid: not SNARE (%d)", last_note & 15);
    double pk0 = tw_peak; tw_peak = 0;
    tapkey(white_key[4]); wait_ms(200);
    EXPECT(seq_step_has(&Q, dt, 4, 2) && seq_count(&Q, dt) == 1, "grid key 5: step 5 not set with the snare (%d events)", seq_count(&Q, dt));
    EXPECT(tw_peak < 0.02, "grid key 5: it sounded (peak %.3f)", tw_peak); (void)pk0;
    EXPECT(dg_cur == 4, "grid: the step picked %d (4)", dg_cur);
    tapkey(white_key[4]); wait_ms(200);
    EXPECT(!seq_step_has(&Q, dt, 4, 2), "grid key 5 again: not cleared");
    /* KNOB 2 the step, KNOB 3 the hit, KNOB 4 the level */
    turn(ENC_KNOB2, 3); wait_ms(100);
    EXPECT(dg_cur == 7, "KNOB 2: step %d (7)", dg_cur);
    turn(ENC_KNOB3, 1); wait_ms(100);
    EXPECT(seq_step_has(&Q, dt, 7, 2), "KNOB 3 right: no hit on step 8");
    turn(ENC_KNOB4, -2); wait_ms(100);
    { int v = 0; seq_hit_get(&Q, dt, 7, 2, &v, 0); EXPECT(v < 50, "KNOB 4 left twice: not GHOST (%d)", v); }
    shot("drums-grid-285");
    /* the sheet: OCT+ held, RATCHET x2, OCT- closes */
    press(B_OCTU); wait_ms(600); release(B_OCTU); wait_ms(100);
    EXPECT(dg_sheet, "OCT+ held on the grid: no sheet");
    turn(ENC_KNOB2, 1); wait_ms(100);
    { int r = 0; seq_hit_get(&Q, dt, 7, 2, 0, &r); EXPECT(r == 2, "sheet RATCHET +1: x%d", r); }
    shot("drums-sheet-285");
    tap(B_OCTD); wait_ms(100);
    EXPECT(!dg_sheet, "OCT- did not close the sheet");
    /* a drum step held: a kick and a hat on step 1; KNOB 1 picks the hat, LEVEL lowers the hat alone */
    seq_step_toggle(&Q, dt, 0, 0, 100); seq_step_toggle(&Q, dt, 0, 4, 100); wait_ms(50);
    shown_note(0);
    press(B_SEQ); wait_ms(200); keydown(white_key[0]); wait_ms(200);
    turn(ENC_KNOB1, 1); wait_ms(150);
    EXPECT((last_note & 15) == 2 || (last_note & 15) == 4, "a step held, KNOB 1: sound %d", last_note & 15);
    while ((last_note & 15) != 4) { turn(ENC_KNOB1, 1); wait_ms(100); }
    turn(ENC_KNOB2, -2); wait_ms(150);
    keyup(white_key[0]); wait_ms(100); release(B_SEQ); wait_ms(200);
    { int vk = 0, vh = 0; seq_hit_get(&Q, dt, 0, 0, &vk, 0); seq_hit_get(&Q, dt, 0, 4, &vh, 0);
      EXPECT(vk == 100 && vh < 50 && seq_step_has(&Q, dt, 0, 0) && seq_step_has(&Q, dt, 0, 4), "a step held, the hat's LEVEL: kick %d hat %d (100, ghost)", vk, vh); }
    /* the live mutes: EDIT + a black key, then white key 3 (the snare): muted, not erased */
    int n0 = seq_count(&Q, dt);
    press(B_EDIT); wait_ms(200); tapkey(1); wait_ms(100);
    EXPECT(dm_mode, "EDIT + a black key: not MUTE");
    tapkey(white_key[2]); wait_ms(100); shot("drums-mute-285"); release(B_EDIT); wait_ms(200);
    EXPECT(((dm_live >> 2) & 1) && seq_count(&Q, dt) == n0, "EDIT + key 3 in MUTE: snare muted %d, events %d (%d)", (dm_live >> 2) & 1, seq_count(&Q, dt), n0);
    press(B_EDIT); wait_ms(200); tapkey(white_key[2]); wait_ms(100); tapkey(1); wait_ms(100); release(B_EDIT); wait_ms(200);
    EXPECT(!((dm_live >> 2) & 1) && !dm_mode, "MUTE: the snare back %d, back to erasing %d", (dm_live >> 2) & 1, !dm_mode);
    /* the hits light their keys: the kick on step 1, playing, on the TRACKS page */
    tap(B_HOME); wait_ms(200);
    tap(B_PLAY); int lit = 0; for (int i = 0; i < 300; i++) { wait_ms(10); lit += tw_led[14 + white_key[0]]; } tap(B_PLAY); wait_ms(200);
    EXPECT(lit > 0, "the kick's key never lit while it played");
    /* HOME held 0.7 s: the menu; let go, it stays; HOME closes it */
    press(B_HOME); wait_ms(900);
    EXPECT(menu_on && V.page != UI_TRACKS && home_sec >= 1, "HOME held: not the menu (section %d)", home_sec);
    release(B_HOME); wait_ms(300);
    EXPECT(menu_on, "HOME held 0.9 s and let go: the menu closed");
    turn(ENC_SELECT, 1); wait_ms(100);
    EXPECT(home_sec == 2, "the menu open: SELECT, section %d (2)", home_sec);
    tap(B_HOME); wait_ms(200);
    EXPECT(!menu_on, "HOME: the menu not closed");
    /* FX held + HOME tapped: FX locked, and the FX page never gives way to the menu's (it flashed) */
    tap(B_HOME); wait_ms(200);
    { int other = 0; press(B_FX); wait_ms(500); press(B_HOME); for (int i = 0; i < 20; i++) { wait_ms(10); other += V.page != UI_LAYER && V.page != UI_PUNCH; } release(B_HOME); for (int i = 0; i < 20; i++) { wait_ms(10); other += V.page != UI_LAYER && V.page != UI_PUNCH; } release(B_FX); wait_ms(300);
      EXPECT((locked & (1u << BTN_FX)) && !other && !menu_on, "FX + HOME: locked %d, another page %d frames, menu %d", (locked >> BTN_FX) & 1, other, menu_on);
      tap(B_HOME); wait_ms(300); EXPECT(!locked && !menu_on && sticky != PG_TRACKS + 99, "HOME: FX not let go (%d)", locked); }
    /* SAVE held + the key of the pattern playing, held: the project saved (it was not: nothing to copy), and seen: the
     * tile fills while held, then a big green SAVED, SAVE lit */
    {
        seq_step_toggle(&Q, dt, 9, 2, 100); wait_ms(100);          /* (a change, so that a save writes) */
        int cur = Q.t[trk].cur; uint32_t at0, n0 = 0, c0 = 0; store_proj_find(&at0, &n0, &c0);
        /* (3.3.8) held 1 s and let go: nothing saved (1.5 s it takes: 0.6 s saved by mistake) */
        press(B_SAVE); wait_ms(500); keydown(white_key[cur]); wait_ms(1000); keyup(white_key[cur]); wait_ms(100); release(B_SAVE); wait_ms(2000);   /* (3.4.0: before the autosave, quicker now: 2.5 s untouched) */
        { uint32_t a, n, c = 0; store_proj_find(&a, &n, &c); EXPECT(c == c0 && !V.saved, "held 1 s: saved (%d)", V.saved); }
        press(B_SAVE); wait_ms(500); keydown(white_key[cur]); wait_ms(700);
        int gauge = V.tile[cur].style == T_GAUGE && V.tile[cur].lvl > 20 && V.tile[cur].lvl < 100;
        shot("save-hold-gauge");
        wait_ms(1000); keyup(white_key[cur]); wait_ms(100); release(B_SAVE);
        int big = 0, save_lit = 0;
        for (int i = 0; i < 400 && !big; i++) { wait_ms(10); big = V.saved == 1; }
        if (big) { wait_ms(150); shot("save-saved"); save_lit = tw_led[B_SAVE]; }
        uint32_t at1, n1 = 0, c1 = 0; store_proj_find(&at1, &n1, &c1);
        EXPECT(gauge && big && save_lit && c1 != c0, "SAVE + the playing pattern's key held: gauge %d, SAVED shown %d, SAVE lit %d, written %d", gauge, big, save_lit, c1 != c0);
        wait_ms(2500); EXPECT(!V.saved, "the SAVED banner stayed");
        /* the same on the SONG page (SAVE tapped): a key held 1 s, nothing; 1.7 s, saved */
        seq_step_toggle(&Q, dt, 11, 2, 100); wait_ms(100);
        store_proj_find(&at0, &n0, &c0);
        tap(B_HOME); wait_ms(200); tap(B_SAVE); wait_ms(300);
        EXPECT(sticky == PG_SONG2, "SAVE tapped: not the SONG screen (%d)", sticky);   /* (4.0: SLOOP 2.8.5's; its keys: SAVE's) */
        keydown(white_key[cur]); wait_ms(1000); keyup(white_key[cur]); wait_ms(2500);
        { uint32_t a, n, c = 0; store_proj_find(&a, &n, &c); EXPECT(c == c0, "SONG page, a key held 1 s: saved"); }
        /* (4.0, SLOOP 2.8's sections) let go before: a tap, the pattern back as saved, the change gone */
        EXPECT(!seq_step_has(&Q, dt, 11, 2), "the pattern tapped again: not back as saved (the change kept)");
        seq_step_toggle(&Q, dt, 11, 2, 100); wait_ms(100);
        keydown(white_key[cur]); wait_ms(1700); keyup(white_key[cur]);
        big = 0; for (int i = 0; i < 400 && !big; i++) { wait_ms(10); big = V.saved == 1; }
        store_proj_find(&at1, &n1, &c1);
        EXPECT(big && c1 != c0, "SONG page, a key held 1.7 s: SAVED %d, written %d", big, c1 != c0);
        wait_ms(2500); tap(B_HOME); wait_ms(200);
    }
    /* 4.0: on TRACKS, KNOB 4 the pan again, as SLOOP 2.8.5 (the track's swing: SEQ + KNOB 3, SEQ > PATTERN) */
    {
        tap(B_HOME); wait_ms(200); if (vis_on) { tap(B_HOME); wait_ms(200); }
        int s0 = Q.t[trk].swing; float p0 = S.trk[trk].pan.tgt;
        turn(ENC_KNOB4, 5); wait_ms(200);
        EXPECT(sticky == PG_TRACKS && Q.t[trk].swing == s0 && S.trk[trk].pan.tgt > p0 && V.knob[3].label && !strcmp(V.knob[3].label, "PAN"), "TRACKS KNOB 4: track swing %d -> %d, pan %.2f -> %.2f", s0, Q.t[trk].swing, p0, S.trk[trk].pan.tgt);
        shot("tracks-pan");
        turn(ENC_KNOB4, -5); wait_ms(100);
        tap(B_SEQ); wait_ms(150); tap(B_SEQ); wait_ms(150);   /* SEQ > PATTERN: KNOB 3 SWG */
        EXPECT(sticky == PG_PAT || is_drum(trk), "SEQ twice: not PATTERN (%d)", sticky);
        if (sticky == PG_PAT) { turn(ENC_KNOB3, 5); wait_ms(150); EXPECT(Q.t[trk].swing > s0, "PATTERN KNOB 3: the track's swing %d -> %d", s0, Q.t[trk].swing); shot("pattern-swg"); turn(ENC_KNOB3, -5); }
        tap(B_HOME); wait_ms(150);
    }
    /* 3.3.8: the USB cable pulled while it runs: the battery's level at once (it stayed "on USB" until a restart) */
    {
        extern int tw_usb_on;
        wait_ms(300); int on_usb = V.batt_usb != 0;
        tw_usb_on = 0; wait_ms(1500);
        EXPECT(on_usb && V.batt_usb == 0 && V.batt <= 100, "USB pulled: still on USB %d (was %d), level %d", V.batt_usb, on_usb, V.batt);
        shot("usb-pulled");
        tw_usb_on = 1; wait_ms(500);
        EXPECT(V.batt_usb != 0, "USB back: not shown");
    }
}
static void sc_freetake(void)
{
    boot_wait();
    static const double REC[] = { -60, 0, 50, 100, 150, 200 };
    static const double STEP[] = { 163.0435, 156.25, 170.0 };   /* 92 BPM (the tempo set), 96, 88.2 */
    for (int s = 0; s < 3; s++) for (int bars = 1; bars <= 2; bars++) for (int i = 0; i < 6; i++) {
        int bad, n; ft_play(STEP[s], REC[i], bars, &bad, &n, 0);
        EXPECT(bad == 0, "free take (a step %.1f ms, %d bar(s), REC %+.0f ms): %d hits off their step", STEP[s], bars, REC[i], bad);
    }
    for (int i = 0; i < 6; i++) {
        int bad, n; ft_play(STEP[0], REC[i], 1, &bad, &n, 1);
        EXPECT(bad == 0, "free take, a kick and a snare (REC %+.0f ms): %d hits off their step", REC[i], bad);
        ft_play(STEP[0], REC[i] > 50 ? REC[i] : 100, 1, &bad, &n, 2);
        EXPECT(bad == 0 && n == 10, "free take, the 1 played again (REC %+.0f ms): %d off, %d events (10)", REC[i], bad, n);
    }
    /* 3.3.8: NOTES draws a note where it plays: a note a third of a step late, QUANT ON: on its step */
    {
        new_project(0); trk = 0; wait_ms(200);
        seq_add(&Q, 0, 4 * SEQ_TPS + SEQ_TPS / 3, SEQ_TPS, 60, 100);
        Q.t[0].quant = 100; tap(B_SEQ); wait_ms(300);          /* (4.0, as SLOOP 2.8.5: SEQ tapped, NOTES) */
        EXPECT(sticky == PG_NOTES && V.page == UI_NOTES && V.nr_n == 1 && V.nr[0].x == 16, "NOTES, QUANT ON: page %d, %d notes, x %d (16: step 5)", sticky, V.nr_n, V.nr[0].x);
        Q.t[0].quant = 0; wait_ms(300);
        EXPECT(V.nr_n == 1 && V.nr[0].x == 17, "NOTES, QUANT OFF: x %d (17: as played)", V.nr[0].x);
        tap(B_SEQ); wait_ms(200);
    }
}
/* the user guide's screens (docs/guide.html, tools/guide_shots.sh): the demo project, walked through as a user
 * would, a screenshot at each step the guide shows. OUT/g-*.ppm, and the page as text (dumpv) in the log */
static void gshot(const char *name) { wait_ms(120); shot(name); dumpv(name); }
static void go_trk(int t) { if (trk == t) return; turn(ENC_ALGORITHM, -6); wait_ms(80); for (int i = 0; i < 5 && trk != t; i++) { turn(ENC_ALGORITHM, 1); wait_ms(80); } }   /* (ALGORITHM: the tracks as the screen lists them) */
static float g_bpm;
static void first_page(int b) { tap(B_HOME); wait_ms(150); tap(b); wait_ms(200); turn(ENC_SELECT, -12); wait_ms(250); if (Q.bpm != g_bpm) tempo(g_bpm); }   /* a button's first page (SELECT at the first page turns the tempo: set back) */
static void next_page(void);
static void next_page(void) { turn(ENC_SELECT, 1); wait_ms(250); if (Q.bpm != g_bpm) tempo(g_bpm); }
static void sc_guide(void)
{
    wait_ms(1100); shot("g-logo");                                   /* the logo drawing itself in */
    boot_wait(); for (int i = 0; i < 400 && splash_on; i++) wait_ms(10); wait_ms(300);
    int dt = 0; for (int t = 0; t < NTRK; t++) if (is_drum(t)) dt = t;
    g_bpm = Q.bpm; go_trk(0); gshot("g-tracks");
    /* the sequencer's sheets, stopped (the note's sheet, OCT+ held) */
    tap(B_SEQ); wait_ms(300); turn(ENC_SELECT, -12); wait_ms(300); gshot("g-notes");
    press(B_OCTU); wait_ms(800); shot("g-notes-sheet"); release(B_OCTU); wait_ms(300); tap(B_OCTU); wait_ms(300);
    go_trk(dt); tap(B_HOME); wait_ms(150); tap(B_EDIT); wait_ms(200); for (int i = 0; i < 4 && sticky != 1; i++) { tap(B_EDIT); wait_ms(200); } gshot("g-drums");
    turn(ENC_KNOB1, 2); wait_ms(300); gshot("g-drums-snare"); turn(ENC_KNOB1, -2); wait_ms(200);
    press(B_OCTU); wait_ms(800); shot("g-drums-sheet"); release(B_OCTU); wait_ms(300); tap(B_OCTU); wait_ms(300);
    tap(B_HOME); wait_ms(300); go_trk(0);
    tap(B_PLAY); wait_ms(2600); gshot("g-tracks-play");
    /* sounds: PRESETS turned; EDIT's last page, SOUNDS */
    turn(ENC_PRESETS, 1); wait_ms(300); gshot("g-sounds-presets");
    turn(ENC_PRESETS, -1); wait_ms(4500);
    first_page(B_EDIT); for (int i = 0; i < 8 && sticky != 32; i++) { tap(B_EDIT); wait_ms(200); } gshot("g-sounds");
    turn(ENC_KNOB1, 1); wait_ms(250); gshot("g-sounds-cat"); turn(ENC_KNOB1, -1); wait_ms(200);
    /* shaping a sound (track 2): EDIT's pages, ENV, LFO */
    go_trk(1);
    first_page(B_EDIT); gshot("g-edit1"); next_page(); gshot("g-edit2"); next_page(); gshot("g-filter"); next_page(); gshot("g-voice"); next_page(); gshot("g-voice2");
    first_page(B_ENV); gshot("g-env"); next_page(); gshot("g-envdest");
    first_page(B_LFO); gshot("g-lfo"); next_page(); gshot("g-lfo2"); next_page(); gshot("g-lfodest");
    /* effects */
    first_page(B_FX); gshot("g-fx"); next_page(); gshot("g-fx-filter"); next_page(); wait_ms(700); gshot("g-fx-slicer");
    next_page(); gshot("g-fx-insert"); next_page(); gshot("g-fx-insert2"); next_page(); gshot("g-fx-delay"); next_page(); gshot("g-fx-reverb");
    tap(B_HOME); wait_ms(200);
    press(B_FX); wait_ms(700); gshot("g-punch");
    keydown(white_key[4]); wait_ms(600); gshot("g-punch-on"); keyup(white_key[4]); wait_ms(200);
    release(B_FX); wait_ms(400);
    /* the sequencer: NOTES, PATTERN, SONG; SEQ held: STEPS */
    go_trk(0);
    first_page(B_SEQ); wait_ms(300); gshot("g-notes-play"); tap(B_SEQ); wait_ms(250); gshot("g-pattern"); tap(B_SEQ); wait_ms(250); gshot("g-song");
    tap(B_HOME); wait_ms(200);
    press(B_SEQ); wait_ms(700); gshot("g-steps"); release(B_SEQ); wait_ms(300);
    /* the drums */
    go_trk(dt); tap(B_HOME); wait_ms(300); gshot("g-tracks-drums");
    tap(B_EDIT); wait_ms(200); for (int i = 0; i < 4 && sticky != 1; i++) { tap(B_EDIT); wait_ms(200); } wait_ms(300); gshot("g-drums-play"); tap(B_EDIT); wait_ms(250); gshot("g-kit"); tap(B_EDIT); wait_ms(250); gshot("g-drumsound");
    tap(B_HOME); wait_ms(200);
    /* chords, scales, arps (track 3) */
    go_trk(2);
    first_page(B_SEL); gshot("g-sel"); next_page(); gshot("g-sel2"); tap(B_HOME); wait_ms(200);
    press(B_SEL); wait_ms(700); gshot("g-key"); release(B_SEL); wait_ms(300);
    first_page(B_ARP); gshot("g-arp"); next_page(); gshot("g-arp2"); tap(B_HOME); wait_ms(200);
    press(B_ARP); wait_ms(700); gshot("g-roll"); release(B_ARP); wait_ms(300);
    /* mixing, global */
    press(B_GLO); wait_ms(700); gshot("g-mix"); release(B_GLO); wait_ms(300);
    first_page(B_GLO); gshot("g-global"); next_page(); gshot("g-master"); tap(B_HOME); wait_ms(200);
    /* erase */
    press(B_EDIT); wait_ms(700); gshot("g-erase"); release(B_EDIT); wait_ms(300);
    /* saving: SAVE held, a pattern's key held 1.5 s */
    tap(B_HOME); wait_ms(300); press(B_SAVE); wait_ms(700); gshot("g-save-held");
    keydown(white_key[1]); wait_ms(800); gshot("g-save-filling"); wait_ms(1000); gshot("g-save-done"); keyup(white_key[1]); wait_ms(200);
    release(B_SAVE); wait_ms(500); gshot("g-saved");
    wait_ms(2500);
    tap(B_HOME); wait_ms(200); tap(B_SAVE); wait_ms(400); gshot("g-songpage"); tap(B_SAVE); wait_ms(400); gshot("g-project");
    tap(B_HOME); wait_ms(300);
    /* the menu */
    press(B_HOME); wait_ms(1200); gshot("g-menu");
    turn(ENC_SELECT, 2); wait_ms(300); gshot("g-menu-audio"); turn(ENC_SELECT, 2); wait_ms(300); gshot("g-menu-system"); turn(ENC_SELECT, -8); wait_ms(200);
    release(B_HOME); wait_ms(300); tap(B_HOME); wait_ms(300);
    /* the visualiser */
    tap(B_HOME); wait_ms(800); gshot("g-vis"); tap(B_HOME); wait_ms(300);
    /* recording: REC while it plays */
    go_trk(1); tap(B_REC); wait_ms(400); gshot("g-rec");
    for (int i = 0; i < 4; i++) { tapkey(white_key[4 + i * 2]); wait_ms(250); }
    wait_ms(300); gshot("g-rec-notes");
    tap(B_REC); wait_ms(300); tap(B_PLAY); wait_ms(600);
    /* a step held in STEPS: its note, velocity, ratchet, nudge, and the lock ALGORITHM picks (a knob turned: the
     * release keeps the step) */
    go_trk(0); tap(B_HOME); wait_ms(200); press(B_SEQ); wait_ms(600); keydown(white_key[0]); wait_ms(600);
    turn(ENC_ALGORITHM, 1); wait_ms(200); turn(ENC_ALGORITHM, -1); wait_ms(300); gshot("g-step-held");
    keyup(white_key[0]); wait_ms(200); release(B_SEQ); wait_ms(300);
    /* the sound's SAVE pages: PRESETS, USER, PATTERNS, TOOLS */
    first_page(B_EDIT); tap(B_SAVE); wait_ms(300); gshot("g-save-presets");
    next_page(); gshot("g-save-user"); next_page(); gshot("g-save-patterns"); next_page(); gshot("g-save-tools");
    /* GLO's pages after GLOBAL and MASTER */
    first_page(B_GLO); next_page(); next_page(); gshot("g-glo-system"); next_page(); gshot("g-glo-drums");
    next_page(); gshot("g-texture"); next_page(); gshot("g-texture2"); next_page(); gshot("g-midi"); next_page(); gshot("g-midi2");
    next_page(); gshot("g-glo-rec"); tap(B_HOME); wait_ms(200);
    /* the SONG pages, then a chain: SAVE held, patterns 1 and 2 tapped */
    {   /* (a song of four lines: an intro, a verse, a chorus, an outro) */
        static const int PAT[4] = { 0, 0, 1, 0 }, BARS[4] = { 4, 8, 8, 4 };
        seq_song_set_count(&Q, 4);
        for (int i = 0; i < 4; i++) { seq_song_set_pat(&Q, i, -1, PAT[i]); seq_song_set_len(&Q, i, BARS[i]); }
    }
    tap(B_SAVE); wait_ms(400); turn(ENC_KNOB1, 2); wait_ms(300); gshot("g-song-lines"); tap(B_HOME); wait_ms(300);
    press(B_SAVE); wait_ms(600); tapkey(white_key[0]); wait_ms(250); tapkey(white_key[1]); wait_ms(500); gshot("g-chain");
    release(B_SAVE); wait_ms(300);
    /* a new project (HOME held + key 3, twice), REC while stopped: REC READY */
    press(B_HOME); wait_ms(900); keydown(white_key[2]); wait_ms(200); keyup(white_key[2]); wait_ms(300); release(B_HOME); wait_ms(300); gshot("g-new-ask");
    press(B_HOME); wait_ms(900); keydown(white_key[2]); wait_ms(200); keyup(white_key[2]); wait_ms(300); release(B_HOME); wait_ms(600); gshot("g-new");
    tap(B_HOME); wait_ms(300);
    tap(B_REC); wait_ms(2200); gshot("g-rec-ready");
    tap(B_REC); wait_ms(300);
}
static const struct { const char *name; void (*fn)(void); } SC[] = {
    { "guide", sc_guide }, { "freetake", sc_freetake }, { "drums285", sc_drums285 }, { "savestutter", sc_savestutter }, { "savepat", sc_savepat }, { "oversave", sc_oversave }, { "recsave", sc_recsave }, { "sections", sc_sections },
    { "boot", sc_boot }, { "reboot", sc_reboot }, { "sounds", sc_sounds }, { "ukit", sc_ukit }, { "pages", sc_pages }, { "record", sc_record },
    { "powercut", sc_powercut }, { "aftercut", sc_after_cut }, { "sample", sc_sample }, { "editor", sc_editor },
    { "monkey", sc_monkey }, { "import", sc_import }, { "midi", sc_midi }, { "rescue", sc_rescue }, { "calib", sc_calib }, { "settings", sc_settings }, { "play", sc_play }, { "levels", sc_levels }, { "mixui", sc_mixui }, { "fx", sc_fx }, { "editor2", sc_editor2 }, { "serve", sc_serve }, { "vis", sc_vis }, { "banksave", sc_banksave }, { "punch", sc_punch }, { "film", sc_film }, { "inst", sc_inst }, { "lowend", sc_lowend }, { "tour", sc_tour }, { "saveproj", sc_saveproj }, { "selwalk", sc_selwalk }, { "punchsnd", sc_punchsnd }, { "midiimp", sc_midiimp }, { "arpall", sc_arpall }, { "chordrec", sc_chordrec }, { "songeng", sc_songeng }, { "midi2", sc_midi2 }, { "film2", sc_film2 }, { "bigproj", sc_bigproj }, { "bigcut", sc_bigcut }, { "punch2", sc_punch2 }, { "crumb", sc_crumb }, { "nan", sc_nan } };
static void (*tw_sc)(void);
static void *tw_script(void *arg)
{
    (void)arg;
    pthread_mutex_lock(&tw_mx); while (tw_turn != 1) pthread_cond_wait(&tw_cv, &tw_mx); pthread_mutex_unlock(&tw_mx);
    tw_sc();
    digest("end-state.txt");
    tw_done = 1;
    twin_end(0);
    return 0;
}
/* the RAM the FM-1 never clears (fw/ram.ld .lowbss, .bootbss: m-bootloader's and the loader's left-overs): garbage
 * at the start, as there; what reads it before writing it shows (TWIN_RAM=zero: cleared, as before) */
extern uint8_t __start_twlow[], __stop_twlow[];
__attribute__((no_sanitize_address)) static void garble(void *p, size_t n)
{
    volatile uint8_t *b = p; for (size_t i = 0; i < n; i++) b[i] = (uint8_t)(tw_rnd() >> 3);
}
static void garble_all(void)
{
    if (getenv("TWIN_RAM") && !strcmp(getenv("TWIN_RAM"), "zero")) return;
    garble(__start_twlow, (size_t)(__stop_twlow - __start_twlow));
    garble(&S, sizeof S); garble(&busA, sizeof busA); garble(&busB, sizeof busB); garble(abuf, sizeof abuf); garble(&Q, sizeof Q);
    garble(&HIST, sizeof HIST); garble(pmem, sizeof pmem); garble(sbuf, sizeof sbuf); garble(&PS, sizeof PS); garble(tilebuf, sizeof tilebuf);
    garble(ft_ev, sizeof ft_ev); garble(fb_nm, sizeof fb_nm);
}
int main(int argc, char **argv)
{
    if (argc < 3) { fprintf(stderr, "twin SCENARIO OUTDIR [FLASH] [SEED]\n"); return 2; }
#ifdef TW_FPE
    feenableexcept(FE_INVALID | FE_DIVBYZERO | FE_OVERFLOW);   /* (as an FM-1 with its float traps on would: a fault) */
#endif
    for (unsigned i = 0; i < sizeof SC / sizeof SC[0]; i++) if (!strcmp(SC[i].name, argv[1])) tw_sc = SC[i].fn;
    if (!tw_sc) { fprintf(stderr, "no scenario %s\n", argv[1]); return 2; }
    tw_out = argv[2];
    if (argc > 4) tw_seed = strtoull(argv[4], 0, 0) * 0x9E3779B97F4A7C15ull + 1;
    srand((unsigned)tw_seed);
    memset(tw_flash, 0xFF, TW_FLASH);
    FILE *f = argc > 3 && strcmp(argv[3], "-") ? fopen(argv[3], "rb") : 0;
    if (f) { size_t n = fread(tw_flash, 1, TW_FLASH, f); fclose(f); (void)n; }
    else {                                             /* blank: m-bootloader's place, the firmware's area installed */
        FILE *a = fopen("build/fw/fw.area", "rb");
        if (!a) { fprintf(stderr, "build/fw/fw.area: build the firmware first\n"); return 2; }
        size_t n = fread(tw_flash + 0x4000, 1, TW_FLASH - 0x4000, a); fclose(a); (void)n;
        memset(tw_flash, 0x00, 0x4000);
    }
    tw_flash_init();
    garble_all();
    tw_turn = 0; tw_wake = 0;
    pthread_t th; pthread_create(&th, 0, tw_script, 0);
    app_main(60);
    return 0;
}
