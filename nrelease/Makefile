#########################################################################
#				ENHANCEMENTS				#
#########################################################################

# Optional targets may be specified which set make variables which enhance
# the build in various ways.
#
gui nopkgs binpkgs:

#########################################################################
#				SETUP					#
#########################################################################

SRCDIR=		${.CURDIR}/..

DPORTS_PATH?=	/usr/dports
ISODIR?=	/usr/obj/release
ISOROOT?=	${ISODIR}/root
GITURL_SRC?=	git://git.dragonflybsd.org/dragonfly.git
GITURL_DPORTS?=	git://mirror-master.dragonflybsd.org/dports.git
NREL_MAKE_JOBS?= $$(sysctl -n hw.ncpu)
.if !defined(NOFSCHG)
MTREE_FSCHG=	-i
.endif

GITREV!=	sh ${SRCDIR}/tools/gitrev.sh

# The label/name of the disklabel64(5) slice in the ".img" file.  This is
# used to identify the USB device with DragonFly installation image, avoiding
# hardcoding the USB device name (e.g., "da8").
#
# This label is also used as the volume ID of the ".iso" file.
#
LABEL?=		DragonFly_${GITREV}

CHROOT_CMD?=	/usr/sbin/chroot ${ISOROOT} sh -c

ISOFILE?=	${ISODIR}/dfly.iso
IMGFILE?=	${ISODIR}/dfly.img
IMGMNT?=	${ISODIR}/mnt

.if !make(nopkgs)
# User may specify extra packages in addition to the defaults
#
DPORTS_EXTRA_PACKAGES?=

# dports packages to be built and installed on the release ISO
#
DPORTS_PACKAGES?=	security/ca_root_nss \
			devel/git-lite \
			dns/bind-tools \
			net/isc-dhcp44-server \
			sysutils/cdrtools \
			${DPORTS_EXTRA_PACKAGES}

# dports options to use when building packages
#
DPORTS_OPTIONS+=	-DBATCH
DPORTS_OPTIONS+=	dns_bind-tools_UNSET=PYTHON
.endif

# Specify which root skeletons are required, and let the user include
# their own.  They are copied into ISODIR during the `customizeiso'
# target; each overwrites the last.
#
REQ_ROOTSKELS=	${.CURDIR}/root
ROOTSKELS?=	${REQ_ROOTSKELS}

.if make(gui)
ISOFILE?=		${ISODIR}/dfly-gui.iso
IMGFILE?=		${ISODIR}/dfly-gui.img

.if !make(nopkgs)
# NOTE: order important, do not sort package list
#
DPORTS_PACKAGES+=	x11/xorg \
			x11-wm/fluxbox \
			x11-wm/fvwm \
			www/firefox \
			graphics/xpdf \
			shells/zsh \
			editors/emacs \
			editors/vim \
			irc/irssi \
			lang/perl5 \
			editors/nano \
			shells/bash \
			devel/ctags \
			archivers/zip \
			security/sudo \
			www/links \
			ftp/wget \
			x11-fonts/terminus-font \
			net/rsync \
			x11-clocks/asclock \
			sysutils/screen
.endif

ROOTSKELS+=		${.CURDIR}/gui
.endif  # make(gui)

# one port may have multiple binary packages (e.g., Python flavors) or have a
# different name that may not be found by pkg(8), therefore, allow to specify
# the exact binary package name for a port by setting 'PKG_<port>=<pkg-name>'.
#
.for PORT in ${DPORTS_PACKAGES}
.if defined(PKG_${PORT})
PACKAGES+=	${PKG_${PORT}}
.else
PACKAGES+=	${PORT}
.endif
.endfor

#########################################################################
#				BASE ISO TARGETS			#
#########################################################################

release:	check clean buildworld1 buildkernel1 \
		buildiso customizeiso pkgs srcs mkiso mkimg

quickrel:	check clean buildworld2 buildkernel2 \
		buildiso customizeiso pkgs srcs mkiso mkimg

realquickrel:	check clean \
		buildiso customizeiso pkgs srcs mkiso mkimg

restartpkgs:	check pkgs srcs mkiso mkimg

quick:		quickrel

realquick:	realquickrel

#########################################################################
#			CORE SUPPORT TARGETS				#
#########################################################################

check:
.if !exists(/usr/local/bin/mkisofs) && !exists(/usr/pkg/bin/mkisofs)
	@echo
	@echo "Your machine does not have cdrtools installed.  You can install it with:"
	@echo "    pkg install cdrtools"
.endif
	@if [ ! -d ${DPORTS_PATH} ]; then \
		echo "${DPORTS_PATH} does not exist."; \
		echo ""; \
		echo "Please set DPORTS_PATH to the dports tree that shall be used for"; \
		echo "package building. The default is /usr/dports. See the Makefile in"; \
		echo "/usr if you are unfamiliar with dports."; \
		/usr/bin/false; \
	fi
.if !exists(/usr/local/bin/mkisofs) && !exists(/usr/pkg/bin/mkisofs)
	@/usr/bin/false
.endif
	@echo "Using ${DPORTS_PATH} as the dports tree."

buildworld1 buildworld2:
	( cd ${SRCDIR}; \
		${WORLD_CCVER:C/^..*$/WORLD_CCVER=/}${WORLD_CCVER} \
			make -j ${NREL_MAKE_JOBS} -DWANT_INSTALLER \
			${.TARGET:C/build(.*)2/quick\1/:C/1//} )

buildkernel1 buildkernel2:
	( cd ${SRCDIR}; \
		${WORLD_CCVER:C/^..*$/WORLD_CCVER=/}${WORLD_CCVER} \
			make -j ${NREL_MAKE_JOBS} \
			${.TARGET:C/build(.*)2/quick\1/:C/1//} )

# Unconditionally clean out ${ISOROOT} so a previous img build
# does not blow up a future quick iso build
#
buildiso:
	-chflags -R noschg ${ISOROOT}
	rm -rf ${ISOROOT}
	mkdir -p ${ISOROOT}
	( cd ${SRCDIR}; \
		make -DWANT_INSTALLER DESTDIR=${ISOROOT} installworld )
	# Do not mess with any /usr/obj directories not related to
	# buildworld, buildkernel, or nrelease.
	( cd ${SRCDIR}/etc && \
		tmpdir=`mktemp -d -t nrelease` && \
		MAKEOBJDIRPREFIX=$${tmpdir} \
			make -m ${SRCDIR}/share/mk \
			DESTDIR=${ISOROOT} distribution && \
		rm -rf $${tmpdir} )
	cpdup ${ISOROOT}/etc ${ISOROOT}/etc.hdd
	( cd ${SRCDIR}; make DESTDIR=${ISOROOT} reinstallkernel )
	rm -rf ${ISOROOT}/boot/kernel.old
	ln -sf kernel ${ISOROOT}/boot/kernel/kernel.BOOTP
	mtree ${MTREE_FSCHG} -deU -f ${SRCDIR}/etc/mtree/BSD.var.dist \
		-p ${ISOROOT}/var
	dev_mkdb -f ${ISOROOT}/var/run/dev.db ${ISOROOT}/dev
	${CHROOT_CMD} "rcrestart ldconfig"

# The GUI build includes the full system source (~500 MB) and the full
# dports tree (~250 MB).  The nominal release build only includes the
# kernel source (~30 MB).
#
srcs:
	rm -rf ${ISOROOT}/usr/dports
	rm -f ${ISOROOT}/usr/src-sys.tar.bz2
.if !defined(WITHOUT_SRCS)
.if make(gui)
	( cd ${ISOROOT}/usr && \
		make dports-create-shallow GITURL_DPORTS=${GITURL_DPORTS} )
	( cd ${ISOROOT}/usr && \
		make src-create-shallow GITURL_SRC=${GITURL_SRC} )
.else
	( cd ${SRCDIR} && \
		tar --exclude .git -s '/^\./src/' -cf - \
			./Makefile ./Makefile.inc1 ./sys \
			./share/syscons/fonts | \
			bzip2 -9 > ${ISOROOT}/usr/src-sys.tar.bz2 )
.endif
.endif

# Customize the ISO by copying rootskels in reverse priority order.
#
customizeiso:
	# Copy the rootskels.  Allow sources to be owned by someone other
	# than root (as is common when checked out via git).
	#
.for ROOTSKEL in ${ROOTSKELS}
	cpdup -X cpignore -o ${ROOTSKEL} ${ISOROOT}
	@test -O ${.CURDIR} || echo "chowning copied files to root:wheel"
	@test -O ${.CURDIR} || ((cd ${ROOTSKEL} && find .) | fgrep -v cpignore | (cd ${ISOROOT} && xargs chown root:wheel))
.endfor

	pwd_mkdb -p -d ${ISOROOT}/etc ${ISOROOT}/etc/master.passwd
	pw -V ${ISOROOT}/etc useradd installer -o -u 0 -g 0 \
		-c "DragonFly Installer" -d /root -s /usr/sbin/installer
	${CHROOT_CMD} "chpass -p '' root"
	${CHROOT_CMD} "chpass -p '' installer"

.for UPGRADE_ITEM in Makefile			\
		     etc.${MACHINE_ARCH} 	\
		     rc.d/Makefile		\
		     periodic/Makefile		\
		     periodic/daily/Makefile	\
		     periodic/security/Makefile	\
		     periodic/weekly/Makefile	\
		     periodic/monthly/Makefile
	cp -R ${SRCDIR}/etc/${UPGRADE_ITEM} ${ISOROOT}/etc/${UPGRADE_ITEM}
.endfor

# Install packages by using pkg(8) or building from dports.
#
pkgs:
.if !empty(DPORTS_PACKAGES)
	cp /etc/resolv.conf ${ISOROOT}/etc
	-@umount ${ISOROOT}/dev
	mount_null /dev ${ISOROOT}/dev

.if make(binpkgs)
	${CHROOT_CMD} "cd /usr && make pkg-bootstrap-force"
	${CHROOT_CMD} "pkg update"
.for PKG in ${PACKAGES}
	@${CHROOT_CMD} "pkg search --exact --search name ${PKG}" || \
	${CHROOT_CMD} "pkg search --exact --search origin ${PKG}" || \
	{ \
		echo "ERROR: Cannot find the package for port '${PKG}'!"; \
		echo "-----> Use 'PKG_${PKG}=<pkg-name>' to specify the package name."; \
		false; \
	}
.endfor
	${CHROOT_CMD} "pkg install --yes ${PACKAGES}"

.else  # !make(binpkgs)
	-@umount ${ISOROOT}/usr/distfiles
	-@umount ${ISOROOT}/usr/dports
	rm -rf ${ISOROOT}/usr/obj/dports

	mkdir -p ${ISOROOT}/usr/dports
	mkdir -p ${ISOROOT}/usr/distfiles

	# Mount /usr/dports read-only for safety, else a failed umount
	# and our rm -rf will do bad things.
	mount_null -o ro ${DPORTS_PATH} ${ISOROOT}/usr/dports

	# Make sure /usr/distfiles is writable
	cp /etc/shells ${ISOROOT}/usr/distfiles/.test > /dev/null 2>&1 \
	    || mount_null ${ISODIR}/distfiles ${ISOROOT}/usr/distfiles

.for PKG in ${DPORTS_PACKAGES}
.if make(restartpkgs)
	${CHROOT_CMD} "cd /usr/dports/${PKG} && make ${DPORTS_OPTIONS} deinstall"
.endif
	${CHROOT_CMD} "cd /usr/dports/${PKG} && make ${DPORTS_OPTIONS} install"
.endfor
.for PKG in ${DPORTS_PACKAGES}
	${CHROOT_CMD} "cd /usr/dports/${PKG} && make ${DPORTS_OPTIONS} clean"
.endfor

	${CHROOT_CMD} "pkg autoremove --yes"

	-umount ${ISOROOT}/usr/distfiles
	umount ${ISOROOT}/usr/dports

	rm -rf ${ISOROOT}/usr/dports
	rm -rf ${ISOROOT}/usr/distfiles
	rm -rf ${ISOROOT}/usr/obj/dports
.endif  # make(binpkgs)

	# Update the locate(8) and whatis(1) databases, allow ISODIR
	# to be on tmpfs (fails to create locate database then)
	#
	-${CHROOT_CMD} /etc/periodic/weekly/310.locate
	-${CHROOT_CMD} /etc/periodic/weekly/320.whatis

	umount ${ISOROOT}/dev
	rm -f ${ISOROOT}/etc/resolv.conf

.if exists(${ISOROOT}/usr/local/etc)
	echo "dummy /usr/local/etc tmpfs rw,-C 0 0" >> ${ISOROOT}/etc/fstab
.endif

	# Recopy files that dports may have updated in /etc into /etc.hdd
	#
	cpdup ${ISOROOT}/etc/shells ${ISOROOT}/etc.hdd/shells
	cpdup ${ISOROOT}/etc/group ${ISOROOT}/etc.hdd/group
	cpdup ${ISOROOT}/etc/passwd ${ISOROOT}/etc.hdd/passwd
	cpdup ${ISOROOT}/etc/master.passwd ${ISOROOT}/etc.hdd/master.passwd
.endif  # !empty(DPORTS_PACKAGES)

mkiso:
	if [ ! -d ${IMGMNT} ]; then mkdir -p ${IMGMNT}; fi
	-if [ -f ${ISODIR}/vn.which ]; then umount ${IMGMNT}; 		\
	    vnconfig -u `cat ${ISODIR}/vn.which`; fi
	newfs_msdos -C 400k -F 12 -L EFI -m 0xf8 ${ISOROOT}/boot/efiboot.img
	vnconfig -l | grep "not in use" | head -n 1 | 			\
	    cut -f 1 -d: > ${ISODIR}/vn.which
	vnconfig -e `cat ${ISODIR}/vn.which` ${ISOROOT}/boot/efiboot.img
	mount_msdos /dev/`cat ${ISODIR}/vn.which` ${IMGMNT}
	mkdir -p ${IMGMNT}/EFI/BOOT
	cp ${ISOROOT}/boot/loader.efi ${IMGMNT}/EFI/BOOT/BOOTX64.EFI
	umount ${IMGMNT}
	( cd ${ISOROOT}; mkisofs -R -J -o ${ISOFILE} \
	    -b boot/cdboot -no-emul-boot \
	    -eltorito-alt-boot -eltorito-platform efi \
	    -eltorito-boot boot/efiboot.img -no-emul-boot \
	    -V ${LABEL:C/(^.{32}).*/\1/} . )  # cut label to <= 32 characters
	rm -f ${ISOROOT}/boot/efiboot.img

mkimg:
	if [ ! -d ${IMGMNT} ]; then mkdir -p ${IMGMNT}; fi
	-if [ -f ${ISODIR}/vn.which ]; then umount ${IMGMNT}; 		\
	    vnconfig -u `cat ${ISODIR}/vn.which`; fi
.ifdef IMGSIZE
	@echo "STEP: use an image size of ${IMGSIZE} 512-byte sectors"
	rm -f ${IMGFILE}
	sz=`bc -e "((${IMGSIZE}) * 512)" -equit`; truncate -s $${sz} ${IMGFILE}
.else
	@echo "STEP: Determine required image size in 1GB steps"
	@echo "      Leave ~600MB of unused space"
	sz=`du -ck ${ISOROOT} | tail -n 1 | cut -f 1`;			\
	    sz=`bc -e "(($${sz}) * 1.15 + 999999 + 600000) / 1000000" -equit | \
	    cut -f1 -d.`;						\
	    sz=`bc -e "(($${sz}) * 953)" -equit | cut -f1 -d.`;		\
	    rm -f ${IMGFILE};						\
	    truncate -s $${sz}M ${IMGFILE};
.endif
	@echo "STEP: determine free vn device"
	vnconfig -l | grep "not in use" | head -n 1 | 			\
	    cut -f 1 -d: > ${ISODIR}/vn.which
	vnconfig -e `cat ${ISODIR}/vn.which` ${IMGFILE}
	@echo "STEP: set up legacy MBR"
	fdisk -b ${ISOROOT}/boot/mbr -IB `cat ${ISODIR}/vn.which`
	fdisk -s `cat ${ISODIR}/vn.which` >${ISODIR}/fdisk.dat
	awk '(NR==1){printf("g c%s h%s s%s\n", $$2, $$4, $$6);}' \
	    ${ISODIR}/fdisk.dat >${ISODIR}/fdisk.conf
	echo "p 1 239 63 257985" >>${ISODIR}/fdisk.conf
	awk '($$1=="1:"){printf("p 2 108 258048 %lu\n", $$3 - 258528);}' \
	    ${ISODIR}/fdisk.dat >>${ISODIR}/fdisk.conf
	echo "a 2" >>${ISODIR}/fdisk.conf
	fdisk -iv -f ${ISODIR}/fdisk.conf `cat ${ISODIR}/vn.which`
	rm ${ISODIR}/fdisk.conf ${ISODIR}/fdisk.dat
	newfs_msdos -F 32 -c 2 -L EFI -m 0xf8 `cat ${ISODIR}/vn.which`s1
	mount_msdos /dev/`cat ${ISODIR}/vn.which`s1 ${IMGMNT}
	mkdir -p ${IMGMNT}/EFI/BOOT
	cp ${ISOROOT}/boot/boot1.efi ${IMGMNT}/EFI/BOOT/BOOTX64.EFI
	umount ${IMGMNT}
	@echo "STEP: write standard disklabel"
	disklabel -w -r `cat ${ISODIR}/vn.which`s2 auto
	@echo "STEP: read disklabel back"
	disklabel -r `cat ${ISODIR}/vn.which`s2 > ${IMGFILE}.label
	@echo "STEP: set disklabel name"
	echo "label: ${LABEL}" >> ${IMGFILE}.label
	@echo "STEP: add slice parition"
	echo "a: * * 4.2BSD" >> ${IMGFILE}.label;
	@echo "STEP: write modified disklabel back"
	disklabel -R -r `cat ${ISODIR}/vn.which`s2 ${IMGFILE}.label
	rm ${IMGFILE}.label
	disklabel -B -b ${ISOROOT}/boot/boot1_64 -s ${ISOROOT}/boot/boot2_64 \
	    `cat ${ISODIR}/vn.which`s2
	newfs /dev/`cat ${ISODIR}/vn.which`s2a
	mount /dev/`cat ${ISODIR}/vn.which`s2a ${IMGMNT}
	cpdup ${ISOROOT} ${IMGMNT}
	@echo "STEP: fixup ${IMGMNT}/etc/fstab"
	echo "/dev/part-by-label/${LABEL}.a / ufs rw,noatime 0 1" > ${IMGMNT}/etc/fstab
	echo "dummy /tmp tmpfs rw 0 0" >> ${IMGMNT}/etc/fstab
	echo "dummy /var/tmp tmpfs rw 0 0" >> ${IMGMNT}/etc/fstab
	echo "dummy /var/run tmpfs rw,-C 0 0" >> ${IMGMNT}/etc/fstab
	echo "dummy /usr/obj tmpfs rw 0 0" >> ${IMGMNT}/etc/fstab
	echo "proc /proc procfs rw 0 0" >> ${IMGMNT}/etc/fstab
	@echo "STEP: fixup ${IMGMNT}/boot/loader.conf"
	-fgrep -v kernel_options ${IMGMNT}/boot/loader.conf > ${IMGMNT}/boot/loader.conf.new
	echo 'vfs.root.mountfrom="ufs:part-by-label/${LABEL}.a"' >> ${IMGMNT}/boot/loader.conf.new
	mv ${IMGMNT}/boot/loader.conf.new ${IMGMNT}/boot/loader.conf
	@echo "STEP: create /firstboot"
	touch ${IMGMNT}/firstboot
	@echo "STEP: cleanup"
	df ${IMGMNT}
	sync
	sleep 1
	umount ${IMGMNT}
	vnconfig -u `cat ${ISODIR}/vn.which`
	rm -f ${ISODIR}/vn.which
	rmdir ${IMGMNT}
	@echo "STEP: done, image files are in ${ISODIR}"

clean:
	sync
	sleep 1
	-umount ${ISOROOT}/usr/distfiles > /dev/null 2>&1
	-umount ${ISOROOT}/usr/dports > /dev/null 2>&1
	-umount ${ISOROOT}/dev > /dev/null 2>&1
	-if [ -f ${ISODIR}/vn.which ]; then umount ${IMGMNT}; 		\
	    vnconfig -u `cat ${ISODIR}/vn.which`; fi
	if [ -d ${ISOROOT} ]; then chflags -R noschg ${ISOROOT}; fi
	rm -rf ${ISOROOT}
	rm -f ${ISODIR}/.didbootstrap ${ISODIR}/vn.which

realclean:	clean
	rm -rf ${ISODIR}/packages
	rm -rf ${ISODIR}/distfiles

.MAIN: help
help:
	@echo "Targets:"
	@echo "  release     - full build from scratch"
	@echo "  quick       - attempt to do an incremental rebuild"
	@echo "  realquick   - attempt to restart after world & kernel"
	@echo "  restartpkgs - attempt to restart at the pkg building stage"
	@echo ""
	@echo "Optional targets:"
	@echo "  nopkgs      - do not install any packages"
	@echo "  binpkgs     - use binary packages with pkg(8)"
	@echo "  gui         - do a GUI release"
	@echo ""
	@echo "Variables:"
	@echo "  DPORTS_EXTRA_PACKAGES: add additional packages"
	@echo "  GITURL_SRC: override the Git URL to source repository"
	@echo "  GITURL_DPORTS: override the Git URL to dports repository"
	@echo "  IMGSIZE: override the size of .img (in 512-byte sectors)"
	@echo "  NREL_MAKE_JOBS: override the default value (sysctl hw.ncpu)"
	@echo "  PKG_<port>: specify the package name for port <port>"
	@echo "  WITHOUT_SRCS: do not package source code if set"
	@echo ""

.PHONY: release quickrel realquickrel
.PHONY: quick realquick
.PHONY: check buildworld1 buildworld2
.PHONY: buildkernel1 buildkernel2 buildiso customizeiso mkiso mkimg
.PHONY: clean realclean help all srcs pkgs
