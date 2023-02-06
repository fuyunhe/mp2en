/*
 * The simplest mpeg audio layer 2 encoder
 * Copyright (c) 2000, 2001 Fabrice Bellard
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

/**
 * @file
 * The simplest mpeg audio layer 2 encoder.
 */

#include <stdio.h>
#include <string.h>
#include <stdarg.h>

#define FRAC_PADDING    0

#define FRAC_BITS   15   /* fractional bits for sb_samples and dct */
#define WFRAC_BITS  14   /* fractional bits for window */

//#include "mpegaudio.h"
//#include "mpegaudiodsp.h"
//#include "mpegaudiodata.h"
//#include "mpegaudiotab.h"
#include "mp2en.h"

#define TABLE_GENERATE      0

//-----------------
typedef struct PutBitContext {
    uint32_t bit_buf;
    int bit_left;
    uint8_t* buf, * buf_ptr, * buf_end;
    int size_in_bits;
} PutBitContext;

/**
 * Initialize the PutBitContext s.
 *
 * @param buffer the buffer where to put bits
 * @param buffer_size the size in bytes of buffer
 */
static inline void init_put_bits(PutBitContext* s, uint8_t* buffer, int buffer_size)
{
    if (buffer_size < 0) {
        buffer_size = 0;
        buffer = NULL;
    }

    s->size_in_bits = 8 * buffer_size;
    s->buf = buffer;
    s->buf_end = s->buf + buffer_size;
    s->buf_ptr = s->buf;
    s->bit_left = 32;
    s->bit_buf = 0;
}

/**
 * @return the total number of bits written to the bitstream.
 */
static inline int put_bits_count(PutBitContext* s)
{
    return (s->buf_ptr - s->buf) * 8 + 32 - s->bit_left;
}

/**
 * Pad the end of the output stream with zeros.
 */
static inline void flush_put_bits(PutBitContext* s)
{
#ifndef BITSTREAM_WRITER_LE
    if (s->bit_left < 32)
        s->bit_buf <<= s->bit_left;
#endif
    while (s->bit_left < 32) {
        av_assert0(s->buf_ptr < s->buf_end);
#ifdef BITSTREAM_WRITER_LE
        * s->buf_ptr++ = s->bit_buf;
        s->bit_buf >>= 8;
#else
        * s->buf_ptr++ = s->bit_buf >> 24;
        s->bit_buf <<= 8;
#endif
        s->bit_left += 8;
    }
    s->bit_left = 32;
    s->bit_buf = 0;
}

/**
 * Write up to 31 bits into a bitstream.
 * Use put_bits32 to write 32 bits.
 */
static inline void put_bits(PutBitContext* s, int n, unsigned int value)
{
    unsigned int bit_buf;
    int bit_left;

    av_assert2(n <= 31 && value < (1U << n));

    bit_buf = s->bit_buf;
    bit_left = s->bit_left;

    /* XXX: optimize */
#ifdef BITSTREAM_WRITER_LE
    bit_buf |= value << (32 - bit_left);
    if (n >= bit_left) {
        if (3 < s->buf_end - s->buf_ptr) {
            AV_WL32(s->buf_ptr, bit_buf);
            s->buf_ptr += 4;
        }
        else {
            av_log(NULL, AV_LOG_ERROR, "Internal error, put_bits buffer too small\n");
            av_assert2(0);
        }
        bit_buf = value >> bit_left;
        bit_left += 32;
    }
    bit_left -= n;
#else
    if (n < bit_left) {
        bit_buf = (bit_buf << n) | value;
        bit_left -= n;
    }
    else {
        bit_buf <<= bit_left;
        bit_buf |= value >> (n - bit_left);
        if (3 < s->buf_end - s->buf_ptr) {
            AV_WB32(s->buf_ptr, bit_buf);
            s->buf_ptr += 4;
        }
        else {
            av_log(NULL, AV_LOG_ERROR, "Internal error, put_bits buffer too small\n");
            av_assert2(0);
        }
        bit_left += 32 - n;
        bit_buf = value;
    }
#endif

    s->bit_buf = bit_buf;
    s->bit_left = bit_left;
}
//------------------------------------------------

static const int costab32[30] = {
    FIX(0.54119610014619701222),
    FIX(1.3065629648763763537),

    FIX(0.50979557910415917998),
    FIX(2.5629154477415054814),
    FIX(0.89997622313641556513),
    FIX(0.60134488693504528634),

    FIX(0.5024192861881556782),
    FIX(5.1011486186891552563),
    FIX(0.78815462345125020249),
    FIX(0.64682178335999007679),
    FIX(0.56694403481635768927),
    FIX(1.0606776859903470633),
    FIX(1.7224470982383341955),
    FIX(0.52249861493968885462),

    FIX(10.19000812354803287),
    FIX(0.674808341455005678),
    FIX(1.1694399334328846596),
    FIX(0.53104259108978413284),
    FIX(2.0577810099534108446),
    FIX(0.58293496820613388554),
    FIX(0.83934964541552681272),
    FIX(0.50547095989754364798),
    FIX(3.4076084184687189804),
    FIX(0.62250412303566482475),
    FIX(0.97256823786196078263),
    FIX(0.51544730992262455249),
    FIX(1.4841646163141661852),
    FIX(0.5531038960344445421),
    FIX(0.74453627100229857749),
    FIX(0.5006029982351962726),
};

static const int bitinv32[32] = {
    0,  16,  8, 24,  4,  20,  12,  28,
    2,  18, 10, 26,  6,  22,  14,  30,
    1,  17,  9, 25,  5,  21,  13,  29,
    3,  19, 11, 27,  7,  23,  15,  31
};


/* signal to noise ratio of each quantification step (could be
   computed from quant_steps[]). The values are dB multiplied by 10
*/
static const unsigned short quant_snr[17] = {
     70, 110, 160, 208,
    253, 316, 378, 439,
    499, 559, 620, 680,
    740, 800, 861, 920,
    980
};

/* fixed psycho acoustic model. Values of SNR taken from the 'toolame'
   project */
static const float fixed_smr[SBLIMIT] = {
    30, 17, 16, 10, 3, 12, 8, 2.5,
    5, 5, 6, 6, 5, 6, 10, 6,
    -4, -10, -21, -30, -42, -55, -68, -75,
    -75, -75, -75, -75, -91, -107, -110, -108
};

static const unsigned char nb_scale_factors[4] = { 3, 2, 1, 2 };

//--------mpegaudiodata.c---------------------------------
const uint16_t avpriv_mpa_bitrate_tab[2][3][15] = {
    { {0, 32, 64, 96, 128, 160, 192, 224, 256, 288, 320, 352, 384, 416, 448 },
      {0, 32, 48, 56,  64,  80,  96, 112, 128, 160, 192, 224, 256, 320, 384 },
      {0, 32, 40, 48,  56,  64,  80,  96, 112, 128, 160, 192, 224, 256, 320 } },
    { {0, 32, 48, 56,  64,  80,  96, 112, 128, 144, 160, 176, 192, 224, 256},
      {0,  8, 16, 24,  32,  40,  48,  56,  64,  80,  96, 112, 128, 144, 160},
      {0,  8, 16, 24,  32,  40,  48,  56,  64,  80,  96, 112, 128, 144, 160}
    }
};

const uint16_t avpriv_mpa_freq_tab[3] = { 44100, 48000, 32000 };

/*******************************************************/
/* layer 2 tables */

const int ff_mpa_sblimit_table[5] = { 27 , 30 , 8, 12 , 30 };

const int ff_mpa_quant_steps[17] = {
    3,     5,    7,    9,    15,
    31,    63,  127,  255,   511,
    1023,  2047, 4095, 8191, 16383,
    32767, 65535
};

/* we use a negative value if grouped */
const int ff_mpa_quant_bits[17] = {
    -5,  -7,  3, -10, 4,
     5,  6,  7,  8,  9,
    10, 11, 12, 13, 14,
    15, 16
};

/* encoding tables which give the quantization index. Note how it is
   possible to store them efficiently ! */
static const unsigned char alloc_table_1[] = {
 4,  0,  2,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14, 15, 16,
 4,  0,  2,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14, 15, 16,
 4,  0,  2,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14, 15, 16,
 4,  0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 16,
 4,  0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 16,
 4,  0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 16,
 4,  0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 16,
 4,  0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 16,
 4,  0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 16,
 4,  0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 16,
 4,  0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 16,
 3,  0,  1,  2,  3,  4,  5, 16,
 3,  0,  1,  2,  3,  4,  5, 16,
 3,  0,  1,  2,  3,  4,  5, 16,
 3,  0,  1,  2,  3,  4,  5, 16,
 3,  0,  1,  2,  3,  4,  5, 16,
 3,  0,  1,  2,  3,  4,  5, 16,
 3,  0,  1,  2,  3,  4,  5, 16,
 3,  0,  1,  2,  3,  4,  5, 16,
 3,  0,  1,  2,  3,  4,  5, 16,
 3,  0,  1,  2,  3,  4,  5, 16,
 3,  0,  1,  2,  3,  4,  5, 16,
 3,  0,  1,  2,  3,  4,  5, 16,
 2,  0,  1, 16,
 2,  0,  1, 16,
 2,  0,  1, 16,
 2,  0,  1, 16,
 2,  0,  1, 16,
 2,  0,  1, 16,
 2,  0,  1, 16,
};

static const unsigned char alloc_table_3[] = {
 4,  0,  1,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14, 15,
 4,  0,  1,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14, 15,
 3,  0,  1,  3,  4,  5,  6,  7,
 3,  0,  1,  3,  4,  5,  6,  7,
 3,  0,  1,  3,  4,  5,  6,  7,
 3,  0,  1,  3,  4,  5,  6,  7,
 3,  0,  1,  3,  4,  5,  6,  7,
 3,  0,  1,  3,  4,  5,  6,  7,
 3,  0,  1,  3,  4,  5,  6,  7,
 3,  0,  1,  3,  4,  5,  6,  7,
 3,  0,  1,  3,  4,  5,  6,  7,
 3,  0,  1,  3,  4,  5,  6,  7,
};

static const unsigned char alloc_table_4[] = {
 4,  0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14,
 4,  0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14,
 4,  0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14,
 4,  0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14,
 3,  0,  1,  3,  4,  5,  6,  7,
 3,  0,  1,  3,  4,  5,  6,  7,
 3,  0,  1,  3,  4,  5,  6,  7,
 3,  0,  1,  3,  4,  5,  6,  7,
 3,  0,  1,  3,  4,  5,  6,  7,
 3,  0,  1,  3,  4,  5,  6,  7,
 3,  0,  1,  3,  4,  5,  6,  7,
 2,  0,  1,  3,
 2,  0,  1,  3,
 2,  0,  1,  3,
 2,  0,  1,  3,
 2,  0,  1,  3,
 2,  0,  1,  3,
 2,  0,  1,  3,
 2,  0,  1,  3,
 2,  0,  1,  3,
 2,  0,  1,  3,
 2,  0,  1,  3,
 2,  0,  1,  3,
 2,  0,  1,  3,
 2,  0,  1,  3,
 2,  0,  1,  3,
 2,  0,  1,  3,
 2,  0,  1,  3,
 2,  0,  1,  3,
 2,  0,  1,  3,
};

const unsigned char* const ff_mpa_alloc_tables[5] =
{ alloc_table_1, alloc_table_1, alloc_table_3, alloc_table_3, alloc_table_4, };

//---------------------------------------------
/* half mpeg encoding window (full precision) */
#if TABLE_GENERATE
const int32_t ff_mpa_enwindow[257] = {
     0,    -1,    -1,    -1,    -1,    -1,    -1,    -2,
    -2,    -2,    -2,    -3,    -3,    -4,    -4,    -5,
    -5,    -6,    -7,    -7,    -8,    -9,   -10,   -11,
   -13,   -14,   -16,   -17,   -19,   -21,   -24,   -26,
   -29,   -31,   -35,   -38,   -41,   -45,   -49,   -53,
   -58,   -63,   -68,   -73,   -79,   -85,   -91,   -97,
  -104,  -111,  -117,  -125,  -132,  -139,  -147,  -154,
  -161,  -169,  -176,  -183,  -190,  -196,  -202,  -208,
   213,   218,   222,   225,   227,   228,   228,   227,
   224,   221,   215,   208,   200,   189,   177,   163,
   146,   127,   106,    83,    57,    29,    -2,   -36,
   -72,  -111,  -153,  -197,  -244,  -294,  -347,  -401,
  -459,  -519,  -581,  -645,  -711,  -779,  -848,  -919,
  -991, -1064, -1137, -1210, -1283, -1356, -1428, -1498,
 -1567, -1634, -1698, -1759, -1817, -1870, -1919, -1962,
 -2001, -2032, -2057, -2075, -2085, -2087, -2080, -2063,
  2037,  2000,  1952,  1893,  1822,  1739,  1644,  1535,
  1414,  1280,  1131,   970,   794,   605,   402,   185,
   -45,  -288,  -545,  -814, -1095, -1388, -1692, -2006,
 -2330, -2663, -3004, -3351, -3705, -4063, -4425, -4788,
 -5153, -5517, -5879, -6237, -6589, -6935, -7271, -7597,
 -7910, -8209, -8491, -8755, -8998, -9219, -9416, -9585,
 -9727, -9838, -9916, -9959, -9966, -9935, -9863, -9750,
 -9592, -9389, -9139, -8840, -8492, -8092, -7640, -7134,
  6574,  5959,  5288,  4561,  3776,  2935,  2037,  1082,
    70,  -998, -2122, -3300, -4533, -5818, -7154, -8540,
 -9975,-11455,-12980,-14548,-16155,-17799,-19478,-21189,
-22929,-24694,-26482,-28289,-30112,-31947,-33791,-35640,
-37489,-39336,-41176,-43006,-44821,-46617,-48390,-50137,
-51853,-53534,-55178,-56778,-58333,-59838,-61289,-62684,
-64019,-65290,-66494,-67629,-68692,-69679,-70590,-71420,
-72169,-72835,-73415,-73908,-74313,-74630,-74856,-74992,
 75038,
};
#endif

/* currently, cannot change these constants (need to modify
   quantization stage) */
#define MUL(a,b) (((int64_t)(a) * (int64_t)(b)) >> FRAC_BITS)

#define SAMPLES_BUF_SIZE 4096

typedef struct MpegAudioContext {
    PutBitContext pb;
    int nb_channels;
    int lsf;           /* 1 if mpeg2 low bitrate selected */
    int bitrate_index; /* bit rate */
    int freq_index;
    int frame_size; /* frame size, in bits, without padding */
#if FRAC_PADDING
    /* padding computation */
    int frame_frac, frame_frac_incr;
#endif
    int do_padding;
    short samples_buf[MPA_MAX_CHANNELS][SAMPLES_BUF_SIZE]; /* buffer for filter */
    int samples_offset[MPA_MAX_CHANNELS];       /* offset in samples_buf */
    int sb_samples[MPA_MAX_CHANNELS][3][12][SBLIMIT];
    unsigned char scale_factors[MPA_MAX_CHANNELS][SBLIMIT][3]; /* scale factors */
    /* code to group 3 scale factors */
    unsigned char scale_code[MPA_MAX_CHANNELS][SBLIMIT];
    int sblimit; /* number of used subbands */
    const unsigned char *alloc_table;
} MpegAudioContext;


#if !USE_FLOATS
#define P 15
#endif

#if TABLE_GENERATE
#include <math.h>
static int16_t s_filter_bank[512];
static int s_scale_factor_table[64];
static unsigned char s_scale_diff_table[128];
#if USE_FLOATS
static float s_scale_factor_inv_table[64];
#else
static int8_t s_scale_factor_shift[64];
static unsigned short s_scale_factor_mult[64];
#endif
static unsigned short s_total_quant_bits[17]; /* total number of bits per allocation group */
#else
static const int16_t s_filter_bank[512] = {
     0,      0,      0,      0,      0,      0,      0,      0,
     0,      0,      0,     -1,     -1,     -1,     -1,     -1,
    -1,     -1,     -2,     -2,     -2,     -2,     -2,     -3,
    -3,     -3,     -4,     -4,     -5,     -5,     -6,     -6,
    -7,     -8,     -9,     -9,    -10,    -11,    -12,    -13,
   -14,    -16,    -17,    -18,    -20,    -21,    -23,    -24,
   -26,    -28,    -29,    -31,    -33,    -35,    -37,    -38,
   -40,    -42,    -44,    -46,    -47,    -49,    -50,    -52,
    53,     55,     56,     56,     57,     57,     57,     57,
    56,     55,     54,     52,     50,     47,     44,     41,
    37,     32,     27,     21,     14,      7,      0,     -9,
   -18,    -28,    -38,    -49,    -61,    -73,    -87,   -100,
  -115,   -130,   -145,   -161,   -178,   -195,   -212,   -230,
  -248,   -266,   -284,   -302,   -321,   -339,   -357,   -374,
  -392,   -408,   -424,   -440,   -454,   -467,   -480,   -490,
  -500,   -508,   -514,   -519,   -521,   -522,   -520,   -516,
   509,    500,    488,    473,    456,    435,    411,    384,
   354,    320,    283,    243,    199,    151,    101,     46,
   -11,    -72,   -136,   -203,   -274,   -347,   -423,   -501,
  -582,   -666,   -751,   -838,   -926,  -1016,  -1106,  -1197,
 -1288,  -1379,  -1470,  -1559,  -1647,  -1734,  -1818,  -1899,
 -1977,  -2052,  -2123,  -2189,  -2249,  -2305,  -2354,  -2396,
 -2432,  -2459,  -2479,  -2490,  -2491,  -2484,  -2466,  -2437,
 -2398,  -2347,  -2285,  -2210,  -2123,  -2023,  -1910,  -1783,
  1644,   1490,   1322,   1140,    944,    734,    509,    271,
    18,   -249,   -530,   -825,  -1133,  -1454,  -1788,  -2135,
 -2494,  -2864,  -3245,  -3637,  -4039,  -4450,  -4869,  -5297,
 -5732,  -6173,  -6620,  -7072,  -7528,  -7987,  -8448,  -8910,
 -9372,  -9834, -10294, -10751, -11205, -11654, -12097, -12534,
-12963, -13383, -13794, -14194, -14583, -14959, -15322, -15671,
-16005, -16322, -16623, -16907, -17173, -17420, -17647, -17855,
-18042, -18209, -18354, -18477, -18578, -18657, -18714, -18748,
 18760,  18748,  18714,  18657,  18578,  18477,  18354,  18209,
 18042,  17855,  17647,  17420,  17173,  16907,  16623,  16322,
 16005,  15671,  15322,  14959,  14583,  14194,  13794,  13383,
 12963,  12534,  12097,  11654,  11205,  10751,  10294,   9834,
  9372,   8910,   8448,   7987,   7528,   7072,   6620,   6173,
  5732,   5297,   4869,   4450,   4039,   3637,   3245,   2864,
  2494,   2135,   1788,   1454,   1133,    825,    530,    249,
   -18,   -271,   -509,   -734,   -944,  -1140,  -1322,  -1490,
  1644,   1783,   1910,   2023,   2123,   2210,   2285,   2347,
  2398,   2437,   2466,   2484,   2491,   2490,   2479,   2459,
  2432,   2396,   2354,   2305,   2249,   2189,   2123,   2052,
  1977,   1899,   1818,   1734,   1647,   1559,   1470,   1379,
  1288,   1197,   1106,   1016,    926,    838,    751,    666,
   582,    501,    423,    347,    274,    203,    136,     72,
    11,    -46,   -101,   -151,   -199,   -243,   -283,   -320,
  -354,   -384,   -411,   -435,   -456,   -473,   -488,   -500,
   509,    516,    520,    522,    521,    519,    514,    508,
   500,    490,    480,    467,    454,    440,    424,    408,
   392,    374,    357,    339,    321,    302,    284,    266,
   248,    230,    212,    195,    178,    161,    145,    130,
   115,    100,     87,     73,     61,     49,     38,     28,
    18,      9,      0,     -7,    -14,    -21,    -27,    -32,
   -37,    -41,    -44,    -47,    -50,    -52,    -54,    -55,
   -56,    -57,    -57,    -57,    -57,    -56,    -56,    -55,
    53,     52,     50,     49,     47,     46,     44,     42,
    40,     38,     37,     35,     33,     31,     29,     28,
    26,     24,     23,     21,     20,     18,     17,     16,
    14,     13,     12,     11,     10,      9,      9,      8,
     7,      6,      6,      5,      5,      4,      4,      3,
     3,      3,      2,      2,      2,      2,      2,      1,
     1,      1,      1,      1,      1,      1,      0,      0,
     0,      0,      0,      0,      0,      0,      0,      0,
};

static const int s_scale_factor_table[64] = {
2097152, 1664510, 1321122, 1048576,  832255,  660561,  524288,  416127,
 330280,  262144,  208063,  165140,  131072,  104031,   82570,   65536,
  52015,   41285,   32768,   26007,   20642,   16384,   13003,   10321,
   8192,    6501,    5160,    4096,    3250,    2580,    2048,    1625,
   1290,    1024,     812,     645,     512,     406,     322,     256,
    203,     161,     128,     101,      80,      64,      50,      40,
     32,      25,      20,      16,      12,      10,       8,       6,
      5,       4,       3,       2,       2,       1,       1,       1,
};

static const int8_t s_scale_factor_shift[64] = {
  6,   6,   6,   5,   5,   5,   4,   4,
  4,   3,   3,   3,   2,   2,   2,   1,
  1,   1,   0,   0,   0,  -1,  -1,  -1,
 -2,  -2,  -2,  -3,  -3,  -3,  -4,  -4,
 -4,  -5,  -5,  -5,  -6,  -6,  -6,  -7,
 -7,  -7,  -8,  -8,  -8,  -9,  -9,  -9,
-10, -10, -10, -11, -11, -11, -12, -12,
-12, -13, -13, -13, -14, -14, -14, -15,
};

static const unsigned short s_scale_factor_mult[64] = {
 32768,  41285,  52015,  32768,  41285,  52015,  32768,  41285,
 52015,  32768,  41285,  52015,  32768,  41285,  52015,  32768,
 41285,  52015,  32768,  41285,  52015,  32768,  41285,  52015,
 32768,  41285,  52015,  32768,  41285,  52015,  32768,  41285,
 52015,  32768,  41285,  52015,  32768,  41285,  52015,  32768,
 41285,  52015,  32768,  41285,  52015,  32768,  41285,  52015,
 32768,  41285,  52015,  32768,  41285,  52015,  32768,  41285,
 52015,  32768,  41285,  52015,  32768,  41285,  52015,  32768,
};

static const unsigned char s_scale_diff_table[128] = {
  0,   0,   0,   0,   0,   0,   0,   0,
  0,   0,   0,   0,   0,   0,   0,   0,
  0,   0,   0,   0,   0,   0,   0,   0,
  0,   0,   0,   0,   0,   0,   0,   0,
  0,   0,   0,   0,   0,   0,   0,   0,
  0,   0,   0,   0,   0,   0,   0,   0,
  0,   0,   0,   0,   0,   0,   0,   0,
  0,   0,   0,   0,   0,   0,   1,   1,
  2,   3,   3,   4,   4,   4,   4,   4,
  4,   4,   4,   4,   4,   4,   4,   4,
  4,   4,   4,   4,   4,   4,   4,   4,
  4,   4,   4,   4,   4,   4,   4,   4,
  4,   4,   4,   4,   4,   4,   4,   4,
  4,   4,   4,   4,   4,   4,   4,   4,
  4,   4,   4,   4,   4,   4,   4,   4,
  4,   4,   4,   4,   4,   4,   4,   4,
};

static const unsigned short s_total_quant_bits[17] = {
 60,  84, 108, 120, 144, 180, 216, 252, 288, 324, 360, 396, 432, 468, 504, 540, 576,
};

#endif

#ifndef ff_log2
#if HAVE_FAST_CLZ
#   define ff_log2(x) (31 - __builtin_clz((x)|1))
#else
const uint8_t ff_log2_tab[256] = {
        0,0,1,1,2,2,2,2,3,3,3,3,3,3,3,3,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,
        5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,
        6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,
        6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,
        7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,
        7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,
        7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,
        7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7
};

#define ff_log2 ff_log2_c
static av_always_inline av_const int ff_log2_c(unsigned int v)
{
    int n = 0;
    if (v & 0xffff0000) {
        v >>= 16;
        n += 16;
    }
    if (v & 0xff00) {
        v >>= 8;
        n += 8;
    }
    n += ff_log2_tab[v];

    return n;
}
#endif
#endif /* ff_log2 */

int av_log2(unsigned v)
{
    return ff_log2(v);
}

/* bitrate is in kb/s */
int ff_mpa_l2_select_table(int bitrate, int nb_channels, int freq, int lsf)
{
    int ch_bitrate, table;

    ch_bitrate = bitrate / nb_channels;
    if (!lsf) {
        if ((freq == 48000 && ch_bitrate >= 56) ||
            (ch_bitrate >= 56 && ch_bitrate <= 80))
            table = 0;
        else if (freq != 48000 && ch_bitrate >= 96)
            table = 1;
        else if (freq != 32000 && ch_bitrate <= 48)
            table = 2;
        else
            table = 3;
    }
    else {
        table = 4;
    }
    return table;
}

int MPA_encode_init(AVCodecContext *avctx)
{
    MpegAudioContext *s = avctx->priv_data;
    int freq = avctx->sample_rate;
    int bitrate = avctx->bit_rate;
    int channels = avctx->channels;
    int i, table;

    if (channels <= 0 || channels > 2){
        av_log(avctx, AV_LOG_ERROR, "encoding %d channel(s) is not allowed in mp2\n", channels);
        return AVERROR(EINVAL);
    }
    bitrate = bitrate / 1000;
    s->nb_channels = channels;
    avctx->frame_size = MPA_FRAME_SIZE;
    avctx->initial_padding = 512 - 32 + 1;

    /* encoding freq */
    s->lsf = 0;
    for(i=0;i<3;i++) {
        if (avpriv_mpa_freq_tab[i] == freq)
            break;
        if ((avpriv_mpa_freq_tab[i] / 2) == freq) {
            s->lsf = 1;
            break;
        }
    }
    if (i == 3){
        av_log(avctx, AV_LOG_ERROR, "Sampling rate %d is not allowed in mp2\n", freq);
        return AVERROR(EINVAL);
    }
    s->freq_index = i;

    /* encoding bitrate & frequency */
    for(i=1;i<15;i++) {
        if (avpriv_mpa_bitrate_tab[s->lsf][1][i] == bitrate)
            break;
    }
    if (i == 15 && !avctx->bit_rate) {
        i = 14;
        bitrate = avpriv_mpa_bitrate_tab[s->lsf][1][i];
        avctx->bit_rate = bitrate * 1000;
    }
    if (i == 15){
        av_log(avctx, AV_LOG_ERROR, "bitrate %d is not allowed in mp2\n", bitrate);
        return AVERROR(EINVAL);
    }
    s->bitrate_index = i;
#if FRAC_PADDING
    /* compute total header size & pad bit */
#define PADDING_FRAC    65536UL
    //float a = (float)(bitrate * 1000 * MPA_FRAME_SIZE) / (freq * 8.0);
    //s->frame_size = ((int)a) * 8;
    //double floor(double x);
    ///* frame fractional size to compute padding */
    //s->frame_frac = 0;
    //s->frame_frac_incr = (int)((a - floor(a)) * PADDING_FRAC);

    unsigned int fb = bitrate * 1000 * MPA_FRAME_SIZE / 8;
    s->frame_size = (fb / freq) * 8; //  8bit alignment
    s->frame_frac_incr = ((fb - s->frame_size / 8 * freq) * PADDING_FRAC + freq/2) / freq;
#else
    s->frame_size = (bitrate * 1000 / 8 * MPA_FRAME_SIZE / freq) * 8; //  8bit alignment
#endif
    /* select the right allocation table */
    table = ff_mpa_l2_select_table(bitrate, s->nb_channels, freq, s->lsf);

    /* number of used subbands */
    s->sblimit = ff_mpa_sblimit_table[table];
    s->alloc_table = ff_mpa_alloc_tables[table];
#if FRAC_PADDING
    ff_dlog(avctx, "%d kb/s, %d Hz, frame_size=%d bits, table=%d, padincr=%x\n",
            bitrate, freq, s->frame_size, table, s->frame_frac_incr);
#endif
    for(i=0;i<s->nb_channels;i++)
        s->samples_offset[i] = 0;
#if TABLE_GENERATE
    int v;
    for(i=0;i<257;i++) {
        v = ff_mpa_enwindow[i];
#if WFRAC_BITS != 16
        v = (v + (1 << (16 - WFRAC_BITS - 1))) >> (16 - WFRAC_BITS);
#endif
        s_filter_bank[i] = v;
        if ((i & 63) != 0)
            v = -v;
        if (i != 0)
            s_filter_bank[512 - i] = v;
    }
#if TABLE_GENERATE >= 2
    printf("static const int16_t s_filter_bank[512] = {\n");
    for (i = 0; i < sizeof(s_filter_bank) / sizeof(s_filter_bank[0]); i++) {
        printf("%6d, ", s_filter_bank[i]);
        if ((i + 1) % 8 == 0) {
            printf("\n");
        }
    }
    printf("};\n\n");
#endif
    for(i=0;i<64;i++) {
        v = (int)(exp2((3 - i) / 3.0) * (1 << 20));
        if (v <= 0)
            v = 1;
        s_scale_factor_table[i] = v;
#if USE_FLOATS
        s->scale_factor_inv_table[i] = exp2(-(3 - i) / 3.0) / (float)(1 << 20);
#else
        s_scale_factor_shift[i] = 21 - P - (i / 3);
        s_scale_factor_mult[i] = (1 << P) * exp2((i % 3) / 3.0);
#endif
    }

#if TABLE_GENERATE >= 2
    printf("static const int s_scale_factor_table[64] = {\n");
    for (i = 0; i < sizeof(s_scale_factor_table) / sizeof(s_scale_factor_table[0]); i++) {
        printf("%7d, ", s_scale_factor_table[i]);
        if ((i + 1) % 8 == 0) {
            printf("\n");
        }
    }
    printf("};\n\n");

    printf("static const int8_t s_scale_factor_shift[64] = {\n");
    for (i = 0; i < sizeof(s_scale_factor_shift) / sizeof(s_scale_factor_shift[0]); i++) {
        printf("%3d, ", s_scale_factor_shift[i]);
        if ((i + 1) % 8 == 0) {
            printf("\n");
        }
    }
    printf("};\n\n");

    printf("static const unsigned short s_scale_factor_mult[64] = {\n");
    for (i = 0; i < sizeof(s_scale_factor_mult) / sizeof(s_scale_factor_mult[0]); i++) {
        printf("%6d, ", s_scale_factor_mult[i]);
        if ((i + 1) % 8 == 0) {
            printf("\n");
        }
    }
    printf("};\n\n");
#endif

    for(i=0;i<128;i++) {
        v = i - 64;
        if (v <= -3)
            v = 0;
        else if (v < 0)
            v = 1;
        else if (v == 0)
            v = 2;
        else if (v < 3)
            v = 3;
        else
            v = 4;
        s_scale_diff_table[i] = v;
    }
#if TABLE_GENERATE >= 2
    printf("static const unsigned char s_scale_diff_table[128] = {\n");
    for (i = 0; i < sizeof(s_scale_diff_table) / sizeof(s_scale_diff_table[0]); i++) {
        printf("%3d, ", s_scale_diff_table[i]);
        if ((i + 1) % 8 == 0) {
            printf("\n");
        }
    }
    printf("};\n\n");
#endif
    for(i=0;i<17;i++) {
        v = ff_mpa_quant_bits[i];
        if (v < 0)
            v = -v;
        else
            v = v * 3;
        s_total_quant_bits[i] = 12 * v;
    }
#if TABLE_GENERATE >= 2
    printf("static const unsigned short s_total_quant_bits[17] = {\n");
    for (i = 0; i < sizeof(s_total_quant_bits) / sizeof(s_total_quant_bits[0]); i++) {
        printf("%3d, ", s_total_quant_bits[i]);
    }
    printf("\n};\n\n");
#endif
#endif
    return 0;
}

/* 32 point floating point IDCT without 1/sqrt(2) coef zero scaling */
static void idct32(int *out, int *tab)
{
    int i, j;
    int *t, *t1, xr;
    const int *xp = costab32;

    for(j=31;j>=3;j-=2) tab[j] += tab[j - 2];

    t = tab + 30;
    t1 = tab + 2;
    do {
        t[0] += t[-4];
        t[1] += t[1 - 4];
        t -= 4;
    } while (t != t1);

    t = tab + 28;
    t1 = tab + 4;
    do {
        t[0] += t[-8];
        t[1] += t[1-8];
        t[2] += t[2-8];
        t[3] += t[3-8];
        t -= 8;
    } while (t != t1);

    t = tab;
    t1 = tab + 32;
    do {
        t[ 3] = -t[ 3];
        t[ 6] = -t[ 6];

        t[11] = -t[11];
        t[12] = -t[12];
        t[13] = -t[13];
        t[15] = -t[15];
        t += 16;
    } while (t != t1);


    t = tab;
    t1 = tab + 8;
    do {
        int x1, x2, x3, x4;

        x3 = MUL(t[16], FIX(M_SQRT2*0.5));
        x4 = t[0] - x3;
        x3 = t[0] + x3;

        x2 = MUL(-(t[24] + t[8]), FIX(M_SQRT2*0.5));
        x1 = MUL((t[8] - x2), xp[0]);
        x2 = MUL((t[8] + x2), xp[1]);

        t[ 0] = x3 + x1;
        t[ 8] = x4 - x2;
        t[16] = x4 + x2;
        t[24] = x3 - x1;
        t++;
    } while (t != t1);

    xp += 2;
    t = tab;
    t1 = tab + 4;
    do {
        xr = MUL(t[28],xp[0]);
        t[28] = (t[0] - xr);
        t[0] = (t[0] + xr);

        xr = MUL(t[4],xp[1]);
        t[ 4] = (t[24] - xr);
        t[24] = (t[24] + xr);

        xr = MUL(t[20],xp[2]);
        t[20] = (t[8] - xr);
        t[ 8] = (t[8] + xr);

        xr = MUL(t[12],xp[3]);
        t[12] = (t[16] - xr);
        t[16] = (t[16] + xr);
        t++;
    } while (t != t1);
    xp += 4;

    for (i = 0; i < 4; i++) {
        xr = MUL(tab[30-i*4],xp[0]);
        tab[30-i*4] = (tab[i*4] - xr);
        tab[   i*4] = (tab[i*4] + xr);

        xr = MUL(tab[ 2+i*4],xp[1]);
        tab[ 2+i*4] = (tab[28-i*4] - xr);
        tab[28-i*4] = (tab[28-i*4] + xr);

        xr = MUL(tab[31-i*4],xp[0]);
        tab[31-i*4] = (tab[1+i*4] - xr);
        tab[ 1+i*4] = (tab[1+i*4] + xr);

        xr = MUL(tab[ 3+i*4],xp[1]);
        tab[ 3+i*4] = (tab[29-i*4] - xr);
        tab[29-i*4] = (tab[29-i*4] + xr);

        xp += 2;
    }

    t = tab + 30;
    t1 = tab + 1;
    do {
        xr = MUL(t1[0], *xp);
        t1[0] = (t[0] - xr);
        t[0] = (t[0] + xr);
        t -= 2;
        t1 += 2;
        xp++;
    } while (t >= tab);

    for(i=0;i<32;i++) {
        out[i] = tab[bitinv32[i]];
    }
}

#define WSHIFT (WFRAC_BITS + 15 - FRAC_BITS)

static void filter(MpegAudioContext *s, int ch, const short *samples, int incr)
{
    const short *p, *q;
    int sum, offset, i, j;
    int tmp[64];
    int tmp1[32];
    int *out;

    offset = s->samples_offset[ch];
    out = &s->sb_samples[ch][0][0][0];
    for(j=0;j<36;j++) {
        /* 32 samples at once */
        for(i=0;i<32;i++) {
            s->samples_buf[ch][offset + (31 - i)] = samples[0];
            samples += incr;
        }

        /* filter */
        p = s->samples_buf[ch] + offset;
        q = s_filter_bank;
        /* maxsum = 23169 */
        for(i=0;i<64;i++) {
            sum = p[0*64] * q[0*64];
            sum += p[1*64] * q[1*64];
            sum += p[2*64] * q[2*64];
            sum += p[3*64] * q[3*64];
            sum += p[4*64] * q[4*64];
            sum += p[5*64] * q[5*64];
            sum += p[6*64] * q[6*64];
            sum += p[7*64] * q[7*64];
            tmp[i] = sum;
            p++;
            q++;
        }
        tmp1[0] = tmp[16] >> WSHIFT;
        for( i=1; i<=16; i++ ) tmp1[i] = (tmp[i+16]+tmp[16-i]) >> WSHIFT;
        for( i=17; i<=31; i++ ) tmp1[i] = (tmp[i+16]-tmp[80-i]) >> WSHIFT;

        idct32(out, tmp1);

        /* advance of 32 samples */
        offset -= 32;
        out += 32;
        /* handle the wrap around */
        if (offset < 0) {
            memmove(s->samples_buf[ch] + SAMPLES_BUF_SIZE - (512 - 32),
                    s->samples_buf[ch], (512 - 32) * 2);
            offset = SAMPLES_BUF_SIZE - 512;
        }
    }
    s->samples_offset[ch] = offset;
}

static void compute_scale_factors(MpegAudioContext *s,
                                  unsigned char scale_code[SBLIMIT],
                                  unsigned char scale_factors[SBLIMIT][3],
                                  int sb_samples[3][12][SBLIMIT],
                                  int sblimit)
{
    int *p, vmax, v, n, i, j, k, code;
    int index, d1, d2;
    unsigned char *sf = &scale_factors[0][0];

    for(j=0;j<sblimit;j++) {
        for(i=0;i<3;i++) {
            /* find the max absolute value */
            p = &sb_samples[i][0][j];
            vmax = abs(*p);
            for(k=1;k<12;k++) {
                p += SBLIMIT;
                v = abs(*p);
                if (v > vmax)
                    vmax = v;
            }
            /* compute the scale factor index using log 2 computations */
            if (vmax > 1) {
                n = av_log2(vmax);
                /* n is the position of the MSB of vmax. now
                   use at most 2 compares to find the index */
                index = (21 - n) * 3 - 3;
                if (index >= 0) {
                    while (vmax <= s_scale_factor_table[index+1])
                        index++;
                } else {
                    index = 0; /* very unlikely case of overflow */
                }
            } else {
                index = 62; /* value 63 is not allowed */
            }

            ff_dlog(NULL, "%2d:%d in=%x %x %d\n",
                    j, i, vmax, s_scale_factor_table[index], index);
            /* store the scale factor */
            av_assert2(index >=0 && index <= 63);
            sf[i] = index;
        }

        /* compute the transmission factor : look if the scale factors
           are close enough to each other */
        d1 = s_scale_diff_table[sf[0] - sf[1] + 64];
        d2 = s_scale_diff_table[sf[1] - sf[2] + 64];

        /* handle the 25 cases */
        switch(d1 * 5 + d2) {
        case 0*5+0:
        case 0*5+4:
        case 3*5+4:
        case 4*5+0:
        case 4*5+4:
            code = 0;
            break;
        case 0*5+1:
        case 0*5+2:
        case 4*5+1:
        case 4*5+2:
            code = 3;
            sf[2] = sf[1];
            break;
        case 0*5+3:
        case 4*5+3:
            code = 3;
            sf[1] = sf[2];
            break;
        case 1*5+0:
        case 1*5+4:
        case 2*5+4:
            code = 1;
            sf[1] = sf[0];
            break;
        case 1*5+1:
        case 1*5+2:
        case 2*5+0:
        case 2*5+1:
        case 2*5+2:
            code = 2;
            sf[1] = sf[2] = sf[0];
            break;
        case 2*5+3:
        case 3*5+3:
            code = 2;
            sf[0] = sf[1] = sf[2];
            break;
        case 3*5+0:
        case 3*5+1:
        case 3*5+2:
            code = 2;
            sf[0] = sf[2] = sf[1];
            break;
        case 1*5+3:
            code = 2;
            if (sf[0] > sf[2])
              sf[0] = sf[2];
            sf[1] = sf[2] = sf[0];
            break;
        default:
            av_assert2(0); //cannot happen
            code = 0;           /* kill warning */
        }

        ff_dlog(NULL, "%d: %2d %2d %2d %d %d -> %d\n", j,
                sf[0], sf[1], sf[2], d1, d2, code);
        scale_code[j] = code;
        sf += 3;
    }
}

/* The most important function : psycho acoustic module. In this
   encoder there is basically none, so this is the worst you can do,
   but also this is the simpler. */
static void psycho_acoustic_model(MpegAudioContext *s, short smr[SBLIMIT])
{
    int i;

    for(i=0;i<s->sblimit;i++) {
        smr[i] = (int)(fixed_smr[i] * 10);
    }
}


#define SB_NOTALLOCATED  0
#define SB_ALLOCATED     1
#define SB_NOMORE        2

/* Try to maximize the smr while using a number of bits inferior to
   the frame size. I tried to make the code simpler, faster and
   smaller than other encoders :-) */
static void compute_bit_allocation(MpegAudioContext *s,
                                   short smr1[MPA_MAX_CHANNELS][SBLIMIT],
                                   unsigned char bit_alloc[MPA_MAX_CHANNELS][SBLIMIT],
                                   int *padding)
{
    int i, ch, b, max_smr, max_ch, max_sb, current_frame_size, max_frame_size;
    int incr;
    short smr[MPA_MAX_CHANNELS][SBLIMIT];
    unsigned char subband_status[MPA_MAX_CHANNELS][SBLIMIT];
    const unsigned char *alloc;

    memcpy(smr, smr1, s->nb_channels * sizeof(short) * SBLIMIT);
    memset(subband_status, SB_NOTALLOCATED, s->nb_channels * SBLIMIT);
    memset(bit_alloc, 0, s->nb_channels * SBLIMIT);

    /* compute frame size and padding */
    max_frame_size = s->frame_size;
#if FRAC_PADDING
    s->frame_frac += s->frame_frac_incr;
    if (s->frame_frac >= PADDING_FRAC) {
        s->frame_frac -= PADDING_FRAC;
        s->do_padding = 1;
        max_frame_size += 8;
    } else {
        s->do_padding = 0;
    }
#else
    s->do_padding = 0;
#endif
    /* compute the header + bit alloc size */
    current_frame_size = 32;
    alloc = s->alloc_table;
    for(i=0;i<s->sblimit;i++) {
        incr = alloc[0];
        current_frame_size += incr * s->nb_channels;
        alloc += 1 << incr;
    }
    for(;;) {
        /* look for the subband with the largest signal to mask ratio */
        max_sb = -1;
        max_ch = -1;
        max_smr = INT_MIN;
        for(ch=0;ch<s->nb_channels;ch++) {
            for(i=0;i<s->sblimit;i++) {
                if (smr[ch][i] > max_smr && subband_status[ch][i] != SB_NOMORE) {
                    max_smr = smr[ch][i];
                    max_sb = i;
                    max_ch = ch;
                }
            }
        }
        if (max_sb < 0)
            break;
        ff_dlog(NULL, "current=%d max=%d max_sb=%d max_ch=%d alloc=%d\n",
                current_frame_size, max_frame_size, max_sb, max_ch,
                bit_alloc[max_ch][max_sb]);

        /* find alloc table entry (XXX: not optimal, should use
           pointer table) */
        alloc = s->alloc_table;
        for(i=0;i<max_sb;i++) {
            alloc += 1 << alloc[0];
        }

        if (subband_status[max_ch][max_sb] == SB_NOTALLOCATED) {
            /* nothing was coded for this band: add the necessary bits */
            incr = 2 + nb_scale_factors[s->scale_code[max_ch][max_sb]] * 6;
            incr += s_total_quant_bits[alloc[1]];
        } else {
            /* increments bit allocation */
            b = bit_alloc[max_ch][max_sb];
            incr = s_total_quant_bits[alloc[b + 1]] -
                s_total_quant_bits[alloc[b]];
        }

        if (current_frame_size + incr <= max_frame_size) {
            /* can increase size */
            b = ++bit_alloc[max_ch][max_sb];
            current_frame_size += incr;
            /* decrease smr by the resolution we added */
            smr[max_ch][max_sb] = smr1[max_ch][max_sb] - quant_snr[alloc[b]];
            /* max allocation size reached ? */
            if (b == ((1 << alloc[0]) - 1))
                subband_status[max_ch][max_sb] = SB_NOMORE;
            else
                subband_status[max_ch][max_sb] = SB_ALLOCATED;
        } else {
            /* cannot increase the size of this subband */
            subband_status[max_ch][max_sb] = SB_NOMORE;
        }
    }
    *padding = max_frame_size - current_frame_size;
    av_assert0(*padding >= 0);
}

/*
 * Output the MPEG audio layer 2 frame. Note how the code is small
 * compared to other encoders :-)
 */
static void encode_frame(MpegAudioContext *s,
                         unsigned char bit_alloc[MPA_MAX_CHANNELS][SBLIMIT],
                         int padding)
{
    int i, j, k, l, bit_alloc_bits, b, ch;
    unsigned char *sf;
    int q[3];
    PutBitContext *p = &s->pb;

    /* header */

    put_bits(p, 12, 0xfff);
    put_bits(p, 1, 1 - s->lsf); /* 1 = MPEG-1 ID, 0 = MPEG-2 lsf ID */
    put_bits(p, 2, 4-2);  /* layer 2 */
    put_bits(p, 1, 1); /* no error protection */
    put_bits(p, 4, s->bitrate_index);
    put_bits(p, 2, s->freq_index);
    put_bits(p, 1, s->do_padding); /* use padding */
    put_bits(p, 1, 0);             /* private_bit */
    put_bits(p, 2, s->nb_channels == 2 ? MPA_STEREO : MPA_MONO);
    put_bits(p, 2, 0); /* mode_ext */
    put_bits(p, 1, 0); /* no copyright */
    put_bits(p, 1, 1); /* original */
    put_bits(p, 2, 0); /* no emphasis */

    /* bit allocation */
    j = 0;
    for(i=0;i<s->sblimit;i++) {
        bit_alloc_bits = s->alloc_table[j];
        for(ch=0;ch<s->nb_channels;ch++) {
            put_bits(p, bit_alloc_bits, bit_alloc[ch][i]);
        }
        j += 1 << bit_alloc_bits;
    }

    /* scale codes */
    for(i=0;i<s->sblimit;i++) {
        for(ch=0;ch<s->nb_channels;ch++) {
            if (bit_alloc[ch][i])
                put_bits(p, 2, s->scale_code[ch][i]);
        }
    }

    /* scale factors */
    for(i=0;i<s->sblimit;i++) {
        for(ch=0;ch<s->nb_channels;ch++) {
            if (bit_alloc[ch][i]) {
                sf = &s->scale_factors[ch][i][0];
                switch(s->scale_code[ch][i]) {
                case 0:
                    put_bits(p, 6, sf[0]);
                    put_bits(p, 6, sf[1]);
                    put_bits(p, 6, sf[2]);
                    break;
                case 3:
                case 1:
                    put_bits(p, 6, sf[0]);
                    put_bits(p, 6, sf[2]);
                    break;
                case 2:
                    put_bits(p, 6, sf[0]);
                    break;
                }
            }
        }
    }

    /* quantization & write sub band samples */

    for(k=0;k<3;k++) {
        for(l=0;l<12;l+=3) {
            j = 0;
            for(i=0;i<s->sblimit;i++) {
                bit_alloc_bits = s->alloc_table[j];
                for(ch=0;ch<s->nb_channels;ch++) {
                    b = bit_alloc[ch][i];
                    if (b) {
                        int qindex, steps, m, sample, bits;
                        /* we encode 3 sub band samples of the same sub band at a time */
                        qindex = s->alloc_table[j+b];
                        steps = ff_mpa_quant_steps[qindex];
                        for(m=0;m<3;m++) {
                            sample = s->sb_samples[ch][k][l + m][i];
                            /* divide by scale factor */
#if USE_FLOATS
                            {
                                float a;
                                a = (float)sample * s->scale_factor_inv_table[s->scale_factors[ch][i][k]];
                                q[m] = (int)((a + 1.0) * steps * 0.5);
                            }
#else
                            {
                                int q1, e, shift, mult;
                                e = s->scale_factors[ch][i][k];
                                shift = s_scale_factor_shift[e];
                                mult = s_scale_factor_mult[e];

                                /* normalize to P bits */
                                if (shift < 0)
                                    q1 = sample << (-shift);
                                else
                                    q1 = sample >> shift;
                                q1 = (q1 * mult) >> P;
                                q1 += 1 << P;
                                if (q1 < 0)
                                    q1 = 0;
                                q[m] = (q1 * (unsigned)steps) >> (P + 1);
                            }
#endif
                            if (q[m] >= steps)
                                q[m] = steps - 1;
                            av_assert2(q[m] >= 0 && q[m] < steps);
                        }
                        bits = ff_mpa_quant_bits[qindex];
                        if (bits < 0) {
                            /* group the 3 values to save bits */
                            put_bits(p, -bits,
                                     q[0] + steps * (q[1] + steps * q[2]));
                        } else {
                            put_bits(p, bits, q[0]);
                            put_bits(p, bits, q[1]);
                            put_bits(p, bits, q[2]);
                        }
                    }
                }
                /* next subband in alloc table */
                j += 1 << bit_alloc_bits;
            }
        }
    }

    /* padding */
    for(i=0;i<padding;i++)
        put_bits(p, 1, 0);

    /* flush */
    flush_put_bits(p);
}


int MPA_encode_frame(AVCodecContext *avctx, int16_t* samples, uint8_t *encoded)
{
    MpegAudioContext *s = avctx->priv_data;
    //const int16_t *samples = (const int16_t *)frame->data[0];
    short smr[MPA_MAX_CHANNELS][SBLIMIT];
    unsigned char bit_alloc[MPA_MAX_CHANNELS][SBLIMIT];
    int padding, i;//, ret;

    for(i=0;i<s->nb_channels;i++) {
        filter(s, i, samples + i, s->nb_channels);
    }

    for(i=0;i<s->nb_channels;i++) {
        compute_scale_factors(s, s->scale_code[i], s->scale_factors[i],
                              s->sb_samples[i], s->sblimit);
    }
    for(i=0;i<s->nb_channels;i++) {
        psycho_acoustic_model(s, smr[i]);
    }
    compute_bit_allocation(s, smr, bit_alloc, &padding);

    //if ((ret = ff_alloc_packet2(avctx, avpkt, MPA_MAX_CODED_FRAME_SIZE, 0)) < 0)
    //    return ret;
    //avpkt->data = MPA_encoded;
    //avpkt->size = sizeof(MPA_encoded);

    init_put_bits(&s->pb, encoded, MPA_MAX_CODED_FRAME_SIZE);

    encode_frame(s, bit_alloc, padding);

    //if (frame->pts != AV_NOPTS_VALUE)
    //    avpkt->pts = frame->pts - ff_samples_to_time_base(avctx, avctx->initial_padding);

    //avpkt->size = put_bits_count(&s->pb) / 8;
    //*got_packet_ptr = 1;
    return put_bits_count(&s->pb) / 8;
}

//static const AVCodecDefault mp2_defaults[] = {
//    { "b", "0" },
//    { NULL },
//};

#ifdef _CONSOLE
const uint8_t pcm1k[96] = {
    0x00, 0x00, 0xD4, 0x0B, 0x74, 0x17, 0xAD, 0x22, 0x4F, 0x2D, 0x2A, 0x37, 0x13, 0x40, 0xE4, 0x47,
    0x7A, 0x4E, 0xB8, 0x53, 0x88, 0x57, 0xD8, 0x59, 0x9E, 0x5A, 0xD8, 0x59, 0x88, 0x57, 0xB8, 0x53,
    0x7A, 0x4E, 0xE4, 0x47, 0x13, 0x40, 0x2A, 0x37, 0x4F, 0x2D, 0xAD, 0x22, 0x74, 0x17, 0xD4, 0x0B,
    0x00, 0x00, 0x2C, 0xF4, 0x8C, 0xE8, 0x53, 0xDD, 0xB1, 0xD2, 0xD6, 0xC8, 0xED, 0xBF, 0x1C, 0xB8,
    0x86, 0xB1, 0x48, 0xAC, 0x78, 0xA8, 0x28, 0xA6, 0x62, 0xA5, 0x28, 0xA6, 0x78, 0xA8, 0x48, 0xAC,
    0x86, 0xB1, 0x1C, 0xB8, 0xED, 0xBF, 0xD6, 0xC8, 0xB1, 0xD2, 0x53, 0xDD, 0x8C, 0xE8, 0x2C, 0xF4
};

int main(int argc, void* argv[])
{
    AVCodecContext mp2_ctx;
    MpegAudioContext mp2_priv_data;
    mp2_ctx.priv_data = &mp2_priv_data;
    memset(&mp2_priv_data, 0, sizeof(mp2_priv_data));

    mp2_ctx.sample_rate = 44100;
    mp2_ctx.bit_rate = 192000;
    mp2_ctx.channels = 2;

    MPA_encode_init(&mp2_ctx);

    char* infilename = "in.raw";
    char* outfilename = "out.mp3";

    if (argc >= 2) {
        infilename = argv[1];
        if (argc >= 3) {
            outfilename = argv[2];
        }
    }

    FILE* fpin, *fpout;
    fpin = fopen(infilename, "rb");
    fpout = fopen(outfilename, "wb");

    int frame = 0;
    int pcm1k_pos = 0;
    for (;;) {
        short inpcm[1152 * 2];
        uint8_t encout[1024];

        frame++;
#if 1
        int rdsize = fread(inpcm, 2 * mp2_ctx.channels, 1152, fpin);
        if (rdsize != 1152) {
            break;
        }
#else
        if (frame > 100) break;
        for (int i = 0; i < 1152; i++) {
            inpcm[i] = ((short*)pcm1k)[pcm1k_pos++];
            if (pcm1k_pos >= sizeof(pcm1k) / 2) {
                pcm1k_pos = 0;
            }
        }
#endif
        int osize = MPA_encode_frame(&mp2_ctx, inpcm, encout);
        fwrite(encout, 1, osize, fpout);
    }

    fclose(fpin);
    fclose(fpout);
    return 0;
}
#endif

