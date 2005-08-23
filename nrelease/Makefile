# $DragonFly: src/nrelease/Makefile,v 1.40 2005/08/23 21:33:29 cpressey Exp $
#

ISODIR ?= /usr/release
ISOFILE ?= ${ISODIR}/dfly.iso
ISOROOT = ${ISODIR}/root
OBJSYS= ${.OBJDIR}/../sys
KERNCONF ?= GENERIC

# Specify which packages are required on the ISO, and let the user
# specify additional packages to include.  During the `pkgaddiso'
# target, the packages are obtained from PACKAGES_LOC.
#
REQ_PACKAGES= cdrtools-2.01 cvsup-without-gui-16.1h
REL_PACKAGES?= ${REQ_PACKAGES} ${EXTRA_PACKAGES}
.if defined(PACKAGES)
PACKAGES_LOC?= ${PACKAGES}/All
.else
PACKAGES_LOC?= /usr/ports/packages/All
.endif
PACKAGE_SITES?=http://www.bsdinstaller.org/packages/ \
	       http://cvs.bsdinstaller.org/packages/

# Specify which root skeletons are required, and let the user include
# their own.  They are copied into ISODIR during the `pkgcustomizeiso'
# target; each overwrites the last.
#
REQ_ROOTSKELS= ${.CURDIR}/root
ROOTSKELS?= ${REQ_ROOTSKELS} ${EXTRA_ROOTSKELS}

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
		buildiso customizeiso pkgaddiso mklocatedb mkiso

quickrel:	check clean buildworld2 buildkernel2 \
		buildiso customizeiso pkgaddiso mklocatedb mkiso

realquickrel:	check clean \
		buildiso customizeiso pkgaddiso mklocatedb mkiso

#########################################################################
#			ISO TARGETS WITH INSTALLER			#
#########################################################################

INSTALLER_PKGS= libaura-3.1 libdfui-4.1 libinstaller-5.1 \
		dfuibe_installer-1.1.6 dfuife_curses-1.5 \
		thttpd-notimeout-2.24 dfuife_cgi-1.4
INSTALLER_SKELS= installer

INSTALLER_ENV= EXTRA_PACKAGES="${INSTALLER_PKGS} ${EXTRA_PACKAGES}" \
		EXTRA_ROOTSKELS="${INSTALLER_SKELS} ${EXTRA_ROOTSKELS}"

installer_check:
		@${INSTALLER_ENV} ${MAKE} check

installer_fetchpkgs:
		@${INSTALLER_ENV} ${MAKE} fetchpkgs

installer_release:
		${INSTALLER_ENV} ${MAKE} release

installer_quickrel:
		${INSTALLER_ENV} ${MAKE} quickrel

installer_realquickrel:
		${INSTALLER_ENV} ${MAKE} realquickrel

#########################################################################
#				HELPER TARGETS				#
#########################################################################

check:
	@if [ ! -f /usr/local/bin/mkisofs ]; then \
		echo "You need to install the sysutils/cdrtools port for"; \
		echo "this target"; \
		exit 1; \
	fi
.for PKG in ${REL_PACKAGES}
	@if [ ! -f ${PACKAGES_LOC}/${PKG}.tgz ]; then \
		echo "Unable to find ${PACKAGES_LOC}/${PKG}.tgz."; \
		echo "(Perhaps you need to download or build it first?)"; \
		echo ""; \
		echo "If you are trying to build the installer, the"; \
		echo "required packages can be obtained from:"; \
		echo ""; \
		echo "    http://www.bsdinstaller.org/packages/"; \
		echo ""; \
		echo "They can be automatically downloaded by issuing"; \
		echo "'make installer_fetchpkgs' from this directory."; \
		echo ""; \
		exit 1; \
	fi
.endfor
	@echo "check: all preqs found"

fetchpkgs:
.for PKG in ${REL_PACKAGES}
	@if [ ! -f ${PACKAGES_LOC}/${PKG}.tgz ]; then \
		cd ${PACKAGES_LOC} && \
		echo "fetching ${PKG}..." && \
		for SITE in ${PACKAGE_SITES}; do \
			if [ ! -f ${PKG}.tgz ]; then \
				fetch $${SITE}${PKG}.tgz || \
				    echo "Not available from $${SITE}"; \
			fi; \
		done; \
		if [ ! -f ${PKG}.tgz ]; then \
			echo "Couldn't retrieve ${PKG}.tgz!"; \
			exit 1; \
		fi; \
	fi
.endfor

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

pkgcleaniso:
	rm -f ${ISOROOT}/tmp/chrootscript
	echo "#!/bin/sh" > ${ISOROOT}/tmp/chrootscript
.for PKG in ${REL_PACKAGES}
	echo "pkg_delete -f ${PKG}" >> ${ISOROOT}/tmp/chrootscript
.endfor
	chmod a+x ${ISOROOT}/tmp/chrootscript
	chroot ${ISOROOT}/ /tmp/chrootscript || exit 0
	rm ${ISOROOT}/tmp/chrootscript

pkgaddiso:
	rm -f ${ISOROOT}/tmp/chrootscript
	echo "#!/bin/sh" > ${ISOROOT}/tmp/chrootscript
.for PKG in ${REL_PACKAGES}
	if [ ! -d ${ISOROOT}/var/db/pkg/${PKG} ]; then \
		cp ${PACKAGES_LOC}/${PKG}.tgz ${ISOROOT}/tmp/${PKG}.tgz; \
		echo "echo 'Installing package ${PKG}...' && \\" >> \
		    ${ISOROOT}/tmp/chrootscript; \
		echo "pkg_add /tmp/${PKG}.tgz && \\" >> \
		    ${ISOROOT}/tmp/chrootscript; \
	fi
.endfor
	echo "echo 'All packages added successfully!'" >> \
	    ${ISOROOT}/tmp/chrootscript
	chmod a+x ${ISOROOT}/tmp/chrootscript
	chroot ${ISOROOT}/ /tmp/chrootscript
	rm ${ISOROOT}/tmp/chrootscript
.for PKG in ${REL_PACKAGES}
	rm -f ${ISOROOT}/tmp/${PKG}.tgz
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

.include <bsd.prog.mk>
