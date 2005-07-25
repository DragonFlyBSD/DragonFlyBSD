#	@(#)Makefile	8.1 (Berkeley) 5/31/93
# $FreeBSD: src/bin/rmail/Makefile,v 1.8.2.6 2002/03/25 21:31:30 gshapiro Exp $
# $DragonFly: src/bin/rmail/Makefile,v 1.7 2005/07/25 00:24:31 gshapiro Exp $

SENDMAIL_DIR=${.CURDIR}/../../contrib/sendmail-8.13.4
.PATH:	${SENDMAIL_DIR}/rmail

WARNS?=	0	# XXX Not yet.

PROG=	rmail
SRCS=	rmail.c
MAN=	rmail.8
CFLAGS+=-I${SENDMAIL_DIR}/include -I.

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

# If you want to have your rmail queuing the mail only, uncomment the
# following:
# CFLAGS+= -DQUEUE_ONLY

# Not much point this being static. It calls a shared sendmail...
NOSHARED?= NO

sm_os.h:
	${LN} -sf ${SENDMAIL_DIR}/include/sm/os/sm_os_dragonfly.h sm_os.h

.include <bsd.prog.mk>
