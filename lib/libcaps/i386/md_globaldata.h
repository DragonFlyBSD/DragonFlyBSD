/*
 * MD_GLOBALDATA.H
 *
 * $DragonFly: src/lib/libcaps/i386/md_globaldata.h,v 1.3 2007/04/13 12:12:27 corecode Exp $
 */

#ifndef _MD_GLOBALDATA_H_
#define _MD_GLOBALDATA_H_

extern int __mycpu__dummy;
 
static __inline
globaldata_t
_get_mycpu(void)
{
    globaldata_t gd;

    __asm ("movl %%gs:0,%0" : "=r" (gd) : "m"(__mycpu__dummy));
    return(gd);
}

static __inline
void
_set_mycpu(int selector)
{
    __asm __volatile("mov %0,%%gs" :: "g"(selector));
}

#define mycpu   _get_mycpu()

void md_gdinit1(globaldata_t gd);
void md_gdinit2(globaldata_t gd);
void cpu_user_switch(void);
void cpu_rfork_start(void) __dead2;

#endif

