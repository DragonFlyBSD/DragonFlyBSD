/* report.h */
/* $FreeBSD: src/libexec/bootpd/report.h,v 1.1.1.1.14.2 2003/02/15 05:36:01 kris Exp $ */
/* $DragonFly: src/libexec/bootpd/report.h,v 1.2 2003/06/17 04:27:07 dillon Exp $ */

#ifdef	__STDC__
#define P(args) args
#else
#define P(args) ()
#endif

extern void report_init P((int nolog));
extern void report P((int, const char *, ...)) __printflike(2, 3);
extern const char *get_errmsg P((void));

#undef P
