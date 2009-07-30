#	@(#)Makefile	8.1 (Berkeley) 7/19/93
# $FreeBSD: src/libexec/mail.local/Makefile,v 1.10.2.7 2002/03/25 21:31:38 gshapiro Exp $
# $DragonFly: src/libexec/mail.local/Makefile,v 1.9 2007/04/14 17:43:48 gshapiro Exp $

SENDMAIL_DIR=${.CURDIR}/../../contrib/sendmail-8.14
.PATH:	${SENDMAIL_DIR}/mail.local

PROG=	mail.local
SRCS=	mail.local.c
MAN=	mail.local.8
CFLAGS+=-I${SENDMAIL_DIR}/include -I.
WARNS?=	2

.if exists(${.OBJDIR}/../../lib/libsm)
LIBSMDIR:=	${.OBJDIR}/../../lib/libsm
.else
LIBSMDIR!=	cd ${.CURDIR}/../../lib/libsm; make -V .OBJDIR
.endif
LIBSM:=		${LIBSMDIR}/libsm.a

DPADD=	${LIBSM}
LDADD=	${LIBSM}

SRCS+=	sm_os.h
CLEANFILES+=sm_os.h

# User customizations to the sendmail build environment
CFLAGS+=${SENDMAIL_CFLAGS}
DPADD+=${SENDMAIL_DPADD}
LDADD+=${SENDMAIL_LDADD}
LDFLAGS+=${SENDMAIL_LDFLAGS}

sm_os.h:
	${LN} -sf ${SENDMAIL_DIR}/include/sm/os/sm_os_dragonfly.h sm_os.h

.include <bsd.prog.mk>
