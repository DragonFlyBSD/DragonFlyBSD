#
# $FreeBSD: src/Makefile,v 1.234.2.19 2003/04/16 09:59:40 ru Exp $
# $DragonFly: src/Makefile,v 1.9 2004/11/12 09:09:56 dillon Exp $
#
# The user-driven targets are:
#
# buildworld          - Rebuild *everything*, including glue to help do
#                       upgrades.
# quickworld	      - Skip bootstrap, build and cross-build tool steps
# realquickworld      - Skip above steps, plus depend
# crossworld	      - Just do the bootstrap, build, and cross-build steps
# installworld        - Install everything built by "buildworld".
# world               - buildworld + installworld.
# buildkernel         - Rebuild the kernel and the kernel-modules from scratch
#			using build/bootstrap/cross tools from the last
#			buildworld.
# nativekernel	      - Rebuild the kernel and the kernel-modules from scratch
#		        using native tools.
# quickkernel	      - rebuild the kernel quickly (build or native), skip
#			the make depend step and do not clean out the obj
#			modules.
# installkernel       - Install the kernel and the kernel-modules.
# reinstallkernel     - Reinstall the kernel and the kernel-modules.
# kernel              - buildkernel + installkernel.
# update              - Convenient way to update your source tree (cvs).
# preupgrade	      - Certain upgrades may have to be done before installworld.
#			installworld will complain if they have not been done.  This
#			target will do those upgrades... typically the addition of
#			new special groups and users used by installed utilities.
# upgrade             - Upgrade the files in /etc and also setup the rest
#			of the system for DragonFly. ex. two compilers
# most                - Build user commands, no libraries or include files.
# installmost         - Install user commands, no libraries or include files.
#
# This makefile is simple by design. The FreeBSD make automatically reads
# the /usr/share/mk/sys.mk unless the -m argument is specified on the 
# command line. By keeping this makefile simple, it doesn't matter too
# much how different the installed mk files are from those in the source
# tree. This makefile executes a child make process, forcing it to use
# the mk files from the source tree which are supposed to DTRT.
#
# The user-driven targets (as listed above) are implemented in Makefile.inc1.
#
# If you want to build your system from source be sure that /usr/obj has
# at least 400MB of diskspace available.
#
# For individuals wanting to build from the sources currently on their
# system, the simple instructions are:
#
# 1.  `cd /usr/src'  (or to the directory containing your source tree).
# 2.  `make world'
#
# For individuals wanting to upgrade their sources (even if only a
# delta of a few days):
#
# 1.  `cd /usr/src'       (or to the directory containing your source tree).
# 2.  `make buildworld'
# 3.  `make buildkernel KERNCONF=YOUR_KERNEL_HERE'     (default is GENERIC).
# 4.  `make installkernel KERNCONF=YOUR_KERNEL_HERE'   (default is GENERIC).
# 5.  `reboot'        (in single user mode: boot -s from the loader prompt).
# 6.  `mergemaster -p'
# 7.  `make installworld'
# 8.  `mergemaster'
# 9.  `reboot'
#
# See src/UPDATING `COMMON ITEMS' for more complete information.
#
# If TARGET_ARCH=arch (e.g. amd64) is specified you can
# cross build world for other architectures using the buildworld target,
# and once the world is built you can cross build a kernel using the
# buildkernel target.
#
# Define the user-driven targets. These are listed here in alphabetical
# order, but that's not important.
#
TGTS=	all all-man buildkernel quickkernel nativekernel \
	buildworld crossworld quickworld realquickworld checkdpadd clean \
	cleandepend cleandir depend distribute distributeworld everything \
	hierarchy install installcheck installkernel \
	reinstallkernel installmost installworld libraries lint maninstall \
	mk most obj objlink regress rerelease tags update

BITGTS=	files includes
BITGTS:=${BITGTS} ${BITGTS:S/^/build/} ${BITGTS:S/^/install/}

.ORDER: buildworld installworld
.ORDER: buildworld distributeworld
.ORDER: buildworld buildkernel nativekernel quickkernel
.ORDER: buildkernel nativekernel quickkernel installkernel
.ORDER: buildkernel nativekernel quickkernel reinstallkernel

PATH=	/sbin:/bin:/usr/sbin:/usr/bin
MAKE=	PATH=${PATH} make -m ${.CURDIR}/share/mk -f Makefile.inc1

#
# Handle the user-driven targets, using the source relative mk files.
#
${TGTS} ${BITGTS}:
	@cd ${.CURDIR}; \
		${MAKE} ${.TARGET}

# Set a reasonable default
.MAIN:	all

STARTTIME!= LC_ALL=C date
#
# world
#
# Attempt to rebuild and reinstall *everything*, with reasonable chance of
# success, regardless of how old your existing system is.
#
world:
	@echo "--------------------------------------------------------------"
	@echo ">>> elf make world started on ${STARTTIME}"
	@echo "--------------------------------------------------------------"
.if target(pre-world)
	@echo
	@echo "--------------------------------------------------------------"
	@echo ">>> Making 'pre-world' target"
	@echo "--------------------------------------------------------------"
	@cd ${.CURDIR}; ${MAKE} pre-world
.endif
	@cd ${.CURDIR}; ${MAKE} buildworld
	@cd ${.CURDIR}; ${MAKE} -B installworld
.if target(post-world)
	@echo
	@echo "--------------------------------------------------------------"
	@echo ">>> Making 'post-world' target"
	@echo "--------------------------------------------------------------"
	@cd ${.CURDIR}; ${MAKE} post-world
.endif
	@echo
	@echo "--------------------------------------------------------------"
	@printf ">>> elf make world completed on `LC_ALL=C date`\n                        (started ${STARTTIME})\n"
	@echo "--------------------------------------------------------------"

#
# kernel
#
# Short hand for `make buildkernel installkernel'
#
kernel: buildkernel installkernel

#
# A simple test target used as part of the test to see if make supports
# the -m argument.  Also test that make will only evaluate a conditional
# as far as is necessary to determine its value.
#
test:
.if defined(notdef)
.undef notdef
.if defined(notdef) && ${notdef:U}
.endif
.endif

#
# Upgrade the installed make to the current version using the installed
# headers, libraries and build tools. This is required on installed versions
# prior to 2.2.5 in which the installed make doesn't support the -m argument.
#
make:
	@echo
	@echo "--------------------------------------------------------------"
	@echo " Upgrading the installed make"
	@echo "--------------------------------------------------------------"
	@cd ${.CURDIR}/usr.bin/make; \
		make obj && make depend && make all && make install

#
# Handle the upgrade of /etc
#

preupgrade:
	@cd ${.CURDIR}/etc; make preupgrade

upgrade:	upgrade_etc

#
# Handle post-installworld updating of static files (e.g. like /etc/rc)
#
upgrade_etc:
	@cd ${.CURDIR}/etc; make upgrade_etc
