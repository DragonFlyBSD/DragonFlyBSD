/*
 * SYS/TLS.H
 *
 *	Implements the architecture independant TLS info structure.
 *
 * $DragonFly: src/sys/sys/tls.h,v 1.2 2005/02/22 02:17:56 dillon Exp $
 */

#ifndef _SYS_TLS_H_
#define _SYS_TLS_H_

struct tls_info {
	void *base;
	int size;
};

#ifndef _KERNEL
int sys_set_tls_area(int which, struct tls_info *info, int infosize);
int sys_get_tls_area(int which, struct tls_info *info, int infosize);
#endif

#endif
