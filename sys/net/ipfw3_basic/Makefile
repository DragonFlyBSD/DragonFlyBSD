KMOD=	ipfw3_basic
SRCS=	ip_fw3_basic.c
SRCS+=	ip_fw3_log.c
SRCS+=  ip_fw3_state.c
SRCS+=  ip_fw3_table.c
SRCS+=  ip_fw3_sync.c
SRCS+=	opt_ipfw.h opt_inet.h

.ifndef BUILDING_WITH_KERNEL
opt_inet.h:
	echo '#define INET 1' > ${.OBJDIR}/${.TARGET}

opt_ipfw.h:
	echo '#define IPFIREWALL3 1' > ${.OBJDIR}/${.TARGET}
	# Debug prints
	#echo '#define IPFIREWALL_DEBUG 1' >> ${.OBJDIR}/${.TARGET}
	#
	# If you want it verbose
	#echo '#define IPFIREWALL_VERBOSE 1' >> ${.OBJDIR}/${.TARGET}
	#
	# If you want it to pass all packets by default
	#echo '#define IPFIREWALL_DEFAULT_TO_ACCEPT 1' >> ${.OBJDIR}/${.TARGET}
.endif

.include <bsd.kmod.mk>
