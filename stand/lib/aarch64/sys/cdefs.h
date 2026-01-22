#ifndef _SYS_CDEFS_H_
#define _SYS_CDEFS_H_

#define __weak_reference(sym, alias)

#if defined(__GNUC__)
#define __GNUC_PREREQ__(ma, mi) (__GNUC__ > (ma) || (__GNUC__ == (ma) && __GNUC_MINOR__ >= (mi)))
#else
#define __GNUC_PREREQ__(ma, mi) 0
#endif

#define __CONCAT1(x, y) x ## y
#define __CONCAT(x, y) __CONCAT1(x, y)
#define __STRING(x) #x
#define __XSTRING(x) __STRING(x)

#define __dead2 __attribute__((__noreturn__))
#define __printflike(fmtarg, firstvararg) __attribute__((__format__(__printf__, fmtarg, firstvararg)))
#define __strftimelike(fmtarg, firstvararg) __attribute__((__format__(__strftime__, fmtarg, firstvararg)))
#define __unused __attribute__((__unused__))
#define __packed __attribute__((__packed__))
#define __aligned(x) __attribute__((__aligned__(x)))
#define __always_inline __attribute__((__always_inline__))
#define __section(x) __attribute__((__section__(x)))
#define __used __attribute__((__used__))

#define __GLOBL1(sym) __asm__(".globl " #sym)
#define __GLOBL(sym) __GLOBL1(sym)

#define __FBSDID(s) struct __hack

#ifdef __cplusplus
#define __BEGIN_DECLS extern "C" {
#define __END_DECLS }
#else
#define __BEGIN_DECLS
#define __END_DECLS
#endif

#endif
