# The files usbdevs.h and usbdevs_data.h are generated from usbdevs
#
# $FreeBSD: src/sys/dev/usb/Makefile.usbdevs,v 1.1.2.2 2000/05/07 14:36:41 n_hibma Exp $
# $DragonFly: src/sys/bus/usb/Makefile,v 1.3 2003/08/15 01:19:54 dillon Exp $

AWK=	awk
UNAME=	uname
RM=	rm

# The targets are always remade.

.PHONY=	regen

all:
	@echo "Only the 'regen' target may be used to regenerate the"
	@echo "usbdevs.h and usbdevs_data.h header files"

regen:	usbdevs devlist2h.awk
	/bin/rm -f usbdevs.h usbdevs_data.h
	${AWK} -v type=USB -v os=`${UNAME} -s` -f devlist2h.awk usbdevs

