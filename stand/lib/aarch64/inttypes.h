/*
 * Minimal inttypes.h shim for aarch64 EFI loader build.
 */

#ifndef _INTTYPES_H_
#define _INTTYPES_H_

#include <stdint.h>

/* Format macros for printf - 64-bit */
#define PRId64		"ld"
#define PRIi64		"li"
#define PRIo64		"lo"
#define PRIu64		"lu"
#define PRIx64		"lx"
#define PRIX64		"lX"

#define PRId32		"d"
#define PRIi32		"i"
#define PRIo32		"o"
#define PRIu32		"u"
#define PRIx32		"x"
#define PRIX32		"X"

#define PRId16		"d"
#define PRIi16		"i"
#define PRIo16		"o"
#define PRIu16		"u"
#define PRIx16		"x"
#define PRIX16		"X"

#define PRId8		"d"
#define PRIi8		"i"
#define PRIo8		"o"
#define PRIu8		"u"
#define PRIx8		"x"
#define PRIX8		"X"

#define PRIdMAX		"ld"
#define PRIiMAX		"li"
#define PRIoMAX		"lo"
#define PRIuMAX		"lu"
#define PRIxMAX		"lx"
#define PRIXMAX		"lX"

#define PRIdPTR		"ld"
#define PRIiPTR		"li"
#define PRIoPTR		"lo"
#define PRIuPTR		"lu"
#define PRIxPTR		"lx"
#define PRIXPTR		"lX"

/* intmax_t - just use int64_t */
#ifndef _INTMAX_T_DECLARED
#define _INTMAX_T_DECLARED
typedef int64_t		intmax_t;
#endif
#ifndef _UINTMAX_T_DECLARED
#define _UINTMAX_T_DECLARED
typedef uint64_t	uintmax_t;
#endif

#endif /* !_INTTYPES_H_ */
