#ifndef MP2EN_H
#define MP2EN_H

#include <stdint.h>
#include <limits.h>
#include <errno.h>

//#define DEBUG_MP2

/* max frame size, in samples */
#define MPA_FRAME_SIZE 1152

/* max compressed frame size */
#define MPA_MAX_CODED_FRAME_SIZE 1792

#define MPA_MAX_CHANNELS 2

#define SBLIMIT 32 /* number of subbands */

#define MPA_STEREO  0
#define MPA_JSTEREO 1
#define MPA_DUAL    2
#define MPA_MONO    3

#ifndef FRAC_BITS
#define FRAC_BITS   23   /* fractional bits for sb_samples and dct */
#define WFRAC_BITS  16   /* fractional bits for window */
#endif

#define IMDCT_SCALAR 1.759

#define FRAC_ONE    (1 << FRAC_BITS)

#define FIX(a)   ((int)((a) * FRAC_ONE))

#if USE_FLOATS
#   define INTFLOAT float
#   define SUINTFLOAT float
typedef float MPA_INT;
typedef float OUT_INT;
#elif FRAC_BITS <= 15
#   define INTFLOAT int
#   define SUINTFLOAT SUINT
typedef int16_t MPA_INT;
typedef int16_t OUT_INT;
#else
#   define INTFLOAT int
#   define SUINTFLOAT SUINT
typedef int32_t MPA_INT;
typedef int16_t OUT_INT;
#endif

#ifndef M_SQRT2
#define M_SQRT2        1.41421356237309504880  /* sqrt(2) */
#endif

#ifdef __GNUC__
#    define AV_GCC_VERSION_AT_LEAST(x,y) (__GNUC__ > (x) || __GNUC__ == (x) && __GNUC_MINOR__ >= (y))
#    define AV_GCC_VERSION_AT_MOST(x,y)  (__GNUC__ < (x) || __GNUC__ == (x) && __GNUC_MINOR__ <= (y))
#else
#    define AV_GCC_VERSION_AT_LEAST(x,y) 0
#    define AV_GCC_VERSION_AT_MOST(x,y)  0
#endif

#if AV_GCC_VERSION_AT_LEAST(4,3) || defined(__clang__)
#    define av_cold __attribute__((cold))
#else
#    define av_cold
#endif
#if defined(__GNUC__) || defined(__clang__)
#    define av_unused __attribute__((unused))
#else
#    define av_unused
#endif
#if AV_GCC_VERSION_AT_LEAST(2,6) || defined(__clang__)
#    define av_const __attribute__((const))
#else
#    define av_const
#endif
#ifndef av_always_inline
#if AV_GCC_VERSION_AT_LEAST(3,1)
#    define av_always_inline __attribute__((always_inline)) inline
#elif defined(_MSC_VER)
#    define av_always_inline __forceinline
#else
#    define av_always_inline inline
#endif
#endif

#define AV_STRINGIFY(s)         AV_TOSTRING(s)
#define AV_TOSTRING(s) #s
/**
 * assert() equivalent, that is always enabled.
 */
#ifdef _CONSOLE
#define av_assert0(cond) do {                                           \
    if (!(cond)) {                                                      \
        av_log(NULL, AV_LOG_PANIC, "Assertion %s failed at %s:%d\n",    \
               AV_STRINGIFY(cond), __FILE__, __LINE__);                 \
        abort();                                                        \
    }                                                                   \
} while (0)
#else
#define av_assert0(cond)
#endif
#define av_assert2(cond) ((void)0)  // av_assert0(cond)


#ifndef AV_WB32
#   define AV_WB32(p, val) do {                 \
        uint32_t d = (val);                     \
        ((uint8_t*)(p))[3] = (d);               \
        ((uint8_t*)(p))[2] = (d)>>8;            \
        ((uint8_t*)(p))[1] = (d)>>16;           \
        ((uint8_t*)(p))[0] = (d)>>24;           \
    } while(0)
#endif


#ifdef DEBUG_MP2
#   define av_log(avcl, level, ...)	printf(__VA_ARGS__)
#   define ff_dlog(ctx, ...)        av_log(ctx, AV_LOG_DEBUG, __VA_ARGS__)
#else
#   define av_log(avcl, level, ...)	do {} while (0)
#   define ff_dlog(ctx, ...)        do {} while (0)
#endif

#define AVERROR(e) (-(e))   ///< Returns a negative error code from a POSIX error code, to return from library functions.


typedef struct AVCodecContext {
    void* priv_data;
    /* audio only */
    int sample_rate; ///< samples per second
    int channels;    ///< number of audio channels
    int bit_rate;
    int frame_size;
    int initial_padding;
} AVCodecContext;

int ff_mpa_l2_select_table(int bitrate, int nb_channels, int freq, int lsf);

#endif

