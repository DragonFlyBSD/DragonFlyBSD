# $FreeBSD: src/kerberos5/libexec/Makefile.inc,v 1.2 2004/02/05 18:51:50 ru Exp $
# $DragonFly: src/kerberos5/libexec/Makefile.inc,v 1.2 2005/01/16 14:25:46 eirikn Exp $

BINDIR=	/usr/libexec

CFLAGS+=-I${.OBJDIR}/../../lib/libroken/

.include "../Makefile.inc"
