# $FreeBSD: src/gnu/usr.bin/cc/cc_drv/Makefile,v 1.12 2002/05/10 08:54:46 obrien Exp $
# $DragonFly: src/gnu/usr.bin/cc3/cc_drv/Attic/Makefile,v 1.2 2004/02/03 03:47:11 dillon Exp $

.include "../Makefile.inc"

.PATH: ../cc_tools ${GCCDIR}

LIB=		cc_drv
NOPROFILE=	YES
NOPIC=		YES

SRCS=	multilib.h choose-temp.c obstack.c prefix.c pexecute.c version_local.c

CFLAGS+=	-DIN_GCC

install:
	@true

${OUT_OBJ}.o ${OUT_OBJ}.so:	${OUT_FILE}

.include <bsd.lib.mk>
