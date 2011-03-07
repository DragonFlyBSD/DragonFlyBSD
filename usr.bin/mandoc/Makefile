.PATH:	${.CURDIR}/../../contrib/mdocml

PROG=	mandoc
MAN=	mandoc.1 mandoc_char.7
#MAN+=	man.3 man.7 mdoc.3 mdoc.7 roff.3 roff.7 tbl.7
WARNS?=	3

VERSION=1.10.9
CFLAGS+=-DVERSION=\"${VERSION}\"

MDOCSRCS   = mdoc_macro.c mdoc.c mdoc_hash.c mdoc_strings.c \
	     mdoc_argv.c mdoc_validate.c lib.c att.c \
	     arch.c vol.c msec.c st.c

MAINSRCS   = main.c mdoc_term.c chars.c term.c tree.c compat.c \
	     man_term.c html.c mdoc_html.c man_html.c out.c \
	     term_ps.c term_ascii.c tbl_term.c tbl_html.c

MANSRCS	   = man_macro.c man.c man_hash.c man_validate.c \
	     man_argv.c

MANDOCSRCS = mandoc.c

ROFFSRCS   = roff.c tbl.c tbl_opts.c tbl_layout.c tbl_data.c

SRCS	   = $(MDOCSRCS) $(MAINSRCS) $(MANSRCS) \
	     $(MANDOCSRCS) $(ROFFSRCS)

.include <bsd.prog.mk>
