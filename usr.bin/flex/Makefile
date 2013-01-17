CONTRIBDIR=	${.CURDIR}/../../contrib/flex
.PATH:		${CONTRIBDIR}
.PATH:		${CONTRIBDIR}/doc

PROG=	flex

VMAJ=	2
VMIN=	5
VPT=	37

LINKS+= ${BINDIR}/flex ${BINDIR}/lex
LINKS+= ${BINDIR}/flex ${BINDIR}/lex++
LINKS+= ${BINDIR}/flex ${BINDIR}/flex++

MLINKS+=	flex.1 lex.1
MLINKS+=	flex.1 lex++.1
MLINKS+=	flex.1 flex++.1

INCS=		FlexLexer.h

SUBDIR=		lib

CFLAGS+= 	-I${CONTRIBDIR} -I${.OBJDIR} -I${.CURDIR}
CFLAGS+=	-DHAVE_CONFIG_H

WARNS?= 	1

# verbatim from vendor makefile
flex_SOURCES = \
	ccl.c \
	dfa.c \
	ecs.c \
	scanflags.c \
	gen.c \
	main.c \
	misc.c \
	nfa.c \
	parse.y \
	scan.l \
	skel.c \
	sym.c \
	tblcmp.c \
	yylex.c \
	options.c \
	scanopt.c \
	buf.c \
	tables.c \
	tables_shared.c \
	filter.c \
	regex.c

SRCS=	${flex_SOURCES}

skel.c: flex.skl mkskel.sh flexint.h tables_shared.h
	sed -e 's/m4_/m4postproc_/g' \
	    -e 's/m4preproc_/m4_/g' \
	    ${CONTRIBDIR}/flex.skl \
	| m4 -P -I ${CONTRIBDIR} \
	   -D FLEX_MAJOR_VERSION=${VMAJ} \
	   -D FLEX_MINOR_VERSION=${VMIN} \
	   -D FLEX_SUBMINOR_VERSION=${VPT} \
	| sed 's/m4postproc_/m4_/g' \
	| sh ${CONTRIBDIR}/mkskel.sh > skel.c

DPADD=	${LIBM}
LDADD=	-lm

CLEANFILES=	skel.c

.include <bsd.prog.mk>
