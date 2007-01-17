/* $DragonFly: src/lib/libc/i386/sys/asmcontext.c,v 1.1 2007/01/17 02:17:36 corecode Exp $ */

#define _KERNEL_STRUCTURES
#include <sys/types.h>
#include <sys/ucontext.h>
#include <sys/assym.h>
#include <stddef.h>

ASSYM(UC_SIGMASK, offsetof(ucontext_t, uc_sigmask));
ASSYM(UC_MCONTEXT, offsetof(ucontext_t, uc_mcontext));
ASSYM(SIG_BLOCK, SIG_BLOCK);
ASSYM(SIG_SETMASK, SIG_SETMASK);
ASSYM(MC_LEN, offsetof(mcontext_t, mc_len));
ASSYM(SIZEOF_MCONTEXT_T, sizeof(mcontext_t));
