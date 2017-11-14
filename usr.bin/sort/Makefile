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

.if !defined(BOOTSTRAPPING)
DPADD=	${LIBMD}
LDADD=	-lmd
CFLAGS+= -DSORT_RANDOM

DPADD+=	${LIBPTHREAD}
LDADD+=	-lpthread
CFLAGS+= -DSORT_THREADS

# XXX sys/md5.h shim errata for bootstrap REMOVE_OPENSSL_FILES
CFLAGS+= -I${_SHLIBDIRPREFIX}/usr/include/priv
.endif

.include <bsd.prog.mk>
