# $DragonFly: src/test/stress/fsx/Makefile,v 1.2 2004/05/07 18:04:46 dillon Exp $
#

PROG=	/tmp/fsx

/tmp/fsx: fsx.c
	$(CC) $(CFLAGS) ${.ALLSRC} -o ${.TARGET}

NOMAN=	yes

.include <bsd.prog.mk>
