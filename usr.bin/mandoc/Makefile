.PATH:	${.CURDIR}/../../contrib/mdocml

PROG=	mandoc
MAN=	mandoc.1 mandoc_char.7
#MAN+=	mandoc.3 eqn.7 man.7 mdoc.7 roff.7 tbl.7
WARNS?=	3

VERSION=1.11.1
CFLAGS+=-DVERSION=\"${VERSION}\"

SRCS=	arch.c att.c chars.c compat.c eqn.c html.c lib.c main.c man.c \
	man_hash.c man_html.c man_macro.c man_term.c man_validate.c mandoc.c \
	mdoc.c mdoc_argv.c mdoc_hash.c mdoc_html.c mdoc_macro.c mdoc_term.c \
	mdoc_validate.c msec.c out.c read.c roff.c st.c tbl.c tbl_data.c \
	tbl_html.c tbl_layout.c tbl_opts.c tbl_term.c term.c term_ascii.c \
	term_ps.c tree.c vol.c

.include <bsd.prog.mk>
