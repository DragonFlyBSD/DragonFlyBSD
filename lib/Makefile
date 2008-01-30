#	@(#)Makefile	8.1 (Berkeley) 6/4/93
# $FreeBSD: src/lib/Makefile,v 1.107.2.16 2002/10/10 19:24:35 kbyanc Exp $
# $DragonFly: src/lib/Makefile,v 1.33 2008/01/30 12:57:50 hasso Exp $

# To satisfy shared library or ELF linkage when only the libraries being
# built are visible:
#
# csu must be built before all shared libaries for ELF.
# libcom_err must be built before libkrb and libpam.
# libcrypt must be built before libkrb, libpam and libskey.
# libmd must be built before libatm, libopie, libradius, libskey, and
# libtacplus.
# libncurses must be built before libdialog, libedit and libreadline.
# libradius must be built before libpam.
# libskey must be built before libpam.
# libtacplus must be built before libpam.
# libutil must be built before libpam.
# libsbuf must be built before libcam.
# libthread_xu must be built before libpthread.
# ${_libc_r} must be built before libpthread.
#
# Otherwise, the SUBDIR list should be in alphabetical order.

SUBDIR=	libarchive libbluetooth libcom_err libcrypt libm libmd \
	libncurses libradius libskey libtacplus libutil libsbuf \
	libalias libatm ${_libbind} ${_libbind9} libbz2 libc ${_libc_r} \
	libcalendar libcam libcompat libdevstat libedit libevent libfetch \
	libftpio libipsec libipx libisc libkcore libkinfo libkvm libmagic \
	${_libmilter} ${_libncp} libnetgraph libopie libpam \
	libpcap libposix1e libsdp libthread_xu libpthread librpcsvc ${_libsm} \
	${_libsmb} ${_libsmdb} ${_libsmutil} libstand libtelnet libusbhid \
	${_libvgl} libwrap libxpg4 liby libz i18n_module pam_module \
	libc_rtld libsctp

.if exists(${.CURDIR}/compat/${MACHINE_ARCH}/Makefile)
SUBDIR+= compat/${MACHINE_ARCH}
.endif

.if !defined(NO_LIBC_R)
_libc_r=	libc_r
.endif

.if !defined(NO_BIND)
_libbind=	libbind
_libbind9=	libbind9
.endif

.if !defined(NO_SENDMAIL)
_libmilter=	libmilter
_libsm=		libsm
_libsmdb=	libsmdb
_libsmutil=	libsmutil
.endif

.if ${MACHINE_ARCH} == "i386"
_libncp=	libncp
_libsmb=	libsmb
_libvgl=	libvgl
.endif

.include <bsd.subdir.mk>
