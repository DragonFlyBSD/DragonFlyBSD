# $FreeBSD: src/lib/libsmdb/Makefile,v 1.1.2.5 2002/07/19 18:46:26 ru Exp $
# $DragonFly: src/lib/libsmdb/Makefile,v 1.9 2007/04/14 17:43:48 gshapiro Exp $

SENDMAIL_DIR=${.CURDIR}/../../contrib/sendmail-8.14
.PATH:	${SENDMAIL_DIR}/libsmdb

CFLAGS+=-I${SENDMAIL_DIR}/sendmail -I${SENDMAIL_DIR}/include -I.
CFLAGS+=-DNEWDB -DNOT_SENDMAIL

# User customizations to the sendmail build environment
CFLAGS+=${SENDMAIL_CFLAGS}

LIB=	smdb

SRCS+=	sm_os.h
SRCS+=	smdb.c smdb1.c smdb2.c smndbm.c
CLEANFILES+=sm_os.h
WARNS?=	2

INTERNALLIB=		true

sm_os.h:
	${LN} -sf ${SENDMAIL_DIR}/include/sm/os/sm_os_dragonfly.h sm_os.h

.include <bsd.lib.mk>
