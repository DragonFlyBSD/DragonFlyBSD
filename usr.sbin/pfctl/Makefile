#	$OpenBSD: Makefile,v 1.15 2004/03/10 18:49:49 mcbride Exp $

PROG=	pfctl
SRCS=	pfctl.c parse.y pfctl_parser.c pf_print_state.c pfctl_altq.c
SRCS+=	pfctl_osfp.c pfctl_radix.c pfctl_table.c pfctl_qstats.c
SRCS+=	pfctl_optimize.c pf_ruleset.c
CFLAGS+= -I${.CURDIR}
MAN=	pfctl.8 pf.conf.5 pf.os.5

# Ruleset and Anchor handling
.PATH: ${.CURDIR}/../../sys/net/pf

LDADD+=	-lmd -lm
DPADD+=	${LIBMD} ${LIBM}

WARNS?=	2

# XXX sys/md5.h shim errata for bootstrap REMOVE_OPENSSL_FILES
CFLAGS+= -I${_SHLIBDIRPREFIX}/usr/include/priv

.include <bsd.prog.mk>
