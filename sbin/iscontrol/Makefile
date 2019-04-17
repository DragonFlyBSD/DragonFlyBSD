# $FreeBSD$

SRCS= iscontrol.c pdu.c fsm.c config.c login.c auth_subr.c misc.c
PROG= iscontrol
CFLAGS+=	${PRIVATELIB_CFLAGS}
DPADD=		${LIBCAM} ${LIBCRYPTO}
LDADD=		-lcam -lprivate_crypto
LDFLAGS+=	${PRIVATELIB_LDFLAGS}

CFLAGS += -I${.CURDIR}/../../sys/dev/disk/iscsi/initiator
#CFLAGS += -g -DDEBUG

MAN= iscsi.conf.5 iscontrol.8

WARNS?= 3
.include <bsd.prog.mk>
