#	from: @(#)Makefile	5.11 (Berkeley) 5/21/91
# $FreeBSD: src/etc/Makefile,v 1.219.2.38 2003/03/04 09:49:00 ru Exp $
# $DragonFly: src/etc/Makefile,v 1.105 2006/02/25 19:14:53 swildner Exp $

.if !defined(NO_SENDMAIL)
SUBDIR=	sendmail
.endif

# Files that should be installed read-only (444)
#
BINUPDATE= apmd.conf fbtab gettytab network.subr \
	pf.os \
	protocols \
	rc rc.firewall6 \
	rc.sendmail rc.shutdown \
	rc.subr rpc services \
	etc.${MACHINE_ARCH}/disktab
.if defined(BINARY_UPGRADE) # location of these depends on upgrade method
BINUPDATE+=mail.rc locate.rc
.else
BINUPDATE+=${.CURDIR}/../usr.bin/mail/misc/mail.rc \
	${.CURDIR}/../usr.bin/locate/locate/locate.rc
.endif

# Initial distribution files are installed read-write (644)
#
BIN1=	amd.map auth.conf \
	crontab csh.cshrc csh.login csh.logout \
	devices.conf dhclient.conf dm.conf ftpusers group \
	hosts hosts.allow host.conf hosts.equiv hosts.lpd \
	inetd.conf login.access login.conf \
	motd modems networks newsyslog.conf \
	pf.conf phones printcap profile \
	remote \
	shells sysctl.conf syslog.conf usbd.conf \
	etc.${MACHINE_ARCH}/ttys
.if defined(BINARY_UPGRADE) # location of these depends on upgrade method
BIN1+=	manpath.config
.else
BIN1+=	${.CURDIR}/../gnu/usr.bin/man/manpath/manpath.config
.endif

.if exists(${.CURDIR}/../crypto) && !defined(NO_OPENSSL)
.if !defined(NO_OPENSSH)
DIRS+=	secure/lib/libssh \
	secure/usr.bin/ssh \
	secure/usr.sbin/sshd
.endif
DIRS+=	secure/usr.bin/openssl
.endif

# Files that should be installed read-only-executable (555) root:wheel
#
BIN2=	pccard_ether rc.firewall rc.suspend rc.resume

DEFAULTS= rc.conf make.conf periodic.conf
PAMD_CONF=	README convert.sh ftpd gdm imap login other pop3 sshd \
	telnetd xdm xserver

MTREE=	BSD.include.dist BSD.local.dist BSD.root.dist BSD.usr.dist \
	BSD.var.dist BSD.x11.dist BSD.x11-4.dist
.if !defined(NO_SENDMAIL)
MTREE+=	BSD.sendmail.dist
.endif

NAMEDB=	PROTO.localhost.rev PROTO.localhost-v6.rev named.conf named.root \
	make-localhost getroot README

PPPCNF=	ppp.conf

ETCMAIL=Makefile README mailer.conf access.sample virtusertable.sample \
	mailertable.sample aliases

# Special top level files for FreeBSD
FREEBSD=COPYRIGHT

# List of libraries in /usr/lib/compat that might have to be removed
# from /usr/lib.
COMPAT_LIBS != cd ${DESTDIR}/usr/lib/compat && find . -name '*.so.*'

# Use this directory as the source for new configuration files when upgrading
UPGRADE_SRCDIR?=${.CURDIR}

distribute:
	cd ${.CURDIR} ; ${MAKE} distribution DESTDIR=${DISTDIR}/${DISTRIBUTION}

preupgrade:
.if !defined(NO_SENDMAIL)
	(pw -V ${DESTDIR}/etc groupshow smmsp -q > /dev/null) || \
		pw -V ${DESTDIR}/etc groupadd smmsp -g 25
	(pw -V ${DESTDIR}/etc usershow smmsp -q > /dev/null) || \
		pw -V ${DESTDIR}/etc useradd smmsp -u 25 \
		   -c "Sendmail Submission User" \
		   -d /var/spool/clientmqueue -s /sbin/nologin
.endif
	(pw -V ${DESTDIR}/etc usershow _pflogd -q > /dev/null) || \
		pw -V ${DESTDIR}/etc useradd _pflogd -u 64 \
		   -c "pflogd privsep user" \
		   -d /var/empty -s /sbin/nologin
	(pw -V ${DESTDIR}/etc usershow _ntp -q > /dev/null) || \
		pw -V ${DESTDIR}/etc useradd _ntp -u 65 \
		   -c "ntpd privsep user" \
		   -d /var/empty -s /sbin/nologin
	(pw -V ${DESTDIR}/etc groupshow authpf -q > /dev/null) || \
		pw -V ${DESTDIR}/etc groupadd authpf -g 63
	(pw -V ${DESTDIR}/etc groupshow _pflogd -q > /dev/null) || \
		pw -V ${DESTDIR}/etc groupadd _pflogd -g 64
	(pw -V ${DESTDIR}/etc groupshow _ntp -q > /dev/null) || \
		pw -V ${DESTDIR}/etc groupadd _ntp -g 65

upgrade_etc:	preupgrade
.if !defined(BINARY_UPGRADE) # binary upgrade just copies these files
	cd ${UPGRADE_SRCDIR}/../share/mk; ${MAKE} install
.endif
	cd ${UPGRADE_SRCDIR}; \
	    ${INSTALL} -o ${BINOWN} -g ${BINGRP} -m 444 \
		${BINUPDATE} ${DESTDIR}/etc; \
	    cap_mkdb ${DESTDIR}/etc/login.conf; \
	    ${INSTALL} -o ${BINOWN} -g ${BINGRP} -m 555 \
		${BIN2} ${DESTDIR}/etc;
	cd ${UPGRADE_SRCDIR}/defaults; ${INSTALL} -o ${BINOWN} -g ${BINGRP} -m 444 \
	    ${DEFAULTS} ${DESTDIR}/etc/defaults
	cd ${UPGRADE_SRCDIR}/periodic; ${MAKE} install
	mkdir -p ${DESTDIR}/etc/rc.d
	cd ${UPGRADE_SRCDIR}/rc.d; ${MAKE} install 
	# "../share/termcap/make etc-termap" expanded inline here:
	${LN} -fs ${DESTDIR}/usr/share/misc/termcap ${DESTDIR}/etc/termcap
	# "../usr.sbin/rmt/make etc-rmt" expanded inline here:
	${LN} -fs ${DESTDIR}/usr/sbin/rmt ${DESTDIR}/etc/rmt
.if !defined(BINARY_UPGRADE) # XXX not yet handled by binary upgrade
.if !defined(NO_SENDMAIL)
	cd ${UPGRADE_SRCDIR}/sendmail; ${MAKE} upgrade
.endif
.endif
.if !defined(NO_I4B)
	cd ${UPGRADE_SRCDIR}/isdn; ${MAKE} install
.endif
	cd ${UPGRADE_SRCDIR}/mtree; ${INSTALL} -o ${BINOWN} -g ${BINGRP} -m 444 \
	    ${MTREE} ${DESTDIR}/etc/mtree
.if !exists(${DESTDIR}/etc/pam.d)
	mkdir -p ${DESTDIR}/etc/pam.d
	${INSTALL} -o ${BINOWN} -g ${BINGRP} -m 644 ${UPGRADE_SRCDIR}/pam.d/README ${DESTDIR}/etc/pam.d
	${INSTALL} -o ${BINOWN} -g ${BINGRP} -m 644 ${UPGRADE_SRCDIR}/pam.d/convert.sh ${DESTDIR}/etc/pam.d
	sh ${DESTDIR}/etc/pam.d/convert.sh ${DESTDIR}/etc/pam.d ${DESTDIR}/etc/pam.conf
.else
.for pamconf in README convert.sh
.if !exists(${DESTDIR}/etc/pam.d/${pamconf})
	${INSTALL} -o ${BINOWN} -g ${BINGRP} -m 644 ${UPGRADE_SRCDIR}/pam.d/${pamconf} ${DESTDIR}/etc/pam.d
.endif
.endfor
.endif
.if !defined(BINARY_UPGRADE) # binary upgrade just copies these files
	cd ${UPGRADE_SRCDIR}/..; ${INSTALL} -o ${BINOWN} -g ${BINGRP} -m 444 \
	    ${FREEBSD} ${DESTDIR}/
.if !defined(NOMAN)
	cd ${UPGRADE_SRCDIR}/../share/man; ${MAKE} makedb
.endif
.endif
	rm -f ${DESTDIR}/usr/include/machine/ioctl_meteor.h
	rm -f ${DESTDIR}/usr/include/machine/ioctl_bt848.h
	${LN} -s "../dev/video/bktr/ioctl_bt848.h" ${DESTDIR}/usr/include/machine/ioctl_bt848.h
	${LN} -s "../dev/video/meteor/ioctl_meteor.h" ${DESTDIR}/usr/include/machine/ioctl_meteor.h
.if exists(${DESTDIR}/usr/sbin/named-checkzone)
	rm -f ${DESTDIR}/usr/libexec/named-xfer
	rm -f ${DESTDIR}/usr/bin/dnsquery
	rm -f ${DESTDIR}/usr/bin/dnskeygen
	rm -f ${DESTDIR}/usr/sbin/ndc
.endif
.if exists(${DESTDIR}/usr/lib/crtbegin.o)
	csh -c "rm -f ${DESTDIR}/usr/lib/gcc2/{crtbegin.o,crtbeginS.o,crtend.o,crtendS.o}"
	csh -c "rm -f ${DESTDIR}/usr/lib/gcc3/{crtbegin.o,crtbeginS.o,crtend.o,crtendS.o}"
.endif
.if exists(${DESTDIR}/usr/include/netproto/802_11/if_wavelan_ieee.h)
	rm -f ${DESTDIR}/usr/include/dev/netif/wi/if_wavelan_ieee.h
	rm -f ${DESTDIR}/usr/include/net/if_ieee80211.h
.endif
.if exists(${DESTDIR}/usr/lib/gcc2)
	ldconfig -m ${DESTDIR}/usr/lib/gcc2
.endif
	rm -f ${DESTDIR}/usr/sbin/pccardc ${DESTDIR}/usr/sbin/pccardd
	rm -f ${DESTDIR}/usr/share/examples/etc/defaults/pccard.conf
	csh -c "rm -f ${DESTDIR}/usr/share/man/{man,cat}5/pccard.conf.5.gz"
	csh -c "rm -f ${DESTDIR}/usr/share/man/{man,cat}8/pccardc.8.gz"
	csh -c "rm -f ${DESTDIR}/usr/share/man/{man,cat}8/pccardd.8.gz"
	csh -c "rm -f ${DESTDIR}/usr/share/man/{man,cat}8/rc.pccard.8.gz"
	rm -f ${DESTDIR}/etc/defaults/pccard ${DESTDIR}/etc/rc.d/pccard
	rm -f ${DESTDIR}/etc/defaults/pccard.conf
.if exists(${DESTDIR}/usr/libexec/gcc34) && exists(${DESTDIR}/usr/libexec/binutils215)
	rm -rf ${DESTDIR}/usr/lib/gcc3
	rm -rf ${DESTDIR}/usr/libexec/gcc3
	rm -rf ${DESTDIR}/usr/libdata/ldscripts
	rm -rf ${DESTDIR}/usr/libdata/gcc2
	rm -rf ${DESTDIR}/usr/libdata/gcc3
	rm -rf ${DESTDIR}/usr/libexec/elf
	rm -rf ${DESTDIR}/usr/libexec/aout
	rm -rf ${DESTDIR}/usr/bin/gcc2
	rm -rf ${DESTDIR}/usr/bin/gcc3
	rm -rf ${DESTDIR}/usr/include/g++
	csh -c "rm -f ${DESTDIR}/usr/bin/{genassym,gensetdefs,nawk,ptx,send-pr}"
	csh -c "rm -f ${DESTDIR}/usr/share/man/{man,cat}8/{genassym,gensetdefs}.8.gz"
	csh -c "rm -f ${DESTDIR}/usr/share/man/{man,cat}1/{cccp,gawk,nawk,send-pr}.1.gz"
	csh -c "rm -f ${DESTDIR}/usr/libexec/{cc1,cc1obj,cc1plus}"
	csh -c "rm -f ${DESTDIR}/usr/libexec/{cpp,cpp0,f771,objformat}"
	csh -c "rm -f ${DESTDIR}/usr/libexec/binutils212/{cc1,cc1obj,cc1plus}"
	csh -c "rm -f ${DESTDIR}/usr/libexec/binutils212/{cpp,cpp0,f771,objformat}"
	csh -c "rm -f ${DESTDIR}/usr/libexec/binutils214/{cc1,cc1obj,cc1plus}"
	csh -c "rm -f ${DESTDIR}/usr/libexec/binutils214/{cpp,cpp0,f771,objformat}"
	csh -c "rm -f ${DESTDIR}/usr/lib/{libgcc.a,libgcc_p.a}"
	csh -c "rm -f ${DESTDIR}/usr/lib/{libobjc.a,libobjc_p.a,libstdc++.a}"
	csh -c "rm -f ${DESTDIR}/usr/lib/{libstdc++.so,libstdc++.so.3}"
	csh -c "rm -f ${DESTDIR}/usr/lib/{libstdc++_p.a}"
	rm -rf ${DESTDIR}/usr/libexec/binutils214
.endif
	rm -f ${DESTDIR}/usr/bin/makewhatis
	rm -f ${DESTDIR}/usr/sbin/prebind
	rm -f ${DESTDIR}/modules/checkpt.ko
	csh -c "rm -rf ${DESTDIR}/usr/share/man/{man,cat}1aout"
	csh -c "rm -rf ${DESTDIR}/usr/share/man/en.ISO8859-1/{man,cat}1aout"
	rm -rf ${DESTDIR}/usr/libdata/perl
	csh -c "rm -f ${DESTDIR}/usr/bin/{find2perl,perl5,perl5.00503,pod2latex}"
	test -L ${DESTDIR}/usr/bin/perl || rm -f ${DESTDIR}/usr/bin/perl
	csh -c "rm -f ${DESTDIR}/usr/bin/{pod2text,sperl5,sperl5.00503,splain,suidperl}"
.for prog in a2p c2ph h2ph h2xs perlbug perlcc pl2pm pod2html pod2man s2p
	rm -f ${DESTDIR}/usr/bin/${prog}
	csh -c "rm -f ${DESTDIR}/usr/share/man/{man,cat}1/${prog}.1.gz"
.endfor
	csh -c "rm -f ${DESTDIR}/usr/lib/{libperl.a,libperl.so,libperl.so.3,libperl_p.a}"
	csh -c "rm -f ${DESTDIR}/usr/share/man/{man,cat}1/perl.1.gz"
.for man in 5004delta apio book bot call data debug delta diag doc dsc embed faq \
	    faq1 faq2 faq3 faq4 faq5 faq6 faq7 faq8 faq9 form func guts hist \
	    ipc locale lol mod modinstall modlib obj op opentut pod port re \
	    ref reftut run sec style sub syn thrtut tie toc toot trap var xs \
	    xstut 
	csh -c "rm -f ${DESTDIR}/usr/share/man/{man,cat}1/perl${man}.1.gz"
.endfor
	csh -c "rm -f ${DESTDIR}/usr/share/man/{man,cat}7/style.perl.7.gz"
	rm -rf ${DESTDIR}/usr/share/perl
	rm -rf ${DESTDIR}/usr/share/examples/bc
.if exists(${DESTDIR}/usr/share/info/dc.info.gz)
	gzip -d ${DESTDIR}/usr/share/info/dc.info.gz
	install-info --delete ${DESTDIR}/usr/share/info/dc.info ${DESTDIR}/usr/share/info/dir
	rm -f ${DESTDIR}/usr/share/info/dc.info
.endif
	csh -c "rm -f ${DESTDIR}/usr/include/{gmp,mp}.h"
	csh -c "rm -f ${DESTDIR}/usr/lib/{libmp.a,libmp_p.a,libmp.so,libmp.so.3}"
	csh -c "rm -f ${DESTDIR}/usr/lib/{libgmp.a,libgmp_p.a,libgmp.so,libgmp.so.3}"
.if exists(${DESTDIR}/usr/share/info/gmp.info.gz)
	gzip -d ${DESTDIR}/usr/share/info/gmp.info.gz
	install-info --delete ${DESTDIR}/usr/share/info/gmp.info ${DESTDIR}/usr/share/info/dir
	rm -f ${DESTDIR}/usr/share/info/gmp.info
.endif
	rm -f ${DESTDIR}/etc/rc.d/ntpdate
	rm -f ${DESTDIR}/usr/sbin/ntpdate
	rm -f ${DESTDIR}/modules/if_wx.ko
	csh -c "rm -f ${DESTDIR}/usr/share/man/{man,cat}4/wx.4.gz"
	rm -f ${DESTDIR}/dev/rwcd*
	rm -f ${DESTDIR}/dev/rwst*
	rm -f ${DESTDIR}/dev/wcd*
	rm -f ${DESTDIR}/dev/wd*
	rm -f ${DESTDIR}/dev/wfd*
	csh -c "rm -f ${DESTDIR}/usr/share/man/{man,cat}4/wst.4.gz"
	csh -c "rm -f ${DESTDIR}/usr/share/man/{man,cat}4/{euc,utf2}.4.gz"
	rm -f ${DESTDIR}/usr/share/syscons/keymaps/jp.pc98.iso.kbd
	rm -f ${DESTDIR}/usr/share/syscons/keymaps/jp.pc98.kbd
	csh -c "rm -f ${DESTDIR}/etc/rc.d/{bootconf.sh,lkm1,lkm2,lkm3,mountall,pcvt,wscons}"
	rm -f ${DESTDIR}/modules/canbus.ko
.for lib in ${COMPAT_LIBS:M*.so.*}
.if exists(${DESTDIR}/usr/lib/${lib})
	chflags noschg ${DESTDIR}/usr/lib/${lib}
	rm -f ${DESTDIR}/usr/lib/${lib}
.endif
.endfor
	rm -f ${DESTDIR}/usr/lib/libkeycap.a
	rm -f ${DESTDIR}/usr/lib/libkeycap_p.a
.for cmd in cursor fontedit ispcvt kcon loadfont mcon scon vt220keys vttest
	rm -f ${DESTDIR}/usr/sbin/${cmd}
.endfor
.for man in cursor fontedit kcon loadfont mcon scon vt220keys vttest
	csh -c "rm -f ${DESTDIR}/usr/share/man/{man,cat}1/${man}.1.gz"
.endfor
.for man in keycap kgetent kgetflag kgetnum kgetstr
	rm -f ${DESTDIR}/usr/share/man/{man,cat}3/${man}.3.gz
.endfor
	csh -c "rm -f ${DESTDIR}/usr/share/man/{man,cat}4/pcvt.4.gz"
	csh -c "rm -f ${DESTDIR}/usr/share/man/{man,cat}4/vt.4.gz"
	csh -c "rm -f ${DESTDIR}/usr/share/man/{man,cat}5/keycap.5.gz"
	csh -c "rm -f ${DESTDIR}/usr/share/man/{man,cat}8/ispcvt.8.gz"
	rm -f ${DESTDIR}/usr/share/misc/keycap.pcvt
	rm -rf ${DESTDIR}/usr/share/misc/fonts
	rm -rf ${DESTDIR}/usr/share/misc/pcvtfonts
	rm -rf ${DESTDIR}/usr/share/pcvt
	csh -c "rm -f ${DESTDIR}/usr/share/man/{man,cat}1/battd.1.gz"
	rm -f ${DESTDIR}/usr/include/machine/bus_pc98.h
	csh -c "rm -f ${DESTDIR}/usr/share/man/{man,cat}4/pcic.4.gz"
.if exists(${DESTDIR}/usr/share/info/bzip2.info.gz)
	gzip -d ${DESTDIR}/usr/share/info/bzip2.info.gz
	install-info --delete ${DESTDIR}/usr/share/info/bzip2.info ${DESTDIR}/usr/share/info/dir
	rm -f ${DESTDIR}/usr/share/info/bzip2.info
.endif
.if exists(${DESTDIR}/usr/share/info/gasp.info.gz)
	gzip -d ${DESTDIR}/usr/share/info/gasp.info.gz
	install-info --delete ${DESTDIR}/usr/share/info/gasp.info ${DESTDIR}/usr/share/info/dir
	rm -f ${DESTDIR}/usr/share/info/gasp.info
.endif
.if exists(${DESTDIR}/usr/share/info/gperf.info.gz)
	gzip -d ${DESTDIR}/usr/share/info/gperf.info.gz
	install-info --delete ${DESTDIR}/usr/share/info/gperf.info ${DESTDIR}/usr/share/info/dir
	rm -f ${DESTDIR}/usr/share/info/gperf.info
.endif
.if exists(${DESTDIR}/usr/share/info/gxxint.info.gz)
	gzip -d ${DESTDIR}/usr/share/info/gxxint.info.gz
	install-info --delete ${DESTDIR}/usr/share/info/gxxint.info ${DESTDIR}/usr/share/info/dir
	rm -f ${DESTDIR}/usr/share/info/gxxint.info
.endif
.if exists(${DESTDIR}/usr/share/info/iostream.info.gz)
	gzip -d ${DESTDIR}/usr/share/info/iostream.info.gz
	install-info --delete ${DESTDIR}/usr/share/info/iostream.info ${DESTDIR}/usr/share/info/dir
	rm -f ${DESTDIR}/usr/share/info/iostream.info
.endif
.if exists(${DESTDIR}/usr/share/info/ptx.info.gz)
	gzip -d ${DESTDIR}/usr/share/info/ptx.info.gz
	install-info --delete ${DESTDIR}/usr/share/info/ptx.info ${DESTDIR}/usr/share/info/dir
	rm -f ${DESTDIR}/usr/share/info/ptx.info
.endif
.if exists(${DESTDIR}/usr/share/info/send-pr.info.gz)
	gzip -d ${DESTDIR}/usr/share/info/send-pr.info.gz
	install-info --delete ${DESTDIR}/usr/share/info/send-pr.info ${DESTDIR}/usr/share/info/dir
	rm -f ${DESTDIR}/usr/share/info/send-pr.info
.endif
	rm -f ${DESTDIR}/usr/share/info/cpp2.info.gz
	rm -f ${DESTDIR}/usr/share/info/gcc2.info.gz
	rm -f ${DESTDIR}/usr/sbin/ibcs2
	rm -f ${DESTDIR}/usr/sbin/linux
	rm -f ${DESTDIR}/usr/sbin/ntp-genkeys
	rm -f ${DESTDIR}/usr/sbin/ntpdc
	rm -f ${DESTDIR}/usr/sbin/ntptime
	rm -f ${DESTDIR}/usr/sbin/ntptimeset
	rm -f ${DESTDIR}/usr/sbin/ntptrace
	rm -f ${DESTDIR}/usr/sbin/scriptdump
	rm -f ${DESTDIR}/usr/sbin/svr4
	rm -f ${DESTDIR}/usr/bin/cvsbug
	rm -f ${DESTDIR}/usr/bin/gdbreplay
	rm -f ${DESTDIR}/usr/bin/gperf
	rm -f ${DESTDIR}/usr/bin/joy
	rm -f ${DESTDIR}/usr/bin/minigzip
	rm -f ${DESTDIR}/usr/bin/ntpq
	rm -f ${DESTDIR}/usr/bin/perldoc
	rm -f ${DESTDIR}/usr/bin/sendbug
.for man in gasp gperf minigzip sendbug
	csh -c "rm -f ${DESTDIR}/usr/share/man/{man,cat}1/${man}.1.gz"
.endfor
.for man in dft_fgbg dllockinit dynamic_fieldinfo fgetrune fputrune fungetrune \
	    mbmb mbrrune mbrune menu_attribs multibyte rune setinvalidrune \
	    setrunelocale sgetrune sputrune archive_entry_gname_w \
	    archive_entry_uname_w dbm_dirnfo exp10 exp10f
	csh -c "rm -f ${DESTDIR}/usr/share/man/{man,cat}3/${man}.3.gz"
.endfor
.for man in drivers.conf gbk ntp.conf ntp.keys
	csh -c "rm -f ${DESTDIR}/usr/share/man/{man,cat}5/${man}.5.gz"
.endfor
.for man in ibcs2 joy linux ntp-genkeys ntpdate ntpdc ntpq ntptime ntptrace \
	    picobsd svr4 sysinstall cvsbug
	csh -c "rm -f ${DESTDIR}/usr/share/man/{man,cat}8/${man}.8.gz"
.endfor
	csh -c "rm -f ${DESTDIR}/usr/share/man/{man,cat}9/suser_xxx.9.gz"
	csh -c "rm -f ${DESTDIR}/usr/share/man/{man,cat}9/NDINIT.9.gz"
	rm -f ${DESTDIR}/usr/share/examples/cvsup/DragonFly-supfile
	rm -f ${DESTDIR}/usr/share/examples/cvsup/DragonFly-stable-supfile
	rm -f ${DESTDIR}/usr/share/nls/*/.cat
	rm -f ${DESTDIR}/usr/include/machine/gencount.h
	rm -f ${DESTDIR}/usr/include/machine/pcvt_ioctl.h
	rm -f ${DESTDIR}/usr/include/net/bpf_compat.h
	rm -f ${DESTDIR}/usr/include/netinet/ipprotosw.h
	rm -f ${DESTDIR}/usr/include/rune.h
	rm -f ${DESTDIR}/usr/include/runetype.h
	rm -f ${DESTDIR}/modules/if_snc.ko
.for tzfile in Africa/Timbuktu America/Argentina/ComodRivadavia \
	America/Indianapolis America/Louisville CST6CDT EST EST5EDT \
	Europe/Belfast HST MST MST7MDT PST8PDT Pacific/Yap
	rm -f ${DESTDIR}/usr/share/zoneinfo/${tzfile}
.endfor
.if exists(${DESTDIR}/usr/freebsd_pkg/sbin/pkg_add)
	csh -c "rm -f ${DESTDIR}/usr/sbin/pkg_{install,add,create,delete,info,sign,version,check,update}"
	csh -c "rm -f ${DESTDIR}/usr/share/man/man1/pkg_{add,create,delete,info,sign,version,check,update}.1.gz"
	csh -c "rm -f ${DESTDIR}/usr/share/man/cat1/pkg_{add,create,delete,info,sign,version,check,update}.1.gz"
.endif
	rm -f ${DESTDIR}/etc/rc.d/altqd
	csh -c "rm -f ${DESTDIR}/etc/rc.d/network{,1,2,3}"
	rm -f ${DESTDIR}/usr/share/examples/cvsup/DragonFly-dfports-supfile
	rm -f ${DESTDIR}/etc/periodic/weekly/400.status-pkg
	csh -c "rm -f ${DESTDIR}/usr/share/man/{man,cat}7/ports.7.gz"
	csh -c "rm -f ${DESTDIR}/usr/share/man/{man,cat}9/SPLASSERT.9.gz"
	csh -c "rm -f ${DESTDIR}/usr/share/man/{man,cat}9/spl.9.gz"
.for splman in 0 bio clock high imp net softclock softtty statclock tty vm x
	csh -c "rm -f ${DESTDIR}/usr/share/man/{man,cat}9/spl${splman}.9.gz"
.endfor
	ldconfig -R
.if !defined(BINARY_UPGRADE) # binary upgrade just copies these nodes
.if !defined(NO_MAKEDEV)
	cd ${UPGRADE_SRCDIR}; ${INSTALL} -o ${BINOWN} -g ${BINGRP} -m 555 \
	    MAKEDEV.local MAKEDEV ${DESTDIR}/dev
.if !defined(NO_MAKEDEV_RUN)
	cd ${DESTDIR}/dev; sh MAKEDEV all
.endif
.endif
.endif

distribution:
	cd ${.CURDIR}; \
	    ${INSTALL} -o ${BINOWN} -g ${BINGRP} -m 444 \
		${BINUPDATE} ${DESTDIR}/etc; \
	    ${INSTALL} -o ${BINOWN} -g ${BINGRP} -m 644 \
		${BIN1} ${DESTDIR}/etc; \
	    cap_mkdb ${DESTDIR}/etc/login.conf; \
	    ${INSTALL} -o ${BINOWN} -g ${BINGRP} -m 555 \
		${BIN2} ${DESTDIR}/etc; \
	    ${INSTALL} -o ${BINOWN} -g ${BINGRP} -m 600 \
		master.passwd nsmb.conf opieaccess ${DESTDIR}/etc; \
	    pwd_mkdb -p -d ${DESTDIR}/etc ${DESTDIR}/etc/master.passwd
	cd ${.CURDIR}/defaults; ${INSTALL} -o ${BINOWN} -g ${BINGRP} -m 644 \
	    ${DEFAULTS} ${DESTDIR}/etc/defaults
	cd ${.CURDIR}/pam.d; ${INSTALL} -o ${BINOWN} -g ${BINGRP} -m 644 \
	    ${PAMD_CONF} ${DESTDIR}/etc/pam.d
	cd ${.CURDIR}/periodic; ${MAKE} install
	cd ${.CURDIR}/rc.d; ${MAKE} install 
	cd ${.CURDIR}/../share/termcap; ${MAKE} etc-termcap
	cd ${.CURDIR}/../usr.sbin/rmt; ${MAKE} etc-rmt
.if !defined(NO_I4B)
	cd ${.CURDIR}/isdn; ${MAKE} install
.endif
.if !defined(NO_SENDMAIL)
	cd ${.CURDIR}/sendmail; ${MAKE} obj
	cd ${.CURDIR}/sendmail; ${MAKE} distribution
.endif
.for dir in ${DIRS}
.if exists(${.CURDIR}/../${dir}/Makefile.etc)
	cd ${.CURDIR}/../${dir}; ${MAKE} -f Makefile.etc obj
	cd ${.CURDIR}/../${dir}; ${MAKE} -f Makefile.etc install
.endif
.endfor
.if !defined(NO_MAKEDEV)
	cd ${.CURDIR}; ${INSTALL} -o ${BINOWN} -g ${BINGRP} -m 555 \
	    MAKEDEV.local MAKEDEV ${DESTDIR}/dev
.if !defined(NO_MAKEDEV_RUN)
	cd ${DESTDIR}/dev; sh MAKEDEV all
.endif
.endif
	cd ${.CURDIR}/root; \
	    ${INSTALL} -o ${BINOWN} -g ${BINGRP} -m 644 \
		dot.cshrc ${DESTDIR}/root/.cshrc; \
	    ${INSTALL} -o ${BINOWN} -g ${BINGRP} -m 644 \
		dot.klogin ${DESTDIR}/root/.klogin; \
	    ${INSTALL} -o ${BINOWN} -g ${BINGRP} -m 644 \
		dot.login ${DESTDIR}/root/.login; \
	    ${INSTALL} -o ${BINOWN} -g ${BINGRP} -m 644 \
		dot.profile ${DESTDIR}/root/.profile; \
	    rm -f ${DESTDIR}/.cshrc ${DESTDIR}/.profile; \
	    ${LN} ${DESTDIR}/root/.cshrc ${DESTDIR}/.cshrc; \
	    ${LN} ${DESTDIR}/root/.profile ${DESTDIR}/.profile
	cd ${.CURDIR}/mtree; ${INSTALL} -o ${BINOWN} -g ${BINGRP} -m 444 \
	    ${MTREE} ${DESTDIR}/etc/mtree
	cd ${.CURDIR}/namedb; ${INSTALL} -o ${BINOWN} -g ${BINGRP} -m 644 \
	    ${NAMEDB} ${DESTDIR}/etc/namedb
	cd ${.CURDIR}/ppp; ${INSTALL} -o root -g ${BINGRP} -m 600 \
	    ${PPPCNF} ${DESTDIR}/etc/ppp
	cd ${.CURDIR}/mail; ${INSTALL} -o ${BINOWN} -g ${BINGRP} -m 644 \
	    ${ETCMAIL} ${DESTDIR}/etc/mail
	@if [ -d ${DESTDIR}/etc/mail -a -f ${DESTDIR}/etc/mail/aliases -a \
	      ! -f ${DESTDIR}/etc/aliases ]; then \
		set -x; \
		${LN} -s mail/aliases ${DESTDIR}/etc/aliases; \
	fi
	${INSTALL} -o ${BINOWN} -g operator -m 664 /dev/null \
	    ${DESTDIR}/etc/dumpdates
	${INSTALL} -o nobody -g ${BINGRP} -m 644 /dev/null \
	    ${DESTDIR}/var/db/locate.database
	${INSTALL} -o ${BINOWN} -g ${BINGRP} -m 600 /dev/null \
	    ${DESTDIR}/var/log/auth.log
	${INSTALL} -o ${BINOWN} -g ${BINGRP} -m 600 /dev/null \
	    ${DESTDIR}/var/log/cron
	${INSTALL} -o ${BINOWN} -g ${BINGRP} -m 644 /dev/null \
	    ${DESTDIR}/var/log/lpd-errs
	${INSTALL} -o ${BINOWN} -g ${BINGRP} -m 640 /dev/null \
	    ${DESTDIR}/var/log/maillog
	${INSTALL} -o ${BINOWN} -g ${BINGRP} -m 644 /dev/null \
	    ${DESTDIR}/var/log/lastlog
	${INSTALL} -o ${BINOWN} -g ${BINGRP} -m 644 /dev/null \
	    ${DESTDIR}/var/log/messages
	${INSTALL} -o ${BINOWN} -g ${BINGRP} -m 600 /dev/null \
	    ${DESTDIR}/var/log/security
	${INSTALL} -o ${BINOWN} -g network -m 640 /dev/null \
	    ${DESTDIR}/var/log/slip.log
	${INSTALL} -o ${BINOWN} -g network -m 640 /dev/null \
	    ${DESTDIR}/var/log/ppp.log
	${INSTALL} -o ${BINOWN} -g ${BINGRP} -m 644 /dev/null \
	    ${DESTDIR}/var/log/wtmp
	${INSTALL} -o ${BINOWN} -g ${BINGRP} -m 644 /dev/null \
	    ${DESTDIR}/var/run/utmp
	${INSTALL} -o ${BINOWN} -g ${BINGRP} -m 644 ${.CURDIR}/minfree \
	    ${DESTDIR}/var/crash
	cd ${.CURDIR}/..; ${INSTALL} -o ${BINOWN} -g ${BINGRP} -m 444 \
	    ${FREEBSD} ${DESTDIR}/
.if !defined(NOMAN)
	cd ${.CURDIR}/../share/man; ${MAKE} makedb
.endif

distrib-dirs:
	-set - `grep "^[a-zA-Z]" ${.CURDIR}/locale.deprecated`; \
	while [ $$# -gt 0 ] ; \
	do \
		for dir in /usr/share/locale \
			   /usr/share/nls \
			   /usr/local/share/nls; \
		do \
			test -d ${DESTDIR}/$${dir} && cd ${DESTDIR}/$${dir}; \
			test -L "$$2" && rm -rf "$$2"; \
			test \! -L "$$1" && test -d "$$1" && mv "$$1" "$$2"; \
		done; \
		shift; shift; \
	done
	mtree -deU -f ${.CURDIR}/mtree/BSD.root.dist -p ${DESTDIR}/
	mtree -deU -f ${.CURDIR}/mtree/BSD.var.dist -p ${DESTDIR}/var
	mtree -deU -f ${.CURDIR}/mtree/BSD.usr.dist -p ${DESTDIR}/usr
	mtree -deU -f ${.CURDIR}/mtree/BSD.include.dist \
		-p ${DESTDIR}/usr/include
.if !defined(NO_SENDMAIL)
	mtree -deU -f ${.CURDIR}/mtree/BSD.sendmail.dist -p ${DESTDIR}/
.endif
	cd ${DESTDIR}/etc/namedb; rm -f etc/namedb; ${LN} -s ".." etc/namedb
	cd ${DESTDIR}/; rm -f ${DESTDIR}/sys; ${LN} -s usr/src/sys sys
	cd ${DESTDIR}/usr/share/man/en.ISO8859-1; ${LN} -sf ../man* .
	cd ${DESTDIR}/usr/share/man; \
	set - `grep "^[a-zA-Z]" ${.CURDIR}/man.alias`; \
	while [ $$# -gt 0 ] ; \
	do \
		rm -rf "$$1"; \
		${LN} -s "$$2" "$$1"; \
		shift; shift; \
	done
	cd ${DESTDIR}/usr/share/locale; \
	set - `grep "^[a-zA-Z]" ${.CURDIR}/locale.alias`; \
	while [ $$# -gt 0 ] ; \
	do \
		rm -rf "$$1"; \
		${LN} -s "$$2" "$$1"; \
		shift; shift; \
	done
	cd ${DESTDIR}/usr/share/openssl/man/en.ISO8859-1; ${LN} -sf ../man* .
	cd ${DESTDIR}/usr/share/nls; \
	set - `grep "^[a-zA-Z]" ${.CURDIR}/nls.alias`; \
	while [ $$# -gt 0 ] ; \
	do \
		rm -rf "$$1"; \
		${LN} -s "$$2" "$$1"; \
		shift; shift; \
	done

etc-examples:
	cd ${.CURDIR}; ${INSTALL} -o ${BINOWN} -g ${BINGRP} -m 444 \
	    ${BINUPDATE} ${BIN1} ${BIN2} nsmb.conf opieaccess \
	    ${DESTDIR}/usr/share/examples/etc
	cd ${.CURDIR}/defaults; ${INSTALL} -o ${BINOWN} -g ${BINGRP} -m 444 \
	    ${DEFAULTS} ${DESTDIR}/usr/share/examples/etc/defaults

.include <bsd.prog.mk>
