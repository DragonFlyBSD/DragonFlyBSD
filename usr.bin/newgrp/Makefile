# $FreeBSD: src/usr.bin/newgrp/Makefile,v 1.2 2004/10/24 16:02:38 ru Exp $
# $DragonFly: src/usr.bin/newgrp/Makefile,v 1.1 2004/11/23 16:26:04 liamfoy Exp $
 
PROG=	newgrp
DPADD=	${LIBCRYPT} ${LIBUTIL}
LDADD=	-lcrypt -lutil

.if defined(ENABLE_SUID_NEWGRP)
BINMODE=	4555
INSTALLFLAGS=	-fschg
.endif

WARNS?=	6
.include <bsd.prog.mk>
