# $FreeBSD: src/kerberos5/libexec/ipropd-master/Makefile,v 1.3.2.3 2002/09/23 14:00:44 nectar Exp $
# $DragonFly: src/kerberos5/libexec/ipropd-master/Makefile,v 1.4 2004/09/28 18:17:37 joerg Exp $

PROG=		ipropd-master
SRCS=		ipropd_master.c iprop.h kadm5_locl.h kadm5_err.h hdb_err.h
CFLAGS+=	-I${KRB5DIR}/include \
		-I${KRB5DIR}/lib/kadm5 \
		-I${KRB5DIR}/lib/krb5 \
		-I${KRB5DIR}/lib/asn1 \
		-I${KRB5DIR}/lib/hdb \
		-I${KRB5DIR}/lib/roken \
		-I${KRB5DIR}/kuser \
		-I${ASN1OBJDIR} \
		-I${HDBOBJDIR} \
		-I${.OBJDIR}
LDADD=		\
		-L${KADM5SOBJDIR} -lkadm5srv \
		-L${KRB5OBJDIR} -lkrb5 \
		-L${HDBOBJDIR} -lhdb -L${ROKENOBJDIR} -lroken \
		-L${VERSOBJDIR} -lvers \
		${_krb4libs} \
		-L${ASN1OBJDIR} -lasn1 -lcrypto -lmd -lcrypt -lcom_err \
		-lutil
DPADD=		${LIBKRB5} ${LIBKAFS5} ${LIBHDB} ${LIBROKEN} ${_krb4deps} \
		${LIBASN1} ${LIBCRYPTO} ${LIBMD} ${LIBCRYPT} ${LIBCOM_ERR} \
		${LIBUTIL}
NOMAN=		yes

.include <bsd.prog.mk>

.PATH:	${KRB5DIR}/lib/kadm5
