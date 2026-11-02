/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception
 * FM1 OP3N — a 4-track kids' groovebox UI for the M-VAVE FM1.
 *
 * Replaces the stock panel application (kept as app/op3n/stock_screen.c.orig).
 * The FM1 binary engine still does all sound generation, sequencing, arp and
 * FX; this file decides what the panel does and drives the public groove_* API.
 *
 *   Engine track 0  DRUMS  808 kit         keys = drum pads
 *   Engine track 1  BASS   303             keys = scale-locked notes
 *   Engine track 2  LEAD   DX7 SYN LEAD 1  keys = scale-locked notes
 *   Engine track 3  MAGIC  DX7 KALIMBA     keys = scale-locked, arp on
 *   Engine tracks 4..7     muted and cleared
 *
 * Panel map
 *   Big left encoder (0)   choose track          K1..K4 (encoders 2..5)  PUNCH COLOR SPACE TWIST
 *   Encoder 6              tempo                 OCT- / OCT+             octave -2..+2
 *   PLAY                   start / stop          REC                     record live into the track
 *   ARP                    arpeggiator on/off    SCL                     C major <-> C pentatonic
 *   FX (hold)              punch-in echo fill    SEQ (hold) + white keys toggle the 16 steps
 *   MASTER pot             volume (always under the hard output ceiling in audio_output.c)
 *
 * Runs only in the control context (groove_screen_control). Integer only. */
#include "instrument.h"
#include "fm_patch.h"
#include "fm1_engine_control.h"
#include "op3n/opm_macros.h"
#include "op3n/opm_gfx.h"
#include "op3n/opm_scenes.h"
#include <string.h>

enum { OCT_DOWN, OCT_UP, FX, SCL, ENV, LFO, EDIT, GLO, HOME, SAVE, ARP, SEQ, PLAY, REC };
#define BUTTON(b) (1U << (b))
enum { ENC_TRACK = 0, ENC_K1 = 2, ENC_MAIN = 6 };
enum { LEAD_PROGRAM = 2 /* SYN LEAD 1 */, MAGIC_PROGRAM = 117 /* KALIMBA */ };
enum { FRAME_MS = 50, PAINT_BUDGET_MS = 4, APPLY_MS = 30, NOTICE_MS = 1300 };
enum { FILL_FEEDBACK = 88 };   /* punch-in echo; still well below runaway */

static const uint8_t white_keys[16] = {0, 2, 4, 6, 7, 9, 11, 12, 14, 16, 18, 19, 21, 23, 24, 26};

static struct {
    unsigned ready, track, scale, recording, fill, seq_down;
    int octave;
    uint8_t macro[OPM_TRACKS][OPM_MACROS];
    unsigned dirty;                       /* bit per track: macros need applying */
    uint32_t applied_ms;
    uint8_t dx_base[OPM_TRACKS][156];     /* neutral DX7 voices (LEAD, MAGIC)     */
    uint8_t bass_base[GROOVE_TONE_PARAMS];
    uint8_t drum_base[11][GROOVE_TONE_PARAMS];
    uint8_t fill_feedback, fill_delay[OPM_TRACKS];
    uint8_t key_note[27], key_track[27], sel_note[OPM_TRACKS];
    uint32_t prev_buttons, prev_keys, blocked_keys;
    const char *notice;
    uint32_t notice_until;
    uint32_t underruns, health_ms;
    /* display */
    opm_frame_t frame;
    unsigned painting, band, hud_needed;
    uint32_t frame_ms, hud_shown;
    uint8_t last_step, beat, pulse, dance, arp_tick;
    uint64_t leds;
} s;
static uint16_t band_pixels[OPM_W * OPM_BAND_ROWS];

static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }
static void say(const char *text) { s.notice = text; s.notice_until = fm1_millis() + NOTICE_MS; }
static int post(uint8_t a, uint8_t b, uint8_t c, unsigned bytes)
{
    const uint8_t message[3] = {a, b, c};
    return groove_midi_local(message, bytes) == 0;
}
static void view_of(unsigned track, struct groove_view *v)
{
    memset(v, 0, sizeof *v);
    groove_get_view(track, 0, v);
}

/* ======================================================================= */
/*  Macro -> engine: compute absolute targets, send only the differences    */
/* ======================================================================= */
static void apply_sends(unsigned t)
{
    uint8_t want[2], have[2];
    opm_space_sends(t, s.macro[t][OPM_SPACE], want);
    if (s.fill) { s.fill_delay[t] = want[1]; want[1] = 15; }  /* fill owns the delay send */
    groove_sends_get(t, have);
    for (unsigned i = 0; i < 2; ++i)
        if (want[i] != have[i]) groove_sends_edit(t, i, (int)want[i] - (int)have[i]);
}

static void apply_macros(unsigned t)
{
    const uint8_t *m = s.macro[t];
    apply_sends(t);
    if (t == OPM_DRUMS) {
        for (unsigned part = 0; part < 11; ++part) {
            uint8_t have[GROOVE_TONE_PARAMS];
            int n = groove_tone_copy(t, part, have);
            for (int i = 0; i < n; ++i) {
                const fm1_param_t *p = groove_tone_parameter(t, part, (unsigned)i);
                if (!p || p->names) continue;              /* skip enum selectors */
                unsigned want = opm_808_target(p->name, s.drum_base[part][i], p->max, m);
                if (want != have[i]) groove_tone_edit(t, part, (unsigned)i, (int)want - (int)have[i]);
            }
        }
    } else if (t == OPM_BASS) {
        uint8_t have[GROOVE_TONE_PARAMS], want[12];
        if (groove_tone_copy(t, 0, have) >= 12) {
            opm_303_target(s.bass_base, m, want);
            for (unsigned i = 0; i < 12; ++i)
                if (want[i] != have[i]) groove_tone_edit(t, 0, i, (int)want[i] - (int)have[i]);
        }
    } else {
        uint8_t have[156], want[156], adsr[4];
        struct groove_fm_controls controls;
        if (groove_fm_copy(t, have, &controls)) return;
        opm_dx7_target(t, s.dx_base[t], m, want, adsr);
        for (unsigned i = 0; i < 145; ++i)
            if (want[i] != have[i]) groove_fm_edit(t, i, (int)want[i] - (int)have[i]);
        if (!controls.enabled) groove_adsr_enable(t, 1);
        for (unsigned i = 0; i < 4; ++i)
            if (adsr[i] != controls.envelope[i])
                groove_adsr_edit(t, i, (int)adsr[i] - (int)controls.envelope[i]);
        if (t == OPM_MAGIC) {
            uint8_t rate, gate;
            opm_twist_arp(m[OPM_TWIST], &rate, &gate);
            groove_arp_edit(t, 0, (int)rate - (int)groove_arp_setting(t, 0));
            groove_arp_edit(t, 3, (int)gate - (int)groove_arp_setting(t, 3));
        }
    }
}

/* FX hold: everything into the echo with high feedback, restored on release. */
static void fill_set(unsigned on)
{
    uint8_t fx[4];
    groove_effects_get(fx);
    if (on && !s.fill) {
        s.fill = 1;
        s.fill_feedback = fx[2];
        groove_effects_edit(2, FILL_FEEDBACK - (int)fx[2]);
        for (unsigned t = 0; t < OPM_TRACKS; ++t) apply_sends(t);
    } else if (!on && s.fill) {
        s.fill = 0;
        groove_effects_edit(2, (int)s.fill_feedback - (int)fx[2]);
        for (unsigned t = 0; t < OPM_TRACKS; ++t) apply_sends(t);
    }
}

/* ======================================================================= */
/*  First-run setup: build the FM1 OP3N kit on top of whatever the engine   */
/*  booted with. Done on the first control poll so it always runs after     */
/*  the engine has finished its own project load.                           */
/* ======================================================================= */
static void starter_patterns(void)
{
    static const uint8_t kick[] = {0, 4, 8, 12}, snare[] = {4, 12}, hat[] = {2, 6, 10, 14};
    for (unsigned i = 0; i < sizeof kick; ++i) groove_sequence_edit(OPM_DRUMS, kick[i], 36, 110);
    for (unsigned i = 0; i < sizeof snare; ++i) groove_sequence_edit(OPM_DRUMS, snare[i], 38, 100);
    for (unsigned i = 0; i < sizeof hat; ++i) groove_sequence_edit(OPM_DRUMS, hat[i], 42, 80);
    static const uint8_t bass_step[] = {0, 3, 6, 8, 11, 14}, bass_note[] = {36, 36, 43, 41, 41, 43};
    for (unsigned i = 0; i < sizeof bass_step; ++i) groove_sequence_edit(OPM_BASS, bass_step[i], bass_note[i], 100);
}

static void setup(void)
{
    for (unsigned t = 0; t < OPM_TRACKS; ++t)
        if (groove_track_role(t) != GROOVE_TRACK_NORMAL) groove_track_role_set(t, GROOVE_TRACK_NORMAL);
    (void)groove_source_set(OPM_DRUMS, GROOVE_808);
    (void)groove_source_set(OPM_BASS, GROOVE_303);
    (void)groove_source_set(OPM_LEAD, GROOVE_DX7);
    (void)groove_source_set(OPM_MAGIC, GROOVE_DX7);

    uint8_t patch[156];
    groove_fm_program(patch, LEAD_PROGRAM);
    patch[DX_PMS] = 3;                                     /* lets TWIST vibrato work */
    (void)groove_fm_import(OPM_LEAD, patch);
    groove_fm_program(patch, MAGIC_PROGRAM);
    (void)groove_fm_import(OPM_MAGIC, patch);

    for (unsigned t = 0; t < GROOVE_TRACKS; ++t) {
        struct groove_view v;
        view_of(t, &v);
        unsigned want_muted = t >= OPM_TRACKS;
        if (!v.muted != !want_muted) groove_mix_toggle(t, 0);
        if (v.solo) groove_mix_toggle(t, 1);
        groove_arp_set(t, t == OPM_MAGIC);
        (void)groove_edit_clear(t, 0, 2);
        if (t < OPM_TRACKS) groove_sequence_length_set(t, 16);
    }
    starter_patterns();

    groove_fm_copy(OPM_LEAD, s.dx_base[OPM_LEAD], NULL);
    groove_fm_copy(OPM_MAGIC, s.dx_base[OPM_MAGIC], NULL);
    groove_tone_copy(OPM_BASS, 0, s.bass_base);
    for (unsigned part = 0; part < 11; ++part) groove_tone_copy(OPM_DRUMS, part, s.drum_base[part]);

    memset(s.macro, OPM_MACRO_NEUTRAL, sizeof s.macro);
    s.macro[OPM_MAGIC][OPM_SPACE] = 80;                    /* a little sparkle by default */
    for (unsigned t = 0; t < OPM_TRACKS; ++t) apply_macros(t);
    s.sel_note[OPM_DRUMS] = 36;
    s.sel_note[OPM_BASS] = 36;
    s.sel_note[OPM_LEAD] = s.sel_note[OPM_MAGIC] = 60;
    groove_edit_forget_history();
    groove_project_clean();
    s.ready = 1;
}

/* ======================================================================= */
/*  Keys                                                                    */
/* ======================================================================= */
static unsigned key_pitch(unsigned key)
{
    unsigned note = groove_key_note(s.track, key, s.octave);
    if (note < 128 && !groove_pad_count(s.track)) note = opm_quantize((uint8_t)note, s.scale);
    return note;
}
static unsigned note_held_elsewhere(unsigned key, unsigned note, unsigned track)
{
    for (unsigned k = 0; k < 27; ++k)
        if (k != key && s.key_note[k] == note && s.key_track[k] == track) return 1;
    return 0;
}
static void key_off(unsigned key)
{
    if (s.key_note[key] == 0xff) return;
    unsigned note = s.key_note[key], track = s.key_track[key];
    s.key_note[key] = 0xff;
    if (!note_held_elsewhere(key, note, track))            /* scale lock can double up */
        (void)post((uint8_t)(0x80 + groove_midi_input_channel(track)), (uint8_t)note, 0, 3);
}
static void release_all(uint32_t held)
{
    for (unsigned k = 0; k < 27; ++k) key_off(k);
    s.blocked_keys |= held;
}
static void play_keys(uint32_t keys, uint32_t pressed)
{
    if (s.seq_down) {                                      /* step edit, no notes */
        for (unsigned i = 0; i < 16; ++i)
            if (pressed & (1U << white_keys[i])) {
                struct groove_view v = {0};
                groove_get_view(s.track, i, &v);
                unsigned on = groove_pad_count(s.track)
                    ? (v.step.drums & (1U << (unsigned)groove_pad_index(s.track, s.sel_note[s.track]))) != 0
                    : v.step.velocity != 0;
                groove_sequence_edit(s.track, i, s.sel_note[s.track], on ? 0 : 100);
            }
        return;
    }
    for (unsigned key = 0; key < 27; ++key) {
        unsigned held = (keys >> key) & 1U;
        if (s.key_note[key] != 0xff && !held) key_off(key);
        if (held && s.key_note[key] == 0xff) {
            unsigned note = key_pitch(key);
            if (note >= 128) continue;
            if (!note_held_elsewhere(key, note, s.track) &&
                !post((uint8_t)(0x90 + groove_midi_input_channel(s.track)), (uint8_t)note, 100, 3))
                continue;                                  /* queue full: retry next poll */
            s.key_note[key] = (uint8_t)note;
            s.key_track[key] = (uint8_t)s.track;
            s.sel_note[s.track] = (uint8_t)note;
        }
    }
}

/* ======================================================================= */
/*  LEDs: SEQ held shows the 16 steps on the white keys; otherwise          */
/*  melodic tracks light the keys that are in the scale.                    */
/* ======================================================================= */
static void leds(const struct groove_view *v)
{
    uint64_t want = 0;
    if (v->playing) want |= 1ULL << PLAY;
    if (s.recording) want |= 1ULL << REC;
    if (groove_arp_enabled(s.track)) want |= 1ULL << ARP;
    if (s.scale) want |= 1ULL << SCL;
    if (s.fill) want |= 1ULL << FX;
    if (s.seq_down) {
        want |= 1ULL << SEQ;
        for (unsigned i = 0; i < 16; ++i) {
            unsigned lit = (v->active_steps >> i) & 1U;
            if (v->playing && v->play_step % 16 == i) lit ^= 1U;
            if (lit) want |= 1ULL << (14 + white_keys[i]);
        }
    } else if (!groove_pad_count(s.track)) {
        for (unsigned key = 0; key < 27; ++key) {
            unsigned raw = groove_key_note(s.track, key, s.octave);
            if (raw < 128 && opm_in_scale((uint8_t)raw, s.scale)) want |= 1ULL << (14 + key);
        }
    }
    uint64_t change = want ^ s.leds;
    for (unsigned i = 0; i < 41; ++i)
        if (change & (1ULL << i)) fm1_led_set(i, (int)((want >> i) & 1U));
    s.leds = want;
}

/* ======================================================================= */
/*  Display: freeze one frame, then send it band by band within a small     */
/*  time budget per poll. HUD bands are only resent when they change.       */
/* ======================================================================= */
static void frame_build(uint32_t now)
{
    opm_frame_t *f = &s.frame;
    struct groove_view v, magic;
    view_of(s.track, &v);
    view_of(OPM_MAGIC, &magic);
    if (v.playing && v.play_step != s.last_step) s.beat = 255;
    else s.beat = (uint8_t)(s.beat * 5 / 8);
    s.last_step = v.play_step;
    /* Magic "dance": step the robot's eyes at the arp's own rate (2^rate
     * notes per beat at the current tempo) while the Magic track sounds.
     * The track meter alone is too quiet to detect individual arp notes. */
    {
        uint32_t per_beat = 1U << groove_arp_setting(OPM_MAGIC, 0);
        uint32_t tick = (uint32_t)((uint64_t)now * magic.tempo * per_beat / 60000U);
        if (magic.level >= 2 && (uint8_t)tick != s.arp_tick) { s.pulse = 255; s.dance++; }
        else s.pulse = (uint8_t)(s.pulse * 5 / 8);
        s.arp_tick = (uint8_t)tick;
    }

    f->frame++;
    f->track = (uint8_t)s.track;
    memcpy(f->macro, s.macro, sizeof f->macro);
    f->playing = v.playing;
    f->recording = (uint8_t)s.recording;
    f->scale = (uint8_t)s.scale;
    f->arp_on = (uint8_t)groove_arp_enabled(s.track);
    f->octave = (int8_t)s.octave;
    f->step = v.play_step;
    f->length = v.sequence_length ? v.sequence_length : 16;
    f->tempo = v.tempo;
    f->steps = (uint16_t)(v.active_steps & 0xffffU);
    f->beat = s.beat;
    f->pulse = s.pulse;
    f->dance = s.dance;
    f->level = (uint8_t)(v.level * 2U > 255U ? 255U : v.level * 2U);
    f->fill = (uint8_t)s.fill;
    f->notice = s.notice && (int32_t)(s.notice_until - now) > 0 ? s.notice : NULL;
    if (!f->notice) s.notice = NULL;
    uint32_t sig = opm_hud_signature(f);
    s.hud_needed = sig != s.hud_shown;
    s.hud_shown = sig;
}

static void paint(uint32_t now)
{
    if (!s.painting && now - s.frame_ms >= FRAME_MS) {
        s.frame_ms = now;
        frame_build(now);
        s.painting = 1;
        s.band = 0;
    }
    while (s.painting) {
        int pending = fm1_screen_pending();
        if (pending > 0) break;                            /* DMA still owns the buffer */
        if (pending < 0) s.hud_shown = ~s.hud_shown;       /* transfer failed: resend HUD */
        int top = (int)s.band * OPM_BAND_ROWS;
        if (top < OPM_HUD_TOP || s.hud_needed) {
            opm_gfx_band(band_pixels, top);
            opm_scene_band(&s.frame, top);
            int r = fm1_screen_submit(0, (unsigned)top, OPM_W, OPM_BAND_ROWS, band_pixels, OPM_W * OPM_BAND_ROWS);
            if (r == 1) break;                             /* busy: same band next poll */
            if (r < 0) s.hud_shown = ~s.hud_shown;
        }
        if (++s.band >= OPM_BANDS) s.painting = 0;
        if (fm1_millis() - now >= PAINT_BUDGET_MS) break;
    }
}

/* ======================================================================= */
/*  Engine entry points                                                     */
/* ======================================================================= */
void groove_screen_init(void)
{
    memset(&s, 0, sizeof s);
    memset(s.key_note, 0xff, sizeof s.key_note);
    s.scale = OPM_SCALE_PENTATONIC;                        /* every key sounds good */
    s.track = OPM_DRUMS;
    s.hud_shown = ~0U;
    fm1_controls_init();
    for (unsigned i = 0; i < 41; ++i) fm1_led_set(i, 0);
    fm1_screen_init();
    s.frame_ms = fm1_millis() - FRAME_MS;
}

void groove_screen_control(void)
{
    struct fm1_controls c;
    fm1_controls_read(&c);
    const uint32_t now = fm1_millis();
    (void)groove_master_update(c.knob);
    if (!s.ready) setup();

    const uint32_t pressed = c.buttons & ~s.prev_buttons;
    const uint32_t released = ~c.buttons & s.prev_buttons;
    s.blocked_keys &= c.keys;                              /* unblock keys once released */
    const uint32_t keys = c.keys & ~s.blocked_keys;
    const uint32_t key_down = keys & ~s.prev_keys;

    if (c.encoders[ENC_TRACK]) {
        unsigned next = (unsigned)clampi((int)s.track + c.encoders[ENC_TRACK], 0, OPM_TRACKS - 1);
        if (next != s.track) {
            release_all(c.keys);
            if (s.recording) { s.recording = 0; groove_edit_record(0, s.track, 0); }
            s.track = next;
        }
    }
    for (unsigned i = 0; i < OPM_MACROS; ++i) {
        int d = c.encoders[ENC_K1 + i];
        if (!d) continue;
        int mag = d < 0 ? -d : d;
        d *= mag > 2 ? 6 : 2;                              /* ~64 detents end to end */
        s.macro[s.track][i] = (uint8_t)clampi(s.macro[s.track][i] + d, 0, OPM_MACRO_MAX);
        s.dirty |= 1U << s.track;
    }
    if (c.encoders[ENC_MAIN]) groove_tempo_edit(c.encoders[ENC_MAIN]);

    if (pressed & (BUTTON(OCT_DOWN) | BUTTON(OCT_UP))) {
        int next = clampi(s.octave + ((pressed & BUTTON(OCT_UP)) ? 1 : 0) - ((pressed & BUTTON(OCT_DOWN)) ? 1 : 0), -2, 2);
        if (next != s.octave) { release_all(c.keys); s.octave = next; }
    }
    if (pressed & BUTTON(SCL)) {
        release_all(c.keys);
        s.scale = !s.scale;
        say(s.scale ? "PENTATONIC" : "C MAJOR");
    }
    struct groove_view v;
    view_of(s.track, &v);
    if (pressed & BUTTON(PLAY)) (void)post(v.playing ? 0xfc : 0xfa, 0, 0, 1);
    if (pressed & BUTTON(REC)) {
        s.recording = !s.recording;
        say(s.recording ? "RECORDING" : "REC OFF");
    }
    groove_edit_record(s.recording, s.track, s.recording ? groove_edit_cursor() % 16U : 0);
    if (pressed & BUTTON(ARP)) {
        groove_arp_set(s.track, !groove_arp_enabled(s.track));
        say(groove_arp_enabled(s.track) ? "ARP ON" : "ARP OFF");
    }
    if (pressed & BUTTON(FX)) fill_set(1);
    if (released & BUTTON(FX)) fill_set(0);
    if ((pressed | released) & BUTTON(SEQ)) {
        release_all(c.keys);
        s.seq_down = (c.buttons & BUTTON(SEQ)) != 0;
    }

    if (s.dirty && now - s.applied_ms >= APPLY_MS) {
        for (unsigned t = 0; t < OPM_TRACKS; ++t)
            if (s.dirty & (1U << t)) apply_macros(t);
        s.dirty = 0;
        s.applied_ms = now;
    }
    play_keys(c.keys & ~s.blocked_keys, key_down);

    if (now - s.health_ms >= 500U) {
        struct fm1_engine_stats health;
        fm1_engine_stats_read(&health);
        if (health.underruns != s.underruns) say("BUSY - FEWER NOTES");
        s.underruns = health.underruns;
        s.health_ms = now;
    }
    leds(&v);
    paint(now);
    s.prev_buttons = c.buttons;
    s.prev_keys = keys;
}

void groove_screen_tap(uint32_t now)
{
    if (groove_tempo_tap(now) > 0) say("TAP TEMPO");
}

void groove_screen_mmc(unsigned command)
{
    unsigned transport = command == 1 || command == 9 ? 0xfc :
                         command == 2 || command == 3 || command == 6 ? 0xfb : 0;
    if (transport) (void)post((uint8_t)transport, 0, 0, 1);
}

#ifdef FM1_HOST_TEST
/* Browser simulator hooks. FM1 OP3N has no stock pages, so these are minimal. */
static char web_knobs[8][2][16];
void groove_screen_snapshot(uint8_t state[8])
{
    memset(state, 0, 8);
    state[0] = (uint8_t)s.track;
    state[4] = (uint8_t)s.recording;
    state[7] = (uint8_t)(s.octave + 24);
}
int groove_screen_open(unsigned target) { (void)target; return 0; }
int groove_screen_preset(unsigned slot) { (void)slot; return GROOVE_PROJECT_INVALID; }
unsigned groove_screen_preset_position(void) { return 0; }
void groove_screen_web_fine(unsigned fine) { (void)fine; }
unsigned groove_screen_web_context(void) { return s.track; }
unsigned groove_screen_web_mask(void) { return 0; }
const void *groove_screen_web_targets(void) { return NULL; }
unsigned groove_screen_web_target_count(void) { return 0; }
int groove_screen_web_adjust(unsigned context, unsigned action, unsigned index, unsigned owner, int delta)
{ (void)context; (void)action; (void)index; (void)owner; (void)delta; return -1; }
const char *groove_screen_web_knobs(void) { return &web_knobs[0][0][0]; }
unsigned groove_screen_web_first(void) { return 0; }
int groove_screen_web_step(unsigned step, unsigned toggle)
{
    if (step >= 16 || !toggle) return -1;
    struct groove_view v = {0};
    groove_get_view(s.track, step, &v);
    groove_sequence_edit(s.track, step, s.sel_note[s.track], v.step.velocity ? 0 : 100);
    return 0;
}
/* Test access for the host harness. */
const uint8_t *opm_test_macros(void) { return &s.macro[0][0]; }
unsigned opm_test_dance(void) { return s.dance; }
#endif
