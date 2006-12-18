# $FreeBSD: src/sys/modules/ipfilter/Makefile,v 1.2.2.2 2001/06/07 05:11:44 jlemon Exp $ 
# $DragonFly: src/sys/net/ipfilter/Makefile,v 1.4 2006/12/18 23:26:36 pavalos Exp $ 

.PATH: ${.CURDIR}/../../contrib/ipfilter/netinet
KMOD=	ipl
SRCS=	mlfk_ipl.c ip_nat.c ip_frag.c ip_state.c ip_proxy.c ip_auth.c \
	ip_log.c ip_fil.c fil.c opt_inet6.h
NOMAN=

# XXX:  Building this outside of a buildkernel with a kernel that does not have
# INET6 is not supported since the module will complain about undefined symbols
# when loaded.
.if !defined(BUILDING_WITH_KERNEL)
opt_inet6.h:
	${ECHO} "#define INET6   1" > ${.TARGET}
.endif

CFLAGS+= -include ipf_inet6.h
CFLAGS+= -DIPFILTER_LKM -DIPFILTER_LOG
CFLAGS+= -I${.CURDIR}/../../contrib/ipfilter -I${.CURDIR}
#
# If you don't want log functionality remove -DIPFILTER_LOG
#

.include <bsd.kmod.mk>
