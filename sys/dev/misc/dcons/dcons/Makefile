# $Id: Makefile,v 1.6 2003/10/24 15:41:26 simokawa Exp $
# $FreeBSD: src/sys/modules/dcons/Makefile,v 1.2 2004/07/13 09:37:49 simokawa Exp $
# $DragonFly: src/sys/dev/misc/dcons/dcons/Makefile,v 1.2 2004/09/23 06:18:46 simokawa Exp $

.PATH:	${.CURDIR}/..

KMOD	= dcons
SRCS	= dcons.c dcons.h opt_dcons.h opt_ddb.h opt_comconsole.h

opt_ddb.h:
	echo "#define DDB 1" > $@

opt_comconsole.h:
	echo "#define ALT_BREAK_TO_DEBUGGER 1" > $@

.include <bsd.kmod.mk>
