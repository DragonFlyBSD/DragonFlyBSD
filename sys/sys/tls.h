/*
 * SYS/TLS.H
 *
 *	Implements the architecture independant TLS info structure.
 *
 * $DragonFly: src/sys/sys/tls.h,v 1.1 2005/02/21 21:40:58 dillon Exp $
 */

#ifndef _SYS_TLS_H_
#define _SYS_TLS_H_

struct tls_info {
	void *base;
	int size;
};

#endif
