# $FreeBSD: src/kerberos5/lib/libhdb/Makefile,v 1.2.2.5 2002/08/19 16:10:22 ru Exp $
# $DragonFly: src/kerberos5/lib/libhdb/Makefile,v 1.3 2004/03/22 20:58:00 dillon Exp $

LIB=	hdb
CFLAGS+=-I${KRB5DIR}/include \
	-I${INCLUDEOBJDIR} \
	-I${KRB5DIR}/lib/krb5 \
	-I${KRB5DIR}/lib/hdb \
	-I${KRB5DIR}/lib/asn1 \
	-I${KRB5DIR}/lib/roken \
	-I${.OBJDIR} \
	-I${ASN1OBJDIR}

SRCS=	\
	common.c				\
	db.c					\
	db3.c					\
	hdb-ldap.c				\
	hdb.c					\
	keytab.c				\
	mkey.c					\
	ndbm.c					\
	print.c					\
	hdb_err.c				\
	hdb_err.h				\
	hdb_asn1.h				\
	k524_err.h				\
	krb5_err.h				\
	heim_err.h				\
	${GEN:S/.x/.c/g}

GEN=						\
	asn1_Key.x				\
	asn1_GENERATION.x			\
	asn1_Event.x				\
	asn1_HDBFlags.x				\
	asn1_hdb_entry.x			\
	asn1_Salt.x

.include <bsd.lib.mk>

.PATH:	${KRB5DIR}/lib/hdb
.PATH:	${KRB5DIR}/lib/asn1
.PATH:	${KRB5DIR}/lib/vers
.PATH:	${KRB5DIR}/lib/roken

build-tools:	make-print-version.nx asn1_compile.nx

.for I in ${GEN}
${I:S/.x/.c/}:  ${I}
	cmp -s ${.OODATE} ${.TARGET} 2> /dev/null || cp ${.OODATE} ${.TARGET}
.endfor

CLEANFILES+=	${GEN:S/.x/.c/g} hdb_asn1.h asn1_files

${GEN} hdb_asn1.h:	asn1_compile.nx hdb.asn1
	./asn1_compile.nx ${KRB5DIR}/lib/hdb/hdb.asn1 hdb_asn1

asn1_compile.nx:	parse.no lex.no main.c hash.c symbol.c emalloc.c gen.c \
		gen_encode.c gen_decode.c gen_free.c gen_length.c \
		gen_copy.c gen_glue.c getarg.c warnerr.c print_version.no \
		get_window_size.c strupr.c
	${NXCC} ${NXCFLAGS} ${.ALLSRC} -o ${.TARGET}

.if defined(BOOTSTRAPPING)
asn1_compile.nx: getprogname.c setprogname.c
.endif

parse.no:	parse.c

.ORDER: parse.c parse.h
parse.h parse.c:	parse.y
	${YACC}	-d ${.OODATE}
	cp y.tab.c parse.c
	cp y.tab.h parse.h

lex.o:		lex.l

print_version.o: print_version.h print_version.c
	${CC} ${CFLAGS} -c -o ${.TARGET} ${KRB5DIR}/lib/vers/print_version.c

print_version.h: make-print-version.nx
	./make-print-version.nx print_version.h

CLEANFILES+=	${GEN} asn1_compile.nx lex.no parse.no parse.c parse.h \
		hdb_asn1.h make-print-version.nx print_version.h \
		print_version.no y.tab.c y.tab.h
