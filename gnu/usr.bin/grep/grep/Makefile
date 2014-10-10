.include "../Makefile.inc"

.PATH: ${BASEDIR}/src ${BASEDIR}/doc

PROG=		grep
MAN=		grep.1
MLINKS+=	grep.1 fgrep.1
MLINKS+=	grep.1 egrep.1

CFLAGS+=	-I${BASEDIR}/src -I${.OBJDIR}
CFLAGS+=	-I${BASEDIR}/lib -I${.CURDIR}/../libgreputils
CFLAGS+=	-DHAVE_CONFIG_H

DPADD=		../libgreputils/libgreputils.a
LDADD=		../libgreputils/libgreputils.a
BINDIR?=	/usr/bin

SRCS=		grep.c \
		searchutils.c \
		dfa.c \
		dfasearch.c \
		kwset.c \
		kwsearch.c \
		pcresearch.c

.include <bsd.prog.mk>
