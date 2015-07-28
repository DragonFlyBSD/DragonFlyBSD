# $FreeBSD: head/usr.bin/sort/Makefile 275042 2014-11-25 14:29:10Z bapt $

PROG=	sort
SRCS=	bwstring.c \
	coll.c \
	file.c \
	mem.c \
	radixsort.c \
	sort.c \
	vsort.c

DPADD=	${LIBMD}
LDADD=	-lmd

DPADD+=	${LIBPTHREAD}
LDADD+=	-lpthread
CFLAGS+=	-DSORT_THREADS

.include <bsd.prog.mk>
