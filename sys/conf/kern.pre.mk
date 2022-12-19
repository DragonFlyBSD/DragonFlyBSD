#
# This Makefile covers the top part of the MI kernel build instructions
#

# Can be overridden by makeoptions or /etc/make.conf
KERNEL?=	kernel

# If a different binutils is installed in /usr/local it probably
# won't recognize LDVER or use the default, creating an incompatibility
# between buildkernel and nativekernel as well as between buildkernel and
# manual make's inside /usr/obj/usr/src/sys/KERNEL_CONFIG.
#
# /usr/local/bin must be removed from the path.
#
RMLDPATH=/usr/local/bin
PATH:=${PATH:C/${RMLDPATH}//g}
.export PATH

fubar:
	echo ${PATH}

# build this target if none is specified on the command line
.MAIN:	all

# Set the platform and machine architectures
#
P=	${MACHINE_PLATFORM}
M=	${MACHINE_ARCH}

SIZE?=		size
OBJCOPY?=	objcopy

COPTFLAGS?=-O2 -pipe
#COPTFLAGS?=-O -pipe -flto -fno-fat-lto-objects
#COPTFLAGS?=-O -fthread-jumps -fcse-follow-jumps -fcrossjumping -frerun-cse-after-loop -fno-guess-branch-probability --param min-crossjump-insns=1 -pipe
#COPTFLAGS?=-O -fcrossjumping -pipe
#COPTFLAGS?=-Os -fno-strict-aliasing -pipe
#COPTFLAGS?=-O2 -fno-strict-aliasing -pipe

# always use external as(1)
.if ${CCVER:Mclang*}
COPTFLAGS+=	-no-integrated-as
.endif

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
# Real kernel builds do not need /usr/include, but vkernel builds do.
#
.if ${MACHINE_PLATFORM} == "vkernel64"
.if exists($S/../include)
INCLUDES+= -I$S/../include
.else
INCLUDES+= -I/usr/include
.endif
.endif

# This hack lets us use the Intel ACPICA code without spamming a new
# include path into 100+ source files.
.include "$S/conf/acpi.mk"
INCLUDES+= -I${.OBJDIR} -I$S/${OSACPI_MI_DIR} -I$S/${ACPICA_DIR}/include

# ... and the same for Atheros HAL
INCLUDES+= -I$S/dev/netif/ath/ath_hal -I$S/contrib/dev/ath/ath_hal

# Same thing for drm includes
INCLUDES+= -I$S/dev/drm/include
INCLUDES+= -I$S/dev/drm/include/drm
INCLUDES+= -I$S/dev/drm/include/uapi
INCLUDES+= -I$S/dev/drm/amd/include

COPTS=	${INCLUDES} ${IDENT} -D_KERNEL -DHAVE_KERNEL_OPTION_HEADERS -include opt_global.h
CFLAGS=	${COPTFLAGS} ${KCFLAGS} ${CWARNFLAGS} -std=${CSTD} ${DEBUG} ${COPTS}

# XXX LOCORE means "don't declare C stuff" not "for locore.s".
ASM_CFLAGS= -x assembler-with-cpp -DLOCORE ${CFLAGS:N-flto}

# Put configuration-specific C flags last so that they
# can override the others.
CFLAGS+=	${CONF_CFLAGS}

# XXX handle this explicitly, fw wrappers use implicit .c.o: rule (LINT64)
.if defined(FASTER_DEPEND)
CFLAGS+= -MD
.endif

NORMAL_C= ${CC} -c ${CFLAGS} ${.IMPSRC}
NORMAL_C_C= ${CC} -c ${CFLAGS} ${.IMPSRC}
NORMAL_S= ${CC} -c ${ASM_CFLAGS} ${.IMPSRC}

NORMAL_M= awk -f $S/tools/makeobjops.awk -- -c $<; \
	${CC} -c ${CFLAGS} ${.PREFIX}.c

NORMAL_FW= uudecode -o ${.TARGET} ${.ALLSRC}
NORMAL_FWO= ${LD} -b binary -d -warn-common -r -o ${.TARGET} ${.ALLSRC:M*.fw}

.if !defined(NO_WERROR) && (${CCVER} == "gcc47" || ${CCVER} == "gcc80")
WERROR=-Werror
.endif

GEN_CFILES= $S/platform/$P/$M/genassym.c
SYSTEM_CFILES= ioconf.c config.c
SYSTEM_SFILES= $S/platform/$P/$M/locore.s
SYSTEM_DEP= Makefile ${SYSTEM_OBJS}
SYSTEM_OBJS= locore.o ${OBJS} ioconf.o config.o hack.So
SYSTEM_LD= @${CC} -nostdlib -ffreestanding -Wl,--hash-style=sysv \
	-Wl,-Bdynamic -Wl,-T,$S/platform/$P/conf/ldscript.$M \
	-Wl,--export-dynamic -Wl,--dynamic-linker,/red/herring \
	-o ${.TARGET} -Wl,-X ${SYSTEM_OBJS} vers.o

# In case of LTO provide all standard CFLAGS!
.if ${CFLAGS:M-flto}
SYSTEM_LD+= ${CFLAGS}
## This one eats a lot of ram, may be needed to correctly link the kernel.
## Default "balanced" might create kernel that "Fatal trap 12" on boot!!!
#. if !${CFLAGS:M-flto-partition=*}
#SYSTEM_LD+= -flto-partition=one -flto-report-wpa
#. endif
.endif

# The max-page-size for gnu ld is 0x200000 on x86_64
# For the gold linker, it is only 0x1000 on both x86_64
# The penalty for changing the gold default for x86_64 is larger binaries
# and shared libraries, and forcing them to use more address space than
# required.  The only application that needs such a large page size is the
# kernel itself, so leave the gold default alone and treat the kernel
# page size as an exception.
#
.if ${P} == "pc64"
SYSTEM_LD+= -Wl,-z,max-page-size=0x200000
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


MKMODULESENV=	MAKEOBJDIRPREFIX=${.OBJDIR} KERNBUILDDIR=${.OBJDIR}
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

