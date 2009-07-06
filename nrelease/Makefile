# $DragonFly: src/nrelease/Makefile,v 1.90 2008/09/01 21:20:30 swildner Exp $
#

#########################################################################
#				ENHANCEMENTS	 			#
#########################################################################

# New method e.g. 'make gui release'.  A series of enhancement
# targes may be specified which set make variables which enhance
# the build in various ways.
#
gui installer img:

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
PKGSRC_BOOTSTRAP_URL?=	http://avalon.dragonflybsd.org/DragonFly-pkgsrc-packages/i386/1.12.0-RELEASE-BUILD
CVSUP_BOOTSTRAP_KIT?=	cvsup-bootstrap-20070716

# We use env -i in the chroot, so that environment variables won't
# disturb any (pkgsrc) build.  This has happened for TARGET_ARCH,
# which is used by gmake in completely different context.
#
CHROOT_CMD?=		env -i CCVER=${CCVER:Q} /usr/sbin/chroot ${ISOROOT} sh -c

# User may specify extra packages in addition to the defaults
#
PKGSRC_EXTRA_PACKAGES?=

# Pkgsrc packages to be built and installed on the release ISO
#
PKGSRC_PACKAGES?=	pkgtools/pkg_leaves \
			pkgtools/pkg_tarup \
			devel/scmgit \
			net/isc-dhcp4 \
			net/isc-dhcpd4 \
			sysutils/cdrtools \
			${PKGSRC_EXTRA_PACKAGES}

# pkgsrc options to use when building packages
#
PKGSRC_OPTIONS+=	PKG_DEFAULT_OPTIONS='dri inet6'
.if !make(gui)
PKGSRC_OPTIONS+=	PKG_OPTIONS.scmgit=-scmgit-gui
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
# their own.  They are copied into ISODIR during the `customizeiso'
# target; each overwrites the last.
#
REQ_ROOTSKELS=	${.CURDIR}/root \
		${.CURDIR}/installer
ROOTSKELS?=	${REQ_ROOTSKELS}

.if make(gui)
ISOFILE?=		${ISODIR}/dfly-gui.iso
IMGFILE?=		${ISODIR}/dfly-gui.img
PKGSRC_PACKAGES+=	meta-pkgs/modular-xorg-apps \
			meta-pkgs/modular-xorg-drivers \
			meta-pkgs/modular-xorg-fonts \
			meta-pkgs/modular-xorg-libs \
			wm/fluxbox \
			wm/fvwm \
			www/firefox3 \
			print/xpdf \
			shells/zsh \
			editors/emacs \
			editors/vim \
			chat/pidgin \
			chat/irssi \
			chat/xchat \
			x11/modular-xorg-server \
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
			time/asclock \
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
		buildiso srcs customizeiso mklocatedb \
		mkiso mkimg

quickrel:	check clean buildworld2 buildkernel2 \
		buildiso srcs customizeiso mklocatedb \
		mkiso mkimg

realquickrel:	check clean \
		buildiso srcs customizeiso mklocatedb \
		mkiso mkimg

restartpkgs:	check customizeiso mklocatedb mkiso mkimg

quick:		quickrel

realquick:	realquickrel


#########################################################################
#			   CORE SUPPORT TARGETS 			#
#########################################################################

check:
.if !exists(${PKGBIN_PKG_ADMIN})
	@echo "You never bootstrapped pkgsrc on your machine.  You can install it with:"
	@echo "    make pkgsrc_bootstrap"
.endif
.if !exists(${PKGSRC_PKG_PATH}/${CVSUP_BOOTSTRAP_KIT}.tgz)
	@echo "The cvsup bootstrap kit is not installed.  You can install it with:"
	@echo "    make fetch"
.endif
.if !exists (${PKGBIN_MKISOFS})
	@echo
	@echo "Your machine does not have cdrtools installed.  You can install it with:"
	@echo "    make pkgsrc_cdrecord"
.endif
.if !defined(PKGSRC_PATH)
	@echo "Please set PKGSRC_PATH to the pkgsrc tree that shall be used for"
	@echo "package building.  For example /usr/pkgsrc.  See the Makefile"
	@echo "in /usr if you are unfamiliar with pkgsrc."
.endif
.if !exists(${PKGSRC_PKG_PATH}/${CVSUP_BOOTSTRAP_KIT}.tgz)
	@exit 1
.endif
.if !exists (${PKGBIN_MKISOFS})
	@exit 1
.endif
.if !defined(PKGSRC_PATH)
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
	ln -s kernel ${ISOROOT}/boot/kernel.BOOTP
	mtree -deU -f ${.CURDIR}/../etc/mtree/BSD.local.dist -p ${ISOROOT}/usr/local/
	mtree -deU -f ${.CURDIR}/../etc/mtree/BSD.var.dist -p ${ISOROOT}/var
	dev_mkdb -f ${ISOROOT}/var/run/dev.db ${ISOROOT}/dev

# Release CD: Kernel sources (~16M) and the full pkgsrc tree (~27M)
# Release DVD: Full sources (~90M) and the full pkgsrc tree (~27M)
#
srcs:
.if !defined(WITHOUT_SRCS)
	rm -f ${ISOROOT}/usr/pkgsrc-all.tgz
	cd ${PKGSRC_PATH} && tar --exclude distfiles --exclude packages --exclude work --exclude CVS --exclude .git \
		-czf ${ISOROOT}/usr/pkgsrc-all.tgz .
.if make(gui)
	rm -f ${ISOROOT}/usr/src-all.tgz
	cd ${.CURDIR}/.. && tar --exclude .git --exclude CVS -s '/^\./src/' \
		-czf ${ISOROOT}/usr/src-all.tgz .
.else
	rm -f ${ISOROOT}/usr/src-sys.tgz
	cd ${.CURDIR}/.. && tar --exclude .git --exclude CVS -s '/^\./src/' \
		-czf ${ISOROOT}/usr/src-sys.tgz ./Makefile ./Makefile.inc1 ./sys
.endif
.endif

# Customize the ISO by copying rootskels in reverse priority order,
# building packages, and doing other post-install tasks.
#
customizeiso:
	# Copy the rootskels.  Allow sources to be owned by someone other
	# then root (as is common when checked out via git).
	#
.for ROOTSKEL in ${ROOTSKELS}
	cpdup -X cpignore -o ${ROOTSKEL} ${ISOROOT}
	@test -O ${.CURDIR} || echo "chowning copied files to root:wheel"
	@test -O ${.CURDIR} || ((cd ${ROOTSKEL} && find .) | fgrep -v cpignore | (cd ${ISOROOT} && xargs chown root:wheel))
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
	#
	# Setup some things & mount pkgsrc tree.  Use defensive umounts and
	# rm -rf's to allow restarts.  Allow /usr/pkgsrc to be read-only.
	#
	# If we did not get past the bootstrap we clean out the entire
	# /usr/pkg infrastructure.  Otherwise we attempt to pick up where
	# we left off.
	#
	cp -p /etc/resolv.conf ${ISOROOT}/etc
	${CHROOT_CMD} "ldconfig -elf /usr/lib /usr/lib/gcc* /usr/lib/compat"
	-@umount ${ISOROOT}/usr/pkgsrc/distfiles
	-@umount ${ISOROOT}/usr/pkgsrc
	mkdir -p ${ISODIR}/distfiles
	rm -rf ${ISOROOT}/usr/pkgobj
	#
	# Mount /usr/pkgsrc, make sure /usr/pkgsrc/distfiles is writable
	#
	mkdir -p ${ISOROOT}/usr/pkgobj
	mkdir -p ${ISOROOT}/usr/pkgobj/bootstrap
	mkdir -p ${ISOROOT}/usr/pkgsrc
	mount_null ${PKGSRC_PATH} ${ISOROOT}/usr/pkgsrc
	cp /etc/shells ${ISOROOT}/usr/pkgsrc/distfiles/.test > /dev/null 2>&1 \
	    || mount_null ${ISODIR}/distfiles ${ISOROOT}/usr/pkgsrc/distfiles
	#
	# Bootstrap, if not already installed, and add licenses needed
	# for the gui release
	#
	test -e ${ISODIR}/.didbootstrap || \
	    rm -rf ${ISOROOT}/usr/pkg ${ISOROOT}/var/db/pkg \
		    ${ISOROOT}/var/db/pkg.refcount
	test -e ${ISODIR}/.didbootstrap || \
	    ${CHROOT_CMD} "cd /usr/pkgsrc/bootstrap;    \
		./bootstrap --workdir /usr/pkgobj/bootstrap/work"
	test -e ${ISODIR}/.didbootstrap || \
	    echo ".ifdef BSD_PKG_MK	# added by nrelease" \
		>> ${ISOROOT}/usr/pkg/etc/mk.conf
	test -e ${ISODIR}/.didbootstrap || \
	    echo "ACCEPTABLE_LICENSES+=openmotif-license" \
		>> ${ISOROOT}/usr/pkg/etc/mk.conf
	test -e ${ISODIR}/.didbootstrap || \
	    echo "ACCEPTABLE_LICENSES+=vim-license" \
		>> ${ISOROOT}/usr/pkg/etc/mk.conf
	test -e ${ISODIR}/.didbootstrap || \
	    echo ".endif			# added by nrelease" \
		>> ${ISOROOT}/usr/pkg/etc/mk.conf
	test -e ${ISODIR}/.didbootstrap || sync
	test -e ${ISODIR}/.didbootstrap || touch ${ISODIR}/.didbootstrap
	#
	# Build and install packages, skip packages already installed
	#
.for PKG in ${PKGSRC_PACKAGES}
	${CHROOT_CMD} "cd /usr/pkgsrc/${PKG} && \
	    (bmake WRKOBJDIR=/usr/pkgobj check > /dev/null 2>&1 || \
	    bmake WRKOBJDIR=/usr/pkgobj ${PKGSRC_OPTIONS} clean build install)"
.endfor
	#
	# Remove packages which nothing depends on and clean up
	#
	${CHROOT_CMD} "pkg_leaves | xargs pkg_delete -R"
	-umount ${ISOROOT}/usr/pkgsrc/distfiles
	umount ${ISOROOT}/usr/pkgsrc
	rmdir ${ISOROOT}/usr/pkgsrc
	rm -rf ${ISOROOT}/usr/pkgobj
	rm -f ${ISOROOT}/etc/resolv.conf
	makewhatis ${ISOROOT}/usr/local/man
	makewhatis ${ISOROOT}/usr/pkg/man

# So locate works
#
mklocatedb:
	( find -s ${ISOROOT} -path ${ISOROOT}/tmp -or \
		-path ${ISOROOT}/usr/tmp -or -path ${ISOROOT}/var/tmp \
		-prune -o -print | sed -e 's#^${ISOROOT}##g' | \
		/usr/libexec/locate.mklocatedb \
		-presort >${ISOROOT}/var/db/locate.database )

mkiso:
	( cd ${ISOROOT}; ${PKGBIN_MKISOFS} -b boot/cdboot -no-emul-boot \
		-R -J -o ${ISOFILE} \
		-V DragonFly-`${.CURDIR}/../tools/gitrev.sh | cut -c -22` . )


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
	-umount ${ISOROOT}/usr/pkgsrc/distfiles > /dev/null 2>&1
	-umount ${ISOROOT}/usr/pkgsrc > /dev/null 2>&1
	if [ -d ${ISOROOT} ]; then chflags -R noschg ${ISOROOT}; fi
	rm -rf ${ISOROOT}
	rm -rf ${NRLOBJDIR}/nrelease
	rm -f ${ISODIR}/.didbootstrap

realclean:	clean
	rm -rf ${OBJSYS}/${KERNCONF}
	#
	# do not use PKGSRC_PKG_PATH here, we do not want to destroy an
	# override location.
	#
	rm -rf ${ISODIR}/packages
	rm -rf ${ISODIR}/distfiles

fetch:
	@if [ ! -d ${PKGSRC_PKG_PATH} ]; then mkdir -p ${PKGSRC_PKG_PATH}; fi
.if !exists(${PKGSRC_PKG_PATH}/${CVSUP_BOOTSTRAP_KIT}.tgz)
	(cd ${PKGSRC_PKG_PATH}; fetch ${PKGSRC_BOOTSTRAP_URL}/${CVSUP_BOOTSTRAP_KIT}.tgz)
.endif

pkgsrc_bootstrap:
.if !exists(${PKGSRC_PKG_PATH}/${PKGSRC_BOOTSTRAP_KIT}.tgz)
	mkdir -p /usr/release/bootstrap
	(cd ${PKGSRC_PATH}/bootstrap; ./bootstrap --workdir /usr/release/bootstrap)
.endif

pkgsrc_cdrecord:
.if !exists (${PKGBIN_MKISOFS})
	(cd ${PKGSRC_PATH}/sysutils/cdrtools; bmake clean build install)
.endif

help all:
	@echo "make [gui] release   - complete build from scratch"
	@echo "make [gui] quick     - attempt to do an incremental rebuild"
	@echo "make [gui] realquick - attempt to restart after world & kernel"
	@echo "make [gui] restartpkgs - attempt to restart at the pkg building stage"
	@echo ""
	@echo "Extra packages may be specified with PKGSRC_EXTRA_PACKAGES"

.PHONY: release quickrel realquickrel
.PHONY: installer
.PHONY: quick realquick
.PHONY: check buildworld1 buildworld2
.PHONY: buildkernel1 buildkernel2 buildiso customizeiso mklocatedb mkiso mkimg
.PHONY: clean realclean fetch help all srcs

.include <bsd.prog.mk>
