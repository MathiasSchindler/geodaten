#ifndef NEWOS_STDINT_H
#define NEWOS_STDINT_H

#include <limits.h>

#ifndef _INT8_T
#define _INT8_T
typedef signed char int8_t;
#endif

#ifndef _UINT8_T
#define _UINT8_T
typedef unsigned char uint8_t;
#endif

#ifndef _INT16_T
#define _INT16_T
typedef short int16_t;
#endif

#ifndef _UINT16_T
#define _UINT16_T
typedef unsigned short uint16_t;
#endif

#ifndef _INT32_T
#define _INT32_T
typedef int int32_t;
#endif

#ifndef _UINT32_T
#define _UINT32_T
typedef unsigned int uint32_t;
#endif

#ifndef _INT64_T
#define _INT64_T
typedef long long int64_t;
#endif

#ifndef _UINT64_T
#define _UINT64_T
typedef unsigned long long uint64_t;
#endif

#ifndef _INTPTR_T
#define _INTPTR_T
#if defined(__INTPTR_TYPE__)
typedef __INTPTR_TYPE__ intptr_t;
#else
typedef long intptr_t;
#endif
#endif

#ifndef _UINTPTR_T
#define _UINTPTR_T
#if defined(__UINTPTR_TYPE__)
typedef __UINTPTR_TYPE__ uintptr_t;
#else
typedef unsigned long uintptr_t;
#endif
#endif

#ifndef _INTMAX_T
#define _INTMAX_T
typedef long long intmax_t;
#endif

#ifndef _UINTMAX_T
#define _UINTMAX_T
typedef unsigned long long uintmax_t;
#endif

#define INT8_MIN SCHAR_MIN
#define INT8_MAX SCHAR_MAX
#define UINT8_MAX UCHAR_MAX

#define INT16_MIN SHRT_MIN
#define INT16_MAX SHRT_MAX
#define UINT16_MAX USHRT_MAX

#define INT32_MIN INT_MIN
#define INT32_MAX INT_MAX
#define UINT32_MAX UINT_MAX

#define INT64_MIN LLONG_MIN
#define INT64_MAX LLONG_MAX
#define UINT64_MAX ULLONG_MAX

#if defined(__INTPTR_WIDTH__) && __INTPTR_WIDTH__ == 64
#define INTPTR_MIN INT64_MIN
#define INTPTR_MAX INT64_MAX
#define UINTPTR_MAX UINT64_MAX
#else
#define INTPTR_MIN LONG_MIN
#define INTPTR_MAX LONG_MAX
#define UINTPTR_MAX ULONG_MAX
#endif

#define INTMAX_MIN LLONG_MIN
#define INTMAX_MAX LLONG_MAX
#define UINTMAX_MAX ULLONG_MAX

#ifndef SIZE_MAX
#ifdef __SIZE_MAX__
#define SIZE_MAX __SIZE_MAX__
#else
#define SIZE_MAX ULONG_MAX
#endif
#endif

#endif