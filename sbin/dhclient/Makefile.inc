# $FreeBSD: src/sbin/dhclient/Makefile.inc,v 1.2.2.1 2002/04/11 10:21:20 murray Exp $
# $DragonFly: src/sbin/dhclient/Attic/Makefile.inc,v 1.6 2005/01/04 19:58:54 joerg Exp $

WARNS=		0
CFLAGS+=	-I.. -I../includes -I${DIST_DIR} -I${DIST_DIR}/includes

CLIENTBINDIR=	/sbin
RUNDIR=		/var/run
ETCDIR=		/etc
DBDIR=		/var/db

MANFILTER= sed -e 's\#DBDIR\#${DBDIR}\#g;s\#ETCDIR\#${ETCDIR}\#g;s\#CLIENTBINDIR\#${CLIENTBINDIR}\#g;s\#RUNDIR\#${RUNDIR}\#g;'

.if exists(${.OBJDIR}/../common)
LIBDHCP=	${.OBJDIR}/../common/libdhcp.a
.else
LIBDHCP=	${.CURDIR}/../common/libdhcp.a
.endif

.if exists(${.OBJDIR}/../minires)
LIBRES=		${.OBJDIR}/../minires/libres.a
.else
LIBRES=		${.CURDIR}/../minires/libres.a
.endif

.if exists(${.OBJDIR}/../omapip)
LIBOMAPI=	${.OBJDIR}/../omapip/libomapi.a
.else
LIBOMAPI=	${.CURDIR}/../omapip/libomapi.a
.endif

.if exists(${.OBJDIR}/../dst)
LIBDST= 	${.OBJDIR}/../dst/libdst.a
.else
LIBDST=		${.CURDIR}/../dst/libdst.a
.endif

.include "../Makefile.inc"
