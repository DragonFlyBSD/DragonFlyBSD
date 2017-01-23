#ifndef _MACHINE_PARAM_H_
#define _MACHINE_PARAM_H_

#ifndef _MACHINE_PLATFORM
#define _MACHINE_PLATFORM	vkernel64
#endif

#ifndef MACHINE_PLATFORM
#define MACHINE_PLATFORM	"vkernel64"
#endif

#ifdef _KERNEL
#ifndef HZ_DEFAULT
#define HZ_DEFAULT	20
#endif
#endif

#include <cpu/param.h>

#endif
