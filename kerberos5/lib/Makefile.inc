# $FreeBSD: src/kerberos5/lib/Makefile.inc,v 1.6 2003/10/09 19:48:45 nectar Exp $
# $DragonFly: src/kerberos5/lib/Makefile.inc,v 1.3 2005/01/16 14:25:46 eirikn Exp $

SHLIB_MAJOR?=	7

CFLAGS+=-I${.OBJDIR}/../../lib/libroken -I${.OBJDIR}/../../lib/libasn1 \
	-I${.OBJDIR}/../../lib/libkrb5 -I${KRB5DIR}/lib/roken \
	-I${KRB5DIR}/lib/krb5

.include "../Makefile.inc"
