# Common flags and rules for compiling the kernel and kernel modules.

# Clear defaults from <sys.mk>.
CFLAGS=
.SUFFIXES:

CSTD?=		gnu11
CFLAGS+=	-std=${CSTD}

COPTFLAGS?=	-O2 -pipe
#COPTFLAGS?=	-O -pipe -flto -fno-fat-lto-objects

# always use external as(1)
.if ${CCVER:Mclang*}
COPTFLAGS+=	-no-integrated-as
.endif

.if !defined(NO_CPU_COPTFLAGS)
COPTFLAGS+=	${_CPUCFLAGS}
.endif

CFLAGS+=	${COPTFLAGS}

.if !defined(NO_WERROR) && (${CCVER} == "gcc80" || ${CCVER} == "gcc120")
WERROR=		-Werror
.endif

CWARNFLAGS?=	-Wall -Wredundant-decls -Wnested-externs -Wstrict-prototypes \
		-Wmissing-prototypes -Wpointer-arith -Wcast-qual \
		-Wold-style-definition -Wmissing-include-dirs -Winit-self \
		-Wundef

_cnowarnflags=	-Wno-pointer-sign
_cnowarnflags+=	-Wno-unused-but-set-variable

_gccver=	0
.if ${CCVER:Mgcc*}
_gccver=	${CCVER:S/gcc//}
.endif

.if ${_gccver} > 0
CWARNFLAGS+=	-Wold-style-declaration
.endif
.if ${_gccver} >= 90
_cnowarnflags+=	-Wno-address-of-packed-member
.endif
.if ${_gccver} >= 100
_cnowarnflags+=	-Wno-zero-length-bounds
.endif

# Add -Wno-foo flags last
.if !defined(WARNS_AUDIT)
CWARNFLAGS+=	${_cnowarnflags}
.endif

CFLAGS+=	${CWARNFLAGS} ${WERROR}

CFLAGS+=	-D_KERNEL

# Require the proper use of 'extern' for variables.  -fno-common will
# cause duplicate declarations to generate a link error.
# This is the default since GCC 10.
.if ${_gccver} < 100
CFLAGS+=	-fno-common
.endif

# Prevent GCC 3.x from making certain libc based inline optimizations
CFLAGS+=	-ffreestanding

# Don't use any standard include directories.
CFLAGS+=	-nostdinc

# Add per-platform compiler flags.
.include "../platform/${MACHINE_PLATFORM}/conf/kern.mk"

# Additional compiler flags for kernel and modules.
CFLAGS+=	${KCFLAGS}

# LOCORE means "don't declare C stuff", not "for locore.s".
ASM_CFLAGS=	-x assembler-with-cpp -DLOCORE ${CFLAGS:N-flto}

NORMAL_C=	${CC} ${CFLAGS} -c ${.IMPSRC:U${.ALLSRC:[1]}}
NORMAL_S=	${CC} ${ASM_CFLAGS} -c ${.IMPSRC:U${.ALLSRC:[1]}}
NORMAL_M=	awk -f $S/tools/makeobjops.awk -- -c \
		    ${.IMPSRC:U${.ALLSRC:[1]}}; \
		    ${CC} ${CFLAGS} -c ${.PREFIX}.c
NORMAL_FW=	uudecode -o ${.TARGET} ${.ALLSRC}
# Link a firmware blob: prefer a .fw source, otherwise fall back to the
# first prerequisite (module firmware is not always named *.fw).
NORMAL_FWO=	${LD} -b binary -d -warn-common -r -o ${.TARGET} \
		    ${.ALLSRC:M*.fw:S,^$,${.ALLSRC:[1]},}

# Suffix rules for compiling modules.
.SUFFIXES: .o .s .S .c .m .fw .uu
.c.o:
	${NORMAL_C}
.s.o .S.o:
	${NORMAL_S}
.uu.fw:
	${NORMAL_FW}
