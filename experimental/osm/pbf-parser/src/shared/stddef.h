#if !defined(NEWOS_STDDEF_H) || defined(__need_ptrdiff_t) || defined(__need_size_t) || defined(__need_wchar_t) || defined(__need_NULL) || defined(__need_wint_t)

#if !defined(__need_ptrdiff_t) && !defined(__need_size_t) && !defined(__need_wchar_t) && !defined(__need_NULL) && !defined(__need_wint_t)
#define NEWOS_STDDEF_H
#define NEWOS_STDDEF_FULL 1
#endif

#if defined(NEWOS_STDDEF_FULL) || defined(__need_NULL)
#ifndef NULL
#define NULL ((void *)0)
#endif
#undef __need_NULL
#endif

#if defined(NEWOS_STDDEF_FULL) || defined(__need_size_t)
#ifndef NEWOS_SIZE_T_DEFINED
#define NEWOS_SIZE_T_DEFINED
#ifdef __SIZE_TYPE__
typedef __SIZE_TYPE__ size_t;
#else
typedef unsigned long size_t;
#endif
#endif
#undef __need_size_t
#endif

#if defined(NEWOS_STDDEF_FULL) || defined(__need_ptrdiff_t)
#ifndef NEWOS_PTRDIFF_T_DEFINED
#define NEWOS_PTRDIFF_T_DEFINED
#ifdef __PTRDIFF_TYPE__
typedef __PTRDIFF_TYPE__ ptrdiff_t;
#else
typedef long ptrdiff_t;
#endif
#endif
#undef __need_ptrdiff_t
#endif

#if defined(NEWOS_STDDEF_FULL) || defined(__need_wchar_t)
#ifndef NEWOS_WCHAR_T_DEFINED
#define NEWOS_WCHAR_T_DEFINED
#ifdef __WCHAR_TYPE__
typedef __WCHAR_TYPE__ wchar_t;
#else
typedef int wchar_t;
#endif
#endif
#undef __need_wchar_t
#endif

#if defined(__need_wint_t)
#ifndef NEWOS_WINT_T_DEFINED
#define NEWOS_WINT_T_DEFINED
#ifdef __WINT_TYPE__
typedef __WINT_TYPE__ wint_t;
#else
typedef unsigned int wint_t;
#endif
#endif
#undef __need_wint_t
#endif

#define offsetof(type, member) ((size_t)&(((type *)0)->member))

#undef NEWOS_STDDEF_FULL

#endif