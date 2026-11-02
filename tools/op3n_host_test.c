/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception
 * FM1 OP3N host test: runs the open simulator engine + FM1 OP3N natively.
 * Presses real panel controls, renders audio and the LCD, and checks that
 * every macro reaches the engine and that the output ceiling holds.
 *
 * Build (Linux/macOS), from the repository root:
 *   clang -std=c11 -O2 -fwrapv -DFM1_HOST_TEST -DFM1_OPEN_WEB -I web/engine -I web/synth \
 *     -I third_party/synth -I sdk/include -I app/synth -I app \
 *     tools/op3n_host_test.c $(find app web third_party -name '*.c' -not -path app/board/...) -lm
 */
#include "instrument.h"
#include "fm1_engine_control.h"
#include "audio_output.h"
#include "op3n/opm_macros.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void sim_boot(unsigned clear);
void sim_control(unsigned kind, unsigned index, int value);
const float *sim_render(void);
const uint16_t *sim_pixels(void);
const uint8_t *opm_test_macros(void);
unsigned opm_test_dance(void);

enum { OCT_DOWN, OCT_UP, FX, SCL, ENV, LFO, EDIT, GLO, HOME, SAVE, ARP, SEQ, PLAY, REC };
static int failures;
#define CHECK(cond, ...) do { if (cond) printf("  PASS  "); else { printf("  FAIL  "); ++failures; } printf(__VA_ARGS__); printf("\n"); } while (0)

/* ---- audio capture ---- */
static int16_t *wav; static size_t wav_frames, wav_cap;
static float peak_out;
static void render_ms(unsigned ms)
{
    unsigned blocks = (ms * 441u + 1279u) / 1280u;
    for (unsigned b = 0; b < blocks; ++b) {
        const float *o = sim_render();
        if (wav_frames + FM1_BLOCK_FRAMES > wav_cap) { wav_cap = wav_cap * 2 + 65536; wav = realloc(wav, wav_cap * 4); }
        for (unsigned i = 0; i < FM1_BLOCK_WORDS; ++i) {
            float v = o[i];
            if (fabsf(v) > peak_out) peak_out = fabsf(v);
            wav[wav_frames * 2 + i] = (int16_t)lrintf(v * 32767.0f);
        }
        wav_frames += FM1_BLOCK_FRAMES;
    }
}
static void write_wav(const char *path)
{
    FILE *f = fopen(path, "wb");
    uint32_t data = (uint32_t)(wav_frames * 4), rate = 44100, byte_rate = rate * 4, riff = 36 + data;
    uint16_t fmt = 1, ch = 2, align = 4, bits = 16; uint32_t fmt_len = 16;
    fwrite("RIFF", 1, 4, f); fwrite(&riff, 4, 1, f); fwrite("WAVEfmt ", 1, 8, f);
    fwrite(&fmt_len, 4, 1, f); fwrite(&fmt, 2, 1, f); fwrite(&ch, 2, 1, f); fwrite(&rate, 4, 1, f);
    fwrite(&byte_rate, 4, 1, f); fwrite(&align, 2, 1, f); fwrite(&bits, 2, 1, f);
    fwrite("data", 1, 4, f); fwrite(&data, 4, 1, f); fwrite(wav, 4, wav_frames, f); fclose(f);
}

/* ---- panel helpers ---- */
static void tap(unsigned button) { sim_control(0, button, 1); render_ms(20); sim_control(0, button, 0); render_ms(20); }
static void turn(unsigned enc, int detents)
{
    while (detents) { int d = detents > 2 ? 2 : detents < -2 ? -2 : detents; sim_control(2, enc, d); detents -= d; render_ms(12); }
    render_ms(60);
}
static void key(unsigned k, unsigned ms) { sim_control(1, k, 1); render_ms(ms); sim_control(1, k, 0); render_ms(30); }
static void select_track(unsigned t) { turn(0, -8); turn(0, (int)t); }
static unsigned macro(unsigned t, unsigned m) { return opm_test_macros()[t * 4 + m]; }

/* ---- screenshots: 2x2 sheets of 240x240 ---- */
static uint8_t sheet[480 * 480 * 3];
static void grab(unsigned slot)
{
    render_ms(250);              /* let several frames flush through the band painter */
    const uint16_t *px = sim_pixels();
    for (int y = 0; y < 240; ++y)
        for (int x = 0; x < 240; ++x) {
            uint16_t c = px[y * 240 + x];
            uint8_t *d = &sheet[(((slot / 2) * 240 + y) * 480 + (slot % 2) * 240 + x) * 3];
            d[0] = (uint8_t)(((c >> 11) & 31) * 255 / 31); d[1] = (uint8_t)(((c >> 5) & 63) * 255 / 63); d[2] = (uint8_t)((c & 31) * 255 / 31);
        }
}
static void save_sheet(const char *path)
{
    FILE *f = fopen(path, "wb"); fprintf(f, "P6 480 480 255\n"); fwrite(sheet, 1, sizeof sheet, f); fclose(f);
    memset(sheet, 0, sizeof sheet);
}

int main(void)
{
    sim_boot(1);
    render_ms(400);
    printf("FM1 OP3N host test\n\n[setup]\n");
    CHECK(groove_source_type(0) == GROOVE_808, "track 0 is 808 drums");
    CHECK(groove_source_type(1) == GROOVE_303, "track 1 is 303 bass");
    CHECK(groove_source_type(2) == GROOVE_DX7 && groove_source_type(3) == GROOVE_DX7, "tracks 2,3 are DX7");
    { uint8_t p[156]; groove_fm_copy(2, p, NULL); CHECK(!memcmp(p + 145, "SYN LEAD 1", 10), "lead voice = %.10s", (char *)p + 145);
      groove_fm_copy(3, p, NULL); CHECK(!memcmp(p + 145, "KALIMBA", 7), "magic voice = %.10s", (char *)p + 145); }
    unsigned muted = 0;
    for (unsigned t = 0; t < 8; ++t) { struct groove_view v = {0}; groove_get_view(t, 0, &v); muted |= (unsigned)v.muted << t; }
    CHECK(muted == 0xF0, "tracks 4..7 muted, 0..3 audible (mask %02x)", muted);
    CHECK(groove_sequence_length(0) == 16 && groove_sequence_length(3) == 16, "16-step patterns");
    CHECK(groove_drum_steps(0, 0) == 0x1111 && groove_drum_steps(0, 1) == 0x1010, "starter beat: kick %04llx snare %04llx",
          (unsigned long long)groove_drum_steps(0, 0), (unsigned long long)groove_drum_steps(0, 1));
    CHECK(groove_arp_enabled(3) && !groove_arp_enabled(2), "arp on for MAGIC only");

    printf("\n[transport + screens]\n");
    tap(PLAY); render_ms(600);
    { struct groove_view v = {0}; groove_get_view(0, 0, &v); CHECK(v.playing, "PLAY starts the sequencer (step %u)", v.play_step); }
    for (unsigned t = 0; t < 4; ++t) { select_track(t); grab(t); }
    save_sheet("op3n_screens_neutral.ppm");

    printf("\n[macros reach the engine]\n");
    /* DRUMS: TWIST -> 808 Drive, PUNCH -> decay shorter */
    select_track(0);
    uint8_t before[GROOVE_TONE_PARAMS], after[GROOVE_TONE_PARAMS];
    groove_tone_copy(0, 0, before);
    turn(2 + OPM_TWIST, 40); turn(2 + OPM_PUNCH, 30);
    groove_tone_copy(0, 0, after);
    CHECK(after[5] > before[5], "DRUMS TWIST: kick Drive %u -> %u (macro %u)", before[5], after[5], macro(0, OPM_TWIST));
    CHECK(after[2] < before[2], "DRUMS PUNCH: kick Decay %u -> %u", before[2], after[2]);
    grab(0);
    /* BASS: COLOR -> cutoff, TWIST -> drive */
    select_track(1);
    groove_tone_copy(1, 0, before);
    turn(2 + OPM_COLOR, 40); turn(2 + OPM_TWIST, 30);
    groove_tone_copy(1, 0, after);
    CHECK(after[0] > before[0], "BASS COLOR: cutoff %u -> %u", before[0], after[0]);
    CHECK(after[8] > before[8], "BASS TWIST: drive %u -> %u", before[8], after[8]);
    grab(1);
    /* LEAD: COLOR -> modulator OL + feedback, TWIST -> LFO PMD, SPACE -> sends */
    select_track(2);
    uint8_t pb[156], pa[156]; struct groove_fm_controls cb, ca;
    groove_fm_copy(2, pb, &cb);
    uint8_t sb[2], sa[2]; groove_sends_get(2, sb);
    turn(2 + OPM_COLOR, 40); turn(2 + OPM_TWIST, 40); turn(2 + OPM_SPACE, 30); turn(2 + OPM_PUNCH, 25);
    groove_fm_copy(2, pa, &ca); groove_sends_get(2, sa);
    unsigned car = opm_dx7_carriers(pb[DX_ALG]), raised = 0, mods = 0;
    for (unsigned op = 1; op <= 6; ++op) if (!((car >> (op - 1)) & 1)) { ++mods; raised += pa[dx_op(op) + DX_OL] >= pb[dx_op(op) + DX_OL]; }
    CHECK(mods && raised == mods, "LEAD COLOR: %u/%u modulator levels raised (alg %u)", raised, mods, pb[DX_ALG] + 1);
    CHECK(pa[DX_LFO_PMD] > pb[DX_LFO_PMD], "LEAD TWIST: LFO PMD %u -> %u", pb[DX_LFO_PMD], pa[DX_LFO_PMD]);
    CHECK(sa[0] > sb[0] && sa[1] > sb[1], "LEAD SPACE: sends rev %u->%u dly %u->%u", sb[0], sa[0], sb[1], sa[1]);
    CHECK(ca.enabled && ca.envelope[0] < 10, "LEAD PUNCH: ADSR on, attack %u", ca.envelope[0]);
    grab(2);
    /* MAGIC: TWIST -> arp rate */
    select_track(3);
    unsigned rate0 = groove_arp_setting(3, 0);
    turn(2 + OPM_TWIST, 40);
    CHECK(groove_arp_setting(3, 0) > rate0, "MAGIC TWIST: arp rate %u -> %u", rate0, groove_arp_setting(3, 0));
    sim_control(1, 0, 1); sim_control(1, 4, 1); sim_control(1, 7, 1);   /* hold a chord for the arp */
    unsigned dance0 = opm_test_dance();
    render_ms(700);
    CHECK(opm_test_dance() >= dance0 + 4, "MAGIC robot eyes moved %u times in 0.7 s of arp", opm_test_dance() - dance0);
    grab(3);
    sim_control(1, 0, 0); sim_control(1, 4, 0); sim_control(1, 7, 0);
    save_sheet("op3n_screens_turned.ppm");
    render_ms(300);

    printf("\n[keys, scale lock, octave, seq]\n");
    select_track(2);
    tap(OCT_UP);
    for (unsigned k = 0; k < 27; ++k) key(k, 60);                       /* every key, black ones too */
    tap(OCT_DOWN);
    CHECK(opm_quantize(61, OPM_SCALE_PENTATONIC) == 60 && opm_quantize(71, OPM_SCALE_PENTATONIC) == 72 &&
          opm_quantize(66, OPM_SCALE_MAJOR) == 65, "quantizer: C#->C, B->C (penta), F#->F (major)");
    unsigned bad = 0;
    for (unsigned n = 0; n < 128; ++n) bad += !opm_in_scale(opm_quantize((uint8_t)n, OPM_SCALE_PENTATONIC), OPM_SCALE_PENTATONIC);
    CHECK(bad == 0, "every quantized note is in the scale");
    /* SEQ + white key toggles step 2 on the lead */
    sim_control(0, SEQ, 1); render_ms(30); key(4, 30); sim_control(0, SEQ, 0); render_ms(30);
    { struct groove_view v = {0}; groove_get_view(2, 2, &v); CHECK(v.step.velocity != 0, "SEQ + 3rd white key adds lead step 3 (note %u)", v.step.note); }
    tap(FX); render_ms(300);

    printf("\n[output ceiling]\n");
    sim_control(3, 0, 1023);                                              /* MASTER full */
    for (unsigned t = 0; t < 4; ++t) { select_track(t); turn(2 + OPM_PUNCH, 64); turn(2 + OPM_COLOR, 64); turn(2 + OPM_TWIST, 64); }
    sim_control(1, 0, 1); sim_control(1, 2, 1); sim_control(1, 4, 1);
    render_ms(4000);
    sim_control(1, 0, 0); sim_control(1, 2, 0); sim_control(1, 4, 0);
    render_ms(500);
    printf("  info  peak engine+limiter output over the whole run: %.4f (%.2f dBFS)\n", peak_out, 20 * log10f(peak_out));
    CHECK(peak_out <= 0.50001f, "rendered output never exceeded the 0.5 FS ceiling");
    /* Direct stress: full-scale square into the output stage. */
    int32_t block[FM1_BLOCK_WORDS]; int32_t worst = 0;
    for (unsigned b = 0; b < 200; ++b) {
        for (unsigned i = 0; i < FM1_BLOCK_WORDS; ++i) block[i] = ((b + i / 32) & 1) ? 8388607 : -8388608;
        fm1_output_mix(block);
        for (unsigned i = 0; i < FM1_BLOCK_WORDS; ++i) { int32_t a = block[i] < 0 ? -block[i] : block[i]; if (a > worst) worst = a; }
    }
    CHECK(worst <= 4194304, "full-scale square at max master: peak %d / ceiling 4194304 (%.2f dBFS)", worst, 20 * log10((double)worst / 8388608.0));

    write_wav("op3n_demo.wav");
    printf("\n%s: %d failure(s), %.1f s of audio written\n", failures ? "FAILED" : "ALL PASSED", failures, wav_frames / 44100.0);
    return failures != 0;
}
