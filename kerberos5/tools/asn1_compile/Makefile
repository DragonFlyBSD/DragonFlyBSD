# $FreeBSD: src/kerberos5/tools/asn1_compile/Makefile,v 1.5 2004/12/21 08:47:01 ru Exp $
# $DragonFly: src/kerberos5/tools/asn1_compile/Makefile,v 1.1 2005/01/16 14:25:47 eirikn Exp $

PROG=	asn1_compile
NOMAN=

NOINCS=

SRCS=	gen.c \
	gen_copy.c \
	gen_decode.c \
	gen_encode.c \
	gen_free.c \
	gen_glue.c \
	gen_length.c \
	hash.c \
	emalloc.c \
	main.c \
	symbol.c \
	getarg.c \
	warnerr.c \
	lex.l \
	parse.y \
	print_version.c \
	print_version.h \
	roken.h \
	get_window_size.c \
	strupr.c

CFLAGS+=-I${KRB5DIR}/lib/roken -I${KRB5DIR}/lib/asn1 -I.

CLEANFILES= print_version.h roken.h

print_version.h:
	${MAKEPRINTVERSION} ${.TARGET}

roken.h:
	${MAKEROKEN} > ${.TARGET}

.include <bsd.prog.mk>

# There are two print_version.c's, the one we need is in ${KRB5DIR}/lib/vers,
# so the order of paths is important here.
.PATH: ${KRB5DIR}/lib/vers ${KRB5DIR}/lib/roken ${KRB5DIR}/lib/asn1
