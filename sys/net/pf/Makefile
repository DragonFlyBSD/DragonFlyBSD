# $DragonFly: src/sys/net/pf/Makefile,v 1.1 2004/09/19 22:32:47 joerg Exp $

KMOD=	pf
SRCS=	if_pflog.c pf.c pf_if.c pf_ioctl.c pf_norm.c pf_osfp.c pf_subr.c
SRCS+=	pf_table.c
SRCS+=	use_pflog.h use_pfsync.h opt_inet.h opt_inet6.h use_bpf.h
SRCS+=	opt_icmp_bandlim.h
CLEANFILES= use_pflog.h use_pfsync.h use_bpf.h
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

use_bpf.h:
	echo "#define NBPF 1" > ${.TARGET}

.include <bsd.kmod.mk>
