# $DragonFly: src/nrelease/Makefile,v 1.90 2008/09/01 21:20:30 swildner Exp $
#

#########################################################################
#				ENHANCEMENTS	 			#
#########################################################################

# These targets are now obsolete and should not be used 
#
installer_release: warning release
installer_quickrel: warning quickrel
installer_realquickrel: warning realquickrel
installer_fetch: warning fetch

# New method e.g. 'make gui release'.  A series of enhancement
# targes may be specified which set make variables which enhance
# the build in various ways.
#
gui installer img:

warning:
	@echo "WARNING: The installer_* targets are now obsolete, please"
	@echo "use 'make blah' instead of 'make installer_blah'"
	@echo ""
	@echo "will continue in 10 seconds"
	@sleep 10

#########################################################################
#				 SETUP		 			#
#########################################################################

ISODIR ?= /usr/release
ISOROOT ?= ${ISODIR}/root
OBJSYS= ${.OBJDIR}/../sys

.if make(gui)
KERNCONF ?= DFLYLIVE VKERNEL DFLYLIVE-SMP DFLYLIVE-SMP-NOAPIC
.else
KERNCONF ?= GENERIC VKERNEL
.endif

PKGSRC_PREFIX?=		/usr/pkg
PKGBIN_PKG_ADD?=	${PKGSRC_PREFIX}/sbin/pkg_add
PKGBIN_PKG_DELETE?=	${PKGSRC_PREFIX}/sbin/pkg_delete
PKGBIN_PKG_ADMIN?=	${PKGSRC_PREFIX}/sbin/pkg_admin
PKGBIN_MKISOFS?=	${PKGSRC_PREFIX}/bin/mkisofs
PKGSRC_PKG_PATH?=	${ISODIR}/packages
PKGSRC_DB?=		/var/db/pkg
CVSUP_BOOTSTRAP_KIT?=	cvsup-bootstrap-20070716

# pkgsrc packages to be built and installed on the release ISO
#
PKGSRC_PACKAGES?=	devel/scmgit \
			net/isc-dhcp4 \
			net/isc-dhcpd4 \
			pkgtools/pkg_leaves \
			sysutils/cdrtools

# pkgsrc options to use when building packages
#
.if !make(gui)
PKGSRC_OPTIONS?=	PKG_OPTIONS.scmgit=-scmgit-gui
.endif

# Even though buildiso wipes the packages, our check target has to run
# first and old packages (listed as they appear in pkg_info) must be
# cleaned out in order for the pkg_add -n test we use in the check target
# to operate properly.
#
OLD_PKGSRC_PACKAGES?=	cdrtools-2.01.01.27nb1 \
			cdrecord-2.00.3nb2 \
			bootstrap-kit-20070205 \
			dfuibe_installer-1.1.6 \
			gettext-lib-0.14.5 \
			dfuibe_installer-1.1.7nb1 \
			dfuife_curses-1.5 \
			gettext-lib-0.14.6 \
			gettext-tools-0.14.6nb1 \
			libaura-3.1 \
			libdfui-4.2 \
			libinstaller-5.1 \
			bootstrap-kit-20080211 \
			cdrtools-ossdvd-2.01.1.36nb2 \
			isc-dhcp-base-4.0.0 \
			isc-dhcp-server-4.0.0

# Specify which root skeletons are required, and let the user include
# their own.  They are copied into ISODIR during the `pkgcustomizeiso'
# target; each overwrites the last.
#
REQ_ROOTSKELS=	${.CURDIR}/root \
		${.CURDIR}/installer
ROOTSKELS?=	${REQ_ROOTSKELS}

.if make(gui)
ISOFILE?=		${ISODIR}/dfly-gui.iso
IMGFILE?=		${ISODIR}/dfly-gui.img
PKGSRC_PACKAGES+=	meta/modular-xorg-apps \
			meta/modular-xorg-drivers \
			meta/modular-xorg-fonts \
			meta/modular-xorg-libs \
			wm/fluxbox \
			www/firefox3 \
			print/xpdf \
			shells/zsh \
			editors/emacs \
			editors/vim \
			chat/pidgin \
			chat/irssi \
			chat/xchat \
			x11/rxvt-unicode \
			x11/eterm \
			lang/perl5 \
			editors/nano \
			shells/bash \
			devel/exctags \
			archivers/zip \
			security/sudo \
			www/links-gui \
			net/nmap \
			net/wget \
			fonts/terminus-font \
			net/rsync \
			sysutils/idesk \
			misc/screen
ROOTSKELS+=		${.CURDIR}/gui
.endif

ISOFILE ?= ${ISODIR}/dfly.iso
IMGFILE ?= ${ISODIR}/dfly.img

IMGMNT ?= ${ISODIR}/mnt
IMGUSBDEV ?= /dev/da0

# note: we use the '${NRLOBJDIR}/nrelease' construct, that is we add
# the additional '/nrelease' manually, as a safety measure.
#
NRLOBJDIR?= /usr/obj

#########################################################################
#				BASE ISO TARGETS 			#
#########################################################################

release:	check clean buildworld1 buildkernel1 \
		buildiso syssrcs customizeiso mklocatedb mkiso mkimg

quickrel:	check clean buildworld2 buildkernel2 \
		buildiso syssrcs customizeiso mklocatedb mkiso mkimg

realquickrel:	check clean buildiso syssrcs customizeiso mklocatedb mkiso \
		mkimg

quick:		quickrel

realquick:	realquickrel

#########################################################################
#			   CORE SUPPORT TARGETS 			#
#########################################################################

check:
.if !exists(${PKGSRC_PKG_PATH}/${CVSUP_BOOTSTRAP_KIT}.tgz)
	@echo "The cvsup bootstrap kit is not installed.  You can install it with:"
	@echo "    make fetch"
	@exit 1
.endif
.if !defined(PKGSRC_PATH)
	@echo "Please set PKGSRC_PATH to the pkgsrc tree that shall be used for"
	@echo "package building."
	@exit 1
.endif

buildworld1 buildworld2:
	cd ${.CURDIR}/..; ${WORLD_CCVER:C/^..*$/WORLD_CCVER=/}${WORLD_CCVER} make -DWANT_INSTALLER ${.TARGET:C/build(.*)2/quick\1/:C/1//}

buildkernel1 buildkernel2:
.if make(gui)
	cd ${.CURDIR}/..; \
	for kernconf in ${KERNCONF}; do \
		${WORLD_CCVER:C/^..*$/WORLD_CCVER=/}${WORLD_CCVER} \
		make ${.TARGET:C/build(.*)2/quick\1/:C/1//} \
			KERNCONF=$${kernconf} KERNCONFDIR=${.CURDIR}/gui/root; \
	done
.else
	cd ${.CURDIR}/..; \
	first=; \
	for kernconf in ${KERNCONF}; do \
		${WORLD_CCVER:C/^..*$/WORLD_CCVER=/}${WORLD_CCVER} \
		make ${.TARGET:C/build(.*)2/quick\1/:C/1//} \
			KERNCONF=$${kernconf} \
			$${first:+-DNO_MODULES}; \
		first=done; \
	done
.endif

# note that we do not want to mess with any /usr/obj directories not related
# to buildworld, buildkernel, or nrelease, so we must supply the proper
# MAKEOBJDIRPREFIX for targets that are not run through the buildworld and 
# buildkernel mechanism.
#
buildiso:
	if [ ! -d ${ISOROOT} ]; then mkdir -p ${ISOROOT}; fi
	if [ ! -d ${NRLOBJDIR}/nrelease ]; then mkdir -p ${NRLOBJDIR}/nrelease; fi
	( cd ${.CURDIR}/..; make -DWANT_INSTALLER DESTDIR=${ISOROOT} installworld )
	( cd ${.CURDIR}/../etc; MAKEOBJDIRPREFIX=${NRLOBJDIR}/nrelease \
		make -m ${.CURDIR}/../share/mk DESTDIR=${ISOROOT} distribution )
	cpdup ${ISOROOT}/etc ${ISOROOT}/etc.hdd
.if make(gui)
	if [ ! -d ${ISOROOT}/kernel.smp/boot ]; then mkdir -p ${ISOROOT}/kernel.smp/boot; fi
	cd ${.CURDIR}/..; \
	make installkernel DESTDIR=${ISOROOT} \
	KERNCONF=DFLYLIVE DESTKERNNAME=kernel KERNCONFDIR=${.CURDIR}/gui/root; \
	cd ${.CURDIR}/..; \
	make installkernel DESTDIR=${ISOROOT} \
	KERNCONF=VKERNEL DESTKERNNAME=kernel.VKERNEL -DNO_MODULES KERNCONFDIR=${.CURDIR}/gui/root; \
	cd ${.CURDIR}/..; \
	make installkernel DESTDIR=${ISOROOT}/kernel.smp \
	KERNCONF=DFLYLIVE-SMP DESTKERNNAME=kernel KERNCONFDIR=${.CURDIR}/gui/root; \
	cd ${.CURDIR}/..; \
	make installkernel DESTDIR=${ISOROOT}/kernel.smp \
	KERNCONF=DFLYLIVE-SMP-NOAPIC DESTKERNNAME=kernel.noapic \
		KERNCONFDIR=${.CURDIR}/gui/root -DNO_MODULES;
.else
	cd ${.CURDIR}/..; \
	first=; \
	for kernconf in ${KERNCONF}; do \
		make DESTDIR=${ISOROOT} \
			installkernel KERNCONF=$${kernconf} \
			$${first:+DESTKERNNAME=kernel.$${kernconf}} \
			$${first:+-DNO_MODULES}; \
		first=done; \
	done
.endif
	ln -s kernel ${ISOROOT}/kernel.BOOTP
	mtree -deU -f ${.CURDIR}/../etc/mtree/BSD.local.dist -p ${ISOROOT}/usr/local/
	mtree -deU -f ${.CURDIR}/../etc/mtree/BSD.var.dist -p ${ISOROOT}/var
	dev_mkdb -f ${ISOROOT}/var/run/dev.db ${ISOROOT}/dev

# Include kernel sources on the release CD (~14MB)
#
syssrcs:
.if !defined(WITHOUT_SRCS)
	( cd ${.CURDIR}/../..; tar -cf - src/Makefile src/Makefile.inc1 src/sys | bzip2 -9 > ${ISOROOT}/usr/src-sys.tar.bz2 )
.endif

customizeiso:
.for ROOTSKEL in ${ROOTSKELS}
	cpdup -X cpignore -o ${ROOTSKEL} ${ISOROOT}
.endfor
	(cd ${PKGSRC_PKG_PATH}; tar xzpf ${CVSUP_BOOTSTRAP_KIT}.tgz)
	cp -p ${PKGSRC_PKG_PATH}/${CVSUP_BOOTSTRAP_KIT}/usr/local/bin/cvsup ${ISOROOT}/usr/local/bin/cvsup
	cp -p ${PKGSRC_PKG_PATH}/${CVSUP_BOOTSTRAP_KIT}/usr/local/man/man1/cvsup.1 ${ISOROOT}/usr/local/man/man1/cvsup.1
	pwd_mkdb -p -d ${ISOROOT}/etc ${ISOROOT}/etc/master.passwd
.for UPGRADE_ITEM in Makefile			\
		     etc.${MACHINE_ARCH} 	\
		     isdn/Makefile		\
		     rc.d/Makefile		\
		     periodic/Makefile		\
		     periodic/daily/Makefile	\
		     periodic/security/Makefile	\
		     periodic/weekly/Makefile	\
		     periodic/monthly/Makefile
	cp -R ${.CURDIR}/../etc/${UPGRADE_ITEM} ${ISOROOT}/etc/${UPGRADE_ITEM}
.endfor
	# setup some things
	cp -p /etc/resolv.conf ${ISOROOT}/etc
	chroot ${ISOROOT} sh -c "ldconfig -elf /usr/lib /usr/lib/gcc* /usr/lib/compat"
	# mount pkgsrc tree
	mkdir ${ISOROOT}/usr/pkgsrc
	mount_null ${PKGSRC_PATH} ${ISOROOT}/usr/pkgsrc
	# bootstrap
	chroot ${ISOROOT} sh -c "cd /usr/pkgsrc/bootstrap; ./bootstrap; ./cleanup"
	# build and install packages
.for PKG in ${PKGSRC_PACKAGES}
	chroot ${ISOROOT} sh -c "cd /usr/pkgsrc; cd ${PKG}; bmake ${PKGSRC_OPTIONS} build install clean clean-depends"
.endfor
	# remove packages which nothing depends on
	chroot ${ISOROOT} sh -c "pkg_leaves | xargs pkg_delete -R"
	umount ${ISOROOT}/usr/pkgsrc
	rmdir ${ISOROOT}/usr/pkgsrc
	rm -f ${ISOROOT}/etc/resolv.conf
	makewhatis ${ISOROOT}/usr/local/man
	makewhatis ${ISOROOT}/usr/pkg/man

mklocatedb:
	( find -s ${ISOROOT} -path ${ISOROOT}/tmp -or \
		-path ${ISOROOT}/usr/tmp -or -path ${ISOROOT}/var/tmp \
		-prune -o -print | sed -e 's#^${ISOROOT}##g' | \
		/usr/libexec/locate.mklocatedb \
		-presort >${ISOROOT}/var/db/locate.database )

mkiso:
	( cd ${ISOROOT}; ${PKGBIN_MKISOFS} -b boot/cdboot -no-emul-boot \
		-R -J -V DragonFly -o ${ISOFILE} . )


mkimg:
.if make(img)
	if [ ! -d ${IMGMNT} ]; then mkdir -p ${IMGMNT}; fi

	echo "determine required image size" > /dev/null;		\
	sz=`du -ck ${ISOROOT} | tail -n 1 | cut -f 1`;			\
	echo "add 15% more space as required" > /dev/null;		\
	sz=`bc -e "(($${sz}) / 1024) * 1.15" -equit | cut -f1 -d.`;	\
	dd if=/dev/zero of=${IMGFILE} bs=1m count=$${sz};		\
	fdisk -IB -p ${IMGFILE};					\
	echo "determine free vn device" > /dev/null;			\
	vn=`vnconfig -l | grep "not in use" | head -n 1 | cut -f 1 -d:`; \
	vnconfig -e -s labels $${vn} ${IMGFILE}; 			\
	echo "write standard disklabel" > /dev/null;			\
	disklabel -w -r $${vn}s1 auto;					\
	echo "read disklabel back" > /dev/null;				\
	disklabel -r $${vn}s1 > ${IMGFILE}.label;			\
	echo "determine number of sectors of whole disk" > /dev/null; 	\
	secs=`tail -n 1 ${IMGFILE}.label | cut -f 3 -w`;		\
	echo "and add a: partition" > /dev/null;			\
	echo "  a:  $${secs} 0 4.2BSD" >> ${IMGFILE}.label;		\
	echo "write modified disklabel back" > /dev/null;		\
	disklabel -R -r $${vn}s1 ${IMGFILE}.label;			\
	rm ${IMGFILE}.label;						\
	echo "write bootsector" >  /dev/null;				\
	disklabel -B $${vn}s1;						\
	newfs /dev/$${vn}s1a;						\
	mount /dev/$${vn}s1a ${IMGMNT};					\
	cpdup -vvv ${ISOROOT} ${IMGMNT};				\
	echo "fix /etc/fstab" > /dev/null;				\
	echo "${IMGUSBDEV}s1a / ufs rw 0 0" > ${IMGMNT}/etc/fstab; 	\
	echo "proc /proc procfs rw 0 0" >> ${IMGMNT}/etc/fstab; 	\
	df ${IMGMNT};							\
	umount ${IMGMNT};						\
	vnconfig -u $${vn};						\
	rmdir ${IMGMNT}
.endif

clean:
	-umount ${ISOROOT}/usr/pkgsrc
	if [ -d ${ISOROOT} ]; then chflags -R noschg ${ISOROOT}; fi
	if [ -d ${ISOROOT} ]; then rm -rf ${ISOROOT}/*; fi
	if [ -d ${NRLOBJDIR}/nrelease ]; then rm -rf ${NRLOBJDIR}/nrelease; fi

realclean:	clean
	rm -rf ${OBJSYS}/${KERNCONF}
	# do not use PKGSRC_PKG_PATH here, we do not want to destroy an
	# override location.
	if [ -d ${ISODIR}/packages ]; then rm -rf ${ISODIR}/packages; fi

fetch:
	@if [ ! -d ${PKGSRC_PKG_PATH} ]; then mkdir -p ${PKGSRC_PKG_PATH}; fi
.if !exists(${PKGSRC_PKG_PATH}/${CVSUP_BOOTSTRAP_KIT}.tgz)
	(cd ${PKGSRC_PKG_PATH}; fetch ${PKGSRC_BOOTSTRAP_URL}/${CVSUP_BOOTSTRAP_KIT}.tgz)
.endif

pkgsrc_bootstrap:
	mkdir -p ${PKGSRC_PKG_PATH}
.if !exists(${PKGSRC_PKG_PATH}/${PKGSRC_BOOTSTRAP_KIT}.tgz)
	(cd ${PKGSRC_PKG_PATH}; fetch ${PKGSRC_BOOTSTRAP_URL}/${PKGSRC_BOOTSTRAP_KIT}.tgz)
.endif
	(cd ${PKGSRC_PKG_PATH}; tar xzpf ${PKGSRC_BOOTSTRAP_KIT}.tgz)
	(cd ${PKGSRC_PKG_PATH}/${PKGSRC_BOOTSTRAP_KIT}/bootstrap; ./bootstrap)

pkgsrc_cdrecord:
.if !exists (${PKGBIN_MKISOFS})
	${PKGBIN_PKG_ADD} ${PKGSRC_PKG_PATH}/cdrtools*
.endif


.PHONY: all release installer_release quickrel installer_quickrel realquickrel
.PHONY: installer_fetch installer
.PHONY: quick realquick
.PHONY: installer_realquickrel check buildworld1 buildworld2
.PHONY: buildkernel1 buildkernel2 buildiso customizeiso mklocatedb mkiso mkimg
.PHONY: clean realclean fetch

.include <bsd.prog.mk>
