/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception
 * FM1 OP3N: the 4-knob abstraction mapped onto real FM1 engine parameters.
 *
 * Pure integer functions with no engine calls, so they are unit-testable.
 * Every function takes the track's "neutral" base values (captured once at
 * setup) plus the four macro positions (0..127, 64 = neutral) and returns
 * absolute target values. screen.c turns targets into groove_* edit deltas.
 *
 *   Track   Source       PUNCH            COLOR              SPACE        TWIST
 *   DRUMS   808 kit      Attack, Decay    Tone/Snappy/Tune   sends        808 Drive
 *   BASS    303          Decay, Accent    Cutoff/EnvMod/Reso sends        303 Drive
 *   LEAD    DX7 6-op     ADSR + mod rates Modulator OL + FB  sends        LFO wobble
 *   MAGIC   DX7 + ARP    ADSR + mod rates Modulator OL + FB  sends        Arp rate/gate
 */
#ifndef OPM_MACROS_H
#define OPM_MACROS_H
#include <stdint.h>

enum { OPM_DRUMS, OPM_BASS, OPM_LEAD, OPM_MAGIC, OPM_TRACKS };
enum { OPM_PUNCH, OPM_COLOR, OPM_SPACE, OPM_TWIST, OPM_MACROS };
enum { OPM_MACRO_MAX = 127, OPM_MACRO_NEUTRAL = 64 };

/* Expanded DX7 voice: OP6 at 0 ... OP1 at 105, 21 bytes each; globals 126+. */
enum {
    DX_OP_BYTES = 21,
    DX_R1 = 0, DX_R2 = 1, DX_OL = 16,           /* per-operator offsets   */
    DX_ALG = 134, DX_FB = 135, DX_LFO_SPEED = 137, DX_LFO_PMD = 139,
    DX_LFO_AMD = 140, DX_LFO_WAVE = 142, DX_PMS = 143
};
static inline unsigned dx_op(unsigned op1_based) { return (6u - op1_based) * DX_OP_BYTES; }

/* Bit i set = OP(i+1) is a carrier in DX7 algorithm alg0 (0..31). */
unsigned opm_dx7_carriers(unsigned alg0);

/* DX7 tracks (LEAD, MAGIC). adsr[] is the engine's track ADSR override
 * (0..100 each: attack 8 ms/unit, decay 100+8 ms/unit, sustain 10..80 %,
 * release 100+19 ms/unit). */
void opm_dx7_target(unsigned track, const uint8_t base[156], const uint8_t m[OPM_MACROS],
                    uint8_t out[156], uint8_t adsr[4]);

/* BASS (303 parameter order from bass303.h, 12 values). */
void opm_303_target(const uint8_t base[12], const uint8_t m[OPM_MACROS], uint8_t out[12]);

/* DRUMS: one 808 parameter, identified by its descriptor name. */
unsigned opm_808_target(const char *name, unsigned base, unsigned max, const uint8_t m[OPM_MACROS]);

/* SPACE -> per-track reverb/delay send (0..15 each). */
void opm_space_sends(unsigned track, unsigned space, uint8_t sends[2]);

/* MAGIC TWIST -> arpeggiator rate (0=1/4..3=1/32) and gate (0..3). */
void opm_twist_arp(unsigned twist, uint8_t *rate, uint8_t *gate);

/* Hard scale lock. Ties snap down; B snaps up to C in pentatonic. */
enum { OPM_SCALE_MAJOR, OPM_SCALE_PENTATONIC };
uint8_t opm_quantize(uint8_t note, unsigned scale);
unsigned opm_in_scale(uint8_t note, unsigned scale);
#endif
