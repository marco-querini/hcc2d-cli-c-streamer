/*
 * hcc2d_streamer.c
 * HCC2D Streamer — Reed-Solomon sharded HCC2D symbol streamer
 * Copyright Marco Querini
 * SPDX-License-Identifier: Apache-2.0
 * Version 0.9.0
 * Date 2026-05-31
 *
 * Build:  cc -O2 -Wall $(pkg-config --cflags sdl2) hcc2d_streamer.c \
 *             $(pkg-config --libs sdl2) -lz -lm
 * Usage:  ./hcc2d_streamer [--colors 2|4|8] [--ec-level L|M|Q|H] [--version N]
 *                      [--fps N] myfile.pdf
 *         (or --mode qr|hcc2d4|hcc2d8 instead of --colors; not both)
 *
 * Specification compliance:
 *   Intended to conform to the HCC2D Code Specification version 0.9.0.
 *   Reference specification PDF: https://hcc2d.com/hcc2d_specification_v0.9.0.pdf
 *
 * Description:
 *   Reads a file, splits it into Reed-Solomon shards with dynamic k/m groups,
 *   encodes each shard as an HCC2D symbol or a standard QR Code at a fixed
 *   version (shard size is derived to be the byte-perfect max payload for
 *   the chosen mode/EC level/version), then streams all symbols in an SDL2
 *   window in an infinite loop. Press ESC or close the window to exit.
 *
 * Companion app:
 *   To receive this stream and recover the file, point a smartphone camera
 *   at the window using the HCC2D Decoder app:
 *     iOS (Apple App Store):       https://apps.apple.com/us/app/hcc2d-decoder/id6762202762
 *     Android (Google Play):       https://play.google.com/store/apps/details?id=com.hcc2d.decoder
 *     Android (Huawei AppGallery): https://appgallery.cloud.huawei.com/marketshare/app/C117478101
 *
 * Terms of service / no-warranty notice:
 *   This file is provided "as is", without warranties or conditions of any
 *   kind, express or implied, including but not limited to merchantability,
 *   fitness for a particular purpose, and noninfringement. Use of this file
 *   is entirely at your own risk. The author assumes no responsibility or
 *   liability for any damages, losses, claims, unreadable or non-decodable
 *   codes, failed scans, data loss, or other consequences arising from the
 *   use of this file.
 *
 * Apache 2.0 summary:
 *   You may use, copy, modify, and redistribute this file, including for
 *   commercial purposes. You must keep the applicable copyright/license
 *   notices and state significant modifications when redistributing modified
 *   versions. This summary is informational only; the LICENSE file and
 *   Apache License 2.0 text control the actual legal terms.
 *   Full license text: https://www.apache.org/licenses/LICENSE-2.0
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <getopt.h>
#include <zlib.h>
#include <SDL2/SDL.h>

/* =========================================================================
 * Memory helpers
 * malloc/calloc wrappers that abort with a clear message on allocation
 * failure. Most call sites in this file do not (and cannot reasonably)
 * recover from OOM mid-encode, so failing fast beats a NULL-deref crash.
 * ========================================================================= */

static void *xmalloc(size_t size) {
    void *p = malloc(size);
    if (!p) {
        fprintf(stderr, "Error: out of memory (requested %zu bytes)\n", size);
        exit(1);
    }
    return p;
}

static void *xcalloc(size_t nmemb, size_t size) {
    void *p = calloc(nmemb, size);
    if (!p) {
        fprintf(stderr, "Error: out of memory (requested %zu x %zu bytes)\n",
                nmemb, size);
        exit(1);
    }
    return p;
}

/* Parse a base-10 integer strictly within [min_value, max_value]: rejects
 * empty strings, trailing garbage ("12x"), and out-of-range values, instead
 * of atoi's silent fallback to 0 for any unparseable input. Mirrors
 * parse_int_option() in the sibling hcc2d_encoder CLI. */
static int parse_int_option(const char *name, const char *value,
                             int min_value, int max_value, int *out) {
    char *end = NULL;
    errno = 0;
    long parsed = strtol(value, &end, 10);
    if (errno != 0 || end == value || *end != '\0' ||
        parsed < min_value || parsed > max_value) {
        fprintf(stderr, "Error: --%s must be an integer from %d to %d.\n",
                name, min_value, max_value);
        return -1;
    }
    *out = (int)parsed;
    return 0;
}

/* =========================================================================
 * Constants
 * ========================================================================= */

/* Defaults for the three group-sizing constants below; each is now an
 * independently overridable CLI parameter (--k-max, --parity-num,
 * --parity-den) rather than a fixed value -- see the k_max/parity_num/
 * parity_den locals in main(). None of these three are required or
 * derived by the HCC2DST protocol itself; the only figure the protocol
 * actually imposes is rs_k+rs_m <= N_LIMIT. With the defaults, k=150
 * computes to m=105 and n=k+m lands exactly on N_LIMIT. */
#define K_MAX_DEFAULT            150
#define PARITY_NUM_DEFAULT        70
#define PARITY_DEN_DEFAULT       100
#define N_LIMIT          255
/* shard data bytes per symbol; always overwritten in main() from
 * --colors/--ec-level/--version before first use, no meaningful default */
static int g_shard_data_bytes = 0;
#define MAX_SYMBOLS     10000

typedef struct {
    uint32_t k;           /* data shards in this group */
    uint32_t m;           /* parity shards */
    uint32_t n;           /* total = k + m */
    uint32_t data_start;  /* index of first data shard for this group */
} GroupParams;

#define MODE_BITS_BYTE     0x4
#define VERSION_INFO_POLY  0x1F25
#define TYPE_INFO_POLY     0x537
#define TYPE_INFO_MASK_PAT 0x5412
#define MAX_BITS           250000
#define MAX_BLOCKS         256
#define MAX_BLOCK_BYTES    160
#define UNASSIGNED         (-1)

#define DEFAULT_DISPLAY_FPS 10
#define DEFAULT_QUIET_ZONE 4

/* EC level bits for type-info encoding: L=1 M=0 Q=3 H=2 */
static const int EC_LEVEL_BITS_TAB[4] = {1, 0, 3, 2};

static int ec_level_index(char c) {
    switch (c) {
        case 'L': return 0; case 'M': return 1;
        case 'Q': return 2; case 'H': return 3;
    }
    return -1;
}

/* =========================================================================
 * Color palettes  (spec §5)
 * ========================================================================= */

typedef struct { uint8_t r, g, b; } RGB;

/* hcc2d4 model1 palette (screen), spec §5. Indices 1 and 2 are the HCC2D4
 * red and cyan, not the HCC2D8 dark-red/light-cyan pair they were
 * previously copied from. */
static const RGB PALETTE_4_MODEL1[4] = {
    {0,0,0},{220,0,0},{0,200,220},{255,255,255}
};

/* hcc2d8 model1 palette (screen) */
static const RGB PALETTE_8_MODEL1[8] = {
    {0,0,0},{200,0,0},{0,130,0},{0,60,180},
    {0,215,235},{255,220,50},{255,130,230},{255,255,255}
};

/* Parse a complete custom HCC2D palette supplied as a quoted command-line
 * list, for example "0,0,0;220,0,0;0,200,220;255,255,255". */
static int parse_palette_rgb(const char *text, int palette_size, RGB out[8])
{
    char *copy = (char *)xmalloc(strlen(text) + 1);
    strcpy(copy, text);
    char *save = NULL;
    char *token = strtok_r(copy, ",; \t\r\n", &save);
    int component = 0;
    while (token) {
        if (component >= palette_size * 3) {
            fprintf(stderr, "Error: --palette-rgb has more than %d RGB components\n",
                    palette_size * 3);
            free(copy);
            return -1;
        }
        int value;
        if (parse_int_option("palette RGB component", token, 0, 255, &value) != 0) {
            free(copy);
            return -1;
        }
        RGB *color = &out[component / 3];
        if (component % 3 == 0) color->r = (uint8_t)value;
        if (component % 3 == 1) color->g = (uint8_t)value;
        if (component % 3 == 2) color->b = (uint8_t)value;
        component++;
        token = strtok_r(NULL, ",; \t\r\n", &save);
    }
    free(copy);
    if (component != palette_size * 3) {
        fprintf(stderr, "Error: --palette-rgb requires exactly %d RGB components; got %d\n",
                palette_size * 3, component);
        return -1;
    }
    if (out[0].r != 0 || out[0].g != 0 || out[0].b != 0 ||
        out[palette_size - 1].r != 255 || out[palette_size - 1].g != 255 ||
        out[palette_size - 1].b != 255) {
        fprintf(stderr, "Error: --palette-rgb must keep index 0 black and the last index white\n");
        return -1;
    }
    for (int i = 0; i < palette_size; i++) {
        for (int j = i + 1; j < palette_size; j++) {
            if (out[i].r == out[j].r && out[i].g == out[j].g && out[i].b == out[j].b) {
                fprintf(stderr, "Error: --palette-rgb duplicates palette indices %d and %d\n", i, j);
                return -1;
            }
        }
    }
    return 0;
}

/* Standard QR Code Model 2 black/white palette */
static const RGB PALETTE_QR[2] = {
    {0,0,0},{255,255,255}
};

#define DEFAULT_SYMBOL_VERSION 30
#define MAX_HCC2D_PLANES 3

typedef enum {
    /* ORDER_SHUFFLE removed: disabled to keep transmission order always
     * reproducible (interleaved, no re-randomization). */
    ORDER_SEQUENTIAL
} PlayOrderMode;

/* =========================================================================
 * QR Model 2 version table (versions 1–40)
 * ========================================================================= */

typedef struct { int ecpb, count1, data1, count2, data2; } ECEntry;

typedef struct {
    int number;
    int align_count;
    int align[9];
    ECEntry ec[4];
} QRVersion;

static const QRVersion QR_VERSIONS[40] = {
    { 1,0,{0},
      {{7,1,19,0,0},{10,1,16,0,0},{13,1,13,0,0},{17,1,9,0,0}}},
    { 2,2,{6,18},
      {{10,1,34,0,0},{16,1,28,0,0},{22,1,22,0,0},{28,1,16,0,0}}},
    { 3,2,{6,22},
      {{15,1,55,0,0},{26,1,44,0,0},{18,2,17,0,0},{22,2,13,0,0}}},
    { 4,2,{6,26},
      {{20,1,80,0,0},{18,2,32,0,0},{26,2,24,0,0},{16,4,9,0,0}}},
    { 5,2,{6,30},
      {{26,1,108,0,0},{24,2,43,0,0},{18,2,15,2,16},{22,2,11,2,12}}},
    { 6,2,{6,34},
      {{18,2,68,0,0},{16,4,27,0,0},{24,4,19,0,0},{28,4,15,0,0}}},
    { 7,3,{6,22,38},
      {{20,2,78,0,0},{18,4,31,0,0},{18,2,14,4,15},{26,4,13,1,14}}},
    { 8,3,{6,24,42},
      {{24,2,97,0,0},{22,2,38,2,39},{22,4,18,2,19},{26,4,14,2,15}}},
    { 9,3,{6,26,46},
      {{30,2,116,0,0},{22,3,36,2,37},{20,4,16,4,17},{24,4,12,4,13}}},
    {10,3,{6,28,50},
      {{18,2,68,2,69},{26,4,43,1,44},{24,6,19,2,20},{28,6,15,2,16}}},
    {11,3,{6,30,54},
      {{20,4,81,0,0},{30,1,50,4,51},{28,4,22,4,23},{24,3,12,8,13}}},
    {12,3,{6,32,58},
      {{24,2,92,2,93},{22,6,36,2,37},{26,4,20,6,21},{28,7,14,4,15}}},
    {13,3,{6,34,62},
      {{26,4,107,0,0},{22,8,37,1,38},{24,8,20,4,21},{22,12,11,4,12}}},
    {14,4,{6,26,46,66},
      {{30,3,115,1,116},{24,4,40,5,41},{20,11,16,5,17},{24,11,12,5,13}}},
    {15,4,{6,26,48,70},
      {{22,5,87,1,88},{24,5,41,5,42},{30,5,24,7,25},{24,11,12,7,13}}},
    {16,4,{6,26,50,74},
      {{24,5,98,1,99},{28,7,45,3,46},{24,15,19,2,20},{30,3,15,13,16}}},
    {17,4,{6,30,54,78},
      {{28,1,107,5,108},{28,10,46,1,47},{28,1,22,15,23},{28,2,14,17,15}}},
    {18,4,{6,30,56,82},
      {{30,5,120,1,121},{26,9,43,4,44},{28,17,22,1,23},{28,2,14,19,15}}},
    {19,4,{6,30,58,86},
      {{28,3,113,4,114},{26,3,44,11,45},{26,17,21,4,22},{26,9,13,16,14}}},
    {20,4,{6,34,62,90},
      {{28,3,107,5,108},{26,3,41,13,42},{30,15,24,5,25},{28,15,15,10,16}}},
    {21,5,{6,28,50,72,94},
      {{28,4,116,4,117},{26,17,42,0,0},{28,17,22,6,23},{30,19,16,6,17}}},
    {22,5,{6,26,50,74,98},
      {{28,2,111,7,112},{28,17,46,0,0},{30,7,24,16,25},{24,34,13,0,0}}},
    {23,5,{6,30,54,78,102},
      {{30,4,121,5,122},{28,4,47,14,48},{30,11,24,14,25},{30,16,15,14,16}}},
    {24,5,{6,28,54,80,106},
      {{30,6,117,4,118},{28,6,45,14,46},{30,11,24,16,25},{30,30,16,2,17}}},
    {25,5,{6,32,58,84,110},
      {{26,8,106,4,107},{28,8,47,13,48},{30,7,24,22,25},{30,22,15,13,16}}},
    {26,5,{6,30,58,86,114},
      {{28,10,114,2,115},{28,19,46,4,47},{28,28,22,6,23},{30,33,16,4,17}}},
    {27,5,{6,34,62,90,118},
      {{30,8,122,4,123},{28,22,45,3,46},{30,8,23,26,24},{30,12,15,28,16}}},
    {28,6,{6,26,50,74,98,122},
      {{30,3,117,10,118},{28,3,45,23,46},{30,4,24,31,25},{30,11,15,31,16}}},
    {29,6,{6,30,54,78,102,126},
      {{30,7,116,7,117},{28,21,45,7,46},{30,1,23,37,24},{30,19,15,26,16}}},
    {30,6,{6,26,52,78,104,130},
      {{30,5,115,10,116},{28,19,47,10,48},{30,15,24,25,25},{30,23,15,25,16}}},
    {31,6,{6,30,56,82,108,134},
      {{30,13,115,3,116},{28,2,46,29,47},{30,42,24,1,25},{30,23,15,28,16}}},
    {32,6,{6,34,60,86,112,138},
      {{30,17,115,0,0},{28,10,46,23,47},{30,10,24,35,25},{30,19,15,35,16}}},
    {33,6,{6,30,58,86,114,142},
      {{30,17,115,1,116},{28,14,46,21,47},{30,29,24,19,25},{30,11,15,46,16}}},
    {34,6,{6,34,62,90,118,146},
      {{30,13,115,6,116},{28,14,46,23,47},{30,44,24,7,25},{30,59,16,1,17}}},
    {35,7,{6,30,54,78,102,126,150},
      {{30,12,121,7,122},{28,12,47,26,48},{30,39,24,14,25},{30,22,15,41,16}}},
    {36,7,{6,24,50,76,102,128,154},
      {{30,6,121,14,122},{28,6,47,34,48},{30,46,24,10,25},{30,2,15,64,16}}},
    {37,7,{6,28,54,80,106,132,158},
      {{30,17,122,4,123},{28,29,46,14,47},{30,49,24,10,25},{30,24,15,46,16}}},
    {38,7,{6,32,58,84,110,136,162},
      {{30,4,122,18,123},{28,13,46,32,47},{30,48,24,14,25},{30,42,15,32,16}}},
    {39,7,{6,26,54,82,110,138,166},
      {{30,20,117,4,118},{28,40,47,7,48},{30,43,24,22,25},{30,10,15,67,16}}},
    {40,7,{6,30,58,86,114,142,170},
      {{30,19,118,6,119},{28,18,47,31,48},{30,34,24,34,25},{30,20,15,61,16}}},
};

static int plane_count_for_mode(const char *mode) {
    if (strcmp(mode, "qr") == 0) return 1;
    if (strcmp(mode, "hcc2d4") == 0) return 2;
    return 3;
}

static int palette_size_for_mode(const char *mode) {
    return 1 << plane_count_for_mode(mode);
}

static const QRVersion *version_for_mode(int version) {
    if (version < 1 || version > 40)
        return NULL;
    return &QR_VERSIONS[version - 1];
}

/* =========================================================================
 * GF(256) tables + init + multiply  (primitive polynomial 0x011D)
 * ========================================================================= */

static int gf_exp[512];
static int gf_log[256];

static void gf256_init(void) {
    int x = 1;
    for (int i = 0; i < 255; i++) {
        gf_exp[i] = x;
        gf_log[x] = i;
        x <<= 1;
        if (x & 0x100) x ^= 0x011D;
    }
    for (int i = 255; i < 512; i++)
        gf_exp[i] = gf_exp[i - 255];
}

static int gf_mul(int a, int b) {
    if (a == 0 || b == 0) return 0;
    return gf_exp[gf_log[a] + gf_log[b]];
}

/* GF multiplicative inverse: gf_inv(a) = gf_exp[255 - gf_log[a]] */
static int gf_inv(int a) {
    if (a == 0) return 0; /* undefined, treat as 0 */
    return gf_exp[255 - gf_log[a]];
}

/* =========================================================================
 * BitBuffer
 * ========================================================================= */

typedef struct { uint8_t *bits; int count; } BitBuffer;

static void bb_alloc(BitBuffer *bb) {
    bb->bits = (uint8_t *)xmalloc(MAX_BITS);
    bb->count = 0;
}

static void bb_free(BitBuffer *bb) {
    free(bb->bits);
    bb->bits = NULL;
    bb->count = 0;
}

static void bb_append_bit(BitBuffer *bb, int bit) {
    if (bb->count >= MAX_BITS) {
        fprintf(stderr, "Error: internal bit buffer overflow (max %d bits)\n", MAX_BITS);
        exit(1);
    }
    bb->bits[bb->count++] = (uint8_t)(bit & 1);
}

static void bb_append_bits(BitBuffer *bb, int value, int count) {
    for (int s = count - 1; s >= 0; s--)
        bb_append_bit(bb, (value >> s) & 1);
}

static void bb_to_bytes(const BitBuffer *bb, int bit_offset, int num_bytes, uint8_t *out) {
    for (int bi = 0; bi < num_bytes; bi++) {
        int val = 0;
        for (int k = 0; k < 8; k++) {
            int src = bit_offset + bi * 8 + k;
            val = (val << 1) | (src < bb->count ? bb->bits[src] : 0);
        }
        out[bi] = (uint8_t)val;
    }
}

/* =========================================================================
 * Reed-Solomon polynomial arithmetic for QR EC generation
 * ========================================================================= */

static void poly_mul(const int *a, int la, const int *b, int lb, int *out) {
    int n = la + lb - 1;
    for (int i = 0; i < n; i++) out[i] = 0;
    for (int i = 0; i < la; i++)
        for (int j = 0; j < lb; j++)
            out[i + j] ^= gf_mul(a[i], b[j]);
}

static void rs_gen_poly(int degree, int *out, int *out_len) {
    out[0] = 1;
    *out_len = 1;
    int tmp[64];
    for (int i = 0; i < degree; i++) {
        int factor[2] = {1, gf_exp[i]};
        poly_mul(out, *out_len, factor, 2, tmp);
        (*out_len)++;
        memcpy(out, tmp, (size_t)(*out_len) * sizeof(int));
    }
}

static void rs_remainder(const uint8_t *data, int data_len, int ec_words, uint8_t *rem) {
    int gen[64], gen_len;
    rs_gen_poly(ec_words, gen, &gen_len);
    memset(rem, 0, (size_t)ec_words);
    for (int i = 0; i < data_len; i++) {
        int factor = data[i] ^ rem[0];
        memmove(rem, rem + 1, (size_t)(ec_words - 1));
        rem[ec_words - 1] = 0;
        if (factor)
            for (int j = 0; j < ec_words; j++)
                rem[j] ^= (uint8_t)gf_mul(gen[j + 1], factor);
    }
}

/* =========================================================================
 * Matrix helpers
 * ========================================================================= */

#define CELL(mat, dim, y, x) ((mat)[(y) * (dim) + (x)])

static int *alloc_matrix(int dim) {
    int *m = (int *)xmalloc((size_t)dim * dim * sizeof(int));
    for (int i = 0; i < dim * dim; i++) m[i] = UNASSIGNED;
    return m;
}

static void set_if_inside(int *mat, int dim, int x, int y, int val) {
    if (x >= 0 && x < dim && y >= 0 && y < dim)
        CELL(mat, dim, y, x) = val;
}

static void embed_finder(int *mat, int dim, int x0, int y0) {
    for (int y = 0; y < 7; y++)
        for (int x = 0; x < 7; x++) {
            int dx = abs(x - 3), dy = abs(y - 3);
            CELL(mat, dim, y0 + y, x0 + x) = ((dx > dy ? dx : dy) != 2) ? 1 : 0;
        }
    for (int i = -1; i <= 7; i++) {
        set_if_inside(mat, dim, x0 + i, y0 - 1, 0);
        set_if_inside(mat, dim, x0 + i, y0 + 7, 0);
        set_if_inside(mat, dim, x0 - 1, y0 + i, 0);
        set_if_inside(mat, dim, x0 + 7, y0 + i, 0);
    }
}

static void embed_finders(int *mat, int dim) {
    embed_finder(mat, dim, 0,       0);
    embed_finder(mat, dim, dim - 7, 0);
    embed_finder(mat, dim, 0,       dim - 7);
}

static void embed_dark_module(int *mat, int dim) {
    CELL(mat, dim, dim - 8, 8) = 1;
}

static void embed_alignment(int *mat, int dim, int x0, int y0) {
    for (int y = 0; y < 5; y++)
        for (int x = 0; x < 5; x++) {
            int dx = abs(x - 2), dy = abs(y - 2);
            CELL(mat, dim, y0 + y, x0 + x) = ((dx > dy ? dx : dy) != 1) ? 1 : 0;
        }
}

static void embed_alignments(int *mat, int dim, const int *centers, int count) {
    if (count < 1) return;
    int last = centers[count - 1];
    for (int yi = 0; yi < count; yi++)
        for (int xi = 0; xi < count; xi++) {
            int cy = centers[yi], cx = centers[xi];
            if ((cx == 6 && cy == 6) || (cx == 6 && cy == last) || (cx == last && cy == 6))
                continue;
            embed_alignment(mat, dim, cx - 2, cy - 2);
        }
}

static void embed_timing(int *mat, int dim) {
    for (int i = 8; i < dim - 8; i++) {
        int bit = ((i + 1) % 2) ? 1 : 0;
        CELL(mat, dim, 6, i) = bit;
        CELL(mat, dim, i, 6) = bit;
    }
}

/* =========================================================================
 * BCH error-correction codes for type-info and version-info fields
 * ========================================================================= */

static int msb_pos(int v) {
    int p = 0;
    while (v) { p++; v >>= 1; }
    return p;
}

static int bch_code(int value, int poly) {
    int msb = msb_pos(poly);
    value <<= (msb - 1);
    while (msb_pos(value) >= msb)
        value ^= poly << (msb_pos(value) - msb);
    return value;
}

static void embed_type_info(int *mat, int dim, char ec_level, int mask_pattern) {
    static const int cx[15] = {8,8,8,8,8,8,8,8,7,5,4,3,2,1,0};
    static const int cy[15] = {0,1,2,3,4,5,7,8,8,8,8,8,8,8,8};
    int ti = (EC_LEVEL_BITS_TAB[ec_level_index(ec_level)] << 3) | mask_pattern;
    int val = ((ti << 10) | bch_code(ti, TYPE_INFO_POLY)) ^ TYPE_INFO_MASK_PAT;
    for (int i = 0; i < 15; i++) {
        int bit = (val >> i) & 1;
        CELL(mat, dim, cy[i], cx[i]) = bit;
        if (i < 8)
            CELL(mat, dim, 8, dim - i - 1) = bit;
        else
            CELL(mat, dim, dim - 7 + (i - 8), 8) = bit;
    }
}

static void embed_version_info(int *mat, int dim, int ver) {
    if (ver < 7) return;
    int val = (ver << 12) | bch_code(ver, VERSION_INFO_POLY);
    int t = 0;
    for (int i = 0; i < 6; i++)
        for (int j = 0; j < 3; j++) {
            int bit = (val >> t++) & 1;
            CELL(mat, dim, dim - 11 + j, i) = bit;
            CELL(mat, dim, i, dim - 11 + j) = bit;
        }
}

/* =========================================================================
 * QR data mask formulas
 * ========================================================================= */

static int get_mask_bit(int mask, int x, int y) {
    switch (mask) {
        case 0: return (y + x) % 2 == 0;
        case 1: return y % 2 == 0;
        case 2: return x % 3 == 0;
        case 3: return (y + x) % 3 == 0;
        case 4: return ((y / 2) + (x / 3)) % 2 == 0;
        case 5: return (y * x) % 6 == 0;
        case 6: return ((y * x) % 6) < 3;
        case 7: return (y + x + ((y * x) % 3)) % 2 == 0;
    }
    return 0;
}

static void embed_data_bits(int *mat, int dim, const uint8_t *data_bits, int num_bits,
                             int mask_pattern) {
    int bit_idx = 0, direction = -1, x = dim - 1, y = dim - 1;
    while (x > 0) {
        if (x == 6) x--;
        while (y >= 0 && y < dim) {
            for (int xx = x; xx >= x - 1; xx--) {
                if (CELL(mat, dim, y, xx) != UNASSIGNED) continue;
                int bit = (bit_idx < num_bits) ? data_bits[bit_idx] : 0;
                bit_idx++;
                if (get_mask_bit(mask_pattern, xx, y)) bit ^= 1;
                CELL(mat, dim, y, xx) = bit;
            }
            y += direction;
        }
        direction = -direction;
        y += direction;
        x -= 2;
    }
}

/* =========================================================================
 * QR mask penalty rules
 * ========================================================================= */

static int penalty_rule1(const int *mat, int dim) {
    int penalty = 0;
    for (int horiz = 0; horiz <= 1; horiz++) {
        for (int i = 0; i < dim; i++) {
            int run = 0, prev = -1;
            for (int j = 0; j < dim; j++) {
                int bit = horiz ? CELL(mat, dim, i, j) : CELL(mat, dim, j, i);
                if (bit == prev) {
                    run++;
                } else {
                    if (run >= 5) penalty += 3 + (run - 5);
                    run = 1;
                    prev = bit;
                }
            }
            if (run >= 5) penalty += 3 + (run - 5);
        }
    }
    return penalty;
}

static int penalty_rule2(const int *mat, int dim) {
    int penalty = 0;
    for (int y = 0; y < dim - 1; y++)
        for (int x = 0; x < dim - 1; x++) {
            int v = CELL(mat, dim, y, x);
            if (v == CELL(mat, dim, y,     x + 1) &&
                v == CELL(mat, dim, y + 1, x    ) &&
                v == CELL(mat, dim, y + 1, x + 1))
                penalty += 3;
        }
    return penalty;
}

static int penalty_rule3(const int *mat, int dim) {
    static const int finder[7] = {1, 0, 1, 1, 1, 0, 1};
    int penalties = 0;

    for (int y = 0; y < dim; y++) {
        for (int x = 0; x <= dim - 7; x++) {
            int match = 1;
            for (int k = 0; k < 7; k++)
                if (CELL(mat, dim, y, x + k) != finder[k]) { match = 0; break; }
            if (!match) continue;

            int ls = (x >= 4) ? x - 4 : 0;
            int ll = x - ls;
            int rs = x + 7;
            int rl = (rs + 4 <= dim) ? 4 : dim - rs;
            if (rl < 0) rl = 0;

            int lwhite = 1;
            for (int k = 0; k < ll; k++)
                if (CELL(mat, dim, y, ls + k)) { lwhite = 0; break; }
            int rwhite = 1;
            for (int k = 0; k < rl; k++)
                if (CELL(mat, dim, y, rs + k)) { rwhite = 0; break; }

            if ((ll > 0 && lwhite) || (rl == 0 || rwhite))
                penalties++;
        }
    }

    for (int x = 0; x < dim; x++) {
        for (int y = 0; y <= dim - 7; y++) {
            int match = 1;
            for (int k = 0; k < 7; k++)
                if (CELL(mat, dim, y + k, x) != finder[k]) { match = 0; break; }
            if (!match) continue;

            int ts = (y >= 4) ? y - 4 : 0;
            int tl = y - ts;
            int bs = y + 7;
            int bl = (bs + 4 <= dim) ? 4 : dim - bs;
            if (bl < 0) bl = 0;

            int twhite = 1;
            for (int k = 0; k < tl; k++)
                if (CELL(mat, dim, ts + k, x)) { twhite = 0; break; }
            int bwhite = 1;
            for (int k = 0; k < bl; k++)
                if (CELL(mat, dim, bs + k, x)) { bwhite = 0; break; }

            if ((tl > 0 && twhite) || (bl == 0 || bwhite))
                penalties++;
        }
    }

    return penalties * 40;
}

static int penalty_rule4(const int *mat, int dim) {
    int total = dim * dim, dark = 0;
    for (int i = 0; i < total; i++) dark += mat[i];
    return abs(dark * 2 - total) * 10 / total * 10;
}

static int mask_penalty(const int *mat, int dim) {
    return penalty_rule1(mat, dim) + penalty_rule2(mat, dim)
         + penalty_rule3(mat, dim) + penalty_rule4(mat, dim);
}

/* =========================================================================
 * Payload framing
 * BYTE mode: 4-bit mode indicator || count field || payload bytes.
 * HCC2D always uses a 16-bit count field. Standard QR Byte mode uses an
 * 8-bit count for versions 1-9 and 16-bit for versions 10-40 (spec §7.4.3).
 * ========================================================================= */

static void build_payload_bits(BitBuffer *bb, const uint8_t *payload, int len,
                                int count_bits) {
    bb_append_bits(bb, MODE_BITS_BYTE, 4);
    bb_append_bits(bb, len, count_bits);
    for (int i = 0; i < len; i++)
        bb_append_bits(bb, payload[i], 8);
}

static void build_header_bits(BitBuffer *bb, const uint8_t *payload, int len) {
    build_payload_bits(bb, payload, len, 16);
}

static int qr_byte_count_bits(int version) {
    return version <= 9 ? 8 : 16;
}

/* Header overhead in bytes for a given mode/version: 4-bit mode indicator
 * plus the count field, rounded up to whole bytes. HCC2D always uses a
 * fixed 16-bit count (3 bytes); QR uses qr_byte_count_bits(version). */
static int header_overhead_bytes(const char *mode, int version) {
    int count_bits = (strcmp(mode, "qr") == 0) ? qr_byte_count_bits(version) : 16;
    return (4 + count_bits + 7) / 8;
}

static void terminate_bits(BitBuffer *bb, int num_data_bytes) {
    int capacity = num_data_bytes * 8;
    int pad = capacity - bb->count;
    if (pad < 0) pad = 0;
    if (pad > 4) pad = 4;
    for (int i = 0; i < pad; i++) bb_append_bit(bb, 0);
    while (bb->count % 8) bb_append_bit(bb, 0);
    static const uint8_t PAD[2] = {0xEC, 0x11};
    int pi = 0;
    while (bb->count < capacity) { bb_append_bits(bb, PAD[pi % 2], 8); pi++; }
}

/* =========================================================================
 * RS block layout and interleaving
 * ========================================================================= */

static void get_block_layout(int num_total, int num_data, int num_blocks, int block_id,
                              int *data_len_out, int *ec_len_out) {
    int g2 = num_total % num_blocks;
    int g1 = num_blocks - g2;
    int t1 = num_total / num_blocks;
    int d1 = num_data  / num_blocks;
    if (block_id < g1) {
        *data_len_out = d1;       *ec_len_out = t1 - d1;
    } else {
        *data_len_out = d1 + 1;   *ec_len_out = t1 - d1;
    }
}

static BitBuffer interleave_ec(const BitBuffer *data_bb, int num_total,
                                int num_data, int num_blocks) {
    static uint8_t bdata[MAX_BLOCKS][MAX_BLOCK_BYTES];
    static uint8_t bec  [MAX_BLOCKS][MAX_BLOCK_BYTES];
    int bdata_len[MAX_BLOCKS], bec_len[MAX_BLOCKS];

    if (num_blocks > MAX_BLOCKS) {
        fprintf(stderr, "Error: too many RS blocks (%d > MAX_BLOCKS=%d)\n",
                num_blocks, MAX_BLOCKS);
        exit(1);
    }

    uint8_t *src = (uint8_t *)xmalloc((size_t)num_data);
    bb_to_bytes(data_bb, 0, num_data, src);

    int offset = 0, max_d = 0, max_e = 0;
    for (int b = 0; b < num_blocks; b++) {
        int dl, el;
        get_block_layout(num_total, num_data, num_blocks, b, &dl, &el);
        if (dl > MAX_BLOCK_BYTES || el > MAX_BLOCK_BYTES) {
            fprintf(stderr, "Error: RS block %d too large (data=%d ec=%d, max=%d)\n",
                    b, dl, el, MAX_BLOCK_BYTES);
            free(src);
            exit(1);
        }
        memcpy(bdata[b], src + offset, (size_t)dl);
        rs_remainder(bdata[b], dl, el, bec[b]);
        bdata_len[b] = dl;
        bec_len[b]   = el;
        offset += dl;
        if (dl > max_d) max_d = dl;
        if (el > max_e) max_e = el;
    }
    free(src);

    BitBuffer out;
    bb_alloc(&out);
    for (int i = 0; i < max_d; i++)
        for (int b = 0; b < num_blocks; b++)
            if (i < bdata_len[b]) bb_append_bits(&out, bdata[b][i], 8);
    for (int i = 0; i < max_e; i++)
        for (int b = 0; b < num_blocks; b++)
            if (i < bec_len[b]) bb_append_bits(&out, bec[b][i], 8);
    return out;
}

/* =========================================================================
 * Plane extraction
 * ========================================================================= */

static int extract_plane_bits(const uint8_t *bits, int total, int plane_offset,
                               int plane_count, uint8_t *out) {
    int n = 0;
    for (int i = plane_offset; i < total; i += plane_count)
        out[n++] = bits[i];
    return n;
}

/* =========================================================================
 * HCC2D Color Palette Pattern border
 * ========================================================================= */

static int border_color(int row, int col, int dim, int period) {
    if (row == -1 && col >= 8 && col < dim - 8)
        return (col - 8) % period;
    if (row == dim && col >= 8 && col < dim)
        return (col - 8) % period;
    if (col == -1) {
        int s = dim - 9;
        if (row >= 8 && row <= s)
            return (s - row) % period;
    }
    if (col == dim && row >= 8 && row < dim)
        return (row - 8) % period;
    return period - 1;
}

/* =========================================================================
 * Matrix construction
 * ========================================================================= */

static int *build_matrix(const uint8_t *data_bits, int num_bits, char ec_level,
                          const QRVersion *qrv, int mask_pattern) {
    int dim = 17 + 4 * qrv->number;
    int *mat = alloc_matrix(dim);
    embed_finders(mat, dim);
    embed_dark_module(mat, dim);
    embed_alignments(mat, dim, qrv->align, qrv->align_count);
    embed_timing(mat, dim);
    embed_type_info(mat, dim, ec_level, mask_pattern);
    embed_version_info(mat, dim, qrv->number);
    embed_data_bits(mat, dim, data_bits, num_bits, mask_pattern);
    for (int i = 0; i < dim * dim; i++)
        if (mat[i] == UNASSIGNED) mat[i] = 0;
    return mat;
}

static int *build_function_pattern(const QRVersion *qrv) {
    int dim = 17 + 4 * qrv->number;
    int *mat = alloc_matrix(dim);
    embed_finders(mat, dim);
    embed_dark_module(mat, dim);
    embed_alignments(mat, dim, qrv->align, qrv->align_count);
    embed_timing(mat, dim);
    embed_type_info(mat, dim, 'L', 0);
    if (qrv->number >= 7)
        embed_version_info(mat, dim, qrv->number);
    int *fp = (int *)xmalloc((size_t)dim * dim * sizeof(int));
    for (int i = 0; i < dim * dim; i++)
        fp[i] = (mat[i] != UNASSIGNED) ? 1 : 0;
    free(mat);
    return fp;
}

static int choose_mask(const uint8_t *proxy_bits, int proxy_len, char ec_level,
                        const QRVersion *qrv) {
    int best = 0, best_p = -1;
    int dim = 17 + 4 * qrv->number;
    for (int m = 0; m < 8; m++) {
        int *mat = build_matrix(proxy_bits, proxy_len, ec_level, qrv, m);
        int p = mask_penalty(mat, dim);
        free(mat);
        if (best_p < 0 || p < best_p) { best_p = p; best = m; }
    }
    return best;
}

/* =========================================================================
 * Module rendering
 * ========================================================================= */

static int *render_modules(int **planes, int plane_count, const QRVersion *qrv, int period) {
    int dim   = 17 + 4 * qrv->number;
    int full  = dim + 2;
    int white = period - 1;
    int *out  = (int *)xmalloc((size_t)full * full * sizeof(int));
    for (int i = 0; i < full * full; i++) out[i] = white;

    int *fp = build_function_pattern(qrv);

    for (int ry = -1; ry <= dim; ry++) {
        for (int rx = -1; rx <= dim; rx++) {
            int color;
            if (rx >= 0 && rx < dim && ry >= 0 && ry < dim) {
                int idx = ry * dim + rx;
                if (fp[idx]) {
                    color = planes[0][idx] ? 0 : white;
                } else {
                    color = 0;
                    for (int p = 0; p < plane_count; p++)
                        color = (color << 1) | planes[p][idx];
                }
            } else {
                color = border_color(ry, rx, dim, period);
            }
            out[(ry + 1) * full + (rx + 1)] = color;
        }
    }
    free(fp);
    return out;
}

/* =========================================================================
 * Rasterization
 * ========================================================================= */

static inline size_t packed_raster_size_bytes(int width, int height) {
    return ((size_t)width * (size_t)height + 1u) / 2u;
}

static inline void packed_raster_set(uint8_t *px, int width, int x, int y, uint8_t color) {
    size_t index = (size_t)y * (size_t)width + (size_t)x;
    size_t byte_index = index >> 1;
    if ((index & 1u) == 0u) {
        px[byte_index] = (uint8_t)((px[byte_index] & 0x0Fu) | ((color & 0x0Fu) << 4));
    } else {
        px[byte_index] = (uint8_t)((px[byte_index] & 0xF0u) | (color & 0x0Fu));
    }
}

static inline uint8_t packed_raster_get(const uint8_t *px, int width, int x, int y) {
    size_t index = (size_t)y * (size_t)width + (size_t)x;
    uint8_t packed = px[index >> 1];
    return (index & 1u) == 0u ? (uint8_t)((packed >> 4) & 0x0Fu) : (uint8_t)(packed & 0x0Fu);
}

static int choose_integer_scale_for_side(int full_dim, int quiet_zone, int target_side_px) {
    int total_modules = full_dim + quiet_zone * 2;
    if (total_modules <= 0 || target_side_px <= 0) return 1;
    int scale = target_side_px / total_modules;
    return scale > 0 ? scale : 1;
}

static uint8_t *rasterize(const int *modules, int full_dim, int scale, int quiet_zone,
                            int background, int *out_w, int *out_h) {
    int img_mods = full_dim + quiet_zone * 2;
    int img_size = img_mods * scale;
    size_t packed_size = packed_raster_size_bytes(img_size, img_size);
    uint8_t bg = (uint8_t)(background & 0x0F);
    uint8_t packed_bg = (uint8_t)((bg << 4) | bg);
    uint8_t *px  = (uint8_t *)xmalloc(packed_size);
    memset(px, packed_bg, packed_size);
    for (int my = 0; my < full_dim; my++) {
        for (int mx = 0; mx < full_dim; mx++) {
            uint8_t color = (uint8_t)(modules[my * full_dim + mx] & 0x0F);
            int bx = (mx + quiet_zone) * scale;
            int by = (my + quiet_zone) * scale;
            for (int dy = 0; dy < scale; dy++) {
                for (int dx = 0; dx < scale; dx++) {
                    packed_raster_set(px, img_size, bx + dx, by + dy, color);
                }
            }
        }
    }
    *out_w = img_size;
    *out_h = img_size;
    return px;
}

/* =========================================================================
 * EC block capacity helpers
 * ========================================================================= */

static int ec_total_sym(const ECEntry *e) {
    return e->count1 * (e->data1 + e->ecpb) + e->count2 * (e->data2 + e->ecpb);
}

static int ec_total_cw(const ECEntry *e) {
    return e->ecpb * (e->count1 + e->count2);
}

static int ec_num_blocks(const ECEntry *e) {
    return e->count1 + e->count2;
}

static ECEntry hcc2d_ec(const ECEntry *base, int multiplier) {
    ECEntry h;
    h.ecpb   = base->ecpb;
    h.count1 = base->count1 * multiplier;
    h.data1  = base->data1;
    h.count2 = base->count2 * multiplier;
    h.data2  = base->data2;
    return h;
}

/* =========================================================================
 * HCC2D encode
 * ========================================================================= */

typedef struct {
    const char *mode;
    char  ec_level;
    int   version_number;
    int   inner_dim;
    int   full_dim;
    int   mask_pattern;
    int   width, height;
    uint8_t *pixels; /* palette-index raster, packed 4 bpp */
} EncodedSymbol;

static int encode_hcc2d(const uint8_t *payload, int payload_len,
                         const char *mode, char ec_level, int version,
                         int target_side_px, int quiet_zone,
                         EncodedSymbol *sym, char *err) {
    int plane_count   = plane_count_for_mode(mode);
    int border_period = palette_size_for_mode(mode);
    int multiplier    = plane_count;
    int ec_idx        = ec_level_index(ec_level);
    int white_idx     = border_period - 1;

    if (ec_idx < 0) {
        snprintf(err, 256, "EC level must be one of L, M, Q, H");
        return -1;
    }

    int needed = payload_len + 3;

    const QRVersion *qrv = NULL;
    ECEntry hec = {0};

    if (version > 0) {
        qrv = version_for_mode(version);
        if (!qrv) {
            snprintf(err, 256, "version must be 1-40");
            return -1;
        }
        hec = hcc2d_ec(&qrv->ec[ec_idx], multiplier);
        int data_bytes = ec_total_sym(&hec) - ec_total_cw(&hec);
        if (data_bytes < needed) {
            snprintf(err, 256, "payload does not fit in version %d", version);
            return -1;
        }
    } else {
        for (int v = 1; v <= 40; v++) {
            const QRVersion *qv = &QR_VERSIONS[v - 1];
            ECEntry he = hcc2d_ec(&qv->ec[ec_idx], multiplier);
            int data_bytes = ec_total_sym(&he) - ec_total_cw(&he);
            if (data_bytes >= needed) { qrv = qv; hec = he; break; }
        }
        if (!qrv) {
            snprintf(err, 256, "payload does not fit in any supported version");
            return -1;
        }
    }

    int num_blocks = ec_num_blocks(&hec);
    int total_sym  = ec_total_sym(&hec);
    int data_bytes = total_sym - ec_total_cw(&hec);

    BitBuffer bits;
    bb_alloc(&bits);
    build_header_bits(&bits, payload, payload_len);
    terminate_bits(&bits, data_bytes);

    BitBuffer final = interleave_ec(&bits, total_sym, data_bytes, num_blocks);
    bb_free(&bits);

    int total_bits = final.count;

    uint8_t *plane_bits[MAX_HCC2D_PLANES] = {0};
    int      plane_lens[MAX_HCC2D_PLANES] = {0};
    for (int p = 0; p < plane_count; p++) {
        plane_bits[p] = (uint8_t *)xmalloc((size_t)(total_bits / plane_count + 2));
        plane_lens[p] = extract_plane_bits(final.bits, total_bits, p, plane_count,
                                           plane_bits[p]);
    }

    int proxy_len = plane_lens[0];
    uint8_t *proxy = (uint8_t *)xmalloc((size_t)proxy_len);
    for (int i = 0; i < proxy_len; i++) proxy[i] = plane_bits[0][i] ^ 1;
    int mask_pattern = choose_mask(proxy, proxy_len, ec_level, qrv);
    free(proxy);

    int *plane_mats[MAX_HCC2D_PLANES] = {0};
    for (int p = 0; p < plane_count; p++)
        plane_mats[p] = build_matrix(plane_bits[p], plane_lens[p],
                                     ec_level, qrv, mask_pattern);

    for (int p = 0; p < plane_count; p++) free(plane_bits[p]);
    bb_free(&final);

    int *modules = render_modules(plane_mats, plane_count, qrv, border_period);
    for (int p = 0; p < plane_count; p++) free(plane_mats[p]);

    int dim      = 17 + 4 * qrv->number;
    int full_dim = dim + 2;
    int scale    = choose_integer_scale_for_side(full_dim, quiet_zone, target_side_px);
    int w, h;
    uint8_t *pixels = rasterize(modules, full_dim, scale, quiet_zone, white_idx, &w, &h);
    free(modules);

    sym->mode           = mode;
    sym->ec_level       = ec_level;
    sym->version_number = qrv->number;
    sym->inner_dim      = dim;
    sym->full_dim       = full_dim;
    sym->mask_pattern   = mask_pattern;
    sym->width          = w;
    sym->height         = h;
    sym->pixels         = pixels;
    return 0;
}

/* =========================================================================
 * Standard QR Code Model 2 encode (single black/white plane, no HCC2D
 * color-palette border ring). Mirrors encode_hcc2d's pipeline but uses the
 * base QR_VERSIONS ECEntry directly (no HCC2D color-plane multiplier) and
 * the real QR Byte-mode count field width (qr_byte_count_bits).
 * ========================================================================= */

static int encode_qr(const uint8_t *payload, int payload_len,
                      char ec_level, int version,
                      int target_side_px, int quiet_zone,
                      EncodedSymbol *sym, char *err) {
    int ec_idx = ec_level_index(ec_level);
    if (ec_idx < 0) {
        snprintf(err, 256, "EC level must be one of L, M, Q, H");
        return -1;
    }

    const QRVersion *qrv = NULL;
    ECEntry qec = {0};

    if (version > 0) {
        if (version < 1 || version > 40) {
            snprintf(err, 256, "version must be 1-40"); return -1;
        }
        qrv = &QR_VERSIONS[version - 1];
        qec = qrv->ec[ec_idx];
        int data_bytes = ec_total_sym(&qec) - ec_total_cw(&qec);
        int needed_bits = 4 + qr_byte_count_bits(version) + payload_len * 8;
        if (needed_bits > data_bytes * 8) {
            snprintf(err, 256, "qr payload does not fit in version %d", version);
            return -1;
        }
    } else {
        for (int v = 1; v <= 40; v++) {
            const QRVersion *qv = &QR_VERSIONS[v - 1];
            ECEntry qe = qv->ec[ec_idx];
            int data_bytes = ec_total_sym(&qe) - ec_total_cw(&qe);
            int needed_bits = 4 + qr_byte_count_bits(v) + payload_len * 8;
            if (needed_bits <= data_bytes * 8) { qrv = qv; qec = qe; break; }
        }
        if (!qrv) {
            snprintf(err, 256, "qr payload does not fit in any supported version");
            return -1;
        }
    }

    int num_blocks = ec_num_blocks(&qec);
    int total_sym  = ec_total_sym(&qec);
    int data_bytes = total_sym - ec_total_cw(&qec);

    BitBuffer bits;
    bb_alloc(&bits);
    build_payload_bits(&bits, payload, payload_len, qr_byte_count_bits(qrv->number));
    terminate_bits(&bits, data_bytes);

    BitBuffer final = interleave_ec(&bits, total_sym, data_bytes, num_blocks);
    bb_free(&bits);

    int mask_pattern = choose_mask(final.bits, final.count, ec_level, qrv);
    int *mat = build_matrix(final.bits, final.count, ec_level, qrv, mask_pattern);
    bb_free(&final);

    int dim = 17 + 4 * qrv->number;
    int *modules = (int *)xmalloc((size_t)dim * dim * sizeof(int));
    for (int i = 0; i < dim * dim; i++)
        modules[i] = mat[i] ? 0 : 1; /* dark module -> palette index 0 (black) */
    free(mat);

    int scale = choose_integer_scale_for_side(dim, quiet_zone, target_side_px);
    int w, h;
    uint8_t *pixels = rasterize(modules, dim, scale, quiet_zone, 1 /* white */, &w, &h);
    free(modules);

    sym->mode           = "qr";
    sym->ec_level       = ec_level;
    sym->version_number = qrv->number;
    sym->inner_dim      = dim;
    sym->full_dim       = dim;
    sym->mask_pattern   = mask_pattern;
    sym->width          = w;
    sym->height         = h;
    sym->pixels         = pixels;
    return 0;
}

/* =========================================================================
 * HCC2DF memory-based wrapper
 * build_hcc2df_mem: takes a filename string + raw data bytes, wraps in HCC2DF
 * Returns malloced bytes; caller must free.
 * ========================================================================= */

static uint8_t *build_hcc2df_mem(const char *filename, const uint8_t *content, size_t fsz,
                                  size_t *out_len) {
    size_t fnl = strlen(filename);
    if (fnl == 0 || fnl > 127) {
        fprintf(stderr, "Warning: hcc2df filename length out of range (%zu)\n", fnl);
        if (fnl > 127) fnl = 127;
    }

    uint8_t  cflag    = 0;
    const uint8_t *stored   = content;
    size_t   stored_l = fsz;
    uint8_t *compressed = NULL;

    if (fsz >= 128) {
        uLong cb = compressBound((uLong)fsz);
        compressed = (uint8_t *)malloc(cb);
        if (compressed) {
            uLong cl = cb;
            if (compress2(compressed, &cl, content, (uLong)fsz, Z_DEFAULT_COMPRESSION) == Z_OK
                && (double)cl < (double)fsz * 0.9) {
                stored = compressed; stored_l = (size_t)cl; cflag = 1;
            } else {
                free(compressed);
                compressed = NULL;
            }
        }
        /* If the allocation or compression failed, fall through and store
         * the content uncompressed (cflag stays 0) rather than aborting —
         * compression here is an optimization, not a requirement. */
    }

    size_t pl = 6 + 1 + 1 + 1 + fnl + stored_l;
    uint8_t *payload = (uint8_t *)xmalloc(pl);
    uint8_t *p = payload;
    memcpy(p, "HCC2DF", 6);   p += 6;
    *p++ = 0x01;               /* wrapper version */
    *p++ = cflag;
    *p++ = (uint8_t)fnl;
    memcpy(p, filename, fnl);  p += fnl;
    memcpy(p, stored, stored_l);

    if (compressed) free(compressed);

    *out_len = pl;
    return payload;
}

/* =========================================================================
 * Reed-Solomon erasure coding — Cauchy matrix over GF(256)
 * Dynamic k data shards, m parity shards (k <= k_max, m computed from
 * parity_num/parity_den; see the --k-max/--parity-num/--parity-den CLI
 * options and their defaults K_MAX_DEFAULT/PARITY_NUM_DEFAULT/
 * PARITY_DEN_DEFAULT above)
 * Cauchy matrix entry [i][j] = gf_inv(i XOR (m + j))
 *   for i in 0..m-1, j in 0..k-1
 * ========================================================================= */

/* Compute parity shards from data shards.
 * data_shards: flat array, shard s at data_shards + s*g_shard_data_bytes
 * parity_shards: flat array, shard i at parity_shards + i*g_shard_data_bytes
 */
static void rs_encode_parity(const uint8_t *data_shards,
                              uint8_t *parity_shards,
                              int k, int m) {
    /* Build Cauchy matrix: cauchy[i*k+j] = gf_inv(i XOR (m + j)) */
    uint8_t *cauchy = (uint8_t *)xmalloc((size_t)m * k);
    for (int i = 0; i < m; i++)
        for (int j = 0; j < k; j++)
            cauchy[i * k + j] = (uint8_t)gf_inv(i ^ (m + j));

    /* parity[i][b] = XOR over j of (cauchy[i][j] * data_shards[j][b]) */
    for (int i = 0; i < m; i++) {
        memset(parity_shards + (size_t)i * g_shard_data_bytes, 0, g_shard_data_bytes);
        for (int j = 0; j < k; j++) {
            uint8_t c = cauchy[i * k + j];
            if (c == 0) continue;
            const uint8_t *dsj = data_shards + (size_t)j * g_shard_data_bytes;
            uint8_t       *psi = parity_shards + (size_t)i * g_shard_data_bytes;
            for (int b = 0; b < g_shard_data_bytes; b++)
                psi[b] ^= (uint8_t)gf_mul(c, dsj[b]);
        }
    }
    free(cauchy);
}

/* =========================================================================
 * StreamChunk: application-layer header inside every HCC2DF content field.
 * HCC2DF wrapper_version stays 0x01 (unchanged). A decoder identifies a
 * streaming shard by checking content[0..7] == "HCC2DST\0".
 * See PROTOCOL.md for the full two-layer design.
 * ========================================================================= */

typedef struct __attribute__((packed)) {
    char     magic[8];            /* "HCC2DST\0" — identifies streaming content */
    uint8_t  protocol_version;    /* StreamChunk protocol version = 0x02 */
    uint8_t  session_id[4];       /* random 4-byte session ID */
    uint32_t orig_size;           /* original file size before any compression */
    uint8_t  whole_compressed;    /* 1 = whole file was zlib-compressed before sharding */
    uint8_t  fname_len;           /* original filename length (max 32) */
    char     fname[32];           /* original filename (zero-padded) */
    uint16_t n_groups;            /* total RS groups in this session */
    uint16_t group_idx;           /* 0-based index of this shard's RS group */
    uint8_t  rs_k;                /* data shards in this group */
    uint8_t  rs_m;                /* parity shards in this group */
    uint8_t  shard_idx;           /* 0..rs_k+rs_m-1 */
    uint8_t  _pad;                /* reserved, set to 0 */
    uint32_t shard_bytes;         /* valid data bytes in this shard */
    uint32_t file_crc32;          /* v0x02: CRC-32 of the original file */
    uint32_t crc32;               /* v0x02: see stream_chunk_crc32() below */
} StreamChunk; /* 71 bytes */

/* The layout is wire format, so its size is normative, not incidental. The
 * shard crc32 stays last, so it covers every field ahead of it. */
typedef char stream_chunk_is_71_bytes[(sizeof(StreamChunk) == 71) ? 1 : -1];
typedef char stream_chunk_file_crc_at_63[(offsetof(StreamChunk, file_crc32) == 63) ? 1 : -1];
typedef char stream_chunk_crc_at_67[(offsetof(StreamChunk, crc32) == 67) ? 1 : -1];

/* Session-level digest: CRC-32 of the original file, before whole-file
 * compression and before sharding, identical in every shard of a session.
 *
 * The per-shard crc32 below verifies each shard as it arrives, so with
 * correct reassembly the file is right by construction — but "by
 * construction" covers the shards, not the code that orders, truncates,
 * inflates and concatenates them, nor the case of two sessions colliding on
 * a session_id. This field is what a receiver checks once, at completion,
 * against what it actually produced. When whole-file compression is on,
 * zlib's own Adler-32 already verifies the inflated stream; this covers the
 * uncompressed case too, and covers reassembly in both. */

/* Integrity check for one shard, protocol version 0x02.
 *
 * Covers the header with its own crc32 field taken as zero, immediately
 * followed by the shard payload at its FIXED size, padding included. Two
 * choices there are deliberate and neither is cosmetic.
 *
 * Covering the header, not just the payload. The symbol's own Reed-Solomon
 * layer works on independent blocks, so a block that mis-corrects — decodes
 * to a valid but wrong codeword, which no inner check catches — damages the
 * bytes of that block and leaves the others intact. The header sits in the
 * first block or two, so exactly that failure can hand the receiver a sound
 * payload carrying a corrupted group_idx or shard_idx. Range validation
 * cannot see it: shard 5 becoming shard 7 passes every bound. The receiver
 * would then file a good payload under the wrong index, and because the
 * erasure decoder selects its submatrix BY index, the whole group inverts
 * to garbage. Silently. An erasure code carries no integrity of its own; it
 * has to be handed inputs someone else has verified.
 *
 * Covering the fixed shard size, not shard_bytes. If the length fed to the
 * CRC came from a header field, verifying the CRC would require trusting a
 * field the CRC is supposed to protect. The fixed size closes that loop: it
 * follows from symbol version, colour mode and EC level, all of which the
 * receiver knows from the symbol before reading a single header byte.
 *
 * Computed on the uncompressed bytes, before the HCC2DF wrapper, which may
 * compress; the receiver therefore verifies after unwrapping and inflating,
 * and before using any header field to place the shard.
 *
 * CRC-32/ISO-HDLC (reflected 0xEDB88320, init and final xor 0xFFFFFFFF) —
 * zlib's own crc32(), so both ends share one implementation.
 */
static uint32_t stream_chunk_crc32(const StreamChunk *hdr,
                                   const uint8_t *payload, size_t payload_len)
{
    StreamChunk zeroed = *hdr;
    zeroed.crc32 = 0;
    uLong c = crc32(0L, Z_NULL, 0);
    c = crc32(c, (const Bytef *)&zeroed, (uInt)sizeof(StreamChunk));
    c = crc32(c, (const Bytef *)payload, (uInt)payload_len);
    return (uint32_t)c;
}


/* Store a filename in the 32-byte fname field, per PROTOCOL.md.
 *
 * Names that fit are stored whole. Longer ones keep their extension — the
 * last '.' and everything after it, when that is itself shorter than the
 * field — and lose the middle of the stem. A cut must never split a UTF-8
 * sequence, so it backs off over continuation bytes (0x80-0xBF) to the
 * previous character boundary; the field can therefore end up shorter than
 * 32 bytes, which is why fname_len exists.
 *
 * Leaving this to implementations would mean two conforming senders naming
 * the same file differently. */
static size_t utf8_floor(const char *s, size_t n) {
    while (n > 0 && ((unsigned char)s[n] & 0xC0) == 0x80) n--;
    return n;
}

static void set_fname_field(char field[32], uint8_t *len_out, const char *name) {
    size_t n = strlen(name);
    memset(field, 0, 32);
    if (n <= 32) {
        memcpy(field, name, n);
        *len_out = (uint8_t)n;
        return;
    }
    const char *dot = strrchr(name, '.');
    size_t ext = dot ? strlen(dot) : 0;
    if (ext > 0 && ext < 32) {
        size_t stem = utf8_floor(name, 32 - ext);
        memcpy(field, name, stem);
        memcpy(field + stem, dot, ext);
        *len_out = (uint8_t)(stem + ext);
    } else {
        size_t keep = utf8_floor(name, 32);
        memcpy(field, name, keep);
        *len_out = (uint8_t)keep;
    }
}

/* Build the full HCC2DF-wrapped payload for one shard.
 * Returns malloced bytes; caller must free. */
static uint8_t *build_shard_payload(
    const uint8_t *shard_data,        /* g_shard_data_bytes bytes */
    uint32_t       orig_size,         /* original file size before any compression */
    uint8_t        whole_compressed,  /* 1 = whole file was pre-compressed */
    const char    *orig_filename,
    const uint8_t  session_id[4],
    uint16_t       n_groups,
    uint16_t       group_idx,
    uint8_t        rs_k,              /* data shards for this group */
    uint8_t        rs_m,              /* parity shards for this group */
    uint8_t        shard_idx,
    uint32_t       shard_valid_bytes,
    uint32_t       file_crc32,        /* CRC-32 of the original file */
    const char    *hcc2df_filename,   /* HCC2DF wrapper filename */
    size_t        *out_len)
{
    StreamChunk hdr;
    memset(&hdr, 0, sizeof(hdr));
    memcpy(hdr.magic, "HCC2DST\0", 8);
    hdr.protocol_version  = 0x02;
    memcpy(hdr.session_id, session_id, 4);
    hdr.orig_size         = orig_size;
    hdr.whole_compressed  = whole_compressed;
    set_fname_field(hdr.fname, &hdr.fname_len, orig_filename);
    hdr.n_groups  = n_groups;
    hdr.group_idx = group_idx;
    hdr.rs_k      = rs_k;
    hdr.rs_m      = rs_m;
    hdr.shard_idx = shard_idx;
    hdr._pad      = 0;
    hdr.shard_bytes = shard_valid_bytes;
    hdr.file_crc32  = file_crc32;

    /* Last field set: everything the CRC covers must already be in place. */
    hdr.crc32 = stream_chunk_crc32(&hdr, shard_data, g_shard_data_bytes);

    /* raw_payload = StreamChunk header + shard_data */
    size_t raw_len = sizeof(StreamChunk) + g_shard_data_bytes;
    uint8_t *raw = (uint8_t *)xmalloc(raw_len);
    memcpy(raw, &hdr, sizeof(StreamChunk));
    memcpy(raw + sizeof(StreamChunk), shard_data, g_shard_data_bytes);

    /* Wrap in HCC2DF v0x01 (transport layer, unchanged) */
    uint8_t *wrapped = build_hcc2df_mem(hcc2df_filename, raw, raw_len, out_len);
    free(raw);
    return wrapped;
}

/* =========================================================================
 * SymbolFrame: stores one encoded HCC2D symbol
 * ========================================================================= */

typedef struct {
    uint8_t     *pixels;    /* palette-index raster, packed 4 bpp */
    int          width;
    int          height;
    const RGB   *palette;
    int          pal_size;
} SymbolFrame;

/* =========================================================================
 * SDL2 display
 * ========================================================================= */

/* Convert packed 4 bpp palette-index pixels to RGBA8888 and create an SDL_Texture */
static SDL_Texture *create_texture_from_symbol(SDL_Renderer *renderer,
                                               const SymbolFrame *sf) {
    SDL_Texture *tex = SDL_CreateTexture(renderer,
                                         SDL_PIXELFORMAT_RGBA8888,
                                         SDL_TEXTUREACCESS_STATIC,
                                         sf->width, sf->height);
    if (!tex) return NULL;

    uint32_t *rgba = (uint32_t *)malloc((size_t)sf->width * sf->height * 4);
    if (!rgba) {
        SDL_DestroyTexture(tex);
        return NULL;
    }
    for (int y = 0; y < sf->height; y++) {
        for (int x = 0; x < sf->width; x++) {
            int i = y * sf->width + x;
            int idx = packed_raster_get(sf->pixels, sf->width, x, y);
            if (idx < 0 || idx >= sf->pal_size) idx = sf->pal_size - 1;
            RGB c = sf->palette[idx];
            /* SDL_PIXELFORMAT_RGBA8888: R in bits 31-24, G in 23-16, B in 15-8, A in 7-0 */
            rgba[i] = ((uint32_t)c.r << 24) | ((uint32_t)c.g << 16)
                    | ((uint32_t)c.b <<  8) | 0xFF;
        }
    }
    SDL_UpdateTexture(tex, NULL, rgba, sf->width * 4);
    free(rgba);
    return tex;
}

/* =========================================================================
 * main
 * ========================================================================= */

static void print_usage(const char *prog) {
    printf(
"HCC2D Streamer - single-file C reference implementation\n"
"Copyright Marco Querini  |  SPDX-License-Identifier: Apache-2.0  |  Version 0.9.0\n\n"
"Specification compliance:\n"
"  Intended to conform to the HCC2D Code Specification version 0.9.0.\n"
"  Reference specification PDF: https://hcc2d.com/hcc2d_specification_v0.9.0.pdf\n\n"
"Description:\n"
"  Reads <file>, splits it into Reed-Solomon shards with dynamic k/m groups,\n"
"  encodes each shard as an HCC2D symbol or a standard QR Code (shard size\n"
"  is derived to be the byte-perfect max payload for the chosen mode/EC\n"
"  level/version), then streams all symbols in an SDL2 window in an\n"
"  infinite loop. Press ESC or close the window to exit.\n\n"
"Companion app:\n"
"  To receive this stream and recover the file, point a smartphone camera at\n"
"  the window using the HCC2D Decoder app:\n"
"    iOS (Apple App Store):       https://apps.apple.com/us/app/hcc2d-decoder/id6762202762\n"
"    Android (Google Play):       https://play.google.com/store/apps/details?id=com.hcc2d.decoder\n"
"    Android (Huawei AppGallery): https://appgallery.cloud.huawei.com/marketshare/app/C117478101\n\n"
"Usage:\n"
"  %s [options] <file>\n\n"
"Options:\n"
"  --colors {2,4,8}         selects the kind of code to generate, by palette\n"
"                           size: 2 = a standard QR Code (black/white),\n"
"                           4 = HCC2D 4-colour, or 8 = HCC2D 8-colour\n"
"                           (default: 8).\n"
"                           Mutually exclusive with --mode.\n"
"  --mode {qr,hcc2d4,hcc2d8}\n"
"                           selects the kind of code to generate, spelled\n"
"                           out instead of by palette size: qr (same as\n"
"                           --colors 2), hcc2d4 (same as --colors 4), or\n"
"                           hcc2d8 (same as --colors 8; default).\n"
"                           Mutually exclusive with --colors.\n"
"  --ec-level {L,M,Q,H}    error-correction level: L~7%% M~15%% Q~25%% H~30%%\n"
"                           (default: M)\n"
"  --version N              symbol version 1-40; higher versions carry more\n"
"                           data per symbol but render smaller modules.\n"
"                           (default: 30)\n"
"  --fps N                  display frame rate, 10-20 (default: 10)\n"
"  --display N              SDL display index to use for window placement\n"
"                           and size calculation (default: 0)\n"
"  --k-max N                largest number of data shards (rs_k) any single\n"
"                           Reed-Solomon group may hold before the file is\n"
"                           split into an additional group; independent of\n"
"                           --parity-num/--parity-den below (default: 150).\n"
"                           The only limit HCC2DST itself imposes is\n"
"                           rs_k+rs_m <= 255 (single-byte fields), enforced\n"
"                           regardless of this value.\n"
"  --parity-num N           numerator of the parity ratio: each group's\n"
"                           parity shard count is round(k * parity-num /\n"
"                           parity-den), k being that group's own data\n"
"                           shard count (default: 70).\n"
"  --parity-den N           denominator of the parity ratio above\n"
"                           (default: 100). --k-max, --parity-num, and\n"
"                           --parity-den are three independent parameters;\n"
"                           changing one does not require changing the\n"
"                           others, though rs_k+rs_m <= 255 is always\n"
"                           enforced regardless of what they are set to.\n"
"  --quiet-zone N           quiet-zone width in modules around the symbol\n"
"                           (default: 4). Lower values leave more display\n"
"                           height/width for the module scale but shrink\n"
"                           the silent margin the detector relies on to\n"
"                           find the symbol edges.\n"
"  --no-titlebar            hide the window titlebar/decorations, so the\n"
"                           whole display height is usable for the symbol\n"
"                           instead of being reduced by the titlebar\n"
"                           (default: titlebar shown).\n"
"  --palette-rgb LIST       complete custom RGB palette for HCC2D4/8,\n"
"                           quoted as R,G,B;R,G,B;... . Index 0 must be black\n"
"                           and the final index white.\n"
"  --help                   show this help and exit\n\n"
"Examples:\n"
"  %s myfile.pdf\n"
"  %s --colors 8 --ec-level L --version 40 --fps 15 myfile.pdf\n"
"  %s --mode qr --version 10 myfile.pdf\n"
"  %s --k-max 100 --parity-num 20 --parity-den 100 myfile.pdf\n",
        prog, prog, prog, prog, prog);
}

int main(int argc, char **argv) {
    int         colors_opt  = 0;    /* 0 = --colors not given */
    const char *mode_opt    = NULL; /* NULL = --mode not given */
    char ec_level   = 'M';
    int version     = DEFAULT_SYMBOL_VERSION;
    int display_fps = DEFAULT_DISPLAY_FPS;
    int display_index = 0;
    int k_max       = K_MAX_DEFAULT;
    int parity_num  = PARITY_NUM_DEFAULT;
    int parity_den  = PARITY_DEN_DEFAULT;
    int quiet_zone  = DEFAULT_QUIET_ZONE;
    int show_titlebar = 1;
    const char *palette_rgb_opt = NULL;

    static struct option long_opts[] = {
        {"colors",     required_argument, 0, 'c'},
        {"mode",       required_argument, 0, 'm'},
        {"ec-level",   required_argument, 0, 'e'},
        {"version",    required_argument, 0, 'v'},
        {"fps",        required_argument, 0, 'f'},
        {"display",    required_argument, 0, 'd'},
        {"k-max",      required_argument, 0, 'k'},
        {"parity-num", required_argument, 0, 'p'},
        {"parity-den", required_argument, 0, 'q'},
        {"quiet-zone", required_argument, 0, 'z'},
        {"no-titlebar", no_argument,      0, 't'},
        {"palette-rgb", required_argument, 0, 'P'},
        {"help",       no_argument,       0, 'h'},
        {0, 0, 0, 0}
    };

    if (argc <= 1) { print_usage(argv[0]); return 0; }

    int c;
    while ((c = getopt_long(argc, argv, "", long_opts, NULL)) != -1) {
        switch (c) {
            case 'c': {
                int val;
                if (parse_int_option("colors", optarg, 2, 8, &val) != 0
                    || (val != 2 && val != 4 && val != 8)) {
                    fprintf(stderr, "Error: --colors must be 2, 4, or 8\n");
                    return 1;
                }
                colors_opt = val;
                break;
            }
            case 'm':
                if (strcmp(optarg, "qr") != 0 && strcmp(optarg, "hcc2d4") != 0
                    && strcmp(optarg, "hcc2d8") != 0) {
                    fprintf(stderr, "Error: --mode must be qr, hcc2d4, or hcc2d8\n");
                    return 1;
                }
                mode_opt = optarg;
                break;
            case 'e':
                if (strlen(optarg) != 1
                    || ec_level_index((char)toupper((unsigned char)optarg[0])) < 0) {
                    fprintf(stderr, "Error: --ec-level must be one of L, M, Q, H\n");
                    return 1;
                }
                ec_level = (char)toupper((unsigned char)optarg[0]);
                break;
            case 'v':
                if (parse_int_option("version", optarg, 1, 40, &version) != 0)
                    return 1;
                break;
            case 'f':
                if (parse_int_option("fps", optarg, 10, 20, &display_fps) != 0)
                    return 1;
                break;
            case 'd':
                if (parse_int_option("display", optarg, 0, 64, &display_index) != 0)
                    return 1;
                break;
            case 'k':
                /* rs_k's own field range is 1-254 (Table: HCC2DST shard
                 * header); the compound rs_k+rs_m<=255 check below is
                 * still enforced independently of this per-field bound. */
                if (parse_int_option("k-max", optarg, 1, 254, &k_max) != 0)
                    return 1;
                break;
            case 'p':
                if (parse_int_option("parity-num", optarg, 0, 1000000, &parity_num) != 0)
                    return 1;
                break;
            case 'q':
                if (parse_int_option("parity-den", optarg, 1, 1000000, &parity_den) != 0)
                    return 1;
                break;
            case 'z':
                if (parse_int_option("quiet-zone", optarg, 0, 16, &quiet_zone) != 0)
                    return 1;
                break;
            case 't':
                show_titlebar = 0;
                break;
            case 'P':
                palette_rgb_opt = optarg;
                break;
            case 'h':
                print_usage(argv[0]);
                return 0;
            default:
                fprintf(stderr, "Error: unknown option. Use --help.\n");
                return 1;
        }
    }

    if (colors_opt != 0 && mode_opt != NULL) {
        fprintf(stderr, "Error: --colors and --mode cannot be used together\n");
        return 1;
    }

    /* Validate --k-max/--parity-num/--parity-den together up front, at
     * their worst case (a group that actually reaches k_max data shards),
     * rather than relying on the per-group clamp further below: that
     * clamp only ever shrank n=k+m back down to N_LIMIT without also
     * reducing m, which was safe only because the *default* k_max/
     * parity_num/parity_den values were chosen so the clamp could never
     * actually trigger. Now that these three are independent, user-set
     * parameters, an arbitrary combination (e.g. a large --k-max together
     * with a large --parity-num/--parity-den ratio) really can drive
     * rs_k+rs_m past 255 -- which the clamp would then silently paper
     * over by writing a truncated rs_m inconsistent with the true parity
     * shard count, corrupting the stream rather than failing loudly.
     * Reject that combination here instead. */
    {
        uint32_t worst_m = (uint32_t)((double)k_max * parity_num / parity_den + 0.5);
        if ((uint32_t)k_max + worst_m > N_LIMIT) {
            fprintf(stderr,
                "Error: --k-max %d with --parity-num %d --parity-den %d "
                "would need %u parity shards at k=%d, giving rs_k+rs_m=%u, "
                "which exceeds the protocol's %d-shard-per-group limit "
                "(rs_k+rs_m <= %d). Lower --k-max or the parity-num/"
                "parity-den ratio.\n",
                k_max, parity_num, parity_den, worst_m, k_max,
                (uint32_t)k_max + worst_m, N_LIMIT, N_LIMIT);
            return 1;
        }
    }

    if (optind >= argc) {
        fprintf(stderr, "Error: input file required.\n"
                        "Usage: %s [options] <file>\n", argv[0]);
        return 1;
    }
    if (optind + 1 < argc) {
        fprintf(stderr, "Error: unexpected extra argument: %s\n", argv[optind + 1]);
        return 1;
    }
    const char *input_path = argv[optind];

    /* Resolve the unified encoding mode from whichever of --colors/--mode
     * was given (never both, checked above); default is HCC2D8 when
     * neither is given. */
    const char *enc_mode;
    int         pal_size;
    if (mode_opt) {
        enc_mode = mode_opt;
        pal_size = palette_size_for_mode(enc_mode);
    } else if (colors_opt != 0) {
        pal_size = colors_opt;
        enc_mode = (pal_size == 2)  ? "qr"
                 : (pal_size == 4)  ? "hcc2d4"
                                    : "hcc2d8";
    } else {
        pal_size = 8;
        enc_mode = "hcc2d8";
    }

    if (palette_rgb_opt && pal_size == 2) {
        fprintf(stderr, "Error: --palette-rgb is supported for HCC2D4/8 only\n");
        return 1;
    }

    RGB custom_palette[8] = {{0}};
    const RGB *palette     = (pal_size == 2) ? PALETTE_QR
                            : (pal_size == 4) ? PALETTE_4_MODEL1
                                              : PALETTE_8_MODEL1;
    if (palette_rgb_opt) {
        if (parse_palette_rgb(palette_rgb_opt, pal_size, custom_palette) != 0)
            return 1;
        palette = custom_palette;
        printf("Custom %d-color RGB palette enabled.\n", pal_size);
    }
    int        plane_count = plane_count_for_mode(enc_mode);
    int        ec_idx       = ec_level_index(ec_level);

    const int frame_ms = 1000 / display_fps;

    /* Initialize GF(256) tables */
    gf256_init();

    /* Read input file */
    FILE *f = fopen(input_path, "rb");
    if (!f) {
        fprintf(stderr, "Error: cannot open '%s'\n", input_path);
        return 1;
    }
    fseek(f, 0, SEEK_END);
    long file_size_long = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (file_size_long <= 0) {
        fprintf(stderr, "Error: file is empty or unreadable\n");
        fclose(f);
        return 1;
    }
    uint32_t orig_size = (uint32_t)file_size_long;
    uint8_t *file_data = (uint8_t *)xmalloc((size_t)orig_size);
    if (fread(file_data, 1, (size_t)orig_size, f) != (size_t)orig_size) {
        fprintf(stderr, "Error: read error\n");
        free(file_data);
        fclose(f);
        return 1;
    }
    fclose(f);

    /* Session digest over the original bytes, before any compression: what
     * a receiver checks its reassembled output against. */
    uint32_t file_crc32 = (uint32_t)crc32(crc32(0L, Z_NULL, 0),
                                          (const Bytef *)file_data, (uInt)orig_size);

    /* Whole-file pre-compression (PROTOCOL.md §Encoding pipeline).
     * Compressing the whole file before RS sharding gives a much better ratio
     * than per-shard compression on 512-byte blocks.
     * The whole_compressed flag is stored in every StreamChunk so the decoder
     * knows to zlib-decompress after RS reassembly. */
    uint8_t  whole_compressed = 0;
    uint8_t *encode_data      = file_data;
    uint32_t encode_size      = orig_size;
    uint8_t *compressed_buf   = NULL;

    if (orig_size >= 128) {
        uLong cb = compressBound((uLong)orig_size);
        compressed_buf = (uint8_t *)malloc(cb);
        if (compressed_buf) {
            uLong cl = cb;
            if (compress2(compressed_buf, &cl, file_data, (uLong)orig_size,
                          Z_DEFAULT_COMPRESSION) == Z_OK
                && (double)cl < (double)orig_size * 0.98) {
                whole_compressed = 1;
                encode_data      = compressed_buf;
                encode_size      = (uint32_t)cl;
            } else {
                free(compressed_buf);
                compressed_buf = NULL;
            }
        }
    }

    /* Extract original filename */
    const char *orig_fname = strrchr(input_path, '/');
    if (!orig_fname) orig_fname = strrchr(input_path, '\\');
    orig_fname = orig_fname ? orig_fname + 1 : input_path;

    /* Generate random 4-byte session ID */
    srand((unsigned)time(NULL));
    uint8_t session_id[4];
    for (int i = 0; i < 4; i++) session_id[i] = (uint8_t)(rand() & 0xFF);

    /* HCC2D session hex string */
    char session_hex[9];
    snprintf(session_hex, sizeof(session_hex), "%02X%02X%02X%02X",
             session_id[0], session_id[1], session_id[2], session_id[3]);

    /* Get current datetime string */
    time_t now = time(NULL);
    struct tm *tm_now = localtime(&now);
    char datetime_str[16]; /* YYYYMMDDHHMMSS + NUL */
    strftime(datetime_str, sizeof(datetime_str), "%Y%m%d%H%M%S", tm_now);

    /* Derive the byte-perfect max shard size for the chosen mode/EC
     * level/version: raw symbol data capacity minus the mode/count-field/
     * terminator header (header_overhead_bytes; 3 bytes for HCC2D's fixed
     * 16-bit count, 2 or 3 bytes for QR's real Byte-mode count field),
     * minus the StreamChunk header, minus the HCC2DF wrapper overhead
     * (6+1+1+1 fixed bytes + the worst-case filename length, using the
     * widest group/shard index digits the "%04u-%03u" format can print). */
    {
        const QRVersion *qrv = version_for_mode(version);
        if (!qrv) {
            fprintf(stderr, "Error: unsupported mode/version combination: %s v%d\n",
                    enc_mode, version);
            return 1;
        }
        ECEntry hec = hcc2d_ec(&qrv->ec[ec_idx], plane_count);
        int raw_data_bytes = ec_total_sym(&hec) - ec_total_cw(&hec);

        char worst_fname[64];
        snprintf(worst_fname, sizeof(worst_fname), "%s-%s-%04u-%03u.bin",
                 session_hex, datetime_str, 9999u, 254u);
        size_t hcc2df_overhead = 6 + 1 + 1 + 1 + strlen(worst_fname);

        int max_shard_bytes = raw_data_bytes - header_overhead_bytes(enc_mode, version)
                                              - (int)sizeof(StreamChunk)
                                              - (int)hcc2df_overhead;
        if (max_shard_bytes < 1) {
            fprintf(stderr,
                "Error: version %d with mode %s and EC level %c has no room "
                "for shard data; pick a higher version or lower EC level\n",
                version, enc_mode, ec_level);
            return 1;
        }
        g_shard_data_bytes = max_shard_bytes;
    }

    /* Compute number of data chunks (shards) */
    uint32_t n_data_shards_total = (encode_size + g_shard_data_bytes - 1) / g_shard_data_bytes;
    if (n_data_shards_total == 0) n_data_shards_total = 1;

    /* Number of RS groups: ceil(D / k_max) */
    uint32_t n_groups = (n_data_shards_total + (uint32_t)k_max - 1) / (uint32_t)k_max;
    if (n_groups == 0) n_groups = 1;

    /* Compute per-group parameters using equal-split algorithm. No group's
     * k can exceed k_max by construction (n_groups above was chosen so the
     * evenly-split k never does), and --k-max/--parity-num/--parity-den
     * were already validated together against N_LIMIT at their worst case
     * (k=k_max) before we reach this loop -- so n=k+m is guaranteed to
     * stay at or under N_LIMIT for every group here, and unlike the
     * previous version of this code, nothing here needs to (or should)
     * silently clamp n on its own. */
    GroupParams *groups = (GroupParams *)xmalloc(n_groups * sizeof(GroupParams));
    {
        uint32_t assigned = 0;
        for (uint32_t g = 0; g < n_groups; g++) {
            uint32_t remaining_shards = n_data_shards_total - assigned;
            uint32_t remaining_groups = n_groups - g;
            uint32_t k = (remaining_shards + remaining_groups - 1) / remaining_groups;
            uint32_t m = (uint32_t)((double)k * parity_num / parity_den + 0.5);
            uint32_t n = k + m;
            groups[g].k          = k;
            groups[g].m          = m;
            groups[g].n          = n;
            groups[g].data_start = assigned;
            assigned += k;
        }
    }

    /* Total symbols: sum of n[g] across all groups */
    uint32_t n_symbols_total = 0;
    uint32_t max_n = 0;
    for (uint32_t g = 0; g < n_groups; g++) {
        n_symbols_total += groups[g].n;
        if (groups[g].n > max_n) max_n = groups[g].n;
    }

    if (n_symbols_total > MAX_SYMBOLS) {
        fprintf(stderr,
            "Warning: file would produce %u symbols (> MAX_SYMBOLS=%d). "
            "Capping at %d symbols.\n",
            n_symbols_total, MAX_SYMBOLS, MAX_SYMBOLS);
        n_symbols_total = MAX_SYMBOLS;
    }

    printf("File: %s (%u bytes", orig_fname, orig_size);
    if (whole_compressed)
        printf(", pre-compressed to %u bytes (%.0f%%)", encode_size,
               100.0 * encode_size / orig_size);
    printf(")\n");
    printf("Symbol: %s, EC %c, %d colors, version %d, display fps: %d, shard size: %d bytes\n",
           enc_mode, ec_level, pal_size, version, display_fps,
           g_shard_data_bytes);
    printf("RS groups: %u, total symbols: %u\n", n_groups, n_symbols_total);
    for (uint32_t g = 0; g < n_groups && g < 4; g++)
        printf("  group %u: k=%u m=%u n=%u data_start=%u\n",
               g, groups[g].k, groups[g].m, groups[g].n, groups[g].data_start);
    if (n_groups > 4) printf("  ... (showing first 4 groups)\n");

    /* -----------------------------------------------------------------------
     * Pre-encode all groups: compute data and parity shards for every group,
     * store them so we can output in interleaved order.
     * ----------------------------------------------------------------------- */

    /* Allocate per-group shard storage */
    uint8_t **group_data_shards   = (uint8_t **)xcalloc(n_groups, sizeof(uint8_t *));
    uint8_t **group_parity_shards = (uint8_t **)xcalloc(n_groups, sizeof(uint8_t *));

    for (uint32_t g = 0; g < n_groups; g++) {
        GroupParams gp = groups[g];

        group_data_shards[g]   = (uint8_t *)xcalloc(gp.k, g_shard_data_bytes);
        group_parity_shards[g] = (uint8_t *)xcalloc(gp.m, g_shard_data_bytes);

        /* Fill data shards from encode_data */
        for (uint32_t s = 0; s < gp.k; s++) {
            uint32_t global_shard = gp.data_start + s;
            uint32_t byte_offset  = global_shard * g_shard_data_bytes;
            uint32_t bytes_avail  = (byte_offset < encode_size)
                                  ? (encode_size - byte_offset) : 0;
            uint32_t bytes_copy   = (bytes_avail > (uint32_t)g_shard_data_bytes)
                                  ? (uint32_t)g_shard_data_bytes : bytes_avail;
            if (bytes_copy > 0)
                memcpy(group_data_shards[g] + (size_t)s * g_shard_data_bytes,
                       encode_data + byte_offset, bytes_copy);
        }

        /* Compute parity */
        rs_encode_parity(group_data_shards[g], group_parity_shards[g],
                         (int)gp.k, (int)gp.m);
    }

    free(file_data);
    if (compressed_buf) free(compressed_buf);
    compressed_buf = NULL;
    file_data      = NULL;

    /* -----------------------------------------------------------------------
     * Interleaved output: for si in 0..max_n-1, for g in 0..n_groups-1,
     * if si < n[g]: output symbol (group=g, shard=si)
     * ----------------------------------------------------------------------- */

    /* Allocate SymbolFrame array */
    SymbolFrame *frames = (SymbolFrame *)xcalloc(n_symbols_total, sizeof(SymbolFrame));

    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        fprintf(stderr, "SDL_Init error: %s\n", SDL_GetError());
        free(frames);
        return 1;
    }

    int num_displays = SDL_GetNumVideoDisplays();
    if (num_displays <= 0) {
        fprintf(stderr, "SDL_GetNumVideoDisplays error: %s\n", SDL_GetError());
        free(frames);
        SDL_Quit();
        return 1;
    }
    if (display_index >= num_displays) {
        fprintf(stderr, "Error: display index %d out of range; %d display(s) available\n",
                display_index, num_displays);
        free(frames);
        SDL_Quit();
        return 1;
    }

    SDL_DisplayMode dm;
    SDL_Rect display_bounds;
    int screen_w = 2560, screen_h = 1440; /* fallback */
    if (SDL_GetCurrentDisplayMode(display_index, &dm) == 0) {
        screen_w = dm.w;
        screen_h = dm.h;
    }
    if (SDL_GetDisplayBounds(display_index, &display_bounds) != 0) {
        display_bounds.x = 0;
        display_bounds.y = 0;
        display_bounds.w = screen_w;
        display_bounds.h = screen_h;
    }

    int display_size = (int)(screen_h * 1.00);
    if (display_size < 1) display_size = 1;
    int window_x = display_bounds.x + (display_bounds.w - display_size) / 2;
    int window_y = display_bounds.y + (display_bounds.h - display_size) / 2;

    int symbols_encoded = 0;
    int min_symbol_version = 999;
    int max_symbol_version = 0;

    for (uint32_t si = 0; si < max_n && symbols_encoded < (int)n_symbols_total; si++) {
        for (uint32_t g = 0; g < n_groups && symbols_encoded < (int)n_symbols_total; g++) {
            GroupParams gp = groups[g];
            if (si >= gp.n) continue; /* this group doesn't have shard si */

            const uint8_t *shard_data;
            uint32_t shard_valid_bytes;

            if (si < gp.k) {
                /* Data shard */
                shard_data = group_data_shards[g] + (size_t)si * g_shard_data_bytes;
                uint32_t global_shard = gp.data_start + si;
                uint32_t byte_offset  = global_shard * g_shard_data_bytes;
                /* encode_data already freed; but we have the shard copy */
                /* shard_valid_bytes = min(g_shard_data_bytes, bytes left from encode_size) */
                if (byte_offset < encode_size) {
                    uint32_t bytes_left = encode_size - byte_offset;
                    shard_valid_bytes = (bytes_left > (uint32_t)g_shard_data_bytes)
                                      ? (uint32_t)g_shard_data_bytes : bytes_left;
                } else {
                    shard_valid_bytes = 0;
                }
            } else {
                /* Parity shard */
                shard_data = group_parity_shards[g] + (size_t)(si - gp.k) * g_shard_data_bytes;
                shard_valid_bytes = g_shard_data_bytes;
            }

            /* Build HCC2DF filename */
            char hcc2df_fname[64];
            snprintf(hcc2df_fname, sizeof(hcc2df_fname),
                     "%s-%s-%04u-%03u.bin",
                     session_hex, datetime_str, g, si);

            /* Build shard payload (HCC2DF-wrapped) */
            size_t  payload_len = 0;
            uint8_t *payload = build_shard_payload(
                shard_data,
                orig_size,
                whole_compressed,
                orig_fname,
                session_id,
                (uint16_t)n_groups,
                (uint16_t)g,
                (uint8_t)gp.k,
                (uint8_t)gp.m,
                (uint8_t)si,
                shard_valid_bytes,
                file_crc32,
                hcc2df_fname,
                &payload_len);

            if (!payload) {
                fprintf(stderr, "Error: failed to build shard payload g=%u si=%u\n", g, si);
                continue;
            }

            /* Encode symbol at the largest integer scale that fits display_size. */
            EncodedSymbol sym = {0};
            char enc_err[256] = "";
            int ret = (strcmp(enc_mode, "qr") == 0)
                ? encode_qr(payload, (int)payload_len,
                            ec_level, version,
                            display_size, quiet_zone,
                            &sym, enc_err)
                : encode_hcc2d(payload, (int)payload_len,
                               enc_mode, ec_level, version,
                               display_size, quiet_zone,
                               &sym, enc_err);
            free(payload);

            if (ret != 0) {
                fprintf(stderr, "Error encoding shard g=%u si=%u: %s\n", g, si, enc_err);
                continue;
            }

            frames[symbols_encoded].pixels   = sym.pixels;
            frames[symbols_encoded].width    = sym.width;
            frames[symbols_encoded].height   = sym.height;
            frames[symbols_encoded].palette  = palette;
            frames[symbols_encoded].pal_size = pal_size;
            symbols_encoded++;
            if (sym.version_number < min_symbol_version) min_symbol_version = sym.version_number;
            if (sym.version_number > max_symbol_version) max_symbol_version = sym.version_number;

            if (symbols_encoded % 10 == 0 || symbols_encoded == (int)n_symbols_total)
                printf("\rEncoded %d/%u symbols...", symbols_encoded, n_symbols_total);
            fflush(stdout);
        }
    }
    printf("\nEncoded %d symbols total.\n", symbols_encoded);
    if (symbols_encoded > 0) {
        if (min_symbol_version == max_symbol_version) {
            printf("Symbol version: v%d\n", min_symbol_version);
        } else {
            printf("Symbol versions: v%d..v%d\n", min_symbol_version, max_symbol_version);
        }
    }

    /* Free per-group shard storage */
    for (uint32_t g = 0; g < n_groups; g++) {
        free(group_data_shards[g]);
        free(group_parity_shards[g]);
    }
    free(group_data_shards);
    free(group_parity_shards);
    free(groups);

    if (symbols_encoded == 0) {
        fprintf(stderr, "Error: no symbols encoded\n");
        free(frames);
        return 1;
    }

    Uint32 window_flags = SDL_WINDOW_SHOWN;
    if (!show_titlebar) window_flags |= SDL_WINDOW_BORDERLESS;
    SDL_Window *window = SDL_CreateWindow(
        "HCC2D Streamer",
        window_x, window_y,
        display_size, display_size,
        window_flags);
    if (!window) {
        fprintf(stderr, "SDL_CreateWindow error: %s\n", SDL_GetError());
        SDL_Quit();
        free(frames);
        return 1;
    }

    SDL_Renderer *renderer = SDL_CreateRenderer(window, -1,
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!renderer) {
        fprintf(stderr, "SDL_CreateRenderer error: %s\n", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        free(frames);
        return 1;
    }

    /* Pre-create all SDL textures */
    printf("Creating %d textures...\n", symbols_encoded);
    SDL_Texture **textures = (SDL_Texture **)calloc((size_t)symbols_encoded, sizeof(SDL_Texture *));
    if (!textures) {
        fprintf(stderr, "Error: out of memory for textures\n");
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
        free(frames);
        return 1;
    }

    for (int i = 0; i < symbols_encoded; i++) {
        textures[i] = create_texture_from_symbol(renderer, &frames[i]);
        if (!textures[i]) {
            fprintf(stderr, "Warning: failed to create texture %d: %s\n", i, SDL_GetError());
        }
    }
    printf("Ready. Streaming at %d fps. Press ESC or close window to exit.\n", display_fps);

    /* Play order array — always the interleaved order (shuffle mode removed) */
    int *play_order = (int *)xmalloc((size_t)symbols_encoded * sizeof(int));
    for (int i = 0; i < symbols_encoded; i++) play_order[i] = i;

    /* Main display loop */
    int idx     = 0;
    int running = 1;
    SDL_Event ev;

    while (running) {
        Uint32 t0 = SDL_GetTicks();

        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT) { running = 0; break; }
            if (ev.type == SDL_KEYDOWN && ev.key.keysym.sym == SDLK_ESCAPE) {
                running = 0; break;
            }
        }
        if (!running) break;

        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_RenderClear(renderer);

        if (textures[play_order[idx]]) {
            SDL_Rect dst;
            dst.w = frames[play_order[idx]].width;
            dst.h = frames[play_order[idx]].height;
            dst.x = (display_size - dst.w) / 2;
            /* Top-anchored, not centred: the integer pixel scale leaves a
             * few pixels over, and splitting them above and below put a
             * black band under the symbol at every start. Horizontally
             * centred still, vertically flush with the top edge. */
            dst.y = 0;
            SDL_RenderCopy(renderer, textures[play_order[idx]], NULL, &dst);
        }
        SDL_RenderPresent(renderer);

        idx++;
        if (idx >= symbols_encoded) {
            /* End of loop; restart from the beginning of the interleaved
             * order. Fisher-Yates re-shuffling removed along with shuffle
             * mode: the play order is now always ORDER_SEQUENTIAL. */
            idx = 0;
        }

        Uint32 elapsed = SDL_GetTicks() - t0;
        if (elapsed < (Uint32)frame_ms)
            SDL_Delay((Uint32)frame_ms - elapsed);
    }
    free(play_order);

    /* Cleanup */
    for (int i = 0; i < symbols_encoded; i++) {
        free(frames[i].pixels);
        if (textures[i]) SDL_DestroyTexture(textures[i]);
    }
    free(frames);
    free(textures);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
