# $FreeBSD: src/kerberos5/usr.bin/k5passwd/Makefile,v 1.4.2.3 2002/07/25 09:33:16 ru Exp $
# $DragonFly: src/kerberos5/usr.bin/k5passwd/Attic/Makefile,v 1.3 2003/08/05 07:45:41 asmodai Exp $

PROG=		k5passwd
SRCS=		kpasswd.c krb5_err.h heim_err.h k524_err.h
CFLAGS+=	-I${KRB5DIR}/include \
		-I${KRB5DIR}/lib/asn1 \
		-I${KRB5DIR}/lib/krb5 \
		-I${KRB5DIR}/lib/roken \
		-I${KRB5DIR}/kpasswd \
		-I${ASN1OBJDIR} \
		-I${.OBJDIR}
LDADD=		-L${KRB5OBJDIR} -lkrb5 -L${ROKENOBJDIR} -lroken \
		-L${VERSOBJDIR} -lvers \
		${_krb4libs} \
		-L${ASN1OBJDIR} -lasn1 -lcrypto -lmd -lcrypt -lcom_err
DPADD=		${LIBKRB5} ${LIBROKEN} ${_krb4deps} \
		${LIBASN1} ${LIBCRYPTO} ${LIBMD} ${LIBCRYPT} ${LIBCOM_ERR}

.include <bsd.prog.mk>

.PATH:	${KRB5DIR}/kpasswd

k5passwd.1:	kpasswd.1
	ln -sf ${.OODATE} ${.TARGET}
