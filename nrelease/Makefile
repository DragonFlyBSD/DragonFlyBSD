# $DragonFly: src/nrelease/Makefile,v 1.45 2005/12/10 14:47:05 joerg Exp $
#

all: release

# compat target
installer_release: release
installer_quickrel: 
installer_realquickrel: realquickrel

.if make(installer_release) || make(installer_quickrel) || make(installer_realquickrel)
WITH_INSTALLER=
.endif

ISODIR ?= /usr/release
ISOFILE ?= ${ISODIR}/dfly.iso
ISOROOT = ${ISODIR}/root
OBJSYS= ${.OBJDIR}/../sys
KERNCONF ?= GENERIC

PKGSRC_PKG_ADD?=	/usr/pkg/sbin/pkg_add
PKGSRC_PKG_PATH?=	${.CURDIR}
PKGSRC_BOOTSTRAP_FILE?=	${PKGSRC_PKG_PATH}/bootstrap.tar.gz
PKGSRC_DB?=		/var/db/pkg
PKGSRC_PREFIX?=		/usr/pkg
PKGSRC_RSYNC_SRC?=	rsync://packages.stura.uni-rostock.de/dfly14-nrelease

ENV?=	env
TAR?=	tar
RSYNC_CMD?=	rsync -avz

PKGSRC_PACKAGES?=	cdrecord

# Specify which root skeletons are required, and let the user include
# their own.  They are copied into ISODIR during the `pkgcustomizeiso'
# target; each overwrites the last.
#
REQ_ROOTSKELS= ${.CURDIR}/root
ROOTSKELS?=	${REQ_ROOTSKELS}

.if defined(WITH_INSTALLER)
PKGSRC_PACKAGES+=	dfuibe_installer dfuife_curses
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
	@${ECHO} Testing mkisofs...
	@mkisofs --version > /dev/null
	@${ECHO} Testing pkg_add and list of packages...
	@${ENV} PKG_PATH=${PKGSRC_PKG_PATH} ${PKGSRC_PKG_ADD} -n ${PKGSRC_PACKAGES} > /dev/null 2>&1
	@${ECHO} Testing for existence of bootstrap kit...
	@[ -r ${PKGSRC_BOOTSTRAP_FILE} ]

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
.for ROOTSKEL in ${ROOTSKELS}
	cpdup -X cpignore -o ${ROOTSKEL} ${ISOROOT}
.endfor
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
	cd ${ISOROOT} && ${TAR} xzf ${PKGSRC_BOOTSTRAP_FILE}
.for pkg in ${PKGSRC_PACKAGES}
	${ENV} PKG_PATH=${PKGSRC_PKG_PATH} ${PKGSRC_PKG_ADD} -I -K ${ISOROOT}${PKGSRC_DB} -p ${ISOROOT}${PKGSRC_PREFIX} ${pkg}
.endfor

mklocatedb:
	( find -s ${ISOROOT} -path ${ISOROOT}/tmp -or \
		-path ${ISOROOT}/usr/tmp -or -path ${ISOROOT}/var/tmp \
		-prune -o -print | sed -e 's#^${ISOROOT}##g' | \
		/usr/libexec/locate.mklocatedb \
		-presort >${ISOROOT}/var/db/locate.database )

mkiso:
	( cd ${ISOROOT}; mkisofs -b boot/cdboot -no-emul-boot \
		-R -J -V DragonFly -o ${ISOFILE} . )

clean:
	if [ -d ${ISOROOT} ]; then chflags -R noschg ${ISOROOT}; fi
	if [ -d ${ISOROOT} ]; then rm -rf ${ISOROOT}/*; fi
	if [ -d ${NRLOBJDIR}/nrelease ]; then rm -rf ${NRLOBJDIR}/nrelease; fi

realclean:	clean
	rm -rf ${OBJSYS}/${KERNCONF}

fetch:
	mkdir -p ${PKGSRC_PKG_PATH}
	${RSYNC_CMD} ${PKGSRC_RSYNC_SRC} ${PKGSRC_PKG_PATH}

.PHONY: all release installer_release quickrel installer_quickrel realquickrel
.PHONY: installer_realquickrel check buildworld1 buildworld2
.PHONY: buildkernel1 buildkernel2 buildiso customizeiso mklocatedb mkiso
.PHONE: clean realclean fetch

.include <bsd.prog.mk>
