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

all:
	@echo "fastbulk build options"
	@echo "    make scratch      - cleanup, setup, cpdup /etc, bootstrap"
	@echo "    make build        - iterate all dirs / build package"
	@echo "    make setup        - setup/mount the build environment"
	@echo "    make cleanup      - unmount the build environment"
	@echo "    make realclean    - rm -rf the build env except for"
	@echo "                        packages and distfiles"
	@echo "    make changes      - deinstall modified packages"
	@echo "make variables (defaults shown):"
	@echo ""
	@echo "    BUILDBASE=/build/fastbulk     all fastbulk-generated files"
	@echo "    SYSPKGSRC=/usr/pkgsrc         where to get pkgsrc from"
	@echo "    SYSROOT=/                     rootenv to replicate"
	@echo "    NPARALLEL=8                   maximum concurrency"
	@echo ""
	@echo "NOTE: ${BUILDBASE}/{distfiles,packages} are normally retained"
	@echo "      to avoid having to re-pull and for incremental operation"
	@echo ""

scratch: cleanup
	rm -rf ${BUILDBASE}/var.db
	rm -rf ${BUILDBASE}/var.db.pkg
	rm -rf ${BUILDBASE}/var.db.pkg.refcount
	rm -rf ${BUILDBASE}/etc
	rm -rf ${BUILDBASE}/usr.pkg
	rm -rf ${BUILDBASE}/usr.obj/work
	rm -rf ${BUILDBASE}/tmp/logs
	rm -rf ${BUILDBASE}/tmp/track
	cpdup ${SYSROOT}/etc ${BUILDBASE}/etc
	cpdup ${SYSROOT}/var/db ${BUILDBASE}/var.db
	rm -rf ${BUILDBASE}/var.db/pkg
	rm -rf ${BUILDBASE}/var.db/pkg.refcount
	${MAKE} setup bootstrap
	#${MAKE} build

setup: cleanup
	mkdir -p ${SYSROOT}/var/db/pkg
	mkdir -p ${SYSROOT}/var/db/pkg.refcount
	mkdir -p ${SYSROOT}/usr/pkgsrc
	mkdir -p ${SYSROOT}/usr/pkg
	mkdir -p ${BUILDBASE}
	mkdir -p ${BUILDBASE}/packages
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
	mkdir -p ${BUILDBASE}/usr.obj
	mkdir -p ${BUILDBASE}/var.db
	mkdir -p ${BUILDBASE}/var.db/pkg
	mkdir -p ${BUILDBASE}/var.db/pkg.refcount
	mkdir -p ${BUILDBASE}/var.db.pkg
	mkdir -p ${BUILDBASE}/var.db.pkg.refcount
	mkdir -p ${BUILDBASE}/track
	mkdir -p ${BUILDROOT}
	mount_null -o ro ${SYSROOT} ${BUILDROOT}
	mount_null -o ro ${SYSROOT}/usr ${BUILDROOT}/usr
	mount_null -o ro ${SYSROOT}/var ${BUILDROOT}/var
	mount_null ${BUILDBASE}/tmp ${BUILDROOT}/tmp
	mount_null ${BUILDBASE}/tmp ${BUILDROOT}/var/tmp
	mount_null ${BUILDBASE}/track ${BUILDROOT}/tmp/track
	mount_null ${BUILDBASE}/etc ${BUILDROOT}/etc
	mount_null ${BUILDBASE}/usr.pkg ${BUILDROOT}/usr/pkg
	mount_null ${BUILDBASE}/usr.obj ${BUILDROOT}/usr/obj
	mount_null ${BUILDBASE}/var.db ${BUILDROOT}/var/db
	mount_null ${BUILDBASE}/var.db.pkg ${BUILDROOT}/var/db/pkg
	mount_null ${BUILDBASE}/var.db.pkg.refcount \
			${BUILDROOT}/var/db/pkg.refcount
	mount_null ${SYSROOT}/dev ${BUILDROOT}/dev
	mount_null ${SYSPKGSRC} ${BUILDROOT}/usr/pkgsrc
	mount_null ${BUILDBASE}/packages ${BUILDROOT}/usr/pkgsrc/packages
	mount_null ${BUILDBASE}/distfiles ${BUILDROOT}/usr/pkgsrc/distfiles

cleanup:
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

# Cleans everything except packages and distfiles
#
realclean: cleanup
	rm -rf ${BUILDBASE}/bin
	rm -rf ${BUILDBASE}/tmp
	rm -rf ${BUILDBASE}/track
	rm -rf ${BUILDBASE}/usr.pkg
	rm -rf ${BUILDBASE}/usr.obj
	rm -rf ${BUILDBASE}/var.db.pkg
	rm -rf ${BUILDBASE}/var.db.pkg.refcount

# Bootstrap an empty chroot setup
#
bootstrap:
	rm -rf ${BUILDBASE}/usr.obj/work
	${CHROOTENV} "cd /usr/pkgsrc/bootstrap; ./bootstrap --workdir=/usr/obj/work"

# Figure out what has changed and deinstall the related packages
#

PRESTAGE= ${BUILDBASE}/bin/getpkgsrcdir
PRESTAGE+= ${BUILDBASE}/bin/fastbulk
PRESTAGE+= ${BUILDBASE}/track/Makefile
PRESTAGE+= ${BUILDBASE}/track/dochanges
PRESTAGE+= ${BUILDBASE}/track/dobuild
PRESTAGE+= ${BUILDBASE}/track/fastbulk

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

build: ${PRESTAGE}
	rm -rf ${BUILDBASE}/usr.obj/work
	mkdir -p ${BUILDBASE}/usr.obj/work
	${CHROOTENV} "cd /tmp/track; ./fastbulk -j ${NPARALLEL} /usr/pkgsrc"

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
