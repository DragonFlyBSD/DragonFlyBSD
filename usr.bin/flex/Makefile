CONTRIBDIR=	${.CURDIR}/../../contrib/flex
.PATH:		${CONTRIBDIR}/src
.PATH:		${CONTRIBDIR}/doc

PROG=	flex

VMAJ=	2
VMIN=	6
VPT=	4

LINKS+= ${BINDIR}/flex ${BINDIR}/lex
LINKS+= ${BINDIR}/flex ${BINDIR}/lex++
LINKS+= ${BINDIR}/flex ${BINDIR}/flex++

MLINKS+=	flex.1 lex.1
MLINKS+=	flex.1 lex++.1
MLINKS+=	flex.1 flex++.1

INCS=		FlexLexer.h

SUBDIR=		lib

CFLAGS+= 	-I${CONTRIBDIR}/src -I${.OBJDIR} -I${.CURDIR}
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

SRCS=	${flex_SOURCES} skel.c

skel.c: flex.skl mkskel.sh flexint.h tables_shared.h
	sh ${CONTRIBDIR}/src/mkskel.sh \
		${CONTRIBDIR}/src m4 ${VMAJ}.${VMIN}.${VPT} > skel.c

DPADD=	${LIBM}
LDADD=	-lm

CLEANFILES=	skel.c

.include <bsd.prog.mk>
