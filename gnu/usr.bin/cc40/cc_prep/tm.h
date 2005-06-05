/* $DragonFly: src/gnu/usr.bin/cc40/cc_prep/Attic/tm.h,v 1.1 2005/06/05 22:43:20 corecode Exp $ */

#ifndef GCC_TM_H
#define GCC_TM_H
#ifndef FBSD_MAJOR
# define FBSD_MAJOR 4
#endif
#ifdef IN_GCC
# include "config/i386/i386.h"
# include "config/i386/unix.h"
# include "config/i386/att.h"
# include "config/dbxelf.h"
# include "config/elfos.h"
# include "config/dragonfly-spec.h"
# include "config/dragonfly.h"
# include "config/i386/dragonfly.h"
# include "defaults.h"
#endif
#if defined IN_GCC && !defined GENERATOR_FILE && !defined USED_FOR_TARGET
# include "insn-constants.h"
# include "insn-flags.h"
#endif
#endif /* GCC_TM_H */
