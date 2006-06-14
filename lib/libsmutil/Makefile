# $FreeBSD: src/lib/libsmutil/Makefile,v 1.1.2.5 2002/07/19 18:46:26 ru Exp $
# $DragonFly: src/lib/libsmutil/Makefile,v 1.7 2006/06/14 16:45:56 gshapiro Exp $

SENDMAIL_DIR=${.CURDIR}/../../contrib/sendmail-8.13.7
.PATH:	${SENDMAIL_DIR}/libsmutil

CFLAGS+=-I${SENDMAIL_DIR}/sendmail -I${SENDMAIL_DIR}/include -I.
CFLAGS+=-DNEWDB -DNIS -DMAP_REGEX -DNOT_SENDMAIL

# User customizations to the sendmail build environment
CFLAGS+=${SENDMAIL_CFLAGS}

LIB=	smutil

SRCS+=	sm_os.h
SRCS+=	debug.c err.c lockfile.c safefile.c snprintf.c cf.c
CLEANFILES+=sm_os.h

INTERNALLIB=		true

sm_os.h:
	${LN} -sf ${SENDMAIL_DIR}/include/sm/os/sm_os_dragonfly.h sm_os.h

.include <bsd.lib.mk>
