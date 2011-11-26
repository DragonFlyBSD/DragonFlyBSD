/* $FreeBSD: src/sys/i386/linux/linux_genassym.c,v 1.13.2.3 2001/11/05 19:08:23 marcel Exp $ */
/* $DragonFly: src/sys/emulation/linux/i386/linux_genassym.c,v 1.3 2003/08/07 21:17:18 dillon Exp $ */

#include <sys/param.h>
#include <sys/assym.h>

#include "linux.h"

ASSYM(LINUX_SIGF_HANDLER, offsetof(struct l_sigframe, sf_handler));
ASSYM(LINUX_SIGF_SC, offsetof(struct l_sigframe, sf_sc));
ASSYM(LINUX_SC_GS, offsetof(struct l_sigcontext, sc_gs));
ASSYM(LINUX_SC_EFLAGS, offsetof(struct l_sigcontext, sc_eflags));
ASSYM(LINUX_RT_SIGF_HANDLER, offsetof(struct l_rt_sigframe, sf_handler));
ASSYM(LINUX_RT_SIGF_UC, offsetof(struct l_rt_sigframe, sf_sc));
ASSYM(LINUX_RT_SIGF_SC, offsetof(struct l_ucontext, uc_mcontext));
