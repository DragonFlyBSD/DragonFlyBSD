# $FreeBSD: src/share/mk/bsd.kern.mk,v 1.17.2.1 2001/08/01 16:56:56 obrien Exp $

# Warning flags for compiling the kernel and components of the kernel.
#
# Note that -Werror is on by default. To turn it off, e.g. when working
# on adding new warning options, NO_WERROR in make.conf (or on make(1)'s
# command line) should be of material assistance.
#

CWARNFLAGS?=	-Wall -Wredundant-decls -Wnested-externs -Wstrict-prototypes \
		-Wmissing-prototypes -Wpointer-arith -Winline -Wcast-qual \
		-Wold-style-definition -Wold-style-declaration -Wno-inline \
		-Wno-pointer-sign -Winit-self -std=c99

.if ${CCVER} == "gcc47"
CWARNFLAGS+=	-Wno-unused-but-set-variable
.endif

CFLAGS+= -finline-limit=${INLINE_LIMIT}
CFLAGS+= --param inline-unit-growth=100
CFLAGS+= --param large-function-growth=1000

# Require the proper use of 'extern' for variables.  -fno-common will
# cause duplicate declarations to generate a link error.
#
CFLAGS+=	-fno-common

# Prevent GCC 3.x from making certain libc based inline optimizations
#
CFLAGS+=	-ffreestanding

.include "../platform/${MACHINE_PLATFORM}/conf/kern.mk"
