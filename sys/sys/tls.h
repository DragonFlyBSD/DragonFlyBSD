/*
 * SYS/TLS.H
 *
 *	Implements the architecture independant TLS info structure.
 *
 * $DragonFly: src/sys/sys/tls.h,v 1.3 2005/03/21 17:09:52 joerg Exp $
 */

#ifndef _SYS_TLS_H_
#define _SYS_TLS_H_

struct tls_info {
	void *base;
	int size;
};

#ifndef _KERNEL
int sys_set_tls_area(int which, struct tls_info *info, size_t infosize);
int sys_get_tls_area(int which, struct tls_info *info, size_t infosize);
#endif

#endif
