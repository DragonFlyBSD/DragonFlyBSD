# $FreeBSD: src/kerberosIV/usr.sbin/kprop/Makefile,v 1.6.2.1 2001/04/25 09:36:08 ru Exp $
# $DragonFly: src/kerberosIV/usr.sbin/kprop/Attic/Makefile,v 1.2 2003/06/17 04:26:25 dillon Exp $

PROG=		kprop
MAN=		${KRB4DIR}/man/kprop.8
SRCS=		kprop.c protos.h
CFLAGS+=	-I${KRB4DIR}/include \
		-I${KRB4DIR}/lib/roken \
		-I${KRB4DIR}/lib/kdb \
		-I${KRB4DIR}/lib/krb \
		-I${KRB4DIR}/slave \
		-I${INCLOBJDIR} \
		-I${.CURDIR}/../include \
		-I${.OBJDIR}
LDADD=		-L${ROKENOBJDIR} -lroken -L${KRBOBJDIR} -lkrb -lcrypto -lcrypt \
		-lcom_err
DPADD=		${LIBROKEN} ${LIBKRB} ${LIBCRYPTO} ${LIBCRYPT} ${LIBCOM_ERR}

.include <bsd.prog.mk>

.PATH:	${KRB4DIR}/slave
