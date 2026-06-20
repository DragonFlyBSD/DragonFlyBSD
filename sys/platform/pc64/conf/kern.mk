# $FreeBSD: src/sys/conf/kern.mk,v 1.52 2007/05/24 21:53:42 obrien Exp $

# Flags for compiling the kernel and kernel modules.
#
# -fno-strict-aliasing required for -O2 compilation.
#
CFLAGS+=	-fno-stack-protector -fno-strict-aliasing
CFLAGS+=	-fno-strict-overflow
CFLAGS+=	-fno-asynchronous-unwind-tables -fno-omit-frame-pointer

CFLAGS+=	-mcmodel=kernel -mno-red-zone

# For x86_64, we explicitly prohibit the use of FPU, SSE and other SIMD
# operations inside the kernel itself.  These operations are exclusively
# reserved for user applications.
#
# GCC:
# * setting -mno-mmx implies -mno-3dnow -mno-3dnowa
# * setting -mno-sse implies -mno-sse* -mno-avx* -mno-fma* -mfpmath=387 ...
#
CFLAGS+=	-mno-mmx -mno-sse
CFLAGS+=	-msoft-float
CFLAGS+=	-mno-fp-ret-in-387

# Retpoline spectre protection
.if ${CCVER:Mgcc*} && ${CCVER:S/gcc//} >= 80
CFLAGS+=	-mindirect-branch=thunk-inline
.endif
