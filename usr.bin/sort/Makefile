# $FreeBSD: head/usr.bin/sort/Makefile 275042 2014-11-25 14:29:10Z bapt $

PROG=	sort
SRCS=	bwstring.c \
	coll.c \
	file.c \
	mem.c \
	radixsort.c \
	sort.c \
	vsort.c

CFLAGS+= -DWITHOUT_NLS

.if !defined(BOOTSTRAPPING) && !defined(RESCUE)
CFLAGS+= -DSORT_RANDOM
CFLAGS+=	${PRIVATELIB_CFLAGS}
DPADD+=		${LIBCRYPTO}
LDADD+=		-lprivate_crypto
LDFLAGS+=	${PRIVATELIB_LDFLAGS}

DPADD+=	${LIBPTHREAD}
LDADD+=	-lpthread
CFLAGS+= -DSORT_THREADS
.endif

.include <bsd.prog.mk>
