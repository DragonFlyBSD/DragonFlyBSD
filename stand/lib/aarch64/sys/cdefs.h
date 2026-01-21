#ifndef _SYS_CDEFS_H_
#define _SYS_CDEFS_H_

#define __weak_reference(sym, alias)

#define __dead2 __attribute__((__noreturn__))
#define __printflike(fmtarg, firstvararg) __attribute__((__format__(__printf__, fmtarg, firstvararg)))
#define __unused __attribute__((__unused__))

#ifdef __cplusplus
#define __BEGIN_DECLS extern "C" {
#define __END_DECLS }
#else
#define __BEGIN_DECLS
#define __END_DECLS
#endif

#endif
