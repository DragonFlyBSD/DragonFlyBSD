# $FreeBSD: src/sbin/dhclient/Makefile.inc,v 1.2.2.1 2002/04/11 10:21:20 murray Exp $
# $DragonFly: src/usr.sbin/dhcp/Attic/Makefile.inc,v 1.1 2005/01/04 19:58:53 joerg Exp $

WARNS=		0
CFLAGS+=	-I${.OBJDIR}/../${DHCLIENT_DIR} -I${.OBJDIR}/../${DHCLIENT_DIR}/includes -I${DIST_DIR} -I${DIST_DIR}/includes

CLIENTBINDIR=	/sbin
RUNDIR=		/var/run
ETCDIR=		/etc
DBDIR=		/var/db

MANFILTER=	sed -e 's\#DBDIR\#${DBDIR}\#g;s\#ETCDIR\#${ETCDIR}\#g;s\#CLIENTBINDIR\#${CLIENTBINDIR}\#g;s\#RUNDIR\#${RUNDIR}\#g'

LIBDHCP=	${.OBJDIR}/../${DHCLIENT_DIR}/common/libdhcp.a

LIBRES=		${.OBJDIR}/../${DHCLIENT_DIR}/minires/libres.a

LIBOMAPI=	${.OBJDIR}/../${DHCLIENT_DIR}/omapip/libomapi.a

LIBDST= 	${.OBJDIR}/../${DHCLIENT_DIR}/dst/libdst.a

.include "../Makefile.inc"
