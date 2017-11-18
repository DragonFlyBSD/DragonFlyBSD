#	@(#)Makefile	8.1 (Berkeley) 6/4/93
# $FreeBSD: src/lib/Makefile,v 1.107.2.16 2002/10/10 19:24:35 kbyanc Exp $

# To satisfy shared library or ELF linkage when only the libraries being
# built are visible:
#
# [Handled by /usr/src/Makefile.inc1, before this Makefile triggers]
#    libmd must be built before libopie, libradius, libtacplus
#    libthread_xu|libc_r must be built before libpthread.
#    libpthread must be built before liblmza, libprop.
#    libprop must be built before libdevattr
#
# libmd must be built before libfetch, libarchive.
# libcrypt must be built before libfetch, libarchive, and libradius.
# libz must be built before libarchive, libmagic.
# libbz2 must be built before libarchive
# libncurses must be built before libedit.
# libsbuf must be built before libcam.
# libpthread must be built before librt, and libusb.
# liblmza must be built before libarchive.
# libelf must be built before libexecinfo.
#
# Otherwise, the SUBDIR list should be in alphabetical order.

SUBDIR_ORDERED= \
	${_libc_r} \
	librecrypto \
	libressl \
	libssh \
	libcrypt \
	libarchive \
	libz \
	libmd \
	libncurses \
	libradius \
	libsbuf \
	libtacplus \
	libthread_xu \
	libutil \
	libpthread \
	liblzma \
	libfetch \
	libedit \
	libcam \
	librt \
	libusb \
	libelf \
	libexecinfo

SUBDIR=	${SUBDIR_ORDERED} \
	libcipher \
	csu \
	i18n_module \
	libalias \
	libbluetooth \
	libbz2 \
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
	libipsec \
	libkcore \
	libkiconv \
	libkinfo \
	libkvm \
	libm \
	libmagic \
	${_libnetgraph} \
	libopie \
	libpam \
	libpcap \
	libposix1e \
	libprop \
	libpuffs \
	librefuse \
	librpcsvc \
	libsdp \
	${_libsm} \
	libsmb \
	${_libsmdb} \
	${_libsmutil} \
	libstand \
	libtelnet \
	libu4bhid \
	libvgl \
	libwrap \
	liby \
	libypclnt

.if make(install)
SUBDIR:=	libc ${SUBDIR:Nlibc}
.endif

_libdmsg=	libdmsg

SUBDIR+=	libldns \
		libtcplay

.if !defined(NO_LIBC_R)
_libc_r=	libc_r
.endif

.if defined(WANT_NETGRAPH7) 
_libnetgraph=	libnetgraph7
.else
_libnetgraph=	libnetgraph
.endif

.include <bsd.subdir.mk>
