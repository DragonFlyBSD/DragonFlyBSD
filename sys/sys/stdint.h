/*
 * This file is in the public domain.
 * $FreeBSD: src/sys/sys/inttypes.h,v 1.2 1999/08/28 00:51:47 peter Exp $
 * $DragonFly: src/sys/sys/stdint.h,v 1.2 2003/11/19 00:42:30 dillon Exp $
 *
 * Note: since portions of these header files can be included with various
 * other combinations of defines, we cannot surround the whole header file
 * with an #ifndef sequence.  Elements are individually protected.
 */

#include <sys/cdefs.h>
#include <machine/stdint.h>

#ifndef _SYS_STDINT_H_
#define _SYS_STDINT_H_

typedef int		__ct_rune_t;
typedef	__ct_rune_t	__rune_t;
#ifndef __cplusplus
typedef	__ct_rune_t	__wchar_t;
#endif
typedef __ct_rune_t	__wint_t;
typedef __int64_t	__off_t;
typedef __int32_t	__pid_t;

#endif	/* SYS_STDINT_H */

#if !defined(__cplusplus) || defined(__STDC_LIMIT_MACROS)
#ifndef _SYS_STDINT_H_STDC_LIMIT_MACROS_
#define _SYS_STDINT_H_STDC_LIMIT_MACROS_

#ifndef WCHAR_MIN /* Also possibly defined in <wchar.h> */
/* Limits of wchar_t. */
#define WCHAR_MIN       INT32_MIN
#define WCHAR_MAX       INT32_MAX

/* Limits of wint_t. */
#define WINT_MIN        INT32_MIN
#define WINT_MAX        INT32_MAX
#endif

#endif	/* STDINT_H_STDC_LIMIT_MACROS */
#endif	/* STDC_LIMIT_MACROS */
