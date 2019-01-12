.PATH:	${.CURDIR}/../../contrib/mdocml

PROG=	mandoc
MAN=	mandoc.1 mandoc_char.7
#MAN+=	eqn.7 man.7 mdoc.7 roff.7 tbl.7
WARNS?=	3

CFLAGS+=-DHAVE_CONFIG_H -I${.CURDIR}

# libmandoc
SRCS=	man.c man_hash.c man_macro.c man_validate.c
SRCS+=	arch.c att.c lib.c mdoc.c mdoc_argv.c mdoc_hash.c mdoc_macro.c \
	mdoc_validate.c st.c vol.c
SRCS+=	eqn.c roff.c tbl.c tbl_data.c tbl_layout.c tbl_opts.c
SRCS+=	chars.c mandoc.c msec.c read.c

# mandoc
SRCS+=	eqn_html.c html.c man_html.c mdoc_html.c tbl_html.c
SRCS+=	mdoc_man.c
SRCS+=	eqn_term.c man_term.c mdoc_term.c term.c term_ascii.c term_ps.c \
	tbl_term.c
SRCS+=	main.c out.c tree.c mandoc_aux.c

.include <bsd.prog.mk>
