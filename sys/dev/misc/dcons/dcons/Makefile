# $Id: Makefile,v 1.6 2003/10/24 15:41:26 simokawa Exp $
# $FreeBSD: src/sys/modules/dcons/Makefile,v 1.2 2004/07/13 09:37:49 simokawa Exp $
# $DragonFly: src/sys/dev/misc/dcons/dcons/Makefile,v 1.4 2005/02/18 11:41:42 corecode Exp $

.PATH:	${.CURDIR}/..

KMOD	= dcons
SRCS	= dcons.c dcons_os.c
SRCS	+= dcons.h dcons_os.h opt_dcons.h opt_ddb.h opt_comconsole.h

.if !defined(BUILDING_WITH_KERNEL)
opt_ddb.h:
	echo "#define DDB 1" > ${.TARGET}
.endif

opt_comconsole.h:
	echo "#define ALT_BREAK_TO_DEBUGGER 1" > ${.TARGET}

.include <bsd.kmod.mk>
