# $FreeBSD: src/kerberosIV/usr.sbin/ext_srvtab/Makefile,v 1.7.2.1 2001/04/25 09:36:03 ru Exp $
# $DragonFly: src/kerberosIV/usr.sbin/ext_srvtab/Attic/Makefile,v 1.2 2003/06/17 04:26:25 dillon Exp $

PROG=		ext_srvtab
MAN=		${KRB4DIR}/man/ext_srvtab.8
SRCS=		ext_srvtab.c protos.h
CFLAGS+=	-I${KRB4DIR}/include \
		-I${KRB4DIR}/lib/roken \
		-I${KRB4DIR}/admin \
		-I${KRB4DIR}/lib/kadm \
		-I${KRB4DIR}/lib/kdb \
		-I${KRB4DIR}/lib/krb \
		-I${.CURDIR}/../include \
		-I${.OBJDIR}
LDADD=		-L${ROKENOBJDIR} -lroken \
		-L${KDBOBJDIR} -lkdb -L${KRBOBJDIR} -lkrb -lcrypto -lcrypt \
		-lcom_err
DPADD=		${LIBROKEN} ${LIBKDB} ${LIBKRB} ${LIBCRYPTO} ${LIBCRYPT} \
		${LIBCOM_ERR}

.include <bsd.prog.mk>

.PATH:	${KRB4DIR}/admin
