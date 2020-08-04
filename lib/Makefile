#	@(#)Makefile	8.1 (Berkeley) 6/4/93
# $FreeBSD: src/lib/Makefile,v 1.107.2.16 2002/10/10 19:24:35 kbyanc Exp $

# To satisfy shared library or ELF linkage when only the libraries being
# built are visible:
#
# [Handled by /usr/src/Makefile.inc1, before this Makefile triggers]
#    libthread_xu must be built before libpthread.
#    libprop must be built before libdevattr
#
# libcrypt must be built before libfetch, libarchive.
# libz must be built before libarchive, libmagic, libssh.
# libbz2 must be built before libarchive
# liblmza must be built before libarchive.
# libncurses must be built before libedit.
# libsbuf must be built before libcam.
# libpcap must be built before libipfw3.
# libpthread must be built before librt, and libusb.
# libutil must be built before libssh.
# libldns must be built before libssh.
#
# Otherwise, the SUBDIR list should be in alphabetical order.

SUBDIR_ORDERED= \
	librecrypto \
	libressl \
	libcrypt \
	libbz2 \
	liblzma \
	libz \
	libutil \
	libypclnt \
	libncurses \
	libsbuf \
	libthread_xu \
	libpthread \
	libfetch \
	libedit \
	libcam \
	libpcap \
	libprop \
	librt \
	libusb \
	libexecinfo \
	libldns \
	libssh

SUBDIR=	${SUBDIR_ORDERED} \
	csu \
	i18n_module \
	libalias \
	libarchive \
	libbluetooth \
	libc \
	libc_rtld \
	libcalendar \
	libcompat \
	libdevattr \
	libdevinfo \
	libdevstat \
	libdl \
	libdm \
	${_libdmsg} \
	libefivar \
	libevtr \
	libexpat \
	libfsid \
	libftpio \
	libhammer \
	libipfw3 \
	libkcore \
	libkiconv \
	libkinfo \
	libkvm \
	libm \
	libmagic \
	${_libnetgraph} \
	libpam \
	libposix1e \
	librpcsvc \
	libsdp \
	${_libsm} \
	libsmb \
	${_libsmdb} \
	${_libsmutil} \
	libstand \
	libtcplay \
	libtelnet \
	libu4bhid \
	libvgl \
	libwrap \
	liby

.if make(install)
SUBDIR:=	libc ${SUBDIR:Nlibc}
.endif

_libdmsg=	libdmsg

.if defined(WANT_NETGRAPH7) 
_libnetgraph=	libnetgraph7
.else
_libnetgraph=	libnetgraph
.endif

.include <bsd.subdir.mk>
