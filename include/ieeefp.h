/*	$NetBSD: ieeefp.h,v 1.4 1998/01/09 08:03:43 perry Exp $	*/
/* $FreeBSD: src/include/ieeefp.h,v 1.3.2.1 2000/08/17 08:08:14 jhb Exp $ */
/* $DragonFly: src/include/ieeefp.h,v 1.3 2003/11/14 01:01:43 dillon Exp $ */

/* 
 * Written by J.T. Conklin, Apr 6, 1995
 * Public domain.
 */

#ifndef _IEEEFP_H_
#define _IEEEFP_H_

#include <sys/cdefs.h>
#include <machine/ieeefp.h>

#ifdef __i386__
#include <machine/floatingpoint.h>
#else /* !__i386__ */
__BEGIN_DECLS
extern fp_rnd_t    fpgetround (void);
extern fp_rnd_t    fpsetround (fp_rnd_t);
extern fp_except_t fpgetmask (void);
extern fp_except_t fpsetmask (fp_except_t);
extern fp_except_t fpgetsticky (void);
extern fp_except_t fpsetsticky (fp_except_t);
__END_DECLS
#endif /* __i386__ */

#endif /* _IEEEFP_H_ */
