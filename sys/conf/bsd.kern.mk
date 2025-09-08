# $FreeBSD: src/share/mk/bsd.kern.mk,v 1.17.2.1 2001/08/01 16:56:56 obrien Exp $

# Warning flags for compiling the kernel and components of the kernel.
#
# Note that -Werror is on by default. To turn it off, e.g. when working
# on adding new warning options, NO_WERROR in make.conf (or on make(1)'s
# command line) should be of material assistance.
#

CSTD?=		gnu11

CWARNFLAGS?=	-Wall -Wredundant-decls -Wnested-externs -Wstrict-prototypes \
		-Wmissing-prototypes -Wpointer-arith -Wcast-qual \
		-Wold-style-definition -Wmissing-include-dirs \
		-Wno-pointer-sign -Winit-self -Wundef

.if ${CCVER:Mgcc*}
# All flags inside this block are gcc-specific except for --param
# Since inline-limit wasn't recognized, and since --param squawks on clang
# when it isn't used, it was shift to GCC compilers only.
CFLAGS+=	-Wold-style-declaration \
		-finline-limit=${INLINE_LIMIT} \
		--param inline-unit-growth=100 \
		--param large-function-growth=1000
.if ${CCVER:Mgcc1[0-9]0}
CWARNFLAGS+=   -Wno-address-of-packed-member
CWARNFLAGS+=   -Wno-array-bounds
CWARNFLAGS+=   -Wno-maybe-uninitialized
CWARNFLAGS+=   -Wno-format-overflow
CWARNFLAGS+=   -Wno-nonnull
CWARNFLAGS+=   -Wno-stringop-overflow
CWARNFLAGS+=   -Wno-uninitialized
.endif
.if ${CCVER:Mgcc1[1-9]0}
CWARNFLAGS+=   -Wno-stringop-overread
CWARNFLAGS+=   -Wno-array-parameter
.endif
.if ${CCVER:Mgcc1[2-9]0}
CWARNFLAGS+=	-Wno-infinite-recursion
CWARNFLAGS+=	-Wno-address
CWARNFLAGS+=	-Wno-dangling-pointer
.endif
CWARNFLAGS+=	-Wno-unused-but-set-variable
.endif


# Require the proper use of 'extern' for variables.  -fno-common will
# cause duplicate declarations to generate a link error.
#
CFLAGS+=	-fno-common

# Prevent GCC 3.x from making certain libc based inline optimizations
#
CFLAGS+=	-ffreestanding

.include "../platform/${MACHINE_PLATFORM}/conf/kern.mk"
