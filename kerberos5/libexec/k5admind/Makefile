# $FreeBSD: src/kerberos5/libexec/k5admind/Makefile,v 1.4.2.3 2002/09/23 14:00:48 nectar Exp $
# $DragonFly: src/kerberos5/libexec/k5admind/Attic/Makefile,v 1.3 2003/08/05 07:45:40 asmodai Exp $

PROG=		k5admind
SRCS=		kadmind.c server.c kadm_conn.c \
		kadm5/private.h kadm5/admin.h kadm5/kadm5_err.h
CFLAGS+=	-I${KRB5DIR}/include \
		-I${KRB5DIR}/lib/roken \
		-I${KRB5DIR}/lib/krb5 \
		-I${KRB5DIR}/lib/asn1 \
		-I${KRB5DIR}/lib/hdb \
		-I${KRB5DIR}/lib/sl \
		-I${KRB5DIR}/lib/kadm5 \
		-I${KRB5DIR}/kadmin \
		-I${ASN1OBJDIR} \
		-I${HDBOBJDIR} \
		-I${.OBJDIR}
LDADD=		\
		-L${KADM5SOBJDIR} -lkadm5srv \
		-L${KRB5OBJDIR} -lkrb5 \
		-L${HDBOBJDIR} -lhdb \
		-L${ROKENOBJDIR} -lroken \
		-L${VERSOBJDIR} -lvers \
		${_krb4libs} \
		-L${ASN1OBJDIR} -lasn1 -lcrypto -lmd -lcrypt -lcom_err
DPADD=		${LIBKRB5} ${LIBKAFS5} ${LIBROKEN} ${_krb4deps} \
		${LIBKADM5CLNT} ${LIBKADM5SRV} ${LIBHDB} \
		${LIBASN1} ${LIBCRYPTO} ${LIBMD} ${LIBCRYPT} ${LIBCOM_ERR}
NOMAN=		yes

.include <bsd.prog.mk>

.PATH:	${KRB5DIR}/kadmin

kadm5/private.h:	kadm5
	cp ${KRB5DIR}/lib/kadm5/private.h ${.TARGET}

kadm5/admin.h:	kadm5
	cp ${KRB5DIR}/lib/kadm5/admin.h ${.TARGET}

kadm5/kadm5_err.h: kadm5 kadm5_err.h
	cd ${.OBJDIR}/kadm5 && ln -sf ../kadm5_err.h

kadm5:
	mkdir ${.TARGET}

CLEANFILES+=	kadm5/private.h kadm5/admin.h kadm5/kadm5_err.h
CLEANDIRS+=	kadm5
