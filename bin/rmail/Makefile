#	@(#)Makefile	8.1 (Berkeley) 5/31/93
# $FreeBSD: src/bin/rmail/Makefile,v 1.8.2.6 2002/03/25 21:31:30 gshapiro Exp $
# $DragonFly: src/bin/rmail/Makefile,v 1.5 2005/02/06 06:16:40 okumoto Exp $

SENDMAIL_DIR=${.CURDIR}/../../contrib/sendmail
.PATH:	${SENDMAIL_DIR}/rmail

WARNS?=	0	# XXX Not yet.

PROG=	rmail
SRCS=	rmail.c
MAN=	rmail.8
CFLAGS+=-I${SENDMAIL_DIR}/include -I.

# Fake __FreeBSD__ so sendmail uses a reasonable feature set.  osreldate.h
# will generate a __FreeBSD_version for us.
CFLAGS+= -D__FreeBSD__

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
	ln -sf ${SENDMAIL_DIR}/include/sm/os/sm_os_freebsd.h sm_os.h

.include <bsd.prog.mk>
