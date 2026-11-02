/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception
 * FM1 OP3N band rasterizer. Font glyphs are from the stock FM1 screen.c. */
#include "opm_gfx.h"

static uint16_t *band;
static int band_top;
static int cx0, cy0, cx1, cy1;    /* active clip, inclusive, screen coordinates */

void opm_gfx_band(uint16_t *pixels, int top)
{
    band = pixels; band_top = top;
    opm_gfx_unclip();
}
void opm_gfx_unclip(void)
{
    cx0 = 0; cx1 = OPM_W - 1; cy0 = band_top; cy1 = band_top + OPM_BAND_ROWS - 1;
}
void opm_gfx_clip(int x0, int y0, int x1, int y1)
{
    opm_gfx_unclip();
    if (x0 > cx0) cx0 = x0;
    if (x1 < cx1) cx1 = x1;
    if (y0 > cy0) cy0 = y0;
    if (y1 < cy1) cy1 = y1;
}

void opm_clear(uint16_t c)
{
    for (int i = 0; i < OPM_W * OPM_BAND_ROWS; ++i) band[i] = c;
}
void opm_pixel(int x, int y, uint16_t c)
{
    if (x >= cx0 && x <= cx1 && y >= cy0 && y <= cy1) band[(y - band_top) * OPM_W + x] = c;
}
void opm_hline(int x0, int x1, int y, uint16_t c)
{
    if (y < cy0 || y > cy1) return;
    if (x0 > x1) { int t = x0; x0 = x1; x1 = t; }
    if (x1 < cx0 || x0 > cx1) return;
    if (x0 < cx0) x0 = cx0;
    if (x1 > cx1) x1 = cx1;
    uint16_t *p = &band[(y - band_top) * OPM_W + x0];
    for (int x = x0; x <= x1; ++x) *p++ = c;
}
void opm_line(int x0, int y0, int x1, int y1, uint16_t c)
{
    /* Trivially reject lines entirely above/below the band. */
    if ((y0 < cy0 && y1 < cy0) || (y0 > cy1 && y1 > cy1)) return;
    int dx = x1 > x0 ? x1 - x0 : x0 - x1, sx = x0 < x1 ? 1 : -1;
    int dy = y1 > y0 ? y0 - y1 : y1 - y0, sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    for (;;) {
        opm_pixel(x0, y0, c);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}
void opm_rect(int x, int y, int w, int h, uint16_t c)
{
    opm_hline(x, x + w - 1, y, c);
    opm_hline(x, x + w - 1, y + h - 1, c);
    for (int j = y + 1; j < y + h - 1; ++j) { opm_pixel(x, j, c); opm_pixel(x + w - 1, j, c); }
}
void opm_fill_rect(int x, int y, int w, int h, uint16_t c)
{
    int y0 = y < cy0 ? cy0 : y, y1 = y + h - 1 > cy1 ? cy1 : y + h - 1;
    for (int j = y0; j <= y1; ++j) opm_hline(x, x + w - 1, j, c);
}
void opm_circle(int cx, int cy, int r, uint16_t c)
{
    if (cy + r < cy0 || cy - r > cy1) return;
    int x = r, y = 0, err = 1 - r;
    while (x >= y) {
        opm_pixel(cx + x, cy + y, c); opm_pixel(cx - x, cy + y, c);
        opm_pixel(cx + x, cy - y, c); opm_pixel(cx - x, cy - y, c);
        opm_pixel(cx + y, cy + x, c); opm_pixel(cx - y, cy + x, c);
        opm_pixel(cx + y, cy - x, c); opm_pixel(cx - y, cy - x, c);
        ++y;
        if (err < 0) err += 2 * y + 1; else { --x; err += 2 * (y - x) + 1; }
    }
}
void opm_fill_circle(int cx, int cy, int r, uint16_t c)
{
    if (cy + r < cy0 || cy - r > cy1) return;
    int x = r, y = 0, err = 1 - r;
    while (x >= y) {
        opm_hline(cx - x, cx + x, cy + y, c); opm_hline(cx - x, cx + x, cy - y, c);
        opm_hline(cx - y, cx + y, cy + x, c); opm_hline(cx - y, cx + y, cy - x, c);
        ++y;
        if (err < 0) err += 2 * y + 1; else { --x; err += 2 * (y - x) + 1; }
    }
}
void opm_fill_triangle(int x0, int y0, int x1, int y1, int x2, int y2, uint16_t c)
{
    int t;
    if (y0 > y1) { t = y0; y0 = y1; y1 = t; t = x0; x0 = x1; x1 = t; }
    if (y1 > y2) { t = y1; y1 = y2; y2 = t; t = x1; x1 = x2; x2 = t; }
    if (y0 > y1) { t = y0; y0 = y1; y1 = t; t = x0; x0 = x1; x1 = t; }
    if (y2 < cy0 || y0 > cy1) return;
    if (y0 == y2) {
        int lo = x0 < x1 ? x0 : x1, hi = x0 > x1 ? x0 : x1;
        if (x2 < lo) lo = x2;
        if (x2 > hi) hi = x2;
        opm_hline(lo, hi, y0, c);
        return;
    }
    int ys = y0 < cy0 ? cy0 : y0, ye = y2 > cy1 ? cy1 : y2;
    for (int y = ys; y <= ye; ++y) {
        int xa = x0 + (x2 - x0) * (y - y0) / (y2 - y0);
        int xb = (y < y1) ? x0 + (x1 - x0) * (y - y0) / (y1 - y0)
                          : (y2 == y1 ? x1 : x1 + (x2 - x1) * (y - y1) / (y2 - y1));
        opm_hline(xa, xb, y, c);
    }
}

/* ---- font: five columns per glyph, bit 0 at the top ---- */
static const uint8_t font[36][5] = {
    {0x3e, 0x51, 0x49, 0x45, 0x3e}, {0, 0x42, 0x7f, 0x40, 0},       {0x42, 0x61, 0x51, 0x49, 0x46},
    {0x21, 0x41, 0x45, 0x4b, 0x31}, {0x18, 0x14, 0x12, 0x7f, 0x10}, {0x27, 0x45, 0x45, 0x45, 0x39},
    {0x3c, 0x4a, 0x49, 0x49, 0x30}, {0x01, 0x71, 0x09, 0x05, 0x03}, {0x36, 0x49, 0x49, 0x49, 0x36},
    {0x06, 0x49, 0x49, 0x29, 0x1e}, {0x7e, 0x11, 0x11, 0x11, 0x7e}, {0x7f, 0x49, 0x49, 0x49, 0x36},
    {0x3e, 0x41, 0x41, 0x41, 0x22}, {0x7f, 0x41, 0x41, 0x22, 0x1c}, {0x7f, 0x49, 0x49, 0x49, 0x41},
    {0x7f, 0x09, 0x09, 0x09, 0x01}, {0x3e, 0x41, 0x49, 0x49, 0x7a}, {0x7f, 0x08, 0x08, 0x08, 0x7f},
    {0, 0x41, 0x7f, 0x41, 0},       {0x20, 0x40, 0x41, 0x3f, 0x01}, {0x7f, 0x08, 0x14, 0x22, 0x41},
    {0x7f, 0x40, 0x40, 0x40, 0x40}, {0x7f, 0x02, 0x0c, 0x02, 0x7f}, {0x7f, 0x04, 0x08, 0x10, 0x7f},
    {0x3e, 0x41, 0x41, 0x41, 0x3e}, {0x7f, 0x09, 0x09, 0x09, 0x06}, {0x3e, 0x41, 0x51, 0x21, 0x5e},
    {0x7f, 0x09, 0x19, 0x29, 0x46}, {0x46, 0x49, 0x49, 0x49, 0x31}, {0x01, 0x01, 0x7f, 0x01, 0x01},
    {0x3f, 0x40, 0x40, 0x40, 0x3f}, {0x1f, 0x20, 0x40, 0x20, 0x1f}, {0x3f, 0x40, 0x38, 0x40, 0x3f},
    {0x63, 0x14, 0x08, 0x14, 0x63}, {0x07, 0x08, 0x70, 0x08, 0x07}, {0x61, 0x51, 0x49, 0x45, 0x43}};
static const uint8_t symbols[][6] = {
    {'-', 8, 8, 8, 8, 8}, {'/', 0x60, 0x18, 6, 1, 0}, {'+', 8, 8, 0x3e, 8, 8},
    {'.', 0, 0x60, 0x60, 0, 0}, {':', 0, 0x36, 0x36, 0, 0}, {'!', 0, 0, 0x5f, 0, 0},
    {'>', 0x41, 0x22, 0x14, 8, 0}, {'<', 8, 0x14, 0x22, 0x41, 0}};

static const uint8_t *glyph(char ch)
{
    if (ch >= 'a' && ch <= 'z') ch = (char)(ch - 'a' + 'A');
    if (ch >= '0' && ch <= '9') return font[ch - '0'];
    if (ch >= 'A' && ch <= 'Z') return font[10 + ch - 'A'];
    for (unsigned i = 0; i < sizeof symbols / sizeof symbols[0]; ++i)
        if (symbols[i][0] == (uint8_t)ch) return &symbols[i][1];
    return 0;
}
int opm_text_width(const char *s, int scale)
{
    int n = 0;
    while (s[n]) ++n;
    return n ? n * 6 * scale - scale : 0;
}
int opm_text(int x, int y, const char *s, int scale, uint16_t c)
{
    if (y + 7 * scale < cy0 || y > cy1) return x + opm_text_width(s, scale) + scale;
    for (; *s; ++s, x += 6 * scale) {
        const uint8_t *g = glyph(*s);
        if (!g) continue;
        for (int col = 0; col < 5; ++col)
            for (int row = 0; row < 7; ++row)
                if (g[col] & (1u << row)) opm_fill_rect(x + col * scale, y + row * scale, scale, scale, c);
    }
    return x;
}

/* ---- trig: quarter-wave Q15 table, 1024 units per turn ---- */
static const int16_t sin_quarter[257] = {
    0, 201, 402, 603, 804, 1005, 1206, 1407, 1608, 1809, 2009, 2210, 2410, 2611, 2811, 3012,
    3212, 3412, 3612, 3811, 4011, 4210, 4410, 4609, 4808, 5007, 5205, 5404, 5602, 5800, 5998, 6195,
    6393, 6590, 6786, 6983, 7179, 7375, 7571, 7767, 7962, 8157, 8351, 8545, 8739, 8933, 9126, 9319,
    9512, 9704, 9896, 10087, 10278, 10469, 10659, 10849, 11039, 11228, 11417, 11605, 11793, 11980, 12167, 12353,
    12539, 12725, 12910, 13094, 13279, 13462, 13645, 13828, 14010, 14191, 14372, 14553, 14732, 14912, 15090, 15269,
    15446, 15623, 15800, 15976, 16151, 16325, 16499, 16673, 16846, 17018, 17189, 17360, 17530, 17700, 17869, 18037,
    18204, 18371, 18537, 18703, 18868, 19032, 19195, 19357, 19519, 19680, 19841, 20000, 20159, 20317, 20475, 20631,
    20787, 20942, 21096, 21250, 21403, 21554, 21705, 21856, 22005, 22154, 22301, 22448, 22594, 22739, 22884, 23027,
    23170, 23311, 23452, 23592, 23731, 23870, 24007, 24143, 24279, 24413, 24547, 24680, 24811, 24942, 25072, 25201,
    25329, 25456, 25582, 25708, 25832, 25955, 26077, 26198, 26319, 26438, 26556, 26674, 26790, 26905, 27019, 27133,
    27245, 27356, 27466, 27575, 27683, 27790, 27896, 28001, 28105, 28208, 28310, 28411, 28510, 28609, 28706, 28803,
    28898, 28992, 29085, 29177, 29268, 29358, 29447, 29534, 29621, 29706, 29791, 29874, 29956, 30037, 30117, 30195,
    30273, 30349, 30424, 30498, 30571, 30643, 30714, 30783, 30852, 30919, 30985, 31050, 31113, 31176, 31237, 31297,
    31356, 31414, 31470, 31526, 31580, 31633, 31685, 31736, 31785, 31833, 31880, 31926, 31971, 32014, 32057, 32098,
    32137, 32176, 32213, 32250, 32285, 32318, 32351, 32382, 32412, 32441, 32469, 32495, 32521, 32545, 32567, 32589,
    32609, 32628, 32646, 32663, 32678, 32692, 32705, 32717, 32728, 32737, 32745, 32752, 32757, 32761, 32765, 32766,
    32767
};
int32_t opm_sin(int a)
{
    a &= 1023;
    if (a < 256) return sin_quarter[a];
    if (a < 512) return sin_quarter[512 - a];
    if (a < 768) return -sin_quarter[a - 512];
    return -sin_quarter[1024 - a];
}
int32_t opm_cos(int a) { return opm_sin(a + 256); }

opm_xf_t opm_xf(int angle, int tx, int ty)
{
    opm_xf_t t = { opm_sin(angle), opm_cos(angle), tx, ty };
    return t;
}
void opm_xf_pt(const opm_xf_t *t, int x, int y, int *ox, int *oy)
{
    *ox = (int)((x * t->c - y * t->s) >> 15) + t->tx;
    *oy = (int)((x * t->s + y * t->c) >> 15) + t->ty;
}
void opm_xf_line(const opm_xf_t *t, int x0, int y0, int x1, int y1, uint16_t c)
{
    int ax, ay, bx, by;
    opm_xf_pt(t, x0, y0, &ax, &ay); opm_xf_pt(t, x1, y1, &bx, &by);
    opm_line(ax, ay, bx, by, c);
}
void opm_xf_tri(const opm_xf_t *t, int x0, int y0, int x1, int y1, int x2, int y2, uint16_t c)
{
    int ax, ay, bx, by, dx, dy;
    opm_xf_pt(t, x0, y0, &ax, &ay); opm_xf_pt(t, x1, y1, &bx, &by); opm_xf_pt(t, x2, y2, &dx, &dy);
    opm_fill_triangle(ax, ay, bx, by, dx, dy, c);
}
