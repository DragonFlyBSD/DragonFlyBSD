/*
 * $DragonFly: src/sys/platform/pc64/include/param.h,v 1.2 2008/08/29 17:07:17 dillon Exp $
 */

#ifndef _MACHINE_PARAM_H_

#ifndef _NO_NAMESPACE_POLLUTION
#define _MACHINE_PARAM_H_
#endif

#ifndef _MACHINE_PLATFORM
#define _MACHINE_PLATFORM	pc64
#endif

#ifndef _NO_NAMESPACE_POLLUTION

#ifndef MACHINE_PLATFORM
#define MACHINE_PLATFORM	"pc64"
#endif

#endif

#include <cpu/param.h>

/* JG from fbsd/sys/amd64/include/param.h */
#ifndef	KSTACK_PAGES
#define	KSTACK_PAGES	4	/* pages of kstack (with pcb) */
#endif

#endif
