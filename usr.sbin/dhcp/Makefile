# Makefile for ISC DHCP server.
# $DragonFly: src/usr.sbin/dhcp/Attic/Makefile,v 1.1 2005/01/04 19:58:53 joerg Exp $ 

.include "Makefile.common"

BUILDING_SERVER=true
.makeenv BUILDING_SERVER

SUBDIR=	${DHCLIENT_DIR}/includes ${DHCLIENT_DIR}/common ${DHCLIENT_DIR}/dst \
	${DHCLIENT_DIR}/minires ${DHCLIENT_DIR}/omapip dhcpctl relay server

.include <bsd.subdir.mk>
