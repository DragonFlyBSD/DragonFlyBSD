# $FreeBSD: src/share/mk/bsd.kern.mk,v 1.17.2.1 2001/08/01 16:56:56 obrien Exp $
# $DragonFly: src/sys/conf/bsd.kern.mk,v 1.9 2006/07/02 00:57:14 corecode Exp $

#
# Warning flags for compiling the kernel and components of the kernel.
#
# Note that the newly added -Wcast-qual is responsible for generating 
# most of the remaining warnings.  Warnings introduced with -Wall will
# also pop up, but are easier to fix.
#
CWARNFLAGS?=	-Wall -Wredundant-decls -Wnested-externs -Wstrict-prototypes \
		-Wmissing-prototypes -Wpointer-arith -Winline -Wcast-qual \
		-ansi
#
# The following flags are next up for working on:
#	-W
#
# When working on removing warnings from code, the `-Werror' flag should be
# of material assistance.
#

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

.include "kern-${MACHINE_ARCH}.mk"
