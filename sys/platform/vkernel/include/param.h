/*
 * $DragonFly: src/sys/platform/vkernel/include/param.h,v 1.3 2007/09/04 22:07:54 dillon Exp $
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

/*
 * Set the default HZ to the likely resolution of the kqueue timer
 * the vkernel uses, otherwise our ticks will be seriously off and
 * while date/time will be correct, sleep intervals will not.
 */
#ifdef _KERNEL
#ifndef HZ
#define HZ	20
#endif
#endif

#include <cpu/param.h>

#endif

