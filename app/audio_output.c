/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception */
#include "audio_output.h"
#include "fm1_engine_control.h"
#include "fm1_audio_io.h"

static unsigned output_gain;

/* ---- FM1 OP3N kid-safe output ceiling --------------------------------------
 * Hard-coded, applied LAST: after master gain, USB playback and Bluetooth
 * music are mixed, so no input path can bypass it. Q23 full scale = 8388607.
 * 0.5 FS (-6.02 dBFS) is a PLACEHOLDER: calibrate on the real device with the
 * loudest expected headphones and an SPL meter / ear simulator, then lower it
 * until the worst case meets your target (85 dB(A) is the usual limit for
 * children's headphones). No menu, preset or MIDI message can change it. */
#define OPM_CEILING_Q23 4194304
#define OPM_THRESH_Q23  3738383 /* ceiling - 1 dB: limiter target, clamp is backstop */
#define OPM_RELEASE_SHIFT 12    /* release time constant 4096 samples ~ 93 ms */
_Static_assert(OPM_CEILING_Q23 > 0 && OPM_CEILING_Q23 <= 8388607, "ceiling within Q23");
_Static_assert(OPM_THRESH_Q23 < OPM_CEILING_Q23, "threshold below ceiling");
static int32_t limiter_env;
static FM1_AUDIO_CODE void opm_limit(int32_t *output)
{
    int32_t env = limiter_env;
    for (unsigned i = 0; i < FM1_BLOCK_WORDS; i += 2) {
        int32_t l = output[i], r = output[i + 1];
        int32_t al = l < 0 ? -l : l, ar = r < 0 ? -r : r;
        int32_t peak = al > ar ? al : ar;
        /* Instant attack, exponential release, stereo-linked. */
        if (peak > env) env = peak;
        else env -= (env - peak) >> OPM_RELEASE_SHIFT;
        if (env > OPM_THRESH_Q23) {
            /* Q15 gain with a 32-bit divide only: a 64-bit divide would call a
             * compiler-rt helper that may live in Flash, which audio code must
             * not touch (it runs from RAM during Flash writes). THRESH<<8 fits
             * in 32 bits and env>>7 >= 29206 here, so no overflow or /0. */
            int32_t gain = (int32_t)(((uint32_t)OPM_THRESH_Q23 << 8) / ((uint32_t)env >> 7));
            l = (int32_t)(((int64_t)l * gain) >> 15);
            r = (int32_t)(((int64_t)r * gain) >> 15);
        }
        output[i]     = l > OPM_CEILING_Q23 ? OPM_CEILING_Q23 : l < -OPM_CEILING_Q23 ? -OPM_CEILING_Q23 : l;
        output[i + 1] = r > OPM_CEILING_Q23 ? OPM_CEILING_Q23 : r < -OPM_CEILING_Q23 ? -OPM_CEILING_Q23 : r;
    }
    limiter_env = env;
}
static int32_t usb_pcm[FM1_BLOCK_WORDS] __attribute__((aligned(32)));
static int16_t bluetooth_pcm[FM1_BLOCK_WORDS] __attribute__((aligned(32)));

void fm1_output_reset(void) { output_gain = 0; limiter_env = 0; }
static inline __attribute__((always_inline)) int32_t limit(int64_t value)
{
    /* Bias signed 24-bit PCM into [0, 2^24). Unsigned arithmetic also
     * defines the check at the int64 limits, without two wide compares. */
    if (((uint64_t)value + 8388608U) >> 24)
        return value < 0 ? -8388608 : 8388607;
    return (int32_t)value;
}
FM1_AUDIO_CODE void fm1_output_mix(int32_t *output)
{
    if (!output) return;
    unsigned target = fm1_engine_output_level();
    if (target > 127U) target = 127U;
    int delta = (int)target - (int)output_gain;
    output_gain = (unsigned)((int)output_gain + (delta > 8 ? 8 : delta < -8 ? -8 : delta));

    uint16_t usb_gain[2] = {0, 0};
    int usb_frames = fm1_usb_audio_read(usb_pcm, FM1_BLOCK_FRAMES, usb_gain);
    unsigned left = ((unsigned)usb_gain[0] * output_gain) >> 7;
    unsigned right = ((unsigned)usb_gain[1] * output_gain) >> 7;
    /* Accumulate USB before clipping, then add Bluetooth.
     * Discard the block if the USB stream resets. */
    for (unsigned i = 0; i < FM1_BLOCK_WORDS; ++i) {
        int64_t value = ((int64_t)output[i] * output_gain) >> 7;
        if (usb_frames == FM1_BLOCK_FRAMES)
            value += ((int64_t)usb_pcm[i] * ((i & 1U) ? right : left)) >> 7;
        output[i] = usb_frames == -2 ? 0 : limit(value);
    }
    if (fm1_bluetooth_audio_read(bluetooth_pcm, FM1_BLOCK_FRAMES) == FM1_BLOCK_FRAMES) {
        /* output is already clipped to 24 bits. Adding signed 16-bit PCM
         * times at most 254 fits in 25 bits, so this stage needs no int64. */
        for (unsigned i = 0; i < FM1_BLOCK_WORDS; ++i) {
            int32_t value = output[i] + (int32_t)bluetooth_pcm[i] * (int32_t)(output_gain * 2U);
            output[i] = value > 8388607 ? 8388607 : value < -8388608 ? -8388608 : value;
        }
    }
    opm_limit(output);   /* FM1 OP3N: must stay the final stage before the DAC */
    (void)fm1_usb_audio_write(output, FM1_BLOCK_FRAMES);
}
