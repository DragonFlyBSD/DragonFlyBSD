# $FreeBSD: src/gnu/usr.bin/cc/cc_drv/Makefile,v 1.11.2.1 2002/07/19 18:46:23 ru Exp $
# $DragonFly: src/gnu/usr.bin/cc/cc_drv/Attic/Makefile,v 1.2 2003/06/17 04:25:45 dillon Exp $

.include "../Makefile.inc"

.PATH: ../cc_tools ${GCCDIR}

LIB=		cc_drv
INTERNALLIB=	YES

SRCS=	multilib.h choose-temp.c obstack.c prefix.c pexecute.c version.c

CFLAGS+=	-DIN_GCC

${OUT_OBJ}.o ${OUT_OBJ}.so:	${OUT_FILE}

.include <bsd.lib.mk>
