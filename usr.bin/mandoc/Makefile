.PATH:	${.CURDIR}/../../contrib/mdocml

PROG=	mandoc
MAN=	apropos.1 mandoc.1 mandoc.db.5 eqn.7 man.7 mandoc_char.7 mdoc.7 roff.7
MAN+=	tbl.7 makewhatis.8
WARNS?=	3

CFLAGS+=-I${.CURDIR}

# libmandoc
SRCS=	man.c man_macro.c man_validate.c
SRCS+=	att.c lib.c mdoc.c mdoc_argv.c mdoc_macro.c mdoc_state.c \
	mdoc_validate.c st.c
SRCS+=	eqn.c roff.c roff_validate.c tbl.c tbl_data.c tbl_layout.c tbl_opts.c
SRCS+=	arch.c chars.c mandoc.c mandoc_aux.c mandoc_msg.c mandoc_ohash.c \
	mandoc_xr.c msec.c preconv.c read.c

# mandoc
SRCS+=	eqn_html.c html.c man_html.c mdoc_html.c roff_html.c tbl_html.c
SRCS+=	eqn_term.c man_term.c mdoc_term.c roff_term.c term.c term_ascii.c \
	term_ps.c term_tab.c tbl_term.c
SRCS+=	dbm.c dbm_map.c mansearch.c
SRCS+=	dba.c dba_array.c dba_read.c dba_write.c mandocdb.c
SRCS+=	main.c manpath.c mdoc_man.c mdoc_markdown.c out.c tag.c tree.c

SRCS+=	compat_ohash.c

LDADD=	-lz
DPADD=	${LIBZ}

LINKS=	${BINDIR}/mandoc ${BINDIR}/apropos
LINKS+=	${BINDIR}/mandoc ${BINDIR}/makewhatis
LINKS+=	${BINDIR}/mandoc ${BINDIR}/whatis
MLINKS=	apropos.1 whatis.1

.include <bsd.prog.mk>
