#	@(#)Makefile	8.1 (Berkeley) 6/4/93
# $FreeBSD: src/lib/Makefile,v 1.107.2.16 2002/10/10 19:24:35 kbyanc Exp $
# $DragonFly: src/lib/Makefile,v 1.35 2008/09/30 12:20:29 hasso Exp $

# To satisfy shared library or ELF linkage when only the libraries being
# built are visible:
#
# libcom_err must be built before libkrb and libpam.
# libcrypt must be built before libkrb and libpam.
# libmd must be built before libarchive, libatm, libopie, libradius, and libtacplus.
# libncurses must be built before libdialog, libedit and libreadline.
# libradius must be built before libpam.
# libtacplus must be built before libpam.
# libutil must be built before libpam.
# libsbuf must be built before libcam.
# libthread_xu must be built before libpthread.
# ${_libc_r} must be built before libpthread.
# libpthread must be built before libprop.
#
# Otherwise, the SUBDIR list should be in alphabetical order.

SUBDIR=	csu \
	libbluetooth libcom_err libcrypt libm libmd libarchive \
	libncurses libradius librt libtacplus libutil libsbuf \
	libalias libatm libbz2 libc ${_libc_r} \
	libcalendar libcam libcompat libdevinfo libdevstat libedit \
	libexpat libfetch \
	libftpio libipsec libipx libldns libkcore libkiconv libkinfo \
	libkvm libmagic \
	${_libmilter} ${_libncp} libnetgraph libopie libpam \
	libpcap libposix1e libsdp libthread_xu libpthread libprop \
	librpcsvc ${_libsm} \
	${_libsmb} ${_libsmdb} ${_libsmutil} libstand libtelnet libusbhid \
	${_libvgl} libwrap libxpg4 liby libypclnt libz i18n_module pam_module \
	libc_rtld libsctp libevtr libdevattr

.if exists(${.CURDIR}/compat/${MACHINE_ARCH}/Makefile)
SUBDIR+= compat/${MACHINE_ARCH}
.endif

.if !defined(NO_LIBC_R)
_libc_r=	libc_r
.endif

.if !defined(NO_SENDMAIL)
_libmilter=	libmilter
_libsm=		libsm
_libsmdb=	libsmdb
_libsmutil=	libsmutil
.endif

_libncp=	libncp
_libsmb=	libsmb
_libvgl=	libvgl

.include <bsd.subdir.mk>
