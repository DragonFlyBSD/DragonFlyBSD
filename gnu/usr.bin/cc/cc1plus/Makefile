# $FreeBSD: src/gnu/usr.bin/cc/cc1plus/Makefile,v 1.22.2.1 2002/06/20 23:13:31 obrien Exp $
# $DragonFly: src/gnu/usr.bin/cc/cc1plus/Attic/Makefile,v 1.8 2004/04/04 21:31:14 joerg Exp $

.include "../Makefile.inc"

.PATH: ${GCCDIR}/cp

PROG=	cc1plus
SRCS=	parse.y y.tab.h parse.h
SRCS+=	call.c class.c cvt.c decl.c decl2.c errfn.c error.c except.c expr.c \
	friend.c init.c lex.c method.c pt.c ptree.c repo.c rtti.c \
	search.c semantics.c sig.c spew.c tree.c typeck.c typeck2.c xref.c
NOMAN=	1
NOSHARED=yes

CFLAGS+= -I${GCCDIR}/cp -I.

DPADD+=	${LIBCC_INT} 
LDADD+=	${LIBCC_INT}

parse.h: y.tab.h parse.c
	cp -p y.tab.h ${.TARGET}
	grep '^#define[   ]*YYEMPTY' parse.c >>${.TARGET}

CLEANFILES+=	parse.h

HDRS=	exception new new.h typeinfo
INCS=	${HDRS:S;^;${GCCDIR}/cp/inc/;}
INCSDIR=${INCLUDEDIR}/c++/2.95

.include <bsd.prog.mk>
