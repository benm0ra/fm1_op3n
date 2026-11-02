/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception
 * FM1 OP3N flat-vector primitives for the FM1 240x240 panel.
 *
 * The FM1 has no full frame buffer: the panel is fed 16-row bands
 * (FM1_SCREEN_DMA_PIXELS) by DMA. Every primitive therefore draws through a
 * clip window equal to the current band. A scene is drawn once per band from
 * the same frozen state, so the 15 bands always assemble one coherent frame.
 * Integer only: no FPU is assumed on the AC791N/PI32v2 build. */
#ifndef OPM_GFX_H
#define OPM_GFX_H
#include <stdint.h>

enum { OPM_W = 240, OPM_H = 240, OPM_BAND_ROWS = 16, OPM_BANDS = OPM_H / OPM_BAND_ROWS };

/* RGB565, native endian (fm1_screen_submit byte-swaps in place). */
#define OPM_RGB(r, g, b) ((uint16_t)((((r) & 0xF8u) << 8) | (((g) & 0xFCu) << 3) | (((b) & 0xFFu) >> 3)))
enum {
    C_BG     = OPM_RGB(0, 0, 0),
    C_INK    = OPM_RGB(240, 240, 236),
    C_GREY   = OPM_RGB(120, 120, 124),
    C_DIM    = OPM_RGB(52, 52, 56),
    C_BLUE   = OPM_RGB(40, 110, 255),
    C_GREEN  = OPM_RGB(30, 200, 110),
    C_ORANGE = OPM_RGB(255, 96, 24),
    C_YELLOW = OPM_RGB(255, 210, 40),
    C_RED    = OPM_RGB(230, 40, 40),
    C_CYAN   = OPM_RGB(40, 220, 230),
    C_PINK   = OPM_RGB(255, 90, 170),
};

/* Select the band being composed: rows [top, top+16) into pixels[240*16]. */
void opm_gfx_band(uint16_t *pixels, int top);
void opm_gfx_clip(int x0, int y0, int x1, int y1);   /* further restrict, inclusive */
void opm_gfx_unclip(void);                            /* back to the whole band */

void opm_clear(uint16_t c);
void opm_pixel(int x, int y, uint16_t c);
void opm_hline(int x0, int x1, int y, uint16_t c);
void opm_line(int x0, int y0, int x1, int y1, uint16_t c);
void opm_rect(int x, int y, int w, int h, uint16_t c);
void opm_fill_rect(int x, int y, int w, int h, uint16_t c);
void opm_circle(int cx, int cy, int r, uint16_t c);
void opm_fill_circle(int cx, int cy, int r, uint16_t c);
void opm_fill_triangle(int x0, int y0, int x1, int y1, int x2, int y2, uint16_t c);
/* 5x7 font, digits, A-Z, space and a few symbols. Returns the end x. */
int  opm_text(int x, int y, const char *s, int scale, uint16_t c);
int  opm_text_width(const char *s, int scale);

/* Angles: 1024 units per turn. Results are Q15. */
int32_t opm_sin(int a);
int32_t opm_cos(int a);

/* Rotate+translate transform for vector models. */
typedef struct { int32_t s, c; int tx, ty; } opm_xf_t;
opm_xf_t opm_xf(int angle, int tx, int ty);
void opm_xf_pt(const opm_xf_t *t, int x, int y, int *ox, int *oy);
void opm_xf_line(const opm_xf_t *t, int x0, int y0, int x1, int y1, uint16_t c);
void opm_xf_tri(const opm_xf_t *t, int x0, int y0, int x1, int y1, int x2, int y2, uint16_t c);

/* Deterministic per-frame noise (identical across bands of one frame). */
static inline uint32_t opm_hash(uint32_t x)
{
    x ^= x >> 16; x *= 0x7FEB352Du; x ^= x >> 15; x *= 0x846CA68Bu; x ^= x >> 16; return x;
}
#endif
