# $FreeBSD: src/sys/conf/kern.mk,v 1.52 2007/05/24 21:53:42 obrien Exp $
# $DragonFly: src/sys/platform/pc64/conf/kern.mk,v 1.2 2008/08/29 17:07:15 dillon Exp $

#
# Warning flags for compiling the kernel and components of the kernel.
#
# For x86_64, we explicitly prohibit the use of FPU, SSE and other SIMD
# operations inside the kernel itself.  These operations are exclusively
# reserved for user applications.
#
CFLAGS+=	-mpreferred-stack-boundary=4
CFLAGS+=	-mcmodel=kernel -mno-red-zone -mfpmath=387

CFLAGS+=	-mno-mmx -mno-3dnow -mno-sse -mno-sse2 -mno-sse3

.if ${CCVER} == "gcc44"
CFLAGS+=	-mno-ssse3 -mno-sse4.1 -mno-sse4.2 -mno-sse4 -mno-sse4a \
		-mno-sse5 
CFLAGS+=	-mno-abm -mno-aes -mno-avx -mno-pclmul -mno-popcnt
.endif

CFLAGS+=	-msoft-float
CFLAGS+=	-fno-asynchronous-unwind-tables -fno-omit-frame-pointer

INLINE_LIMIT?=	8000
