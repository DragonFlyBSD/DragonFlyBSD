/*
 * ASMACROS.H
 *
 * $DragonFly: src/lib/libcaps/i386/asmacros.h,v 1.1 2003/12/04 22:06:22 dillon Exp $
 */
#include <machine/asmacros.h>

#define PCPU(name)	%gs:gd_ ## name

