#	$OpenBSD: Makefile,v 1.15 2004/03/10 18:49:49 mcbride Exp $
#	$DragonFly: src/usr.sbin/pfctl/Makefile,v 1.2 2005/02/11 22:31:45 joerg Exp $
PROG=	pfctl
SRCS=	pfctl.c parse.y pfctl_parser.c pf_print_state.c pfctl_altq.c
SRCS+=	pfctl_osfp.c pfctl_radix.c pfctl_table.c pfctl_qstats.c
SRCS+=	pfctl_optimize.c pf_ruleset.c
CFLAGS+= -I${.CURDIR}
MAN=	pfctl.8 pf.conf.5 pf.os.5

# Ruleset and Anchor handling
.PATH: ${.CURDIR}/../../sys/net/pf

LDADD+=	-lmd -lm
DPADD+=	${LIBM}

WARNS?=	2

.include <bsd.prog.mk>
