/* $FreeBSD: src/usr.sbin/ctm/ctm_rmail/error.h,v 1.1.12.1 2001/07/05 07:47:00 kris Exp $ */
/* $DragonFly: src/usr.sbin/ctm/ctm_rmail/Attic/error.h,v 1.2 2003/06/17 04:29:53 dillon Exp $ */

extern	void	err_set_log(char *log_file);
extern	void	err_prog_name(char *name);
extern	void	err(const char *fmt, ...) __printflike(1, 2);
