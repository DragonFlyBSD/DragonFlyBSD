# $FreeBSD: src/usr.sbin/keyserv/Makefile,v 1.5.2.1 2001/04/25 12:09:50 ru Exp $
# $DragonFly: src/usr.sbin/keyserv/Makefile,v 1.2 2003/06/17 04:29:55 dillon Exp $

PROG=	keyserv
SRCS=	keyserv.c setkey.c keyserv_uid.c crypt_svc.c crypt_server.c crypt.h

MAN=	keyserv.8

CFLAGS+= -DKEYSERV_RANDOM -DBROKEN_DES -I.
.if $(OBJFORMAT) == elf
CFLAGS+= -DOBJFORMAT_ELF
.endif

DPADD=	${LIBMP} ${LIBRPCSVC}
LDADD=	-lmp -lrpcsvc

RPCDIR=	${DESTDIR}/usr/include/rpcsvc

CLEANFILES= crypt_svc.c crypt.h

RPCGEN= rpcgen -C

crypt_svc.c: ${RPCDIR}/crypt.x
	${RPCGEN} -m -o ${.TARGET} ${RPCDIR}/crypt.x

crypt.h: ${RPCDIR}/crypt.x
	${RPCGEN} -h -o ${.TARGET} ${RPCDIR}/crypt.x

.include <bsd.prog.mk>
