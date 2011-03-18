#
# This Makefile covers the top part of the MI kernel build instructions
#

# Can be overridden by makeoptions or /etc/make.conf
KERNEL?=	kernel

# build this target if none is specified on the command line
.MAIN:	all

# Set the platform and machine architectures
#
P=	${MACHINE_PLATFORM}
M=	${MACHINE_ARCH}

SIZE?=		size
OBJCOPY?=	objcopy

COPTFLAGS?=-O -pipe
#COPTFLAGS?=-O -fthread-jumps -fcse-follow-jumps -fcrossjumping -frerun-cse-after-loop -fno-guess-branch-probability --param min-crossjump-insns=1 -pipe
#COPTFLAGS?=-O -fcrossjumping -pipe
#COPTFLAGS?=-Os -fno-strict-aliasing -pipe
#COPTFLAGS?=-O2 -fno-strict-aliasing -pipe
.if !defined(NO_CPU_COPTFLAGS)
COPTFLAGS+= ${_CPUCFLAGS}
.endif
# don't use -I- so we can use proper source-relative locality for local 
# includes.
#
# -I.  - this is to access the opt_*.h and use_*.h header files generated
#	 in the kernel build directory.
#
# -Iinclude
#	- this is used to access forwarding header files for
#	  <machine/*.h> that exist in the cpu architecture but do not
#	  exist in the platform (machine/) architecture.  This allows
#	  the platform to trivially override the cpu header files.
#
INCLUDES= -nostdinc -I. -Iinclude -I$S
# This hack is to allow kernel compiles to succeed on machines w/out srcdist
.if exists($S/../include)
INCLUDES+= -I$S/../include
.else
INCLUDES+= -I/usr/include
.endif

# This hack lets us use the Intel ACPICA code without spamming a new
# include path into 100+ source files.
.include "$S/conf/acpi.mk"
INCLUDES+= -I${.OBJDIR} -I"$S/${OSACPI_MI_DIR}" -I"$S/${ACPICA_DIR}/include"

# This hack lets us use the ipfilter code without spamming a new
# include path into 100+ source files.
INCLUDES+= -I$S/contrib/ipfilter

# ... and the same for Atheros HAL
INCLUDES+= -I$S/dev/netif/ath/hal -I$S/dev/netif/ath/hal/ath_hal

COPTS=	${INCLUDES} ${IDENT} -D_KERNEL -include opt_global.h
CFLAGS=	${COPTFLAGS} ${CWARNFLAGS} ${DEBUG} ${COPTS}

# XXX LOCORE means "don't declare C stuff" not "for locore.s".
ASM_CFLAGS= -x assembler-with-cpp -DLOCORE ${CFLAGS}

DEFINED_PROF=	${PROF}
.if defined(PROF)
CFLAGS+=	-falign-functions=16
.if ${PROFLEVEL} >= 2
IDENT+=	-DGPROF4 -DGUPROF
PROF+=	-mprofiler-epilogue
.endif
.endif

# Put configuration-specific C flags last (except for ${PROF}) so that they
# can override the others.
CFLAGS+=	${CONF_CFLAGS}

NORMAL_C= ${CC} -c ${CFLAGS} ${PROF} ${.IMPSRC}
NORMAL_C_C= ${CC} -c ${CFLAGS} ${PROF} ${.IMPSRC}
NORMAL_S= ${CC} -c ${ASM_CFLAGS} ${.IMPSRC}
PROFILE_C= ${CC} -c ${CFLAGS} ${.IMPSRC}

NORMAL_M= awk -f $S/tools/makeobjops.awk -- -c $<; \
	${CC} -c ${CFLAGS} ${PROF} ${.PREFIX}.c

.if !defined(NO_WERROR) && (${CCVER} == "gcc41" || ${CCVER} == "gcc44")
WERROR=-Werror
.endif

GEN_CFILES= $S/platform/$P/$M/genassym.c
SYSTEM_CFILES= ioconf.c config.c
SYSTEM_SFILES= $S/platform/$P/$M/locore.s
SYSTEM_DEP= Makefile ${SYSTEM_OBJS}
SYSTEM_OBJS= locore.o ${OBJS} ioconf.o config.o hack.So
SYSTEM_LD= @${LD} -Bdynamic -T $S/platform/$P/conf/ldscript.$M \
	-export-dynamic -dynamic-linker /red/herring \
	-o ${.TARGET} -X ${SYSTEM_OBJS} vers.o
.if ${P} == "pc64"
# XXX swildner: Workaround to make kernels linked with binutils220 boot.
#
SYSTEM_LD+= -z max-page-size=1048576
.endif
SYSTEM_LD_TAIL= @${OBJCOPY} --strip-symbol gcc2_compiled. ${.TARGET} ; \
	${SIZE} ${.TARGET} ; chmod 755 ${.TARGET}
SYSTEM_DEP+= $S/platform/$P/conf/ldscript.$M

# Normalize output files to make it absolutely crystal clear to
# anyone examining the build directory.
#
.if defined(DEBUG)
FULLKERNEL=	${KERNEL}.debug
.if defined(INSTALLSTRIPPED)
SELECTEDKERNEL= ${KERNEL}.stripped
.else
SELECTEDKERNEL= ${KERNEL}.debug
.endif
.else
FULLKERNEL=	${KERNEL}.nodebug
SELECTEDKERNEL= ${KERNEL}.stripped
.endif


MKMODULESENV=	MAKEOBJDIRPREFIX=${.OBJDIR} BUILDING_WITH_KERNEL=${.OBJDIR}
.if defined(MODULES_OVERRIDE)
MKMODULESENV+=	MODULES_OVERRIDE="${MODULES_OVERRIDE}"
.endif
.if defined(DEBUG)
MKMODULESENV+=	DEBUG="${DEBUG}" DEBUG_FLAGS="${DEBUG}"
.endif
.if defined(INSTALLSTRIPPED) || defined(INSTALLSTRIPPEDMODULES)
MKMODULESENV+=	INSTALLSTRIPPEDMODULES=1
.endif
MKMODULESENV+=  MACHINE_ARCH=${MACHINE_ARCH} MACHINE=${MACHINE} MACHINE_PLATFORM=${MACHINE_PLATFORM}

