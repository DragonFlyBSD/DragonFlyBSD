#
#	pccardc Makefile
#
# $FreeBSD: src/usr.sbin/pccard/pccardc/Makefile,v 1.11.2.1 2001/04/25 12:10:23 ru Exp $
# $DragonFly: src/usr.sbin/pccard/pccardc/Attic/Makefile,v 1.2 2003/06/17 04:29:59 dillon Exp $
#
PROG=	pccardc
SRCS=	beep.c dumpcis.c enabler.c pccardc.c pccardmem.c power.c printcis.c \
	rdattr.c rdmap.c rdreg.c readcis.c wrattr.c wrreg.c
MAN=	pccardc.8

CFLAGS+=	-I${.CURDIR}/../pccardd

.include <bsd.prog.mk>
.PATH:	${.CURDIR}/../pccardd
