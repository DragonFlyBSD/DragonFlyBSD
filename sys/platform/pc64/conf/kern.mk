# $FreeBSD: src/sys/conf/kern.mk,v 1.52 2007/05/24 21:53:42 obrien Exp $
# $DragonFly: src/sys/platform/pc64/conf/kern.mk,v 1.1 2007/09/23 04:29:31 yanyh Exp $

#
# Warning flags for compiling the kernel and components of the kernel.
#
# For AMD64, we explicitly prohibit the use of FPU, SSE and other SIMD
# operations inside the kernel itself.  These operations are exclusively
# reserved for user applications.
#
CFLAGS+=	-mpreferred-stack-boundary=4
CFLAGS+=	-mcmodel=kernel -mno-red-zone \
		-mfpmath=387 -mno-sse -mno-sse2 -mno-mmx -mno-3dnow \
		-msoft-float -fno-asynchronous-unwind-tables
INLINE_LIMIT?=	8000
