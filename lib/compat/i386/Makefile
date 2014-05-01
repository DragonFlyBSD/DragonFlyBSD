FILESDIR=    ${LIBCOMPATDIR}
COMPAT_LIBS= libalias.so.4 libarchive.so.1 libbz2.so.1 \
	     libc.so.4 libc_r.so.4 libcalendar.so.2 libcam.so.2 \
	     libcipher.so.2 libcom_err.so.2 libcrypt.so.2 libcrypto.so.3 \
	     libdevstat.so.2 libdialog.so.4 libedit.so.3 libfetch.so.3 \
	     libform.so.5 libftpio.so.5 libg2c.so.1 libgnuregex.so.2 \
	     libhistory.so.5 libipsec.so.1 libisc.so.1 \
	     libkcore.so.1 libkinfo.so.1 libkvm.so.2 libm.so.2 \
	     libmagic.so.1 libmd.so.2 libmenu.so.5 libmilter.so.2 \
	     libncurses.so.5 libnetgraph.so.1 libopie.so.2 \
	     libpam.so.1 libpanel.so.5 libpcap.so.2 libposix1e.so.2 \
	     libradius.so.1 libreadline.so.5 librpcsvc.so.2 libsbuf.so.2 \
	     libskey.so.2 libsmb.so.1 libssh.so.2 libssl.so.3 \
	     libtacplus.so.1 libthread_xu.so.1 libusbhid.so.0 \
	     libutil.so.3 libvgl.so.2 libwrap.so.3 libxpg4.so.3 libz.so.2 \
	     ld-elf.so.1

FILESMODE_ld-elf.so.1=	555
FILESDIR_ld-elf.so.1=	/usr/libexec

.for lib in ${COMPAT_LIBS}
LATEST_${lib}!=	cd ${.CURDIR}; echo ${lib}.* | tr " " "\n" | tail -1
FILES+=	${lib}
CLEANFILES+=	${lib}
${lib}: ${LATEST_${lib}}
	cp ${.ALLSRC} ${.TARGET}
.endfor

show:
.for lib in ${COMPAT_LIBS}
	@echo "Selecting ${LATEST_${lib}} for ${lib}"
.endfor

.include <bsd.lib.mk>
