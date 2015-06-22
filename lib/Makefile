#	@(#)Makefile	8.1 (Berkeley) 6/4/93
# $FreeBSD: src/lib/Makefile,v 1.107.2.16 2002/10/10 19:24:35 kbyanc Exp $

# To satisfy shared library or ELF linkage when only the libraries being
# built are visible:
#
# ${_libc_r} must be built before libpthread.
# libcom_err must be built before libpam.
# libcrypt must be built before libpam.
# libmd must be built before libarchive, libopie, libradius, and libtacplus.
# libncurses must be built before libdialog and libedit.
# libradius must be built before libpam.
# libsbuf must be built before libcam.
# libtacplus must be built before libpam.
# libthread_xu must be built before libpthread.
# libutil must be built before libpam.
# libpthread must be built before libprop.
#
# Otherwise, the SUBDIR list should be in alphabetical order.

SUBDIR_ORDERED=	${_libc_r} \
	libcom_err \
	libcrypt \
	libmd \
	libncurses \
	libradius \
	libsbuf \
	libtacplus \
	libthread_xu \
	libutil \
	libpthread
SUBDIR=	${SUBDIR_ORDERED} \
	csu \
	i18n_module \
	libalias \
	libarchive \
	libbluetooth \
	libbz2 \
	libc \
	libc_rtld \
	libcalendar \
	libcam \
	libcompat \
	libdevattr \
	libdevinfo \
	libdevstat \
	libdl \
	libdm \
	${xxxlibdmsg} \
	libedit \
	libevtr \
	libexpat \
	libfetch \
	libfsid \
	libftpio \
	libhammer \
	libipfw3 \
	libipsec \
	libkcore \
	libkiconv \
	libkinfo \
	libkvm \
	libldns \
	liblzma \
	libm \
	libmagic \
	libmandoc \
	${_libnetgraph} \
	libopie \
	libpam \
	libpcap \
	libposix1e \
	libprop \
	libpuffs \
	librefuse \
	librpcsvc \
	librt \
	libsdp \
	${_libsm} \
	libsmb \
	${_libsmdb} \
	${_libsmutil} \
	libstand \
	libtcplay \
	libtelnet \
	libusb \
	libu4bhid \
	libvgl \
	libwrap \
	liby \
	libypclnt \
	libz \
	pam_module

.if !defined(NO_LIBC_R)
_libc_r=	libc_r
.endif

.if defined(WANT_NETGRAPH7) 
_libnetgraph=	libnetgraph7
.else
_libnetgraph=	libnetgraph
.endif

.include <bsd.subdir.mk>
