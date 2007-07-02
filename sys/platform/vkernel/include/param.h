/*
 * $DragonFly: src/sys/platform/vkernel/include/param.h,v 1.2 2007/07/02 03:44:10 dillon Exp $
 */

#ifndef _MACHINE_PARAM_H_

#ifndef _NO_NAMESPACE_POLLUTION
#define _MACHINE_PARAM_H_
#endif

#ifndef _MACHINE_PLATFORM
#define _MACHINE_PLATFORM	vkernel
#endif

#ifndef _NO_NAMESPACE_POLLUTION

#ifndef MACHINE_PLATFORM
#define MACHINE_PLATFORM	"vkernel"
#endif

#endif

/*
 * This is kinda silly but why not?  We use a 32 bit bitmask so 31 is
 * the most we can have.  We use the msb bit for other purposes in the
 * spinlock code so we can't have 32.
 */
#ifndef SMP_MAXCPU
#define SMP_MAXCPU	31
#endif

#include <cpu/param.h>

#endif

