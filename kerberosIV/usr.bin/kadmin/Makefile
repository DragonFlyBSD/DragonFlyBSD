# $FreeBSD: src/kerberosIV/usr.bin/kadmin/Makefile,v 1.8.2.1 2001/04/25 09:36:01 ru Exp $
# $DragonFly: src/kerberosIV/usr.bin/kadmin/Attic/Makefile,v 1.2 2003/06/17 04:26:25 dillon Exp $

PROG=		kadmin
MAN=		${KRB4DIR}/man/kadmin.8
SRCS=		kadmin.c new_pwd.c random_password.c \
		${KADMOBJDIR}/kadm_err.h ${KRBOBJDIR}/krb_err.h
CFLAGS+=	-I${KRB4DIR}/include \
		-I${KRB4DIR}/lib/roken \
		-I${KRB4DIR}/lib/sl \
		-I${KRB4DIR}/lib/acl \
		-I${KRB4DIR}/lib/kadm \
		-I${KRB4DIR}/lib/kdb \
		-I${KRB4DIR}/lib/krb \
		-I${KRB4DIR}/kadmin \
		-I${KADMOBJDIR} \
		-I${KRBOBJDIR} \
		-I${.CURDIR}/../include
LDADD=		-L${ROKENOBJDIR} -lroken -L${SLOBJDIR} -lsl \
		-L${ACLOBJDIR} -lacl -L${KADMOBJDIR} -lkadm \
		-L${KDBOBJDIR} -lkdb -L${KRBOBJDIR} -lkrb \
		-lcrypto -lcrypt -lcom_err -lreadline -ltermcap
DPADD=		${LIBROKEN} ${LIBSL} ${LIBACL} ${LIBKADM} ${LIBKDB} ${LIBKRB} \
		${LIBCRYPTO} ${LIBCRYPT} ${LIBCOM_ERR} ${LIBREADLINE} \
		${LIBTERMCAP}

.include <bsd.prog.mk>

.PATH:	${KRB4DIR}/kadmin
