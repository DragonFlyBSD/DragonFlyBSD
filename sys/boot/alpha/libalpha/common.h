/*
 * $FreeBSD: src/sys/boot/alpha/libalpha/common.h,v 1.2 1999/08/28 00:39:26 peter Exp $
 * $DragonFly: src/sys/boot/alpha/libalpha/Attic/common.h,v 1.3 2003/08/27 11:42:33 rob Exp $
 * From: $NetBSD: common.h,v 1.2 1998/01/05 07:02:48 perry Exp $	
 */

int prom_open (char*, int);
void OSFpal (void);
void halt (void);
u_int64_t prom_dispatch (int, ...);
int cpu_number (void);
void switch_palcode (void);
