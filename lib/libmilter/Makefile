# $FreeBSD: src/lib/libmilter/Makefile,v 1.1.2.2 2002/07/22 14:21:49 ru Exp $
# $DragonFly: src/lib/libmilter/Makefile,v 1.6 2006/03/22 18:25:43 gshapiro Exp $

SENDMAIL_DIR=${.CURDIR}/../../contrib/sendmail-8.13.6
.PATH:	${SENDMAIL_DIR}/libmilter ${SENDMAIL_DIR}/libsm

CFLAGS+=-I${SENDMAIL_DIR}/sendmail -I${SENDMAIL_DIR}/include -I.
CFLAGS+=-DNETINET6 -DNOT_SENDMAIL -Dsm_snprintf=snprintf
CFLAGS+=-D_THREAD_SAFE

# User customizations to the sendmail build environment
CFLAGS+=${SENDMAIL_CFLAGS}

INCSDIR=${INCLUDEDIR}/libmilter
INCS=	${SENDMAIL_DIR}/include/libmilter/mfapi.h \
	${SENDMAIL_DIR}/include/libmilter/mfdef.h
LIB=	milter

SRCS+=	sm_os.h
SRCS+=	main.c engine.c listener.c handler.c comm.c smfi.c \
	signal.c sm_gethost.c errstring.c strl.c
CLEANFILES+=sm_os.h

sm_os.h:
	${LN} -sf ${SENDMAIL_DIR}/include/sm/os/sm_os_dragonfly.h sm_os.h

.include <bsd.lib.mk>
