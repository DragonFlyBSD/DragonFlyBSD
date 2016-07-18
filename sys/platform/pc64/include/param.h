#ifndef _MACHINE_PARAM_H_
#define _MACHINE_PARAM_H_

#ifndef _MACHINE_PLATFORM
#define _MACHINE_PLATFORM	pc64
#endif

#ifndef MACHINE_PLATFORM
#define MACHINE_PLATFORM	"pc64"
#endif

#include <cpu/param.h>

/* JG from fbsd/sys/amd64/include/param.h */
#ifndef	KSTACK_PAGES
#define	KSTACK_PAGES	4	/* pages of kstack (with pcb) */
#endif

#endif

