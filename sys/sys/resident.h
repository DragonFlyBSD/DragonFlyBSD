/*
 * SYS/RESIDENT.H
 *
 *	Userland system calls for resident executable support.
 *
 * $DragonFly: src/sys/sys/resident.h,v 1.2 2004/02/25 17:38:51 joerg Exp $
 */

#ifndef _SYS_RESIDENT_H_
#define _SYS_RESIDENT_H_

#if !defined(_KERNEL)

int exec_sys_register(void *);
int exec_sys_unregister(int);

#endif

#endif

