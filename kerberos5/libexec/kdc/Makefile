# $FreeBSD: src/kerberos5/libexec/kdc/Makefile,v 1.3.2.4 2002/07/25 09:33:15 ru Exp $
# $DragonFly: src/kerberos5/libexec/kdc/Makefile,v 1.4 2004/09/28 18:17:37 joerg Exp $

PROG=		kdc
SRCS=		\
	config.c	\
	connect.c	\
	kdc_locl.h	\
	kerberos5.c	\
	log.c		\
	main.c		\
	misc.c

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
		-L${ASN1OBJDIR} -lasn1 -lcrypto -lmd -lcrypt -lcom_err \
		-lutil
DPADD=		${LIBKRB5} ${LIBKAFS5} ${LIBHDB} ${LIBROKEN} ${_krb4deps} \
		${LIBASN1} ${LIBCRYPTO} ${LIBMD} ${LIBCRYPT} ${LIBCOM_ERR} \
		${LIBUTIL}
MAN=		kdc.8

.include <bsd.prog.mk>

.PATH:	${KRB5DIR}/kdc
