# $FreeBSD: src/kerberos5/libexec/hpropd/Makefile,v 1.3.2.3 2002/07/25 09:33:15 ru Exp $
# $DragonFly: src/kerberos5/libexec/hpropd/Makefile,v 1.3 2003/08/05 07:45:40 asmodai Exp $

PROG=		hpropd
SRCS=		hpropd.c hprop.h hdb_err.h
CFLAGS+=	-I${KRB5DIR}/include \
		-I${KRB5DIR}/kdc \
		-I${KRB5DIR}/lib/krb5 \
		-I${KRB5DIR}/lib/asn1 \
		-I${KRB5DIR}/lib/hdb \
		-I${KRB5DIR}/lib/roken \
		-I${KRB5DIR}/kuser \
		-I${ASN1OBJDIR} \
		-I${HDBOBJDIR} \
		-I${.OBJDIR}
LDADD=		-L${KRB5OBJDIR} -lkrb5 \
		-L${HDBOBJDIR} -lhdb -L${ROKENOBJDIR} -lroken \
		-L${VERSOBJDIR} -lvers \
		${_krb4libs} \
		-L${ASN1OBJDIR} -lasn1 -lcrypto -lmd -lcrypt -lcom_err
DPADD=		${LIBKRB5} ${LIBKAFS5} ${LIBHDB} ${LIBROKEN} ${_krb4deps} \
		${LIBASN1} ${LIBCRYPTO} ${LIBMD} ${LIBCRYPT} ${LIBCOM_ERR}
MAN=		hpropd.8

.include <bsd.prog.mk>

.PATH:	${KRB5DIR}/kdc
