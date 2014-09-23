/* 
 *
 *
 * $DragonFly: src/sys/platform/pc64/include/cpu.h,v 1.1 2007/09/23 04:42:07 yanyh Exp $
 */

#ifndef _MACHINE_CPU_H_
#define _MACHINE_CPU_H_

#include <cpu/cpu.h>

#define CLKF_USERMODE(framep) \
	(ISPL((framep)->if_cs) == SEL_UPL)

#endif
