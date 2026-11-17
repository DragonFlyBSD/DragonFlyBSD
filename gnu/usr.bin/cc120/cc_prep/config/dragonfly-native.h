/*
 * DragonFly-specific GCC native configuration.
 *
 * This file is NOT part of upstream GCC.  It supplements the
 * upstream DragonFly target configuration with DragonFly's
 * base-system compiler installation layout.
 *
 * It configures:
 *   - executable and data installation prefixes
 *   - linker specifications and runtime library paths
 *
 * PREFIX1 and PREFIX2 are supplied by the DragonFly makefiles.
 * CTOOLS is defined when building bootstrap tools.
 *
 * This file is included through TARGET_INC when generating tm.h.
 */

#undef	PREFIX
#define PREFIX	PREFIX1
#ifndef PREFIX2
#define PREFIX2	PREFIX1
#endif

#define LIBGCC_SPEC \
"%{static|static-libgcc:-lgcc -lgcc_eh} \
 %{!static: \
  %{!static-libgcc: \
   %{!shared-libgcc:-lgcc --as-needed -lgcc_pic --no-as-needed} \
   %{shared-libgcc:-lgcc_pic \
    %{!shared: -lgcc} \
   } \
  } \
 }"

#ifdef CTOOLS
#define RUNPATH "-rpath"
#else
#define RUNPATH "--enable-new-dtags -rpath"
#endif

#undef	LINK_SPEC
#define LINK_SPEC DFBSD_LINK_SPEC \
"%{pg: -L" PREFIX2 "/lib/gcc" GCCSHORTVER "/profile \
  %{!static: " RUNPATH " /usr/lib/gcc" GCCSHORTVER "/profile} \
 } \
 -L" PREFIX2 "/lib/gcc" GCCSHORTVER " \
 %{!static: " RUNPATH " /usr/lib/gcc" GCCSHORTVER "} \
 %{pg: \
  %{!nostdlib: \
   %{!nodefaultlibs: -L" PREFIX2 "/lib/profile \
    %{!static: " RUNPATH " /usr/lib/profile} \
   } \
  } \
 }"

/*
 * Link GCC's libssp_nonshared.a to provide __stack_chk_fail_local() for stack
 * protection.  This hidden symbol avoids PLT overhead on 32-bit x86 by
 * deferring PIC register setup to the cold failure path.
 *
 * On x86-64, GCC calls __stack_chk_fail() directly because RIP-relative
 * addressing makes PLT efficient, but we link libssp_nonshared.a for
 * consistency with other systems.
 *
 * Adapted from gcc/config/freebsd.h
 */
#undef	LINK_SSP_SPEC
#define LINK_SSP_SPEC "%{fstack-protector|fstack-protector-all" \
		       "|fstack-protector-strong|fstack-protector-explicit" \
		       ":-lssp_nonshared}"

/*
 * List of locations to find the start files (e.g., crt*.o)
 *
 * This supercedes STANDARD_STARTFILE_PREFIX{,_1,_2}.
 */
#define STARTFILE_PREFIX_SPEC \
	PREFIX2 "/lib/gcc" GCCSHORTVER "/ " \
	PREFIX2 "/lib/ "
