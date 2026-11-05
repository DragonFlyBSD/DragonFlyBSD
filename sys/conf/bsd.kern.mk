# Common flags for compiling the kernel and kernel modules.

CSTD?=		gnu11

COPTFLAGS?=	-O2 -pipe
#COPTFLAGS?=	-O -pipe -flto -fno-fat-lto-objects

# always use external as(1)
.if ${CCVER:Mclang*}
COPTFLAGS+=	-no-integrated-as
.endif

.if !defined(NO_CPU_COPTFLAGS)
COPTFLAGS+=	${_CPUCFLAGS}
.endif

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

# Require the proper use of 'extern' for variables.  -fno-common will
# cause duplicate declarations to generate a link error.
# This is the default since GCC 10.
.if ${_gccver} < 100
CFLAGS+=	-fno-common
.endif

# Prevent GCC 3.x from making certain libc based inline optimizations
CFLAGS+=	-ffreestanding

.include "../platform/${MACHINE_PLATFORM}/conf/kern.mk"
