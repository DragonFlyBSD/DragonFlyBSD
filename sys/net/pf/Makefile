# $DragonFly: src/sys/net/pf/Makefile,v 1.2 2004/11/09 11:10:10 joerg Exp $

KMOD=	pf
SRCS=	if_pflog.c pf.c pf_if.c pf_ioctl.c pf_norm.c pf_osfp.c pf_subr.c
SRCS+=	pf_table.c
SRCS+=	use_pflog.h use_pfsync.h opt_inet.h opt_inet6.h
SRCS+=	opt_icmp_bandlim.h
CLEANFILES= use_pflog.h use_pfsync.h
NOMAN=
CFLAGS+=  -I${.CURDIR}/../../contrib/pf

use_pflog.h:
	echo "#define NPFLOG 1" > ${.TARGET}

use_pfsync.h:
# .PATH searches only work for targes without source
.if !exists(use_pfsync.h)
	echo "#define NPFSYNC 0" > ${.TARGET}
.endif

opt_inet.h:
	echo "#define INET 1" > ${.TARGET}

opt_inet6.h:
# .PATH searches only work for targes without source
.if !exists(opt_inet6.h)
	echo "#define INET6 1" > ${.TARGET}
.endif

.include <bsd.kmod.mk>
