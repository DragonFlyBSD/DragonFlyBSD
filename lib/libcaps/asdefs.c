/*
 * ASDEFS.C
 *
 * Generate assembly defs.
 *
 * $DragonFly: src/lib/libcaps/asdefs.c,v 1.2 2003/12/07 04:21:52 dillon Exp $
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
    OFFSET(TD_MPCOUNT, offsetof(struct thread, td_mpcount));
    OFFSET(TD_PRI, offsetof(struct thread, td_pri));

    OFFSET(UPC_MAGIC, offsetof(struct upcall, upc_magic));
    OFFSET(UPC_CRITOFF, offsetof(struct upcall, upc_critoff));
    OFFSET(UPC_PENDING, offsetof(struct upcall, upc_pending));
    OFFSET(UPC_UTHREAD, offsetof(struct upcall, upc_uthread));

    OFFSET(TDF_RUNNING, TDF_RUNNING);
    OFFSET(TDPRI_CRIT, TDPRI_CRIT);

    OFFSET(gd_curthread, offsetof(struct globaldata, gd_upcall.upc_uthread));
    OFFSET(gd_cpuid, offsetof(struct globaldata, gd_cpuid));

    return(0);
}

