/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception
 * FM1 OP3N engine screens: cassette (DRUMS), depth gauge (BASS), rocket (LEAD),
 * robot (MAGIC). Flat vector primitives, integer math, 240x240. */
#include "opm_scenes.h"
#include "opm_gfx.h"

static const uint16_t track_color[OPM_TRACKS] = { C_YELLOW, C_CYAN, C_ORANGE, C_PINK };
static const char *const track_name[OPM_TRACKS] = { "DRUMS", "BASS", "LEAD", "MAGIC" };
static const uint16_t macro_color[OPM_MACROS] = { C_BLUE, C_GREEN, C_INK, C_ORANGE };
static const char *const macro_name[OPM_MACROS] = { "PUNCH", "COLOR", "SPACE", "TWIST" };

static void number(char *out, unsigned v)
{
    char tmp[6]; int n = 0;
    do { tmp[n++] = (char)('0' + v % 10); v /= 10; } while (v && n < 5);
    while (n) *out++ = tmp[--n];
    *out = 0;
}

/* ======================================================================= */
/*  LEAD — rocket. COLOR = flame + trails + star speed, PUNCH = flicker,    */
/*  SPACE = star count, TWIST = wobble (LFO), beat = upward kick.           */
/* ======================================================================= */
static const int8_t rocket_lines[][4] = {
    {-14, -28, -14, 28}, {14, -28, 14, 28}, {-14, 28, 14, 28}, {-14, -28, 14, -28},
    {-14, -28, 0, -56}, {14, -28, 0, -56}, {-14, 12, 14, 12},
    {-8, 28, -10, 36}, {8, 28, 10, 36}, {-10, 36, 10, 36},
    {-14, 6, -28, 34}, {-28, 34, -14, 28}, {14, 6, 28, 34}, {28, 34, 14, 28}};
enum { NOZZLE = 36 };

static void stars(uint32_t frame, unsigned speed_q4, unsigned count)
{
    for (unsigned i = 0; i < count; ++i) {
        uint32_t h = opm_hash(i * 0x9E3779B9u + 1u);
        int layer = 1 + (int)(h % 3u);
        int x = (int)((h >> 8) % OPM_W);
        int span = OPM_SCENE_BOTTOM - OPM_SCENE_TOP;
        int y = OPM_SCENE_TOP + (int)(((h >> 16) + ((frame * speed_q4 * (unsigned)layer) >> 4)) % (unsigned)span);
        int len = (int)(speed_q4 * (unsigned)layer) >> 4;
        uint16_t c = layer == 3 ? C_INK : layer == 2 ? C_GREY : C_DIM;
        if (len <= 1) opm_pixel(x, y, c); else opm_line(x, y - len, x, y, c);
    }
}

static void scene_rocket(const opm_frame_t *f)
{
    const uint8_t *m = f->macro[OPM_LEAD];
    const int c = m[OPM_COLOR], p = m[OPM_PUNCH], t = m[OPM_TWIST];
    stars(f->frame, 16u + (unsigned)c / 2u, 10u + m[OPM_SPACE] * 30u / 127u);

    const int wobble = t * 28 * (int)opm_sin((int)f->frame * 12) / (127 * 32768);
    const int bob = (int)((opm_sin((int)f->frame * 20) * 3) >> 15) - f->beat * 6 / 255;
    const opm_xf_t xf = opm_xf(wobble, OPM_W / 2, 92 + bob);

    /* exhaust rings, screen space, behind the flame */
    const int L = 8 + c * 40 / 127, W = 7 + c * 13 / 127;
    for (int i = 0; i < 1 + c * 4 / 127; ++i) {
        int age = (int)((f->frame * 3u + (unsigned)(i * 40 / (1 + c * 4 / 127))) % 40u);
        opm_circle(OPM_W / 2 + ((i & 1) ? age : -age) / 3, 92 + bob + NOZZLE + L + 6 + age, 2 + age / 6, C_DIM);
    }
    /* flame: trails, three layered triangles, sparks */
    uint32_t rng = opm_hash(f->frame * 2654435761u);
    const int flick = 100 - (int)(rng % (unsigned)(5 + p * 26 / 127));
    const int trails = 2 + c * 6 / 127;
    for (int i = 0; i < trails; ++i) {
        int u = ((2 * i + 1) * 256) / trails - 256, au = u < 0 ? -u : u;
        rng = opm_hash(rng);
        int len = L * (55 + (int)(rng % 46u)) / 100 * (256 - au / 3) / 256;
        opm_xf_line(&xf, u * W / 512, NOZZLE, u * W * (12 + c * 8 / 127) / 2560, NOZZLE + len,
                    au > 150 ? C_RED : C_ORANGE);
    }
    static const uint8_t lw[3] = {100, 62, 30}, ll[3] = {100, 72, 42};
    static const uint16_t lc[3] = {C_ORANGE, C_YELLOW, C_INK};
    for (int k = 0; k < 3; ++k) {
        rng = opm_hash(rng);
        int w = W * lw[k] / 100, l = L * ll[k] / 100 * flick / 100;
        int jx = ((int)(rng % 3u) - 1) * (1 + c * 2 / 127);
        opm_xf_tri(&xf, -w, NOZZLE, w, NOZZLE, jx, NOZZLE + l, lc[k]);
    }
    /* body: fins filled, outlines on top, porthole */
    opm_xf_tri(&xf, -14, 6, -28, 34, -14, 28, C_ORANGE);
    opm_xf_tri(&xf, 14, 6, 28, 34, 14, 28, C_ORANGE);
    for (unsigned i = 0; i < sizeof rocket_lines / sizeof rocket_lines[0]; ++i)
        opm_xf_line(&xf, rocket_lines[i][0], rocket_lines[i][1], rocket_lines[i][2], rocket_lines[i][3], C_INK);
    int wx, wy; opm_xf_pt(&xf, 0, -8, &wx, &wy);
    opm_fill_circle(wx, wy, 6, C_BLUE);
    opm_circle(wx, wy, 8, C_INK);
}

/* ======================================================================= */
/*  DRUMS — cassette. Reels spin with the transport, tape moves from left   */
/*  to right across the loop. PUNCH = reel speed, COLOR = label colour,     */
/*  SPACE = echo outline, TWIST (distortion) = shake + crackle.             */
/* ======================================================================= */
static void scene_cassette(const opm_frame_t *f)
{
    const uint8_t *m = f->macro[OPM_DRUMS];
    const int t = m[OPM_TWIST], s = m[OPM_SPACE], c = m[OPM_COLOR];
    uint32_t rng = opm_hash(f->frame * 747796405u + 7u);
    int sx = 0, sy = 0;
    if (t > 8 && f->beat > 128) {                       /* distortion shake on hits */
        int amp = 1 + t / 24;
        sx = (int)(rng % (unsigned)(2 * amp + 1)) - amp;
        sy = (int)((rng >> 8) % (unsigned)(2 * amp + 1)) - amp;
    }
    const int x0 = 32 + sx, y0 = 40 + sy, w = 176, h = 112;
    for (int e = 1; e <= s / 32; ++e)                   /* SPACE: fading echoes */
        opm_rect(x0 + e * 5, y0 + e * 5, w, h, e == 1 ? C_GREY : C_DIM);
    opm_fill_rect(x0, y0, w, h, C_BG);
    opm_rect(x0, y0, w, h, C_INK);
    opm_rect(x0 + 1, y0 + 1, w - 2, h - 2, C_INK);
    const uint16_t label = c < 43 ? C_YELLOW : c < 86 ? C_ORANGE : C_PINK;
    opm_fill_rect(x0 + 12, y0 + 10, w - 24, 22, label);
    opm_text(x0 + 20, y0 + 14, "FM1 OP3N", 2, C_BG);
    opm_text(x0 + w - 44, y0 + 18, "A", 1, C_BG);

    /* tape window and reels */
    const int ry = y0 + 60, lx = x0 + 52, rx = x0 + w - 52;
    const int progress = f->length ? f->step * 16 / f->length : 0;
    const int left_pack = 24 - progress, right_pack = 9 + progress;
    opm_circle(lx, ry, left_pack, C_GREY);
    opm_circle(rx, ry, right_pack, C_GREY);
    opm_rect(x0 + 70, y0 + 46, w - 140, 28, C_DIM);
    const int spin = f->playing ? (int)f->frame * (10 + m[OPM_PUNCH] / 6) : 0;
    for (int r = 0; r < 2; ++r) {
        const int cx = r ? rx : lx;
        opm_fill_circle(cx, ry, 9, C_INK);
        opm_fill_circle(cx, ry, 3, C_BG);
        for (int k = 0; k < 3; ++k) {
            int a = spin + k * 341;
            opm_line(cx + (int)((opm_sin(a) * 4) >> 15), ry - (int)((opm_cos(a) * 4) >> 15),
                     cx + (int)((opm_sin(a) * 9) >> 15), ry - (int)((opm_cos(a) * 9) >> 15), C_BG);
        }
    }
    /* head block */
    opm_line(x0 + 40, y0 + h - 2, x0 + 52, y0 + 86, C_INK);
    opm_line(x0 + w - 40, y0 + h - 2, x0 + w - 52, y0 + 86, C_INK);
    opm_line(x0 + 52, y0 + 86, x0 + w - 52, y0 + 86, C_INK);
    opm_circle(x0 + 70, y0 + 99, 4, C_GREY);
    opm_circle(x0 + w - 70, y0 + 99, 4, C_GREY);
    /* VU strip under the cassette follows the drum level */
    const int vu = f->level * 20 / 255;
    for (int i = 0; i < 20; ++i)
        opm_fill_rect(40 + i * 8, 166, 6, 6, i < vu ? (i > 15 ? C_RED : C_YELLOW) : C_DIM);
    if (t > 32)                                         /* crackle */
        for (int i = 0; i < t / 10; ++i) {
            rng = opm_hash(rng);
            opm_pixel(x0 + (int)(rng % (unsigned)w), y0 + (int)((rng >> 12) % (unsigned)h), C_RED);
        }
}

/* ======================================================================= */
/*  BASS — submarine depth gauge. COLOR (filter) drives the needle: dark =  */
/*  deep. SPACE = bubble count, PUNCH = bubble size/speed, TWIST (drive) =  */
/*  red danger ticks light up.                                              */
/* ======================================================================= */
static void scene_gauge(const opm_frame_t *f)
{
    const uint8_t *m = f->macro[OPM_BASS];
    const int c = m[OPM_COLOR], s = m[OPM_SPACE], p = m[OPM_PUNCH], t = m[OPM_TWIST];
    const int cx = OPM_W / 2, cy = 92, R = 58;

    const unsigned bubbles = 3u + (unsigned)s / 12u;
    const unsigned span = OPM_SCENE_BOTTOM - OPM_SCENE_TOP;
    for (unsigned i = 0; i < bubbles; ++i) {
        uint32_t h = opm_hash(i * 2246822519u + 3u);
        unsigned speed = 1u + (unsigned)p / 40u + (h & 1u);
        int y = OPM_SCENE_BOTTOM - (int)((f->frame * speed + (h >> 4)) % span);
        int x = 18 + (int)((h >> 9) % 204u) + (int)((opm_sin((int)f->frame * 16 + (int)i * 100) * 4) >> 15);
        int r = 2 + (int)(h % 4u) + p / 42;
        opm_circle(x, y, r, C_CYAN);
        if (r > 3) opm_pixel(x - r / 2, y - r / 2, C_INK);
    }
    opm_fill_circle(cx, cy, R, C_BG);
    opm_circle(cx, cy, R, C_INK);
    opm_circle(cx, cy, R - 1, C_INK);
    opm_circle(cx, cy, R - 7, C_DIM);
    const int red = t / 26;                              /* 0..4 danger ticks */
    for (int k = 0; k <= 8; ++k) {
        int a = -384 + k * 96;
        uint16_t col = k >= 9 - red ? C_RED : C_INK;
        int r0 = (k % 2) ? R - 12 : R - 16;
        opm_line(cx + (int)((opm_sin(a) * r0) >> 15), cy - (int)((opm_cos(a) * r0) >> 15),
                 cx + (int)((opm_sin(a) * (R - 6)) >> 15), cy - (int)((opm_cos(a) * (R - 6)) >> 15), col);
    }
    const int depth = 127 - c;
    const int jitter = f->beat > 128 ? (int)(opm_hash(f->frame) % 13u) - 6 : 0;
    const int a = -384 + depth * 768 / 127 + jitter;
    opm_line(cx, cy, cx + (int)((opm_sin(a) * (R - 14)) >> 15), cy - (int)((opm_cos(a) * (R - 14)) >> 15), C_ORANGE);
    opm_line(cx + 1, cy, cx + 1 + (int)((opm_sin(a) * (R - 14)) >> 15), cy - (int)((opm_cos(a) * (R - 14)) >> 15), C_ORANGE);
    opm_fill_circle(cx, cy, 5, C_INK);
    char num[8]; number(num, (unsigned)depth * 3u);
    int end = opm_text(cx - opm_text_width(num, 2) / 2 - 6, cy + 22, num, 2, C_INK);
    opm_text(end + 2, cy + 29, "M", 1, C_GREY);
    opm_text(cx - opm_text_width("DEPTH", 1) / 2, cy + 44, "DEPTH", 1, C_GREY);
}

/* ======================================================================= */
/*  MAGIC — robot. Eyes jump to a new spot on every arp note, antenna       */
/*  lights up. COLOR = mouth wave height, SPACE = sound-wave arcs,          */
/*  PUNCH = blush, TWIST (arp rate) = antenna wiggle.                       */
/* ======================================================================= */
static void arc(int cx, int cy, int r, int a0, int a1, uint16_t c)
{
    int px = 0, py = 0;
    for (int a = a0; a <= a1; a += 16) {
        int x = cx + (int)((opm_sin(a) * r) >> 15), y = cy - (int)((opm_cos(a) * r) >> 15);
        if (a != a0) opm_line(px, py, x, y, c);
        px = x; py = y;
    }
}
static void scene_robot(const opm_frame_t *f)
{
    const uint8_t *m = f->macro[OPM_MAGIC];
    const int c = m[OPM_COLOR], s = m[OPM_SPACE], p = m[OPM_PUNCH], t = m[OPM_TWIST];
    const int hx = 60, hy = 58, hw = 120, hh = 96, cx = OPM_W / 2;
    const int pulse = f->pulse;

    for (int k = 0; k < s / 32; ++k) {                  /* SPACE: sound waves */
        int r = 70 + k * 12;
        uint16_t col = k == 0 ? C_PINK : k == 1 ? C_GREY : C_DIM;
        arc(cx, hy + hh / 2, r, 256 - 80, 256 + 80, col);      /* right */
        arc(cx, hy + hh / 2, r, 768 - 80, 768 + 80, col);      /* left  */
    }
    const int wiggle = (int)((opm_sin((int)f->frame * (8 + t / 4)) * (2 + t / 12)) >> 15);
    opm_line(cx, hy, cx + wiggle, hy - 20, C_INK);
    opm_fill_circle(cx + wiggle, hy - 24, 5, pulse > 96 ? C_PINK : C_DIM);
    opm_circle(cx + wiggle, hy - 24, 5, C_INK);

    opm_fill_rect(hx - 8, hy + 30, 8, 34, C_PINK);
    opm_fill_rect(hx + hw, hy + 30, 8, 34, C_PINK);
    opm_fill_rect(hx, hy, hw, hh, C_BG);
    opm_rect(hx, hy, hw, hh, C_INK);
    opm_rect(hx + 1, hy + 1, hw - 2, hh - 2, C_INK);

    static const int8_t look[4][2] = {{-5, 0}, {5, 0}, {0, -5}, {0, 5}};
    const int lx = look[f->dance & 3][0], ly = look[f->dance & 3][1];
    const int pr = 5 + pulse / 80;
    for (int e = 0; e < 2; ++e) {
        int ex = cx + (e ? 25 : -25), ey = hy + 34;
        opm_circle(ex, ey, 15, C_INK);
        opm_circle(ex, ey, 14, C_INK);
        opm_fill_circle(ex + lx, ey + ly, pr, C_INK);
        opm_pixel(ex + lx - 2, ey + ly - 2, C_BG);
        if (p > 40) opm_fill_circle(ex + (e ? 10 : -10), ey + 24, 2 + p / 40, C_PINK);
    }
    const int amp = 2 + c / 16 + pulse / 50;
    int px = 0, py = 0;
    for (int x = 84; x <= 156; x += 4) {
        int y = hy + 76 + (int)((opm_sin(x * 24 + (int)f->frame * 24) * amp) >> 15);
        if (x != 84) { opm_line(px, py, x, y, C_INK); opm_line(px, py + 1, x, y + 1, C_INK); }
        px = x; py = y;
    }
    if (!f->arp_on)
        opm_text(cx - opm_text_width("PRESS ARP", 1) / 2, 166, "PRESS ARP", 1, C_GREY);
}

/* ======================================================================= */
/*  Header (band 0) and HUD (bands 11..14)                                  */
/* ======================================================================= */
static void header(const opm_frame_t *f)
{
    for (int i = 0; i < OPM_TRACKS; ++i) {
        if (i == f->track) opm_fill_rect(6 + i * 13, 3, 10, 10, track_color[i]);
        else opm_rect(6 + i * 13, 3, 10, 10, C_GREY);
    }
    opm_text(62, 5, track_name[f->track], 1, track_color[f->track]);
    char num[8]; number(num, f->tempo);
    int x = OPM_W - 8 - opm_text_width(num, 1);
    opm_text(x, 5, num, 1, C_INK);
    opm_text(x - 22, 5, f->scale ? "PEN" : "MAJ", 1, C_GREY);
    if (f->octave) opm_text(x - 44, 5, f->octave > 0 ? (f->octave > 1 ? "+2" : "+1") : (f->octave < -1 ? "-2" : "-1"), 1, C_GREY);
    if (f->recording) opm_fill_circle(x - 54, 8, 3, C_RED);
    if (f->beat > 160) opm_fill_rect(0, 15, OPM_W, 1, C_DIM);
}

static void hud(const opm_frame_t *f)
{
    const int cur = f->playing ? f->step % 16 : -1;
    for (int i = 0; i < 16; ++i) {
        int x = 8 + i * 14, y = 180;
        if (i == cur) opm_fill_rect(x, y, 11, 8, track_color[f->track]);
        else if (f->steps & (1u << i)) opm_fill_rect(x, y, 11, 8, C_GREY);
        else opm_rect(x, y, 11, 8, (i % 4) ? C_DIM : C_GREY);
    }
    const uint8_t *m = f->macro[f->track];
    for (int i = 0; i < OPM_MACROS; ++i) {
        const int cx = 30 + i * 60, cy = 208, r = 12;
        const int a = -384 + m[i] * 768 / 127;
        opm_circle(cx, cy, r, macro_color[i]);
        opm_line(cx + (int)((opm_sin(a) * 3) >> 15), cy - (int)((opm_cos(a) * 3) >> 15),
                 cx + (int)((opm_sin(a) * (r - 2)) >> 15), cy - (int)((opm_cos(a) * (r - 2)) >> 15), macro_color[i]);
        opm_fill_circle(cx, cy, 1, macro_color[i]);
        opm_text(cx - opm_text_width(macro_name[i], 1) / 2, 226, macro_name[i], 1,
                 i == 2 ? C_GREY : macro_color[i]);
    }
}

uint32_t opm_hud_signature(const opm_frame_t *f)
{
    uint32_t h = 2166136261u;
    const uint8_t *m = f->macro[f->track];
    uint32_t v[7] = { f->track, m[0], m[1], (uint32_t)m[2] | ((uint32_t)m[3] << 8),
                      f->steps, f->playing ? f->step % 16u : 99u, f->playing };
    for (int i = 0; i < 7; ++i) h = (h ^ v[i]) * 16777619u;
    return h;
}

void opm_scene_band(const opm_frame_t *f, int band_top)
{
    opm_clear(C_BG);
    if (band_top < OPM_SCENE_TOP) header(f);
    if (band_top + OPM_BAND_ROWS > OPM_SCENE_TOP && band_top <= OPM_SCENE_BOTTOM) {
        opm_gfx_clip(0, OPM_SCENE_TOP, OPM_W - 1, OPM_SCENE_BOTTOM);
        switch (f->track) {
        case OPM_DRUMS: scene_cassette(f); break;
        case OPM_BASS:  scene_gauge(f);    break;
        case OPM_LEAD:  scene_rocket(f);   break;
        default:        scene_robot(f);    break;
        }
        if (f->fill) {                                   /* FX punch-in frame */
            opm_rect(2, OPM_SCENE_TOP + 2, OPM_W - 4, OPM_SCENE_BOTTOM - OPM_SCENE_TOP - 3, C_PINK);
            opm_rect(4, OPM_SCENE_TOP + 4, OPM_W - 8, OPM_SCENE_BOTTOM - OPM_SCENE_TOP - 7, C_PINK);
        }
        if (f->notice) {
            int w = opm_text_width(f->notice, 1) + 12;
            opm_fill_rect((OPM_W - w) / 2, 26, w, 13, C_BG);
            opm_rect((OPM_W - w) / 2, 26, w, 13, C_INK);
            opm_text((OPM_W - w) / 2 + 6, 29, f->notice, 1, C_INK);
        }
        opm_gfx_unclip();
    }
    if (band_top + OPM_BAND_ROWS > OPM_HUD_TOP) hud(f);
}
