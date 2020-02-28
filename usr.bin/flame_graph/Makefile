
.PATH: ${.CURDIR}/../systat

PROG=	flame_graph
CFLAGS+= -I${.CURDIR}/../systat -I${.CURDIR}/../../sys
SRCS=	main.c symbols.c collect.c process.c
NOMAN=TRUE

.include <bsd.prog.mk>
