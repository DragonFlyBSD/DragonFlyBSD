/*
 * From: $NetBSD: common.h,v 1.2 1998/01/05 07:02:48 perry Exp $	
 * $FreeBSD: src/sys/boot/alpha/libalpha/common.h,v 1.3 2002/06/29 02:32:32 peter Exp $
 * $DragonFly: src/sys/boot/alpha/libalpha/Attic/common.h,v 1.4 2003/11/10 06:08:29 dillon Exp $
 */

int prom_open(char*, int);
void OSFpal(void);
void halt(void);
u_int64_t prom_dispatch(int, ...);
int cpu_number(void);
void switch_palcode(void);
