/*
 * i386/include/thread.h
 *
 * $DragonFly: src/sys/platform/pc32/include/thread.h,v 1.2 2003/06/22 08:54:20 dillon Exp $
 */

struct mi_thread {
    unsigned int	mtd_cpl;
};

#define td_cpl	td_mach.mtd_cpl

