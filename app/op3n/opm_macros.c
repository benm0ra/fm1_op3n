/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception */
#include "opm_macros.h"

static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }
static int signed_macro(uint8_t m) { return (int)m - OPM_MACRO_NEUTRAL; }  /* -64..+63 */
static int streq(const char *a, const char *b)
{
    while (*a && *a == *b) { ++a; ++b; }
    return *a == *b;
}

/* DX7 algorithm carriers, bit i = OP(i+1). */
static const uint8_t carriers[32] = {
    0x05, 0x05, 0x09, 0x09, 0x15, 0x15, 0x05, 0x05, 0x05, 0x09, 0x09, 0x05, 0x05, 0x05, 0x05, 0x01,
    0x01, 0x01, 0x19, 0x0B, 0x1B, 0x1D, 0x1B, 0x1F, 0x1F, 0x0B, 0x0B, 0x25, 0x17, 0x27, 0x1F, 0x3F};
unsigned opm_dx7_carriers(unsigned alg0) { return carriers[alg0 & 31u]; }

/* Per-track ADSR sustain/release for the DX7 tracks (engine units 0..100). */
static const uint8_t dx_sustain[OPM_TRACKS] = { 0, 0, 72, 10 };
static const uint8_t dx_release[OPM_TRACKS] = { 0, 0, 22, 45 };

void opm_dx7_target(unsigned track, const uint8_t base[156], const uint8_t m[OPM_MACROS],
                    uint8_t out[156], uint8_t adsr[4])
{
    for (unsigned i = 0; i < 156; ++i) out[i] = base[i];
    const unsigned car = opm_dx7_carriers(base[DX_ALG]);
    const int p = signed_macro(m[OPM_PUNCH]);
    const int c = signed_macro(m[OPM_COLOR]);
    const int t = m[OPM_TWIST];
    const int pu = m[OPM_PUNCH];

    for (unsigned op = 1; op <= 6; ++op) {
        const unsigned o = dx_op(op);
        const int carrier = (car >> (op - 1)) & 1u;
        /* PUNCH: faster operator attack (R1) on every op. */
        out[o + DX_R1] = (uint8_t)clampi(base[o + DX_R1] + p * 25 / 64, 0, 99);
        if (!carrier) {
            /* PUNCH: faster modulator decay (R2) = brighter, shorter "pluck".
             * COLOR: modulator output level. DX7 OL is ~0.75 dB per step, so
             * adding steps scales the FM index multiplicatively: +-20 steps
             * is about +-15 dB of modulation, from mellow sine to bright. */
            out[o + DX_R2] = (uint8_t)clampi(base[o + DX_R2] + p * 30 / 64, 0, 99);
            out[o + DX_OL] = (uint8_t)clampi(base[o + DX_OL] + c * 20 / 64, 0, 99);
        }
    }
    /* COLOR: feedback (sine -> saw-like on the fed-back operator). */
    out[DX_FB] = (uint8_t)clampi(base[DX_FB] + c * 3 / 64, 0, 7);

    /* TWIST on LEAD: LFO "wobble" — vibrato depth/speed plus a little tremolo. */
    if (track == OPM_LEAD) {
        out[DX_LFO_WAVE]  = 0;                                  /* triangle */
        out[DX_LFO_SPEED] = (uint8_t)(30 + t * 40 / 127);
        out[DX_LFO_PMD]   = (uint8_t)(t * 40 / 127);
        out[DX_LFO_AMD]   = (uint8_t)(t * 30 / 127);
        if (out[DX_PMS] < 3) out[DX_PMS] = 3;
    }

    /* PUNCH: track amplitude envelope. Attack 320 ms -> 0 ms on a squared
     * curve (most knob travel is in the snappy region); decay shortens. */
    const int soft = OPM_MACRO_MAX - pu;
    adsr[0] = (uint8_t)(soft * soft * 40 / (OPM_MACRO_MAX * OPM_MACRO_MAX));
    adsr[1] = (uint8_t)(soft * 60 / OPM_MACRO_MAX);
    adsr[2] = dx_sustain[track];
    adsr[3] = dx_release[track];
}

enum { B_CUTOFF, B_RESO, B_ENVMOD, B_DECAY, B_ACCENT, B_WAVE, B_TUNE, B_VOLUME,
       B_DRIVE, B_DRVTYPE, B_SLIDE, B_ACCDEC };

void opm_303_target(const uint8_t base[12], const uint8_t m[OPM_MACROS], uint8_t out[12])
{
    for (unsigned i = 0; i < 12; ++i) out[i] = base[i];
    const int p = m[OPM_PUNCH], c = m[OPM_COLOR], t = m[OPM_TWIST];
    /* COLOR = "how far the filter opens". Resonance is capped at 84/127 so
     * the bass never self-oscillates into a whistle. */
    out[B_CUTOFF] = (uint8_t)(12 + c * 96 / 127);
    out[B_ENVMOD] = (uint8_t)(16 + c * 88 / 127);
    out[B_RESO]   = (uint8_t)(40 + c * 44 / 127);
    /* PUNCH = short and accented. */
    out[B_DECAY]  = (uint8_t)(104 - p * 84 / 127);
    out[B_ACCENT] = (uint8_t)(24 + p * 88 / 127);
    /* TWIST = drive (soft clip type once engaged). */
    out[B_DRIVE]  = (uint8_t)(t * 100 / 127);
    if (t > 8) out[B_DRVTYPE] = 1;
}

unsigned opm_808_target(const char *name, unsigned base, unsigned max, const uint8_t m[OPM_MACROS])
{
    const int p = m[OPM_PUNCH], c = m[OPM_COLOR], t = m[OPM_TWIST];
    int v = (int)base;
    if (streq(name, "Attack"))      v = p;                              /* click */
    else if (streq(name, "Decay"))  v = (int)base * (191 - p) / 127;    /* 1.5x .. 0.5x */
    else if (streq(name, "Tone"))   v = c;
    else if (streq(name, "Snappy")) v = 32 + c * 80 / 127;
    else if (streq(name, "Tune"))   v = (int)base + (c - OPM_MACRO_NEUTRAL) / 4; /* +-16 */
    else if (streq(name, "Drive"))  v = t * 96 / 127;
    return (unsigned)clampi(v, 0, (int)max);
}

void opm_space_sends(unsigned track, unsigned space, uint8_t sends[2])
{
    /* Drums and bass get less space so the groove stays tight. */
    static const uint8_t reverb_max[OPM_TRACKS] = { 9, 5, 15, 15 };
    static const uint8_t delay_max[OPM_TRACKS]  = { 6, 3, 13, 15 };
    const unsigned s = space > OPM_MACRO_MAX ? OPM_MACRO_MAX : space;
    sends[0] = (uint8_t)((s * reverb_max[track] + 63) / OPM_MACRO_MAX);
    sends[1] = (uint8_t)((s * s * delay_max[track] + 8064) / (OPM_MACRO_MAX * OPM_MACRO_MAX));
}

void opm_twist_arp(unsigned twist, uint8_t *rate, uint8_t *gate)
{
    /* Low TWIST: lazy 1/8 notes, long gate. High: fizzing 1/32, short gate. */
    *rate = (uint8_t)(1 + (twist >= 43) + (twist >= 96));
    *gate = (uint8_t)(twist < 32 ? 2 : twist < 96 ? 1 : 0);
}

static const int8_t snap[2][12] = {
    /* C  C# D  D# E  F  F# G  G# A  A#  B */
    {  0, 0, 2, 2, 4, 5, 5, 7, 7, 9, 9, 11 },   /* major      */
    {  0, 0, 2, 2, 4, 4, 7, 7, 7, 9, 9, 12 },   /* pentatonic */
};
uint8_t opm_quantize(uint8_t note, unsigned scale)
{
    if (note > 127) return note;
    int q = (int)note - note % 12 + snap[scale != 0][note % 12];
    return (uint8_t)(q > 127 ? 127 : q);
}
unsigned opm_in_scale(uint8_t note, unsigned scale)
{
    return opm_quantize(note, scale) == note;
}
