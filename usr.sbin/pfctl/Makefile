#	$OpenBSD: Makefile,v 1.15 2004/03/10 18:49:49 mcbride Exp $
#	$DragonFly: src/usr.sbin/pfctl/Makefile,v 1.1 2004/09/21 21:25:28 joerg Exp $

PROG=	pfctl
SRCS=	pfctl.c parse.y pfctl_parser.c pf_print_state.c # pfctl_altq.c
SRCS+=	pfctl_osfp.c pfctl_radix.c pfctl_table.c # pfctl_qstats.c
CFLAGS+= -I${.CURDIR}
MAN=	pfctl.8 pf.conf.5 pf.os.5
WARNS?=	6

LDADD+=	-lmd -lm
DPADD+=	${LIBM}

.include <bsd.prog.mk>
