#	from: @(#)Makefile	5.11 (Berkeley) 5/21/91
# $FreeBSD: src/etc/Makefile,v 1.219.2.38 2003/03/04 09:49:00 ru Exp $
# $DragonFly: src/etc/Makefile,v 1.37 2005/01/10 19:56:17 joerg Exp $

.if !defined(NO_SENDMAIL)
SUBDIR=	sendmail
.endif

# Files that should be installed read-only (444)
#
BINUPDATE= apmd.conf fbtab gettytab network.subr \
	pf.os \
	protocols \
	rc rc.firewall rc.firewall6 \
	rc.sendmail rc.shutdown \
	rc.subr rpc services \
	etc.${MACHINE_ARCH}/disktab \
	${.CURDIR}/../usr.bin/mail/misc/mail.rc \
	${.CURDIR}/../usr.bin/locate/locate/locate.rc

# Initial distribution files are installed read-write (644)
#
BIN1=	amd.map auth.conf \
	crontab csh.cshrc csh.login csh.logout \
	dhclient.conf dm.conf ftpusers group \
	hosts hosts.allow host.conf hosts.equiv hosts.lpd \
	inetd.conf login.access login.conf \
	motd modems networks newsyslog.conf \
	pam.conf pf.conf phones printcap profile \
	remote \
	shells sysctl.conf syslog.conf usbd.conf \
	etc.${MACHINE_ARCH}/ttys \
	${.CURDIR}/../gnu/usr.bin/man/manpath/manpath.config

.if exists(${.CURDIR}/../crypto) && !defined(NO_OPENSSL)
.if !defined(NO_OPENSSH)
SSH=	lib/libssh \
	usr.bin/ssh \
	usr.sbin/sshd
.endif
SSL=	${.CURDIR}/../crypto/openssl-0.9.7e/apps/openssl.cnf
.endif

# Files that should be installed read-only-executable (555) root:wheel
#
BIN2=	pccard_ether rc.suspend rc.resume

DEFAULTS= rc.conf make.conf pccard.conf periodic.conf

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

distribute:
	cd ${.CURDIR} ; ${MAKE} distribution DESTDIR=${DISTDIR}/${DISTRIBUTION}

preupgrade:
.if !defined(NO_SENDMAIL)
	(pw groupshow smmsp -q > /dev/null) || \
		pw groupadd smmsp -g 25
	(pw usershow smmsp -q > /dev/null) || \
		pw useradd smmsp -u 25 -c "Sendmail Submission User" \
		   -d /var/spool/clientmqueue -s /sbin/nologin
.endif
	(pw usershow _pflogd -q > /dev/null) || \
		pw useradd _pflogd -u 64 -c "pflogd privsep user" \
		   -d /var/empty -s /sbin/nologin
	(pw usershow _ntp -q > /dev/null) || \
		pw useradd _ntp -u 65 -c "ntpd privsep user" \
		   -d /var/empty -s /sbin/nologin
	(pw groupshow authpf -q > /dev/null) || \
		pw groupadd authpf -g 63
	(pw groupshow _pflogd -q > /dev/null) || \
		pw groupadd _pflogd -g 64
	(pw groupshow _ntp -q > /dev/null) || \
		pw groupadd _ntp -g 65

upgrade_etc:	preupgrade
	cd ${.CURDIR}/../share/mk; ${MAKE} install
	cd ${.CURDIR}; \
	    ${INSTALL} -o ${BINOWN} -g ${BINGRP} -m 444 \
		${BINUPDATE} ${DESTDIR}/etc; \
	    cap_mkdb ${DESTDIR}/etc/login.conf; \
	    ${INSTALL} -o ${BINOWN} -g ${BINGRP} -m 555 \
		${BIN2} ${DESTDIR}/etc;
	cd ${.CURDIR}/defaults; ${INSTALL} -o ${BINOWN} -g ${BINGRP} -m 444 \
	    ${DEFAULTS} ${DESTDIR}/etc/defaults
	cd ${.CURDIR}/periodic; ${MAKE} install
	cd ${.CURDIR}/rc.d; ${MAKE} install 
	cd ${.CURDIR}/../share/termcap; ${MAKE} etc-termcap
	cd ${.CURDIR}/../usr.sbin/rmt; ${MAKE} etc-rmt
.if !defined(NO_I4B)
	cd ${.CURDIR}/isdn; ${MAKE} install
.endif
.if !defined(NO_MAKEDEV)
	cd ${.CURDIR}; ${INSTALL} -o ${BINOWN} -g ${BINGRP} -m 555 \
	    MAKEDEV.local MAKEDEV ${DESTDIR}/dev
.if !defined(NO_MAKEDEV_RUN)
	cd ${DESTDIR}/dev; sh MAKEDEV all
.endif
.endif
	cd ${.CURDIR}/mtree; ${INSTALL} -o ${BINOWN} -g ${BINGRP} -m 444 \
	    ${MTREE} ${DESTDIR}/etc/mtree
	cd ${.CURDIR}/..; ${INSTALL} -o ${BINOWN} -g ${BINGRP} -m 444 \
	    ${FREEBSD} ${DESTDIR}/
.if !defined(NOMAN)
	cd ${.CURDIR}/../share/man; ${MAKE} makedb
.endif
	rm -f ${DESTDIR}/usr/include/machine/ioctl_meteor.h
	rm -f ${DESTDIR}/usr/include/machine/ioctl_bt848.h
	ln -s "../dev/video/bktr/ioctl_bt848.h" ${DESTDIR}/usr/include/machine/ioctl_bt848.h
	ln -s "../dev/video/meteor/ioctl_meteor.h" ${DESTDIR}/usr/include/machine/ioctl_meteor.h
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
	ldconfig -m ${DESTDIR}/usr/lib/gcc2
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
	csh -c "rm -f ${DESTDIR}/usr/share/man/{man8,cat8}/{genassym,gensetdefs}.8.gz"
	csh -c "rm -f ${DESTDIR}/usr/share/man/{man1,cat1}/{gawk,nawk,send-pr}.1.gz"
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
	csh -c "rm -f ${DESTDIR}/usr/bin/CC"
	rm -rf ${DESTDIR}/usr/libexec/binutils214
.endif
	rm -f ${DESTDIR}/usr/bin/makewhatis
	rm -f ${DESTDIR}/usr/sbin/prebind
	rm -f ${DESTDIR}/modules/checkpt.ko
	rm -rf ${DESTDIR}/usr/libdata/perl
	csh -c "rm -f ${DESTDIR}/usr/bin/{find2perl,perl5,perl5.00503,pod2latex}"
	csh -c "rm -f ${DESTDIR}/usr/bin/{pod2text,sperl5,sperl5.00503,splain,suidperl}"
.for prog in a2p c2ph h2ph h2xs perlbug perlcc pl2pm pod2html pod2man s2p
	rm -f ${DESTDIR}/usr/bin/${prog}
	rm -f ${DESTDIR}/usr/share/man/man1/${prog}.1.gz
.endfor
	csh -c "rm -f ${DESTDIR}/usr/lib/{libperl.a,libperl.so,libperl.so.3,libperl_p.a}"
	rm -f ${DESTDIR}/usr/share/man/man1/perl.1.gz
.for man in 5004delta apio book bot call data debug delta diag dsc embed faq \
	    faq.1 faq2 faq3 faq4 faq5 faq6 faq7 faq8 faq9 form func guts hist \
	    ipc locale lol mod modinstall modlib obj op opentut pod port re \
	    ref reftut run sec style sub syn thrtut tie toc toot trap var xs \
	    xstut 
	rm -f ${DESTDIR}/usr/share/man/man1/perl${man}.1.gz
.endfor
	rm -rf ${DESTDIR}/usr/share/perl
	rm -rf ${DESTDIR}/usr/share/examples/bc
.if exists(${DESTDIR}/usr/share/info/dc.info.gz)
	gzip -d ${DESTDIR}/usr/share/info/dc.info.gz
	install-info --delete ${DESTDIR}/usr/share/info/dc.info ${DESTDIR}/usr/share/info/dir
	rm -f ${DESTDIR}/usr/share/info/dc.info ${DESTDIR}/usr/share/info/dc.info.gz
.endif
	ldconfig -R

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
.if exists(${.CURDIR}/../crypto) && !defined(NO_OPENSSL)
.if !defined(NO_OPENSSH)
.for _f in ${SSH}
	cd ${.CURDIR}/../secure/${_f} && ${MAKE} obj
	cd ${.CURDIR}/../secure/${_f} && ${MAKE} etc-ssh
.endfor
.endif
	cd ${.CURDIR}; ${INSTALL} -o ${BINOWN} -g ${BINGRP} -m 644 \
	    ${SSL} ${DESTDIR}/etc/ssl
.endif
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
	    ln ${DESTDIR}/root/.cshrc ${DESTDIR}/.cshrc; \
	    ln ${DESTDIR}/root/.profile ${DESTDIR}/.profile
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
		ln -s mail/aliases ${DESTDIR}/etc/aliases; \
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
	cd ${DESTDIR}/etc/namedb; rm -f etc/namedb; ln -s ".." etc/namedb
	cd ${DESTDIR}/; rm -f ${DESTDIR}/sys; ln -s usr/src/sys sys
	cd ${DESTDIR}/usr/share/man/en.ISO8859-1; ln -sf ../man* .
	cd ${DESTDIR}/usr/share/man; \
	set - `grep "^[a-zA-Z]" ${.CURDIR}/man.alias`; \
	while [ $$# -gt 0 ] ; \
	do \
		rm -rf "$$1"; \
		ln -s "$$2" "$$1"; \
		shift; shift; \
	done
	cd ${DESTDIR}/usr/share/locale; \
	set - `grep "^[a-zA-Z]" ${.CURDIR}/locale.alias`; \
	while [ $$# -gt 0 ] ; \
	do \
		rm -rf "$$1"; \
		ln -s "$$2" "$$1"; \
		shift; shift; \
	done
	cd ${DESTDIR}/usr/share/openssl/man/en.ISO8859-1; ln -sf ../man* .
	cd ${DESTDIR}/usr/share/nls; \
	set - `grep "^[a-zA-Z]" ${.CURDIR}/nls.alias`; \
	while [ $$# -gt 0 ] ; \
	do \
		rm -rf "$$1"; \
		ln -s "$$2" "$$1"; \
		shift; shift; \
	done

etc-examples:
	cd ${.CURDIR}; ${INSTALL} -o ${BINOWN} -g ${BINGRP} -m 444 \
	    ${BINUPDATE} ${BIN1} ${BIN2} nsmb.conf opieaccess \
	    ${DESTDIR}/usr/share/examples/etc
	cd ${.CURDIR}/defaults; ${INSTALL} -o ${BINOWN} -g ${BINGRP} -m 444 \
	    ${DEFAULTS} ${DESTDIR}/usr/share/examples/etc/defaults

.include <bsd.prog.mk>
