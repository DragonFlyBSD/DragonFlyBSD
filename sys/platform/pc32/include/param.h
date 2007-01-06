/*
 * $DragonFly: src/sys/platform/pc32/include/param.h,v 1.2 2007/01/06 08:25:01 dillon Exp $
 */

#ifndef _MACHINE_PARAM_H_

#ifndef _NO_NAMESPACE_POLLUTION
#define _MACHINE_PARAM_H_
#endif

#ifndef _MACHINE
#define _MACHINE        pc32
#endif

#ifndef _NO_NAMESPACE_POLLUTION

#ifndef MACHINE
#define MACHINE         "pc32"
#endif

#define _GDT_ARRAY_PRESENT	/* used by db_disasm */

#endif

#include <cpu/param.h>

#endif

