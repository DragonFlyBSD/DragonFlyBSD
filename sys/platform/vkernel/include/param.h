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

