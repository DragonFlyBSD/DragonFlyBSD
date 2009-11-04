.PATH:	${.CURDIR}/arch/x86_64

SRCS+=	fenv.c
INCS=	fenv.h

.include "${.CURDIR}/arch/i386/Makefile.inc"
