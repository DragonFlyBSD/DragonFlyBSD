# $FreeBSD: src/kerberos5/usr.sbin/Makefile.inc,v 1.1.1.1 2000/01/24 19:56:25 markm Exp $
# $DragonFly: src/kerberos5/usr.sbin/Makefile.inc,v 1.3 2005/01/16 14:25:47 eirikn Exp $

BINDIR=	/usr/sbin

CFLAGS+=-I${.OBJDIR}/../../lib/libroken/

.include "../Makefile.inc"
