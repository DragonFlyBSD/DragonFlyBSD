/*
 * $FreeBSD: src/gnu/usr.bin/cc/cc_tools/freebsd-native.h,v 1.23 2003/07/11 05:33:24 kan Exp $
 * $DragonFly: src/gnu/usr.bin/cc3/cc_tools/Attic/dragonfly-native.h,v 1.5 2004/03/22 20:57:54 dillon Exp $
 */

/* FREEBSD_NATIVE is defined when gcc is integrated into the FreeBSD
   source tree so it can be configured appropriately without using
   the GNU configure/build mechanism. */

#define FREEBSD_NATIVE 1

/* Fake out gcc/config/freebsd<version>.h.  */
#define	FBSD_MAJOR 4

#undef SYSTEM_INCLUDE_DIR		/* We don't need one for now. */
#undef TOOL_INCLUDE_DIR			/* We don't need one for now. */
#undef LOCAL_INCLUDE_DIR		/* We don't wish to support one. */

/* Look for the include files in the system-defined places.  */
#define GPLUSPLUS_INCLUDE_DIR		PREFIX2"/include/c++/3.3"
#define	GPLUSPLUS_BACKWARD_INCLUDE_DIR	PREFIX2"/include/c++/3.3/backward"
#define GCC_INCLUDE_DIR			PREFIX2"/include"
#ifdef CROSS_COMPILE
#define CROSS_INCLUDE_DIR		PREFIX2"/include"
#else
#define STANDARD_INCLUDE_DIR		PREFIX2"/include"
#endif

/* Under FreeBSD, the normal location of the compiler back ends is the
   /usr/libexec directory.

   ``cc --print-search-dirs'' gives:
   install: STANDARD_EXEC_PREFIX/(null)
   programs: /usr/libexec/<OBJFORMAT>/:STANDARD_EXEC_PREFIX:MD_EXEC_PREFIX
   libraries: MD_EXEC_PREFIX:MD_STARTFILE_PREFIX:STANDARD_STARTFILE_PREFIX
*/
#undef	TOOLDIR_BASE_PREFIX		/* Old??  This is not documented. */
#undef	STANDARD_BINDIR_PREFIX		/* We don't need one for now. */
#define	STANDARD_EXEC_PREFIX		PREFIX1"/libexec/gcc3/"
#define	MD_EXEC_PREFIX			PREFIX1"/libexec/gcc3/"
#define	FBSD_DATA_PREFIX		PREFIX1"/libdata/gcc/"

/*
 * XXX at the moment crt1.o, crti.o, and crtn.o are generated from
 * /usr/lib/libcsu and placed in /usr/lib.  crtbegin* and crtend* are
 * generated from the gcc dist and placed in /usr/lib/cc3.
 *
 */
/* Under FreeBSD, the normal location of the various *crt*.o files is the
   /usr/lib directory.  */

#define STANDARD_STARTFILE_PREFIX	PREFIX2"/lib/"
#define STANDARD_STARTFILE_PREFIX_1	PREFIX2"/lib/gcc3/"
#undef MD_STARTFILE_PREFIX

/* For the native system compiler, we actually build libgcc in a profiled
   version.  So we should use it with -pg.  */
#define LIBGCC_SPEC		"%{shared: -lgcc_pic} \
    %{!shared: %{!pg: -lgcc} %{pg: -lgcc_p}}"
#define LIBSTDCXX_PROFILE	"-lstdc++_p"
#define MATH_LIBRARY_PROFILE	"-lm_p"
#define FORTRAN_LIBRARY_PROFILE	"-lg2c_p"

/* FreeBSD is 4.4BSD derived */
#define bsd4_4

/*
 * Dike out [stupid, IMHO] libiberty functions.  This is a real mess
 * because gcc's system.h define's malloc->xmalloc etc if certain defines
 * exist, and some of the lexer files also redefine malloc and realloc,
 * so don't even try to fix it if we're coming from a parser.
 */
#if !defined(FLEX_SCANNER) && !defined(YYBISON) && !defined(YYBYACC)
#if 0
#define	xmalloc_set_program_name(dummy)
#define	xmalloc		malloc
#define	xcalloc		calloc
#define	xrealloc	realloc
#endif
#endif
#define	xstrdup		strdup
#define	xstrerror	strerror

/* And now they want to replace ctype.h.... grr... [stupid, IMHO] */
#define xxxISDIGIT	isdigit
#define xxxISGRAPH	isgraph
#define xxxISLOWER	islower
#define xxxISSPACE	isspace
#define xxxTOUPPER	toupper
