# $DragonFly: src/nrelease/Makefile,v 1.84 2008/07/09 07:21:03 swildner Exp $
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

.if make(installer_release) || make(installer_quickrel) || make(installer_realquickrel) || make(installer_fetch)
WITH_INSTALLER=
.endif

# New method e.g. 'make installer fetch'.  A series of enhancement
# targes may be specified which set make variables which enhance
# the build in various ways.
#
gui installer:

warning:
	@echo "WARNING: The installer_* targets are now obsolete, please"
	@echo "use 'make installer blah' instead of 'make installer_blah'"
	@echo ""
	@echo "will continue in 10 seconds"
	@sleep 10

.if make(gui)
WITH_GUI=
.endif
.if make(installer)
WITH_INSTALLER=
.endif

#########################################################################
#				 SETUP		 			#
#########################################################################

ISODIR ?= /usr/release
ISOROOT ?= ${ISODIR}/root
OBJSYS= ${.OBJDIR}/../sys
KERNCONF ?= GENERIC VKERNEL

PKGSRC_PREFIX?=		/usr/pkg
PKGBIN_PKG_ADD?=	${PKGSRC_PREFIX}/sbin/pkg_add
PKGBIN_PKG_DELETE?=	${PKGSRC_PREFIX}/sbin/pkg_delete
PKGBIN_PKG_ADMIN?=	${PKGSRC_PREFIX}/sbin/pkg_admin
PKGBIN_MKISOFS?=	${PKGSRC_PREFIX}/bin/mkisofs
PKGSRC_PKG_PATH?=	${ISODIR}/packages
PKGSRC_DB?=		/var/db/pkg
PKGSRC_BOOTSTRAP_URL?=	http://pkgbox.dragonflybsd.org/DragonFly-pkgsrc-packages/i386/1.12.0-RELEASE-BUILD

ENVCMD?=	env
TAR?=	tar

PKGSRC_CDRECORD?=	cdrtools-ossdvd-2.01.1.36nb2.tgz
PKGSRC_BOOTSTRAP_KIT?=	bootstrap-kit-20080211
CVSUP_BOOTSTRAP_KIT?=	cvsup-bootstrap-20070716

# Default packages to be installed on the release ISO.
#
PKGSRC_PACKAGES?=	cdrtools-ossdvd-2.01.1.36nb2.tgz

# Even though buildiso wipes the packages, our check target has to run
# first and old packages (listed as they appear in pkg_info) must be
# cleaned out in order for the pkg_add -n test we use in the check target
# to operate properly.
#
OLD_PKGSRC_PACKAGES?= cdrtools-2.01.01.27nb1 cdrecord-2.00.3nb2 \
		      bootstrap-kit-20070205

# Specify which root skeletons are required, and let the user include
# their own.  They are copied into ISODIR during the `pkgcustomizeiso'
# target; each overwrites the last.
#
REQ_ROOTSKELS= ${.CURDIR}/root
ROOTSKELS?=	${REQ_ROOTSKELS}

.if defined(WITH_GUI)
ISOFILE?=		${ISODIR}/dfly-gui.iso
PKGSRC_PACKAGES+=	modular-xorg-apps \
			modular-xorg-drivers \
			modular-xorg-fonts \
			modular-xorg-libs \
			fluxbox
ROOTSKELS+=		${.CURDIR}/gui
.endif

.if defined(WITH_INSTALLER)
ROOTSKELS+=		${.CURDIR}/installer
.endif

ISOFILE ?= ${ISODIR}/dfly.iso

# note: we use the '${NRLOBJDIR}/nrelease' construct, that is we add
# the additional '/nrelease' manually, as a safety measure.
#
NRLOBJDIR?= /usr/obj

WORLD_CCVER ?= ${CCVER}
KERNEL_CCVER ?= ${CCVER}

#########################################################################
#				BASE ISO TARGETS 			#
#########################################################################

release:	check clean buildworld1 buildkernel1 \
		buildiso syssrcs customizeiso mklocatedb mkiso

quickrel:	check clean buildworld2 buildkernel2 \
		buildiso syssrcs customizeiso mklocatedb mkiso

realquickrel:	check clean buildiso syssrcs customizeiso mklocatedb mkiso

quick:		quickrel

realquick:	realquickrel

#########################################################################
#			   CORE SUPPORT TARGETS 			#
#########################################################################

check:
.if !exists(${PKGBIN_PKG_ADD})
	@echo "Unable to find ${PKGBIN_PKG_ADD}.  You can use the following"
	@echo "command to bootstrap pkgsrc:"
	@echo "    make pkgsrc_bootstrap"
	@exit 1
.endif
.for PKG in ${OLD_PKGSRC_PACKAGES}
	@${ENVCMD} PKG_PATH=${PKGSRC_PKG_PATH} ${PKGBIN_PKG_DELETE} -K ${ISOROOT}${PKGSRC_DB} ${PKG} > /dev/null 2>&1 || exit 0
.endfor
.for PKG in ${PKGSRC_PACKAGES}
	@${ENVCMD} PKG_PATH=${PKGSRC_PKG_PATH} ${PKGBIN_PKG_ADD} -K ${ISOROOT}${PKGSRC_DB} -n ${PKG} > /dev/null 2>&1 || \
		(echo "Unable to find ${PKG}, use the following command to fetch required packages:"; echo "    make [installer] fetch"; exit 1)
.endfor
.if !exists(${PKGBIN_MKISOFS})
	@echo "mkisofs is not installed.  It is part of the cdrecord package."
	@echo "You can install it with:"
	@echo "    make pkgsrc_cdrecord"
	@exit 1
.endif
.if !exists(${PKGSRC_PKG_PATH}/${PKGSRC_BOOTSTRAP_KIT}.tgz)
	@echo "The pkgsrc bootstrap kit is not installed.  You can install it with:"
	@echo "    make [installer] fetch"
	@exit 1
.endif
.if !exists(${PKGSRC_PKG_PATH}/${CVSUP_BOOTSTRAP_KIT}.tgz)
	@echo "The cvsup bootstrap kit is not installed.  You can install it with:"
	@echo "    make [installer] fetch"
	@exit 1
.endif

buildworld1 buildworld2:
	cd ${.CURDIR}/..; CCVER=${WORLD_CCVER} make ${WITH_INSTALLER:C/^/-DWANT_INSTALLER/} ${.TARGET:C/build(.*)2/quick\1/:C/1//}

buildkernel1 buildkernel2:
	cd ${.CURDIR}/..; \
	first=; \
	for kernconf in ${KERNCONF}; do \
		CCVER=${KERNEL_CCVER} make ${.TARGET:C/build(.*)2/quick\1/:C/1//} \
			KERNCONF=$${kernconf} \
			$${first:+-DNO_MODULES}; \
		first=done; \
	done

# note that we do not want to mess with any /usr/obj directories not related
# to buildworld, buildkernel, or nrelease, so we must supply the proper
# MAKEOBJDIRPREFIX for targets that are not run through the buildworld and 
# buildkernel mechanism.
#
buildiso:
	if [ ! -d ${ISOROOT} ]; then mkdir -p ${ISOROOT}; fi
	if [ ! -d ${NRLOBJDIR}/nrelease ]; then mkdir -p ${NRLOBJDIR}/nrelease; fi
	( cd ${.CURDIR}/..; make ${WITH_INSTALLER:C/^/-DWANT_INSTALLER/} DESTDIR=${ISOROOT} installworld )
	( cd ${.CURDIR}/../etc; MAKEOBJDIRPREFIX=${NRLOBJDIR}/nrelease \
		make -m ${.CURDIR}/../share/mk DESTDIR=${ISOROOT} distribution )
	cpdup ${ISOROOT}/etc ${ISOROOT}/etc.hdd
	cd ${.CURDIR}/..; \
	first=; \
	for kernconf in ${KERNCONF}; do \
		make DESTDIR=${ISOROOT} \
			installkernel KERNCONF=$${kernconf} \
			$${first:+DESTKERNNAME=kernel.$${kernconf}} \
			$${first:+-DNO_MODULES}; \
		first=done; \
	done
	ln -s kernel ${ISOROOT}/kernel.BOOTP
	mtree -deU -f ${.CURDIR}/../etc/mtree/BSD.local.dist -p ${ISOROOT}/usr/local/
	mtree -deU -f ${.CURDIR}/../etc/mtree/BSD.var.dist -p ${ISOROOT}/var
	dev_mkdb -f ${ISOROOT}/var/run/dev.db ${ISOROOT}/dev

# Include kernel sources on the release CD (~14MB)
#
syssrcs:
.if !defined(WITHOUT_SRCS)
	( cd ${.CURDIR}/../..; tar --exclude CVS -cf - src/Makefile src/Makefile.inc1 src/sys | bzip2 -9 > ${ISOROOT}/usr/src-sys.tar.bz2 )
.endif

customizeiso:
	(cd ${PKGSRC_PKG_PATH}; tar xzpf ${CVSUP_BOOTSTRAP_KIT}.tgz)
.for ROOTSKEL in ${ROOTSKELS}
	cpdup -X cpignore -o ${ROOTSKEL} ${ISOROOT}
.endfor
	rm -rf ${ISOROOT}/tmp/bootstrap ${ISOROOT}/usr/obj/pkgsrc
	cd ${ISOROOT}; tar xvzpf ${PKGSRC_PKG_PATH}/${PKGSRC_BOOTSTRAP_KIT}.tgz
	cp -p ${PKGSRC_PKG_PATH}/${CVSUP_BOOTSTRAP_KIT}/usr/local/bin/cvsup ${ISOROOT}/usr/local/bin/cvsup
	cp -p ${PKGSRC_PKG_PATH}/${CVSUP_BOOTSTRAP_KIT}/usr/local/man/man1/cvsup.1 ${ISOROOT}/usr/local/man/man1/cvsup.1
	rm -rf ${ISOROOT}/tmp/bootstrap ${ISOROOT}/usr/obj/pkgsrc
	rm -rf `find ${ISOROOT} -type d -name CVS -print`
	rm -rf ${ISOROOT}/usr/local/share/pristine
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
.for PKG in ${PKGSRC_PACKAGES}
	${ENVCMD} PKG_PATH=${PKGSRC_PKG_PATH} ${PKGBIN_PKG_ADD} -K ${ISOROOT}${PKGSRC_DB} -I ${PKG}
.endfor
	find ${ISOROOT}${PKGSRC_DB} -name +CONTENTS -type f -exec sed -i '' -e 's,${ISOROOT},,' -- {} \;
	${PKGBIN_PKG_ADMIN} -K ${ISOROOT}${PKGSRC_DB} rebuild
.if defined(WITH_GUI)
.for FONT in 75dpi 100dpi misc Type1 TTF
	chroot ${ISOROOT} /usr/pkg/bin/mkfontdir /usr/pkg/lib/X11/fonts/${FONT}
.endfor
.endif

mklocatedb:
	( find -s ${ISOROOT} -path ${ISOROOT}/tmp -or \
		-path ${ISOROOT}/usr/tmp -or -path ${ISOROOT}/var/tmp \
		-prune -o -print | sed -e 's#^${ISOROOT}##g' | \
		/usr/libexec/locate.mklocatedb \
		-presort >${ISOROOT}/var/db/locate.database )

mkiso:
	( cd ${ISOROOT}; ${PKGBIN_MKISOFS} -b boot/cdboot -no-emul-boot \
		-R -J -V DragonFly -o ${ISOFILE} . )

clean:
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
.for PKG in ${PKGSRC_PACKAGES}
	@${ENVCMD} PKG_PATH=${PKGSRC_PKG_PATH} ${PKGBIN_PKG_ADD} -K ${ISOROOT}${PKGSRC_DB} -n ${PKG} > /dev/null 2>&1 || \
	(cd ${PKGSRC_PKG_PATH}; echo "Fetching ${PKGSRC_BOOTSTRAP_URL}/${PKG}"; fetch ${PKGSRC_BOOTSTRAP_URL}/${PKG})
.endfor
.if !exists(${PKGSRC_PKG_PATH}/${PKGSRC_BOOTSTRAP_KIT}.tgz)
	(cd ${PKGSRC_PKG_PATH}; fetch ${PKGSRC_BOOTSTRAP_URL}/${PKGSRC_BOOTSTRAP_KIT}.tgz)
.endif
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
.PHONY: buildkernel1 buildkernel2 buildiso customizeiso mklocatedb mkiso
.PHONY: clean realclean fetch

.include <bsd.prog.mk>
