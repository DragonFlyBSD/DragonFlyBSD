/*
 * $DragonFly: src/sys/platform/pc32/include/param.h,v 1.3 2007/01/19 07:23:42 dillon Exp $
 */

#ifndef _MACHINE_PARAM_H_

#ifndef _NO_NAMESPACE_POLLUTION
#define _MACHINE_PARAM_H_
#endif

#ifndef _MACHINE_PLATFORM
#define _MACHINE_PLATFORM	pc32
#endif

#ifndef _NO_NAMESPACE_POLLUTION

#ifndef MACHINE_PLATFORM
#define MACHINE_PLATFORM	"pc32"
#endif

#define _GDT_ARRAY_PRESENT	/* used by db_disasm */

#endif

#include <cpu/param.h>

#endif
