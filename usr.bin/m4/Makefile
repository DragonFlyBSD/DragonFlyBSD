#	$OpenBSD: Makefile,v 1.10 2002/04/26 13:13:41 espie Exp $
# $FreeBSD: src/usr.bin/m4/Makefile,v 1.14 2013/01/01 18:26:42 svnexp Exp $

# -DEXTENDED 
# 	if you want the paste & spaste macros.

PROG=	m4
CFLAGS+=-DEXTENDED -I${.CURDIR} -I${.CURDIR}/lib
DPADD=	${LIBY} ${LIBL} ${LIBM}
LDADD=	-ly -ll -lm

SRCS=	eval.c expr.c look.c main.c misc.c gnum4.c trace.c parser.y tokenizer.l
.PATH: ${.CURDIR}/lib
SRCS+=	ohash_create_entry.c ohash_delete.c ohash_do.c ohash_entries.c \
	ohash_enum.c ohash_init.c ohash_int.h ohash_interval.c \
	ohash_lookup_interval.c ohash_lookup_memory.c ohash_qlookup.c \
	ohash_qlookupi.c

tokenizer.o: parser.h

CLEANFILES+=	parser.c parser.h tokenizer.o

.include <bsd.prog.mk>
