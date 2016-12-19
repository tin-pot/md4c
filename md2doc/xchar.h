/* xchar.h */

#ifndef XCHAR_H_INCLUDED
#define XCHAR_H_INCLUDED

#include <limits.h>
#include <stddef.h>

#ifndef MB_LEN_MAX
#error Please include "xchar.h" *after* <stdlib.h> and all other \
Standard library headers.
#endif

#define U8_X_FOR_U   1
#define U8_W_FOR_U   2
#define U8_C32_FOR_U 3
#define U8_WW_FOR_U  4

#ifndef U8_NAMES
#define U8_NAMES U8_X_FOR_U
#endif

#if U8_NAMES != U8_C32_FOR_U
#if (wchar_t)-1 < 0
#define U8_QUALIF signed
#else
#define U8_QUALIF unsigned
#endif
#else
#define U8_QUALIF unsigned
#endif

#if   UCHAR_MAX > 0x10FFFF
typedef U8_QUALIF char U8_CHAR_32;
typedef signed     char U8_INT_32;
#elif USHRT_MAX  > 0x10FFFF
typedef U8_QUALIF short U8_CHAR_32;
typedef signed     short U8_INT_32;
#elif UINT_MAX  > 0x10FFFF
typedef U8_QUALIF int U8_CHAR_32;
typedef signed     int U8_INT_32;
#elif ULONG_MAX > 0x10FFFF
typedef U8_QUALIF long U8_CHAR_32;
typedef U8_QUALIF long U8_INT_32;
#else
#error No integral type wide enough - can't happen!
#endif

#if   UCHAR_MAX >= 0xFFFF
typedef U8_QUALIF char U8_CHAR_16;
typedef signed    char U8_INT_16;
#elif USHRT_MAX  >= 0xFFFF
typedef U8_QUALIF short U8_CHAR_16;
typedef signed    short U8_INT_16;
#elif UINT_MAX  >= 0xFFFF
typedef U8_QUALIF int U8_CHAR_16;
typedef signed    int U8_INT_16;
#elif ULONG_MAX >= 0xFFFF
typedef U8_QUALIF long U8_CHAR_16;
typedef U8_QUALIF long U8_INT_16;
#else
#error No integral type wide enough - can't happen!
#endif

size_t u8len(const char *, size_t);
size_t u8tc32(U8_CHAR_32 *, const char *, size_t);
size_t u8fc32(char *, U8_CHAR_32);

#define U8_LEN_MAX 4
#define U8_CUR_MAX 3

#if U8_NAMES == U8_X_FOR_U

#   define XEOF ((xchar_t)-1)
#   define xchar_t U8_CHAR_32
#   define xint_t  U8_INT_32

#   define mbtoxc(PC_, S_, N_)    u8tc32((PC_), (S_), (N_))
#   define xctomb(S_, C_)         u8fc32((S_), (C_))

#elif U8_NAMES == U8_W_FOR_U

#   undef MB_LEN_MAX
#   undef MB_CUR_MAX
#   define MB_LEN_MAX U8_LEN_MAX
#   define MB_CUR_MAX U8_CUR_MAX

#   undef wchar_t
#   undef wint_t
#   define wchar_t U8_CHAR_32
#   define wint_t  U8_INT_32

#   define mblen(PC_, N_)          u8len((PC_), (N_))
#   define mbtowc(PC_, S_, N_)     u8tc32((PC_), (S_), (N_))
#   define wctomb(S_, C_)          u8fc32((S_), (C_))

#elif U8_NAMES == U8_C32_FOR_U

#   undef char32_t
#   undef char16_t

#   define char32_t U8_CHAR_32
#   define char16_t U8_CHAR_16

#   define mblen(PC_, N_)          u8len((PC_), (N_))
#   define mbtoc32(PC_, S_, N_)    u8tc32((PC_), (S_), (N_))
#   define c32tomb(S_, C_)         u8fc32((S_), (C_))

#elif U8_NAMES == U8_WW_FOR_U

#   define wwchar_t U8_CHAR_32

#   define mblen(PC_, N_)          u8len((PC_), (N_))
#   define mbtowwc(PC_, S_, N_)    u8tc32((PC_), (S_), (N_))
#   define wwctomb(S_, C_)         u8fc32((S_), (C_))

#else /* No disguise. */
#endif

#endif/*XCHAR_H_INCLUDED*/