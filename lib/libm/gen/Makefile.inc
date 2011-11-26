# $DragonFly: src/lib/libm/gen/Makefile.inc,v 1.1 2007/06/17 17:46:01 pavalos Exp $

.PATH:	${.CURDIR}/gen

SRCS+=	nan.c nanf.c nanl.c

MAN+=	nan.3

MLINKS+=nan.3 nanf.3 nan.3 nanl.3
