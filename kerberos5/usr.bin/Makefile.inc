# $FreeBSD: src/kerberos5/usr.bin/Makefile.inc,v 1.1.1.1 2000/01/15 21:38:04 markm Exp $
# $DragonFly: src/kerberos5/usr.bin/Makefile.inc,v 1.3 2005/01/16 14:25:47 eirikn Exp $

BINDIR=	/usr/bin

CFLAGS+=-I${.OBJDIR}/../../lib/libroken/

.include "../Makefile.inc"
