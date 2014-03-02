/*
 * This file is in the public domain.
 * $FreeBSD: src/sys/sys/inttypes.h,v 1.2 1999/08/28 00:51:47 peter Exp $
 *
 * Note: since portions of these header files can be included with various
 * other combinations of defines, we cannot surround the whole header file
 * with an #ifndef sequence.  Elements are individually protected.
 */

#include <sys/cdefs.h>
#include <machine/stdint.h>

#ifndef _SYS_STDINT_H_
#define _SYS_STDINT_H_

/*
 * wchar_t and rune_t have to be of the same type. rune_t is meant
 * for internal use only.
 *
 * wchar_t, wint_t and rune_t are signed, to allow EOF (-1) to naturally
 * assigned.
 *
 * ANSI specifies ``int'' as argument for the is*() and to*() routines.
 * Keeping wchar_t and rune_t as ``int'' instead of the more natural
 * ``long'' helps ANSI conformance. ISO 10646 will most likely end up
 * as 31 bit standard and all supported architectures have
 * sizeof(int) >= 4.
 */
#ifndef __cplusplus
typedef	int		__wchar_t;
#endif
#ifndef ___WINT_T_DECLARED
typedef	int		__wint_t;
#define	___WINT_T_DECLARED
#endif
#ifndef ___RUNE_T_DECLARED
typedef	int		__rune_t;
#define ___RUNE_T_DECLARED
#endif
typedef	void		*__wctrans_t;
typedef	void		*__wctype_t;

/*
 * mbstate_t is an opaque object to keep conversion state, during multibyte
 * stream conversions.  The content must not be referenced by user programs.
 */
typedef union {
	__uint8_t __mbstate8[128];
	__int64_t __mbstateL;	/* for alignment */
} __mbstate_t;

typedef __int64_t	__off_t;
typedef __int32_t	__pid_t;

#endif	/* SYS_STDINT_H */
