.PATH:	${.CURDIR}/../../contrib/mdocml

PROG=	mandoc
MAN=	mandoc.1 mandoc_char.7
#MAN+=	eqn.7 man.7 mdoc.7 roff.7 tbl.7
WARNS?=	3

CFLAGS+=-DHAVE_CONFIG_H

SRCS=	eqn_html.c html.c man_html.c mdoc_html.c tbl_html.c
SRCS+=	mdoc_man.c
SRCS+=	eqn_term.c man_term.c mdoc_term.c term.c term_ascii.c term_ps.c \
	tbl_term.c
SRCS+=	main.c out.c tree.c

LDADD=	-lmandoc
DPADD=	${LIBMANDOC}

.include <bsd.prog.mk>
