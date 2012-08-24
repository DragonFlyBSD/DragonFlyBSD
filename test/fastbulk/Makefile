# Makefile for fastbulk - fast incremental pkgsrc bulk [re]build
#
# deinstall _UPDATE_RUNNING=YES DEINSTALLDEPENDS=ALL
# package
# show-depends	(output format wildcard:relative_path_to_pkgsrc_dir)

BUILDBASE=/build/fastbulk
BUILDROOT=${BUILDBASE}/root
SYSPKGSRC=/usr/pkgsrc
SYSROOT=/
CHROOTENV=chroot ${BUILDROOT} /bin/sh -c
NPARALLEL?= 8
# -DNOTMPFS

all:
	@echo "fastbulk build options"
	@echo "    make init         - create/initialize topology"
	@echo "    make build        - build all packages from sources"
	@echo "    make quick        - incremental build restart"
	@echo "    make umount       - umount the build environment"
	@echo "    make cleanup      - umount, rm binpkgs and pkgs"
	@echo ""
	@echo "    make changes      - deinstall modified packages"
	@echo "    make destroy      - destroy the fastbulk setup"
	@echo ""
	@echo "make variables (defaults shown):"
	@echo ""
	@echo "    BUILDBASE=/build/fastbulk     all fastbulk-generated files"
	@echo "    SYSPKGSRC=/usr/pkgsrc         where to get pkgsrc from"
	@echo "    SYSROOT=/                     rootenv to replicate"
	@echo "    NPARALLEL=8                   maximum concurrency"
	@echo ""
	@echo "NOTE: ${BUILDBASE}/{distfiles,packages} are normally retained"
	@echo "      to avoid having to re-pull and for incremental operation."
	@echo "      'build' will delete all packages, 'quick' will not."
	@echo ""
	@echo "NOTE: When building, the current progress is in "
	@echo "      ${BUILDROOT}/tmp/logs/{run,good,bad}.  Each build"
	@echo "      has its own log file named after the pkgsrc path."

init:	umount initialize mount

build:	umount mount bootstrap
	${CHROOTENV} "cd /tmp/track; ./fastbulk -j ${NPARALLEL} /usr/pkgsrc"

quick:	umount mount quickstrap
	${CHROOTENV} "cd /tmp/track; ./fastbulk -j ${NPARALLEL} /usr/pkgsrc"

cleanup: umount
	cpdup -i0 -s0 -o ${BUILDBASE}/packages ${BUILDBASE}/packages.save
	find -x ${BUILDBASE}/packages -depth -depth +0 -delete
	find -x ${BUILDBASE}/track -depth -depth +0 -delete
	find -x ${BUILDBASE}/bin -depth -depth +0 -delete
	find -x ${BUILDBASE}/tmp/logs -depth -depth +0 -delete
	find -x ${BUILDBASE}/var.db.pkg -depth -depth +0 -delete
	find -x ${BUILDBASE}/var.db.pkg.refcount -depth -depth +0 -delete
	find -x ${BUILDBASE}/usr.pkg -depth -depth +0 -delete
	rm -rf ${BUILDBASE}/usr.obj/work
	mkdir -p ${BUILDBASE}/tmp/logs/run
	mkdir -p ${BUILDBASE}/tmp/logs/good
	mkdir -p ${BUILDBASE}/tmp/logs/bad

destroy: umount
	rm -rf ${BUILDBASE}

umount:
	-resident -d ${BUILDROOT}/usr/pkg/bin/bmake
	-umount ${BUILDROOT}/usr/pkgsrc/distfiles > /dev/null 2>&1
	-umount ${BUILDROOT}/usr/pkgsrc/packages > /dev/null 2>&1
	-umount ${BUILDROOT}/usr/pkgsrc > /dev/null 2>&1
	-umount ${BUILDROOT}/dev > /dev/null 2>&1
	-umount ${BUILDROOT}/var/db/pkg.refcount > /dev/null 2>&1
	-umount ${BUILDROOT}/var/db/pkg > /dev/null 2>&1
	-umount ${BUILDROOT}/var/db > /dev/null 2>&1
	-umount ${BUILDROOT}/usr/obj > /dev/null 2>&1
	-umount ${BUILDROOT}/usr/pkg > /dev/null 2>&1
	-umount ${BUILDROOT}/etc > /dev/null 2>&1
	-umount ${BUILDROOT}/var/tmp > /dev/null 2>&1
	-umount ${BUILDROOT}/tmp/track > /dev/null 2>&1
	-umount ${BUILDROOT}/tmp > /dev/null 2>&1
	-umount ${BUILDROOT}/var > /dev/null 2>&1
	-umount ${BUILDROOT}/usr > /dev/null 2>&1
	-umount ${BUILDROOT} > /dev/null 2>&1

mount:
	mount_null -o ro ${SYSROOT} ${BUILDROOT}
	mount_null -o ro ${SYSROOT}/usr ${BUILDROOT}/usr
	mount_null -o ro ${SYSROOT}/var ${BUILDROOT}/var
	mount_null ${BUILDBASE}/tmp ${BUILDROOT}/tmp
	mount_null ${BUILDBASE}/tmp ${BUILDROOT}/var/tmp
	mount_null ${BUILDBASE}/track ${BUILDROOT}/tmp/track
	mount_null ${BUILDBASE}/etc ${BUILDROOT}/etc
	mount_null ${BUILDBASE}/usr.pkg ${BUILDROOT}/usr/pkg
	mount_null ${BUILDBASE}/usr.pkgsrc ${BUILDROOT}/usr/pkgsrc
.if defined(NOTMPFS)
	mount_null ${BUILDBASE}/usr.obj ${BUILDROOT}/usr/obj
.else
	mount_tmpfs dummy ${BUILDROOT}/usr/obj
.endif
	mount_null ${BUILDBASE}/var.db ${BUILDROOT}/var/db
	mount_null ${BUILDBASE}/var.db.pkg ${BUILDROOT}/var/db/pkg
	mount_null ${BUILDBASE}/var.db.pkg.refcount \
			${BUILDROOT}/var/db/pkg.refcount
	mount_null ${SYSROOT}/dev ${BUILDROOT}/dev
	mount_null ${BUILDBASE}/packages ${BUILDROOT}/usr/pkgsrc/packages
	mount_null ${BUILDBASE}/distfiles ${BUILDROOT}/usr/pkgsrc/distfiles
	-resident ${BUILDROOT}/usr/pkg/bin/bmake > /dev/null 2>&1

initialize:
	#
	# The real root must contain these directories so
	# we can do NULL mounts on them.
	#
	mkdir -p ${SYSROOT}/var/db/pkg
	mkdir -p ${SYSROOT}/var/db/pkg.refcount
	mkdir -p ${SYSROOT}/usr/pkgsrc
	mkdir -p ${SYSROOT}/usr/pkg
	mkdir -p ${BUILDBASE}
	#
	# buildbase directory structure (this is not the chroot topology)
	#
	mkdir -p ${BUILDBASE}
	mkdir -p ${BUILDBASE}/packages
	mkdir -p ${BUILDBASE}/packages.save
	mkdir -p ${BUILDBASE}/distfiles
	mkdir -p ${BUILDBASE}/bin
	mkdir -p ${BUILDBASE}/tmp
	mkdir -p ${BUILDBASE}/tmp/track
	mkdir -p ${BUILDBASE}/tmp/logs
	mkdir -p ${BUILDBASE}/tmp/logs/run
	mkdir -p ${BUILDBASE}/tmp/logs/good
	mkdir -p ${BUILDBASE}/tmp/logs/bad
	mkdir -p ${BUILDBASE}/etc
	mkdir -p ${BUILDBASE}/usr.pkg
	mkdir -p ${BUILDBASE}/usr.pkgsrc
	mkdir -p ${BUILDBASE}/usr.obj
	mkdir -p ${BUILDBASE}/var.db
	mkdir -p ${BUILDBASE}/var.db/pkg
	mkdir -p ${BUILDBASE}/var.db/pkg.refcount
	mkdir -p ${BUILDBASE}/var.db.pkg
	mkdir -p ${BUILDBASE}/var.db.pkg.refcount
	mkdir -p ${BUILDBASE}/track
	#
	# Copy stuff we need, make sure the pkgsrc copy we
	# make has the needed mount points for packages and distfiles.
	#
	cpdup -s0 -i0 ${SYSROOT}/etc ${BUILDBASE}/etc
	cpdup -s0 -i0 -X ${.CURDIR}/vardb.ign \
			${SYSROOT}/var/db ${BUILDBASE}/var.db
	cpdup -s0 -i0 -X ${.CURDIR}/pkgsrc.ign \
			${SYSPKGSRC} ${BUILDBASE}/usr.pkgsrc
	mkdir -p ${BUILDBASE}/usr.pkgsrc/packages
	mkdir -p ${BUILDBASE}/usr.pkgsrc/distfiles
	mkdir -p ${BUILDROOT}
	#find -x -X ${BUILDROOT}/usr/pkgsrc -name work | xargs rm -rf

# quickstrap - get ready to start a fastbulk build
#
# NOTE: ${BUILDROOT}/usr/obj might be mounted from a tmpfs and not
#	from ${BUILDBASE}/usr.obj
#
quickstrap: prestage
	rm -rf ${BUILDBASE}/usr.obj/work
	rm -rf ${BUILDROOT}/usr/obj/work
	rm -rf ${BUILDROOT}/tmp/logs/run
	rm -rf ${BUILDROOT}/tmp/logs/bad
	mkdir -p ${BUILDROOT}/tmp/logs/run
	mkdir -p ${BUILDROOT}/tmp/logs/bad
	mkdir -p ${BUILDROOT}/usr/obj/work
	#
	# Sometimes we get blah.core files in /var/db/pkg, which
	# interferes with pkg_add.  Make sure they are gone.
	#
	find ${BUILDROOT}/var/db/pkg -name '*.core' -delete
	find ${BUILDROOT}/var/db/pkg -type d -depth 1 -delete

bootstrap: quickstrap
	-resident -d ${BUILDROOT}/usr/pkg/bin/bmake
	cpdup -i0 -s0 -o ${BUILDBASE}/packages ${BUILDBASE}/packages.save
	find -x ${BUILDBASE}/packages -depth -depth +0 -delete
	find -x ${BUILDBASE}/usr.pkgsrc/packages -depth -depth +0 -delete
	find -x ${BUILDBASE}/var.db.pkg -depth -depth +0 -delete
	find -x ${BUILDBASE}/var.db.pkg.refcount -depth -depth +0 -delete
	find -x ${BUILDBASE}/usr.pkg -depth -depth +0 -delete
	# note: work dir might be either tmpfs or null mount
	#${CHROOTENV} "pkg_admin -K /var/db/pkg fetch-pkg-vulnerabilities"
	#${CHROOTENV} "pkg_admin rebuild"
	rm -rf ${BUILDROOT}/usr/obj/work
	${CHROOTENV} "cd /usr/pkgsrc/bootstrap; ./bootstrap --workdir=/usr/obj/work"
	-resident ${BUILDROOT}/usr/pkg/bin/bmake > /dev/null 2>&1
	cat mk.conf.add >> ${BUILDROOT}/usr/pkg/etc/mk.conf

# Figure out what has changed and deinstall the related packages
#

PRESTAGE= ${BUILDBASE}/bin/getpkgsrcdir
PRESTAGE+= ${BUILDBASE}/bin/fastbulk
PRESTAGE+= ${BUILDBASE}/track/Makefile
PRESTAGE+= ${BUILDBASE}/track/dochanges
PRESTAGE+= ${BUILDBASE}/track/dobuild
PRESTAGE+= ${BUILDBASE}/track/fastbulk

prestage: ${PRESTAGE}

changes: ${PRESTAGE}
	@if [ ! -f ${BUILDBASE}/track/dochanges ]; then \
		touch ${BUILDBASE}/track/dochanges; fi
	@if [ ! -f ${BUILDBASE}/track/changes.raw ]; then \
		touch ${BUILDBASE}/track/changes.raw; fi
	( cat ${BUILDBASE}/track/changes.raw; \
	  find ${SYSPKGSRC} -mnewer ${BUILDBASE}/track/dochanges ) | \
	    sort | uniq | ${BUILDBASE}/bin/getpkgsrcdir ${SYSPKGSRC} | \
	    sort | uniq > ${BUILDBASE}/track/changes.raw.new
	mv ${BUILDBASE}/track/changes.raw.new ${BUILDBASE}/track/changes.raw
	${BUILDBASE}/bin/getpkgsrcdir -s ${SYSPKGSRC} <	\
		${BUILDBASE}/track/changes.raw > \
		${BUILDBASE}/track/changes.txt
	cp Makefile ${BUILDBASE}/track/Makefile
	${CHROOTENV} "cd /tmp/track; ./dochanges"


${BUILDBASE}/bin/getpkgsrcdir: getpkgsrcdir.c
	cc getpkgsrcdir.c -o ${BUILDBASE}/bin/getpkgsrcdir

${BUILDBASE}/bin/fastbulk: fastbulk.c
	cc fastbulk.c -o ${BUILDBASE}/bin/fastbulk

${BUILDBASE}/track/Makefile: ${.CURDIR}/Makefile
	cp -p ${.CURDIR}/Makefile ${BUILDBASE}/track/Makefile

${BUILDBASE}/track/dochanges: ${.CURDIR}/dochanges
	cp -p ${.CURDIR}/dochanges ${BUILDBASE}/track/dochanges

${BUILDBASE}/track/dobuild: ${.CURDIR}/dobuild
	cp -p ${.CURDIR}/dobuild ${BUILDBASE}/track/dobuild

${BUILDBASE}/track/fastbulk: ${BUILDBASE}/bin/fastbulk
	cp -p ${BUILDBASE}/bin/fastbulk ${BUILDBASE}/track/fastbulk
