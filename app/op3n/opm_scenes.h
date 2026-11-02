/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception
 * FM1 OP3N engine screens. One frozen opm_frame_t is drawn band by band. */
#ifndef OPM_SCENES_H
#define OPM_SCENES_H
#include <stdint.h>
#include "opm_macros.h"

enum { OPM_SCENE_TOP = 16, OPM_SCENE_BOTTOM = 175, OPM_HUD_TOP = 176 };

typedef struct {
    uint32_t frame;                       /* animation tick                      */
    uint8_t  track;                       /* active FM1 OP3N track 0..3           */
    uint8_t  macro[OPM_TRACKS][OPM_MACROS];
    uint8_t  playing, recording, scale, arp_on;
    int8_t   octave;
    uint8_t  step, length;                /* playhead and sequence length        */
    uint16_t tempo;
    uint16_t steps;                       /* active steps on the visible page    */
    uint8_t  beat;                        /* 255 on each step, decays            */
    uint8_t  pulse;                       /* 255 on each Magic note, decays      */
    uint8_t  dance;                       /* Magic note counter (robot eyes)     */
    uint8_t  level;                       /* active track meter 0..255           */
    uint8_t  fill;                        /* FX punch-in held                    */
    const char *notice;                   /* transient centre message or NULL    */
} opm_frame_t;

/* Draw everything that intersects the current band (see opm_gfx_band). */
void opm_scene_band(const opm_frame_t *f, int band_top);
/* Rows [OPM_HUD_TOP, 239] depend only on this; screen.c skips unchanged HUD bands. */
uint32_t opm_hud_signature(const opm_frame_t *f);
#endif
