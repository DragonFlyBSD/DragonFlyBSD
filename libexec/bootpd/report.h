/* report.h */
/* $FreeBSD: src/libexec/bootpd/report.h,v 1.1.1.1.14.2 2003/02/15 05:36:01 kris Exp $ */
/* $DragonFly: src/libexec/bootpd/report.h,v 1.2 2003/06/17 04:27:07 dillon Exp $ */

extern void report_init(int nolog);
extern void report(int, const char *, ...) __printflike(2, 3);
extern const char *get_errmsg(void);
