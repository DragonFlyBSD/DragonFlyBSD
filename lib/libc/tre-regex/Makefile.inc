TRESRC=	${.CURDIR}/../../contrib/tre/lib

.PATH: ${TRESRC} ${.CURDIR}/../libc/tre-regex

CMAPS+=	${.CURDIR}/tre-regex/Symbol.map

SRCS+=	tre-ast.c \
	tre-compile.c \
	tre-match-backtrack.c \
	tre-match-parallel.c \
	tre-mem.c \
	tre-parse.c \
	tre-stack.c \
	regcomp.c \
	regerror.c \
	regexec.c

CFLAGS+=	-I${.CURDIR}/../libc/tre-regex
CFLAGS+=	-I${.CURDIR}/../libc/locale
CFLAGS+=	-I${TRESRC}
CFLAGS+=	-DHAVE_CONFIG_H

.if ${LIB} == "c"
INCS=	${.CURDIR}/../libc/tre-regex/regex.h
MAN+=	regex.3
MAN+=	re_format.7

MLINKS+=regex.3 regcomp.3 \
	regex.3 regcomp_l.3 \
	regex.3 regncomp.3 \
	regex.3 regncomp_l.3 \
	regex.3 regwcomp.3 \
	regex.3 regwcomp_l.3 \
	regex.3 regwncomp.3 \
	regex.3 regwncomp_l.3 \
	regex.3 regnexec.3 \
	regex.3 regwexec.3 \
	regex.3 regwnexec.3 \
	regex.3 regexec.3 \
	regex.3 regerror.3 \
	regex.3 regfree.3
.endif
