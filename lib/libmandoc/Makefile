.PATH:	${.CURDIR}/../../contrib/mdocml

LIB=	mandoc
SHLIB_MAJOR=0
WARNS?=	3
MAN=	mandoc.3

SRCS=	man.c man_hash.c man_macro.c man_validate.c
SRCS+=	arch.c att.c lib.c mdoc.c mdoc_argv.c mdoc_hash.c mdoc_macro.c \
	mdoc_validate.c st.c vol.c
SRCS+=	eqn.c roff.c tbl.c tbl_data.c tbl_layout.c tbl_opts.c
SRCS+=	chars.c mandoc.c msec.c read.c

CFLAGS+=-DHAVE_CONFIG_H

INCS=	man.h mdoc.h mandoc.h

MLINKS+=mandoc.3 mandoc_escape.3
MLINKS+=mandoc.3 man_meta.3
MLINKS+=mandoc.3 man_mparse.3
MLINKS+=mandoc.3 man_node.3
MLINKS+=mandoc.3 mchars_alloc.3
MLINKS+=mandoc.3 mchars_free.3
MLINKS+=mandoc.3 mchars_num2char.3
MLINKS+=mandoc.3 mchars_num2uc.3
MLINKS+=mandoc.3 mchars_spec2cp.3
MLINKS+=mandoc.3 mchars_spec2str.3
MLINKS+=mandoc.3 mdoc_meta.3
MLINKS+=mandoc.3 mdoc_node.3
MLINKS+=mandoc.3 mparse_alloc.3
MLINKS+=mandoc.3 mparse_free.3
MLINKS+=mandoc.3 mparse_getkeep.3
MLINKS+=mandoc.3 mparse_keep.3
MLINKS+=mandoc.3 mparse_readfd.3
MLINKS+=mandoc.3 mparse_reset.3
MLINKS+=mandoc.3 mparse_result.3
MLINKS+=mandoc.3 mparse_strerror.3
MLINKS+=mandoc.3 mparse_strlevel.3

.include <bsd.lib.mk>
