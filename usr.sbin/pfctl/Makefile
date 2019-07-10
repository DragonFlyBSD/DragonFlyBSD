#	$OpenBSD: Makefile,v 1.15 2004/03/10 18:49:49 mcbride Exp $

PROG=	pfctl
SRCS=	pfctl.c parse.y pfctl_parser.c pf_print_state.c pfctl_altq.c
SRCS+=	pfctl_osfp.c pfctl_radix.c pfctl_table.c pfctl_qstats.c
SRCS+=	pfctl_optimize.c pf_ruleset.c
CFLAGS+= -I${.CURDIR}
#CFLAGS+=-DPFDEBUG
MAN=	pfctl.8

# Ruleset and Anchor handling
.PATH: ${.CURDIR}/../../sys/net/pf

LDADD=	-lm
DPADD=	${LIBM}

WARNS?=	2

CFLAGS+=	${PRIVATELIB_CFLAGS}
DPADD+=		${LIBCRYPTO}
LDADD+=		-lprivate_crypto
LDFLAGS+=	${PRIVATELIB_LDFLAGS}

.include <bsd.prog.mk>
