/*
 * ASDEFS.C
 *
 * Generate assembly defs.
 *
 * $DragonFly: src/lib/libcaps/asdefs.c,v 1.1 2003/12/04 22:06:19 dillon Exp $
 */

#include <sys/cdefs.h>			/* for __dead2 needed by thread.h */
#include "libcaps/thread.h"
#include <sys/thread.h>
#include "libcaps/globaldata.h"
#include <stddef.h>			/* for offsetof(type, field) */
#include <stdio.h>

#define OFFSET(name, offset)	printf("#define %s %d\n", #name, offset);

int
main(int ac, char **av)
{
    OFFSET(TD_SP, offsetof(struct thread, td_sp));
    OFFSET(TD_FLAGS, offsetof(struct thread, td_flags));
    OFFSET(TD_MPCOUNT, offsetof(struct thread, td_flags));
    OFFSET(TD_PRI, offsetof(struct thread, td_pri));

    OFFSET(TDF_RUNNING, TDF_RUNNING);
    OFFSET(TDPRI_CRIT, TDPRI_CRIT);

    OFFSET(gd_curthread, offsetof(struct globaldata, gd_curthread));
    OFFSET(gd_cpuid, offsetof(struct globaldata, gd_cpuid));

    return(0);
}

