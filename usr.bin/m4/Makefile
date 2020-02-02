#	$OpenBSD: Makefile,v 1.10 2002/04/26 13:13:41 espie Exp $
# $FreeBSD: src/usr.bin/m4/Makefile,v 1.14 2013/01/01 18:26:42 svnexp Exp $

# -DEXTENDED
# 	if you want the paste & spaste macros.

PROG=	m4
CFLAGS+=-DEXTENDED -I${.CURDIR} -I${.CURDIR}/lib -I.
.if defined(BOOTSTRAPPING) && exists(${_SHLIBDIRPREFIX}/usr/lib/libl.a)
LDFLAGS+=	-L${_SHLIBDIRPREFIX}/usr/lib
.endif
DPADD=	${LIBY} ${LIBL} ${LIBM}
LDADD=	-ly -ll -lm

# m4 depends on lex which depends on m4.  Jeeze, gimme a break!  To break
# this loop we manually lex tokenizer.l and installed it in the source
# tree as manual_tokenizer.c.  Now we can build m4 without needing lex.
#
# To regenerate manual_tokenizer.c rename manual_tokenizer.c to tokenizer.l
# and manual_tokenizer.o to tokenizer.o, build, copy tokenizer.c from the
# obj dir to manual_tokenizer.c here, and then adjust the Makefile back to
# using manual_tokenizer.c.
#
# A pox on whatever idiots who thought making lex depend on m4 was a good
# idea.  m4 is such a piece of crap, nobody should be using it anymore.
#
SRCS=	eval.c expr.c look.c main.c misc.c gnum4.c trace.c parser.y
SRCS+=	manual_tokenizer.c
.PATH: ${.CURDIR}/lib
SRCS+=	ohash_create_entry.c ohash_delete.c ohash_do.c ohash_entries.c \
	ohash_enum.c ohash_init.c ohash_int.h ohash_interval.c \
	ohash_lookup_interval.c ohash_lookup_memory.c ohash_qlookup.c \
	ohash_qlookupi.c

manual_tokenizer.o: parser.h

CLEANFILES+=	parser.c parser.h manual_tokenizer.o

.include <bsd.prog.mk>
