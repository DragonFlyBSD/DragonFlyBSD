/*
 * SYS/TLS.H
 *
 *	Implements the architecture independant TLS info structure.
 *
 * $DragonFly: src/sys/sys/tls.h,v 1.5 2005/03/28 03:33:08 dillon Exp $
 */

#ifndef _SYS_TLS_H_
#define _SYS_TLS_H_

#include <sys/types.h>

struct tls_info {
	void *base;
	int size;
};

struct tls_tcb {
	struct tls_tcb *tcb_base;	/* self pointer (data at -OFFSET) */
	void *dtv_base;		/* RTLD tls_get_addr info base */
	void *reserved[6];
};

#define RTLD_STATIC_TLS_ALIGN           16
#define RTLD_STATIC_TLS_ALIGN_MASK      (RTLD_STATIC_TLS_ALIGN - 1)

/*
 * flags for _rtld_allocate_tls() and allocate_tls()
 */
#define RTLD_ALLOC_TLS_FREE_OLD		0x0001

#ifndef _KERNEL
int sys_set_tls_area(int which, struct tls_info *info, size_t infosize);
int sys_get_tls_area(int which, struct tls_info *info, size_t infosize);
#endif

#endif
