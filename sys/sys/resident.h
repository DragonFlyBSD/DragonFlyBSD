/*
 * SYS/RESIDENT.H
 *
 *	Userland system calls for resident executable support.
 *
 * $DragonFly: src/sys/sys/resident.h,v 1.1 2004/01/20 21:03:20 dillon Exp $
 */

#ifndef _SYS_RESIDENT_H_
#define _SYS_RESIDENT_H_

#if !defined(_KERNEL)

int exec_sys_register(void *entry);
int exec_sys_unregister(int id);

#endif

#endif

