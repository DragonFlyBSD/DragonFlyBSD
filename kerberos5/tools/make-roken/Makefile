# $FreeBSD: src/kerberos5/tools/make-roken/Makefile,v 1.2 2004/12/21 08:47:02 ru Exp $
# $DragonFly: src/kerberos5/tools/make-roken/Makefile,v 1.1 2005/01/16 14:25:47 eirikn Exp $

PROG=	make-roken
NOMAN=

CLEANFILES= make-roken.c

.include <bsd.prog.mk>

make-roken.c: ${KRB5DIR}/lib/roken/roken.awk ${KRB5DIR}/lib/roken/roken.h.in
	awk -f ${.ALLSRC} > ${.TARGET}
