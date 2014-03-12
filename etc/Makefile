#	from: @(#)Makefile	5.11 (Berkeley) 5/21/91
# $FreeBSD: src/etc/Makefile,v 1.219.2.38 2003/03/04 09:49:00 ru Exp $

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
	rc.subr rpc \
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
	devd.conf devtab dhclient.conf dm.conf dntpd.conf \
	ftpusers group \
	hosts hosts.allow hosts.equiv hosts.lpd \
	inetd.conf login.access login.conf \
	motd modems netconfig networks newsyslog.conf \
	nscd.conf pf.conf phones printcap profile \
	regdomain.xml remote sensorsd.conf services \
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
DIRS+=	libexec/dma

# Files that should be installed read-only-executable (555) root:wheel
#
BIN2=	pccard_ether rc.firewall rc.suspend rc.resume

DEFAULTS= compilers.conf devfs.conf make.conf mkinitrd.conf periodic.conf \
	  pfi.conf rc.conf uuids

MTREE=	BSD.include.dist BSD.local.dist BSD.root.dist BSD.usr.dist \
	BSD.var.dist
.if !defined(NO_SENDMAIL)
MTREE+=	BSD.sendmail.dist
.endif

PPPCNF=	ppp.conf

ETCMAIL=Makefile README mailer.conf access.sample virtusertable.sample \
	mailertable.sample aliases

# List of libraries in /usr/lib/compat that might have to be removed
# from /usr/lib.
.if ${MACHINE_ARCH} == "i386"
COMPAT_LIBS != cd ${DESTDIR}/usr/lib/compat && find . -name '*.so.*'
.endif

# Use this directory as the source for new configuration files when upgrading
UPGRADE_SRCDIR?=${.CURDIR}

# Include file which contains obsolete files
.if exists(${DESTDIR}/etc/upgrade/Makefile_upgrade.inc)
.include "${DESTDIR}/etc/upgrade/Makefile_upgrade.inc"
.endif

remove-obsolete-files:
	@if [ -z "${TO_REMOVE}" ]; then \
		echo "Please do a 'make installworld' first."; \
		echo "See build(7) for further information."; \
		/usr/bin/false; \
	fi;
	@echo "===> Remove now obsolete files"
	@for item in ${TO_REMOVE:M*.info.gz}; do \
		if [ -e ${DESTDIR}$${item} ]; then \
			install-info --delete ${DESTDIR}$${item} \
			    /usr/share/info/dir; \
		fi; \
	done;
	@for item in ${TO_REMOVE}; do \
		if [ -e ${DESTDIR}$${item} -o -L ${DESTDIR}$${item} ]; then \
			echo "${DESTDIR}$${item}"; \
			chflags -Rf noschg "${DESTDIR}$${item}"; \
			rm -rf "${DESTDIR}$${item}"; \
		fi; \
	done

preupgrade:
	(pw -V ${DESTDIR}/etc usershow mail -q > /dev/null) || \
		pw -V ${DESTDIR}/etc useradd mail -u 6 -g 6 \
		   -c "Mail user" \
		   -d /nonexistent -s /sbin/nologin
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
	(pw -V ${DESTDIR}/etc usershow _sdpd -q > /dev/null) || \
		pw -V ${DESTDIR}/etc useradd _sdpd -u 70 \
		   -c "sdpd privsep user" \
		   -d /var/empty -s /sbin/nologin
	(pw -V ${DESTDIR}/etc usershow _dhcp -q > /dev/null) || \
		pw -V ${DESTDIR}/etc useradd _dhcp -u 77 \
		   -c "DHCP programs" \
		   -d /var/empty -s /sbin/nologin
	(pw -V ${DESTDIR}/etc groupshow authpf -q > /dev/null) || \
		pw -V ${DESTDIR}/etc groupadd authpf -g 63
	(pw -V ${DESTDIR}/etc groupshow _pflogd -q > /dev/null) || \
		pw -V ${DESTDIR}/etc groupadd _pflogd -g 64
	(pw -V ${DESTDIR}/etc groupshow _sdpd -q > /dev/null) || \
		pw -V ${DESTDIR}/etc groupadd _sdpd -g 70
	(pw -V ${DESTDIR}/etc groupshow _dhcp -q > /dev/null) || \
		pw -V ${DESTDIR}/etc groupadd _dhcp -g 77

upgrade_etc:	preupgrade remove-obsolete-files
	# files that should be dirs (not possible with Makefile_upgrade.inc)
.for f in usr/include/dev/misc/ppi usr/share/initrd/sbin
	[ -d ${DESTDIR}/${f} ] || rm -f ${DESTDIR}/${f}
.endfor
.if !defined(BINARY_UPGRADE) # binary upgrade just copies these files
	cd ${UPGRADE_SRCDIR}/../share/mk; ${MAKE} install
.endif
	cd ${UPGRADE_SRCDIR}; \
	    ${INSTALL} -o ${BINOWN} -g ${BINGRP} -m 444 \
		${BINUPDATE} ${DESTDIR}/etc; \
	    cap_mkdb ${DESTDIR}/etc/login.conf; \
	    for f in ${BIN1}; do \
		[ -e "${DESTDIR}/etc/$${f##*/}" ] || \
		    ${INSTALL} -o ${BINOWN} -g ${BINGRP} -m 644 \
			$$f ${DESTDIR}/etc; \
	    done; \
	    ${INSTALL} -o ${BINOWN} -g ${BINGRP} -m 555 \
		${BIN2} ${DESTDIR}/etc;
	cd ${UPGRADE_SRCDIR}/defaults; ${INSTALL} -o ${BINOWN} -g ${BINGRP} -m 444 \
	    ${DEFAULTS} ${DESTDIR}/etc/defaults
	cd ${UPGRADE_SRCDIR}/periodic; ${MAKE} install
	mkdir -p ${DESTDIR}/etc/rc.d
	cd ${UPGRADE_SRCDIR}/rc.d; ${MAKE} install 
	cd ${UPGRADE_SRCDIR}/devd; ${MAKE} install
	# "../share/termcap/make etc-termcap" expanded inline here:
	${LN} -fs /usr/share/misc/termcap ${DESTDIR}/etc/termcap
	# "../usr.sbin/rmt/make etc-rmt" expanded inline here:
	${LN} -fs /usr/sbin/rmt ${DESTDIR}/etc/rmt
.if !defined(BINARY_UPGRADE) # XXX not yet handled by binary upgrade
.if !defined(NO_SENDMAIL)
	cd ${UPGRADE_SRCDIR}/sendmail; ${MAKE} upgrade
.endif
.endif
	cd ${UPGRADE_SRCDIR}/mtree; ${INSTALL} -o ${BINOWN} -g ${BINGRP} -m 444 \
	    ${MTREE} ${DESTDIR}/etc/mtree
	cd ${UPGRADE_SRCDIR}/bluetooth; ${MAKE} install
	cd ${UPGRADE_SRCDIR}; ${INSTALL} -o ${BINOWN} -g ${BINGRP} -m 444 Makefile.usr ${DESTDIR}/usr/Makefile
.if !exists(${DESTDIR}/etc/pam.d)
	mkdir -p ${DESTDIR}/etc/pam.d
	cd ${UPGRADE_SRCDIR}/pam.d; ${MAKE} install
	sh ${DESTDIR}/etc/pam.d/convert.sh ${DESTDIR}/etc/pam.d ${DESTDIR}/etc/pam.conf
.else
.for pamconf in README convert.sh atrun cron passwd rsh su system
.if !exists(${DESTDIR}/etc/pam.d/${pamconf})
	${INSTALL} -o ${BINOWN} -g ${BINGRP} -m 644 ${UPGRADE_SRCDIR}/pam.d/${pamconf} ${DESTDIR}/etc/pam.d
.endif
.endfor
.endif
	# these removals must occur AFTER any pam conversion
	csh -c "rm -f /usr/lib/pam_{cleartext_pass_ok,deny,opie,opieaccess,permit,radius,skey,ssh,tacplus,unix}.so"
	rm -f /etc/pam.conf
.if !defined(BINARY_UPGRADE) # binary upgrade just copies these files
	cd ${UPGRADE_SRCDIR}/..; ${INSTALL} -o ${BINOWN} -g ${BINGRP} -m 444 \
	    COPYRIGHT ${DESTDIR}/
.endif
	rm -f ${DESTDIR}/usr/include/machine/ioctl_meteor.h
	rm -f ${DESTDIR}/usr/include/machine/ioctl_bt848.h
	rm -f ${DESTDIR}/usr/include/sys/ata.h
	${LN} -s "../dev/video/bktr/ioctl_bt848.h" ${DESTDIR}/usr/include/machine/ioctl_bt848.h
	${LN} -s "../dev/video/meteor/ioctl_meteor.h" ${DESTDIR}/usr/include/machine/ioctl_meteor.h
	${LN} -s nata.h ${DESTDIR}/usr/include/sys/ata.h
.if exists(${DESTDIR}/usr/lib/gcc2)
	ldconfig -m ${DESTDIR}/usr/lib/gcc2
.endif
.for lib in ${COMPAT_LIBS:M*.so.*}
.if exists(${DESTDIR}/usr/lib/${lib})
	chflags noschg ${DESTDIR}/usr/lib/${lib}
	rm -f ${DESTDIR}/usr/lib/${lib}
.endif
.endfor
.if !defined(NO_OPENSSH)
	cd ${.CURDIR}/../secure/lib/libssh; ${MAKE} -f Makefile.etc obj
	cd ${.CURDIR}/../secure/lib/libssh; ${MAKE} -f Makefile.etc install
.endif
.if exists(${DESTDIR}/boot)
.if exists(${DESTDIR}/kernel)
	chflags noschg ${DESTDIR}/kernel
	mv ${DESTDIR}/kernel ${DESTDIR}/boot/kernel
	chflags schg ${DESTDIR}/boot/kernel
.endif
.if exists(${DESTDIR}/modules)
	mv ${DESTDIR}/modules ${DESTDIR}/boot/modules
	rm -rf ${DESTDIR}/boot/modules/modules
.endif
.if exists(${DESTDIR}/kernel.old)
	mv ${DESTDIR}/kernel.old ${DESTDIR}/boot/kernel.old
.endif
.if exists(${DESTDIR}/modules.old)
	mv ${DESTDIR}/modules.old ${DESTDIR}/boot/modules.old
	rm -rf ${DESTDIR}/boot/modules.old/modules.old
.endif
.endif
.if exists(${DESTDIR}/etc/settings.conf)
	mv ${DESTDIR}/etc/settings.conf ${DESTDIR}/etc/pkg_radd.conf
.endif
	for ext in a so.0; do \
		f=${DESTDIR}/usr/lib/libpthread.$$ext; \
		dest=$$(readlink "$$f"); \
		case "$$dest" in \
		libthread_xu.*|libc_r.*) \
			ln -fs thread/$$dest $$f ;; \
		esac; \
	done
	ldconfig -R
	mkdir -p ${DESTDIR}/dev

# The existence of cleartext_pass_ok means pam config files are out of date.
	@set - `fgrep cleartext_pass_ok ${DESTDIR}/etc/pam.d/*`; \
	if [ $$# -gt 0 ] ; \
	then \
		echo "It appears your PAM configuration files need to be updated"; \
		echo "This can be done by manually editing the files or:"; \
		echo "	cd ${UPGRADE_SRCDIR}/pam.d"; \
		echo "	make install"; \
	fi

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
	cd ${.CURDIR}/pam.d; ${MAKE} install
	cd ${.CURDIR}/bluetooth; ${MAKE} install
	cd ${.CURDIR}/periodic; ${MAKE} install
	cd ${.CURDIR}/rc.d; ${MAKE} install 
	cd ${.CURDIR}/devd; ${MAKE} install
	cd ${.CURDIR}/../share/termcap; ${MAKE} etc-termcap
	cd ${.CURDIR}/../usr.sbin/rmt; ${MAKE} etc-rmt
	cd ${.CURDIR}; \
	    ${INSTALL} -o ${BINOWN} -g ${BINGRP} -m 444 \
		Makefile.usr ${DESTDIR}/usr/Makefile
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
	mkdir -p ${DESTDIR}/dev
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
	cd ${.CURDIR}/mtree; ${INSTALL} -o ${BINOWN} -g ${BINGRP} -m 444 \
	    ${MTREE} ${DESTDIR}/etc/mtree
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
	${INSTALL} -o ${BINOWN} -g ${BINGRP} -m 640 /dev/null \
	    ${DESTDIR}/var/log/daemon
	${INSTALL} -o ${BINOWN} -g ${BINGRP} -m 644 /dev/null \
	    ${DESTDIR}/var/log/lpd-errs
	${INSTALL} -o ${BINOWN} -g ${BINGRP} -m 640 /dev/null \
	    ${DESTDIR}/var/log/maillog
	${INSTALL} -o ${BINOWN} -g ${BINGRP} -m 644 /dev/null \
	    ${DESTDIR}/var/log/lastlog
	${INSTALL} -o ${BINOWN} -g ${BINGRP} -m 644 /dev/null \
	    ${DESTDIR}/var/log/lastlogx
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
	    ${DESTDIR}/var/log/wtmpx
	${INSTALL} -o ${BINOWN} -g ${BINGRP} -m 644 /dev/null \
	    ${DESTDIR}/var/run/utmp
	${INSTALL} -o ${BINOWN} -g ${BINGRP} -m 644 /dev/null \
	    ${DESTDIR}/var/run/utmpx
	${INSTALL} -o ${BINOWN} -g ${BINGRP} -m 644 ${.CURDIR}/minfree \
	    ${DESTDIR}/var/crash
	cd ${.CURDIR}/..; ${INSTALL} -o ${BINOWN} -g ${BINGRP} -m 444 \
	    COPYRIGHT ${DESTDIR}/
	rm -f ${DESTDIR}/usr/include/machine/ioctl_bt848.h
	rm -f ${DESTDIR}/usr/include/machine/ioctl_meteor.h
	rm -f ${DESTDIR}/usr/include/sys/ata.h
	${LN} -s "../dev/video/bktr/ioctl_bt848.h" ${DESTDIR}/usr/include/machine/ioctl_bt848.h
	${LN} -s "../dev/video/meteor/ioctl_meteor.h" ${DESTDIR}/usr/include/machine/ioctl_meteor.h
	${LN} -s nata.h ${DESTDIR}/usr/include/sys/ata.h
.if !defined(NOMAN)
	cd ${.CURDIR}/../share/man; ${MAKE} makedb
.endif

distrib-dirs:
	mtree -deU -f ${.CURDIR}/mtree/BSD.root.dist -p ${DESTDIR}/
	mtree -deU -f ${.CURDIR}/mtree/BSD.var.dist -p ${DESTDIR}/var
	mtree -deU -f ${.CURDIR}/mtree/BSD.usr.dist -p ${DESTDIR}/usr
	mtree -deU -f ${.CURDIR}/mtree/BSD.include.dist \
		-p ${DESTDIR}/usr/include
.if !defined(NO_SENDMAIL)
	mtree -deU -f ${.CURDIR}/mtree/BSD.sendmail.dist -p ${DESTDIR}/
.endif
	cd ${DESTDIR}/; test -e sys || (rm -f sys;  ${LN} -s usr/src/sys sys)
	cd ${DESTDIR}/usr/share/man/en.ISO8859-1; ${LN} -sf ../man* .
	cd ${DESTDIR}/usr/share/man; \
	set - `grep "^[a-zA-Z]" ${.CURDIR}/man.alias`; \
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
