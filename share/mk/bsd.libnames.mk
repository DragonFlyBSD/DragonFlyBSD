# $FreeBSD: src/share/mk/bsd.libnames.mk,v 1.28.2.10 2002/08/08 09:33:28 ru Exp $
#
# The include file <bsd.libnames.mk> define library names. 
# Other include files (e.g. bsd.prog.mk, bsd.lib.mk) include this 
# file where necessary.

.if !target(__<bsd.init.mk>__)
.error bsd.libnames.mk cannot be included directly.
.endif

LIBALIAS?=	${DESTDIR}${LIBDIR}/libalias.a
LIBARCHIVE?=	${DESTDIR}${LIBDIR}/libarchive.a
LIBBLUETOOTH?=	${DESTDIR}${LIBDIR}/libbluetooth.a
LIBBSDXML?=	${DESTDIR}${LIBDIR}/libbsdxml.a
LIBBZ2?=	${DESTDIR}${LIBDIR}/libbz2.a
LIBC?=		${DESTDIR}${LIBDIR}/libc.a
LIBCALENDAR?=	${DESTDIR}${LIBDIR}/libcalendar.a
LIBCAM?=	${DESTDIR}${LIBDIR}/libcam.a
LIBCIPHER?=	${DESTDIR}${LIBDIR}/libcipher.a	# XXX in secure dist, not base
LIBCOM_ERR?=	${DESTDIR}${LIBDIR}/libcom_err.a
LIBCOMPAT?=	${DESTDIR}${LIBDIR}/libcompat.a
LIBCRYPT?=	${DESTDIR}${LIBDIR}/libcrypt.a
LIBCRYPTO?=	${DESTDIR}${LIBDIR}/libcrypto.a	# XXX in secure dist, not base
LIBCRYPTSETUP?=	${DESTDIR}${LIBDIR}/libcryptsetup.a
LIBCURSES?=	${DESTDIR}${LIBDIR}/libcurses.a
LIBCURSESW?=	${DESTDIR}${LIBDIR}/libcursesw.a
LIBDEVATTR?=	${DESTDIR}${LIBDIR}/libdevattr.a
LIBDEVINFO?=	${DESTDIR}${LIBDIR}/libdevinfo.a
LIBDEVMAPPER?=	${DESTDIR}${LIBDIR}/libdevmapper.a
LIBDEVSTAT?=	${DESTDIR}${LIBDIR}/libdevstat.a
LIBDIALOG?=	${DESTDIR}${LIBDIR}/libdialog.a
LIBDM?=		${DESTDIR}${LIBDIR}/libdm.a
#LIBDMSG?=	${DESTDIR}${LIBDIR}/libdmsg.a
LIBEDIT?=	${DESTDIR}${LIBDIR}/libedit.a
LIBEVTR?=	${DESTDIR}${LIBDIR}/libevtr.a
LIBFETCH?=	${DESTDIR}${LIBDIR}/libfetch.a
LIBFL?=		"don't use LIBFL, use LIBL"
LIBFORM?=	${DESTDIR}${LIBDIR}/libform.a
LIBFSID?=	${DESTDIR}${LIBDIR}/libfsid.a
LIBFTPIO?=	${DESTDIR}${LIBDIR}/libftpio.a
LIBHAMMER?=	${DESTDIR}${LIBDIR}/libhammer.a
LIBIPSEC?=	${DESTDIR}${LIBDIR}/libipsec.a
LIBKCORE?=	${DESTDIR}${LIBDIR}/libkcore.a
LIBKICONV?=	${DESTDIR}${LIBDIR}/libkiconv.a
LIBKINFO?=	${DESTDIR}${LIBDIR}/libkinfo.a
LIBKVM?=	${DESTDIR}${LIBDIR}/libkvm.a
LIBL?=		${DESTDIR}${LIBDIR}/libl.a
LIBLDNS?=	${DESTDIR}${LIBDIR}/libldns.a
LIBLN?=		"don't use LIBLN, use LIBL"
LIBLUKS?=	${DESTDIR}${LIBDIR}/libluks.a
LIBLVM?=	${DESTDIR}${LIBDIR}/liblvm.a
LIBLZMA?=	${DESTDIR}${LIBDIR}/liblzma.a
LIBM?=		${DESTDIR}${LIBDIR}/libm.a
LIBMAGIC?=	${DESTDIR}${LIBDIR}/libmagic.a
LIBMANDOC?=	${DESTDIR}${LIBDIR}/libmandoc.a
LIBMD?=		${DESTDIR}${LIBDIR}/libmd.a
LIBMENU?=	${DESTDIR}${LIBDIR}/libmenu.a
.if !defined(NO_SENDMAIL)
LIBMILTER?=	${DESTDIR}${LIBDIR}/libmilter.a
.endif
LIBMYTINFO?=	"don't use LIBMYTINFO, use LIBNCURSES"
LIBNCURSES?=	${DESTDIR}${LIBDIR}/libncurses.a
LIBNCURSESW?=	${DESTDIR}${LIBDIR}/libncursesw.a
LIBNETGRAPH?=	${DESTDIR}${LIBDIR}/libnetgraph.a
LIBOPIE?=	${DESTDIR}${LIBDIR}/libopie.a

# The static PAM library doesn't know its secondary dependencies,
# so we have to specify them explictly.
LIBPAM?=	${DESTDIR}${LIBDIR}/libpam.a
MINUSLPAM?=	-lpam
.if defined(NOSHARED) && ${NOSHARED} != "no" && ${NOSHARED} != "NO"
LIBPAM+=	${LIBRADIUS} ${LIBTACPLUS} ${LIBOPIE} ${LIBCRYPT} ${LIBMD} \
		${LIBUTIL}
MINUSLPAM+=	-lradius -ltacplus -lopie -lcrypt -lmd -lutil
.endif

LIBPANEL?=	${DESTDIR}${LIBDIR}/libpanel.a
LIBPCAP?=	${DESTDIR}${LIBDIR}/libpcap.a
LIBPOSIX1E?=	${DESTDIR}${LIBDIR}/libposix1e.a
LIBPROP?=	${DESTDIR}${LIBDIR}/libprop.a
LIBPUFFS?=	${DESTDIR}${LIBDIR}/libpuffs.a
LIBRADIUS?=	${DESTDIR}${LIBDIR}/libradius.a
LIBREFUSE?=	${DESTDIR}${LIBDIR}/librefuse.a
LIBRPCSVC?=	${DESTDIR}${LIBDIR}/librpcsvc.a
LIBRT?=		${DESTDIR}${LIBDIR}/librt.a
LIBSBUF?=	${DESTDIR}${LIBDIR}/libsbuf.a
LIBSCTP?=	${DESTDIR}${LIBDIR}/libsctp.a
LIBSDP?=	${DESTDIR}${LIBDIR}/libsdp.a
LIBSMB?=	${DESTDIR}${LIBDIR}/libsmb.a
LIBSSH?=	${DESTDIR}${LIBDIR}/libssh.a	# XXX in secure dist, not base
LIBSSL?=	${DESTDIR}${LIBDIR}/libssl.a	# XXX in secure dist, not base
LIBSTAND?=	${DESTDIR}${LIBDIR}/libstand.a
LIBTACPLUS?=	${DESTDIR}${LIBDIR}/libtacplus.a
LIBTCPLAY?=	${DESTDIR}${LIBDIR}/libtcplay.a
LIBTERMCAP?=	${DESTDIR}${LIBDIR}/libtermcap.a
LIBTERMLIB?=	"don't use LIBTERMLIB, use LIBTERMCAP"
LIBTINFO?=	"don't use LIBTINFO, use LIBNCURSES"
LIBUSBHID?=	${DESTDIR}${LIBDIR}/libusbhid.a
LIBUSB?=	${DESTDIR}${LIBDIR}/libusb.a
LIBUTIL?=	${DESTDIR}${LIBDIR}/libutil.a
LIBVGL?=	${DESTDIR}${LIBDIR}/libvgl.a
LIBWRAP?=	${DESTDIR}${LIBDIR}/libwrap.a
LIBY?=		${DESTDIR}${LIBDIR}/liby.a
LIBYPCLNT?=	${DESTDIR}${LIBDIR}/libypclnt.a
LIBZ?=		${DESTDIR}${LIBDIR}/libz.a

LIBGCC?=	${DESTDIR}${GCCLIBDIR}/libgcc.a
LIBGCC_PIC?=	${DESTDIR}${GCCLIBDIR}/libgcc_pic.a
LIBGCOV?=	${DESTDIR}${GCCLIBDIR}/libgcov.a
LIBGCOV_PIC?=	${DESTDIR}${GCCLIBDIR}/libgcov_pic.a
LIBOBJC?=	${DESTDIR}${GCCLIBDIR}/libobjc.a
LIBSTDCPLUSPLUS?= ${DESTDIR}${GCCLIBDIR}/libstdc++.a
LIBSUPCPLUSPLUS?= ${DESTDIR}${GCCLIBDIR}/libsupc++.a

THREAD_LIB?=	thread_xu
LIBPTHREAD?=	${DESTDIR}${LIBDIR}/thread/lib${THREAD_LIB}.a
