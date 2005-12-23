# $DragonFly: src/nrelease/Makefile,v 1.48.2.1 2005/12/23 21:52:09 dillon Exp $
#

# compat target
installer_release: release
installer_quickrel: 
installer_realquickrel: realquickrel
installer_fetch: fetch

.if make(installer_release) || make(installer_quickrel) || make(installer_realquickrel) || make(installer_fetch)
WITH_INSTALLER=
.endif

ISODIR ?= /usr/release
ISOFILE ?= ${ISODIR}/dfly.iso
ISOROOT = ${ISODIR}/root
OBJSYS= ${.OBJDIR}/../sys
KERNCONF ?= GENERIC

PKGSRC_PREFIX?=		/usr/pkg
PKGBIN_PKG_ADD?=	${PKGSRC_PREFIX}/sbin/pkg_add
PKGBIN_MKISOFS?=	${PKGSRC_PREFIX}/bin/mkisofs
PKGSRC_PKG_PATH?=	${ISODIR}/packages
PKGSRC_DB?=		/var/db/pkg
PKGSRC_BOOTSTRAP_URL?=	http://pkgbox.dragonflybsd.org/DragonFly-pkgsrc-packages/i386/1.4.0-RELEASE-BUILD

ENV?=	env
TAR?=	tar

PKGSRC_CDRECORD?=	cdrecord-2.00.3nb2.tgz
PKGSRC_BOOTSTRAP_KIT?=	bootstrap-kit-20051221

PKGSRC_PACKAGES?=	cdrecord-2.00.3nb2.tgz

# Specify which root skeletons are required, and let the user include
# their own.  They are copied into ISODIR during the `pkgcustomizeiso'
# target; each overwrites the last.
#
REQ_ROOTSKELS= ${.CURDIR}/root
ROOTSKELS?=	${REQ_ROOTSKELS}

.if defined(WITH_INSTALLER)
PKGSRC_PACKAGES+=	dfuibe_installer-1.1.6.tgz dfuife_curses-1.5.tgz
PKGSRC_PACKAGES+=	gettext-lib-0.11.5nb6.tgz libaura-3.1.tgz \
			libdfui-4.1.tgz libinstaller-5.1.tgz
ROOTSKELS+=		installer
.endif

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
		buildiso customizeiso mklocatedb mkiso

quickrel:	check clean buildworld2 buildkernel2 \
		buildiso customizeiso mklocatedb mkiso

realquickrel:	check clean buildiso customizeiso mklocatedb mkiso

check:
.if !exists(${PKGBIN_PKG_ADD})
	@echo "Unable to find ${PKGBIN_PKG_ADD}.  You can use the following"
	@echo "command to bootstrap pkgsrc:"
	@echo "    make pkgsrc_bootstrap"
	@exit 1
.endif
.if !exists(/etc/mk.conf)
	@echo "You do not have an /etc/mk.conf.  You can use the following"
	@echo "command to install one.  Otherwise pkgsrc defaults will not"
	@echo "point to the  right place:"
	@echo "    make pkgsrc_conf"
	@exit 1
.endif
.for PKG in ${PKGSRC_PACKAGES}
	@${ENV} PKG_PATH=${PKGSRC_PKG_PATH} ${PKGBIN_PKG_ADD} -n ${PKG} > /dev/null 2>&1 || \
		(echo "Unable to find ${PKG}, use the following command to fetch required packages:"; echo "    make [installer_]fetch"; exit 1)
.endfor
.if !exists(${PKGBIN_MKISOFS})
	@echo "mkisofs is not installed.  It is part of the cdrecord package."
	@echo "You can install it with:"
	@echo "    make pkgsrc_cdrecord"
	@exit 1
.endif

buildworld1:
	( cd ${.CURDIR}/..; CCVER=${WORLD_CCVER} make buildworld )

buildworld2:
	( cd ${.CURDIR}/..; CCVER=${WORLD_CCVER} make quickworld )

buildkernel1:
	( cd ${.CURDIR}/..; CCVER=${KERNEL_CCVER} make buildkernel KERNCONF=${KERNCONF} )

buildkernel2:
	( cd ${.CURDIR}/..; CCVER=${KERNEL_CCVER} make quickkernel KERNCONF=${KERNCONF} )

# note that we do not want to mess with any /usr/obj directories not related
# to buildworld, buildkernel, or nrelease, so we must supply the proper
# MAKEOBJDIRPREFIX for targets that are not run through the buildworld and 
# buildkernel mechanism.
#
buildiso:
	if [ ! -d ${ISOROOT} ]; then mkdir -p ${ISOROOT}; fi
	if [ ! -d ${NRLOBJDIR}/nrelease ]; then mkdir -p ${NRLOBJDIR}/nrelease; fi
	( cd ${.CURDIR}/..; make DESTDIR=${ISOROOT} installworld )
	( cd ${.CURDIR}/../etc; MAKEOBJDIRPREFIX=${NRLOBJDIR}/nrelease \
		make -m ${.CURDIR}/../share/mk DESTDIR=${ISOROOT} distribution )
	cpdup ${ISOROOT}/etc ${ISOROOT}/etc.hdd
	( cd ${.CURDIR}/..; make DESTDIR=${ISOROOT} \
		installkernel KERNCONF=${KERNCONF} )
	ln -s kernel ${ISOROOT}/kernel.BOOTP
	mtree -deU -f ${.CURDIR}/../etc/mtree/BSD.local.dist -p ${ISOROOT}/usr/local/
	mtree -deU -f ${.CURDIR}/../etc/mtree/BSD.var.dist -p ${ISOROOT}/var
	dev_mkdb -f ${ISOROOT}/var/run/dev.db ${ISOROOT}/dev

customizeiso:
	(cd ${PKGSRC_PKG_PATH}; tar xzpf ${PKGSRC_BOOTSTRAP_KIT}.tgz)
.for ROOTSKEL in ${ROOTSKELS}
	cpdup -X cpignore -o ${ROOTSKEL} ${ISOROOT}
.endfor
	rm -rf ${ISOROOT}/tmp/bootstrap ${ISOROOT}/usr/obj/pkgsrc
	cpdup ${PKGSRC_PKG_PATH}/${PKGSRC_BOOTSTRAP_KIT} ${ISOROOT}/tmp/bootstrap
	chroot ${ISOROOT} csh -c "cd /tmp/bootstrap/bootstrap; ./bootstrap"
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
	${ENV} PKG_PATH=${PKGSRC_PKG_PATH} ${PKGBIN_PKG_ADD} -I -K ${ISOROOT}${PKGSRC_DB} -p ${ISOROOT}${PKGSRC_PREFIX} ${PKG}
.endfor

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
	mkdir -p ${PKGSRC_PKG_PATH}
.for PKG in ${PKGSRC_PACKAGES}
	@${ENV} PKG_PATH=${PKGSRC_PKG_PATH} ${PKGBIN_PKG_ADD} -n ${PKG} > /dev/null 2>&1 || \
	(cd ${PKGSRC_PKG_PATH}; echo "Fetching ${PKGSRC_BOOTSTRAP_URL}/${PKG}"; fetch ${PKGSRC_BOOTSTRAP_URL}/${PKG})
.endfor
.if !exists(${PKGSRC_PKG_PATH}/${PKGSRC_BOOTSTRAP_KIT}.tgz)
	(cd ${PKGSRC_PKG_PATH}; fetch ${PKGSRC_BOOTSTRAP_URL}/${PKGSRC_BOOTSTRAP_KIT}.tgz)
.endif

pkgsrc_bootstrap:
	mkdir -p ${PKGSRC_PKG_PATH}
.if !exists(${PKGSRC_PKG_PATH}/${PKGSRC_BOOTSTRAP_KIT}.tgz)
	(cd ${PKGSRC_PKG_PATH}; fetch ${PKGSRC_BOOTSTRAP_URL}/${PKGSRC_BOOTSTRAP_KIT}.tgz)
.endif
	(cd ${PKGSRC_PKG_PATH}; tar xzpf ${PKGSRC_BOOTSTRAP_KIT}.tgz)
	(cd ${PKGSRC_PKG_PATH}/${PKGSRC_BOOTSTRAP_KIT}/bootstrap; ./bootstrap)

pkgsrc_conf:
.if !exists(/etc/mk.conf) 
	cp ${.CURDIR}/mk.conf.pkgsrc /etc/mk.conf
.else
	fgrep -q BSD_PKG_MK /etc/mk.conf || cat ${.CURDIR}/mk.conf.pkgsrc >> /etc/mk.conf
.endif

pkgsrc_cdrecord:
.if !exists (${PKGBIN_MKISOFS})
	${PKGBIN_PKG_ADD} ${PKGSRC_PKG_PATH}/cdrecord*
.endif

.PHONY: all release installer_release quickrel installer_quickrel realquickrel
.PHONY: installer_fetch
.PHONY: installer_realquickrel check buildworld1 buildworld2
.PHONY: buildkernel1 buildkernel2 buildiso customizeiso mklocatedb mkiso
.PHONY: clean realclean fetch

.include <bsd.prog.mk>
