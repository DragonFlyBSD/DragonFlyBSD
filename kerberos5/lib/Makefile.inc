# $FreeBSD: src/kerberos5/lib/Makefile.inc,v 1.6 2003/10/09 19:48:45 nectar Exp $
# $DragonFly: src/kerberos5/lib/Makefile.inc,v 1.4 2005/04/21 13:42:33 joerg Exp $

SHLIB_MAJOR?=	8

CFLAGS+=-I${.OBJDIR}/../../lib/libroken -I${.OBJDIR}/../../lib/libasn1 \
	-I${.OBJDIR}/../../lib/libkrb5 -I${KRB5DIR}/lib/roken \
	-I${KRB5DIR}/lib/krb5

.include "../Makefile.inc"
