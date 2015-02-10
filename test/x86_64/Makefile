# Makefile - manage a qemu system for testing 64 bit DragonFly builds
#
# This Makefile will patch and build qemu, do all necessary world and
# kernel builds, and create a 64 bit root.img disk.
#

# require it to be specified
SRCDIR ?= ${.CURDIR}/../..
QEMU ?= qemu-0.10.6
ROOTSIZE ?= 2048M

.if defined(WITHSDL)
QEMU_SDL_OPT ?= --extra-cflags=-I/usr/pkg/include \
		--extra-ldflags=-L/usr/pkg/lib
.else
QEMU_SDL_OPT ?= --disable-sdl
.endif

all: help

scratch: qemu world32 world64 kernel64 root64 install64 sysloader
	@echo "Run the environment with:"
	@echo "make QEMUDIR=${QEMUDIR} run"

quickw: quickworld64 quickkernel64 reinstall64 reinstallkernel64 sysloader
	@echo "Run the environment with:"
	@echo "make QEMUDIR=${QEMUDIR} run"

quick: quickkernel64 reinstallkernel64
	@echo "Run the environment with:"
	@echo "make QEMUDIR=${QEMUDIR} run"

help:
	@echo "Setup Instructions:"
	@echo ""
	@echo "    setenv QEMUDIR target_dir"
	@echo ""
	@echo "Meta target components:"
	@echo ""
	@echo "    help     - this help"
	@echo "    clean    - clean up"
	@echo "    scratch  - build and install everything from scratch"
	@echo "               (this is absolutely everything, except gdb64)"
	@echo "    quick    - incremental kernel build & reinstall"
	@echo "    quickw   - incremental world & kernel build & reinstall"
	@echo "    run      - run qemu with QEMUDIR/root.img - curses mode"
	@echo "    xrun     - run qemu with QEMUDIR/root.img - X Window mode"
	@echo "    mount    - mount QEMUDIR/root.img at QEMUDIR/root"
	@echo "    umount   - unmount"
	@echo "    gdb64    - create /usr/loca/bin/gdb64 - debug 64 bit bins"
	@echo ""
	@echo "Individual target components:"
	@echo ""
	@echo "    qemu              - fetch, patch, and build qemu"
	@echo "                        Specify WITHSDL=yes to build X support"
	@echo "    world32           - build the 32 bit world from scratch"
	@echo "    world64           - build the 64 bit world from scratch"
	@echo "    kernel64          - build the 64 bit kernel from scratch"
	@echo "    root64            - create a new, empty root.img"
	@echo "    install64         - install a 64 bit world & kernel"
	@echo "                        into root.img"
	@echo "    quickworld64      - incremental rebuild world64"
	@echo "    quickkernel64     - incremental rebuild kernel64"
	@echo "    reinstall64       - reinstall world64 into root.img"
	@echo "                        (just the installworld piece)"
	@echo "    reinstallkernel64 - reinstall kernel64 into root.img"
	@echo "    sysloader         - reinstall 32-bit system loader"
	@echo "                        (in case the 64 bit loader fails,"
	@echo "                        the 32 bit loader can be used)"
	@echo ""

# Unmount everything, de-configured VN, and clean up.
# (check handles umounting/deconfiguring)
#
clean: check
	rm -rf ${QEMUDIR}/${QEMU}.tar.gz ${QEMUDIR}/${QEMU} \
		${QEMUDIR}/root.img ${QEMUDIR}/root

# Build qemu for Intel 32 and 64 bit environments
#
# If configuring with sdl support from pkgsrc add the following
# options to the configure command:
#
#	--extra-cflags=-I/usr/pkg/include
#	--extra-ldflags=-L/usr/pkg/lib
#
qemu: checkq sdl
.if !exists(${QEMUDIR}/${QEMU}.tar.gz)
	cd ${QEMUDIR} && \
	    fetch http://download.savannah.gnu.org/releases/qemu/${QEMU}.tar.gz
.endif
	rm -rf ${QEMUDIR}/${QEMU}
	cd ${QEMUDIR} && tar xvzpf ${QEMU}.tar.gz
	cd ${QEMUDIR}/${QEMU} && patch -p1 < ${.CURDIR}/qemu.patch
	cd ${QEMUDIR}/${QEMU} &&  \
	    ./configure --disable-gfx-check \
			${QEMU_SDL_OPT} \
			--disable-kqemu \
			--enable-system \
			--disable-aio \
			--disable-kvm \
			--target-list=i386-softmmu,x86_64-softmmu
	cd ${QEMUDIR}/${QEMU} && gmake
	cd ${QEMUDIR}/${QEMU} && gmake install

# SDL dependancies
#
sdl:
.if defined(WITHSDL)
	@echo "SDL enabled"
	pkg_info libX11 > /dev/null 2>&1 || pkg_radd libX11
	pkg_info SDL > /dev/null 2>&1 || pkg_radd SDL
.else
	@echo "SDL not enabled"
.endif

# Build the 32 and 64 bit worlds and the 64 bit kernel
#
#
world32: checkq
	cd ${SRCDIR} && make -j 4 buildworld

world64: checkq
	cd ${SRCDIR} && \
	    make -j 4 TARGET_ARCH=x86_64 TARGET_PLATFORM=pc64 NO_GCC50=1 \
	    buildworld

kernel64: checkq
	cd ${SRCDIR} && \
	    make -j 4 TARGET_ARCH=x86_64 TARGET_PLATFORM=pc64 KERNCONF=X86_64_GENERIC \
	    buildkernel

# Quick build - just rebuild the kernel quickly
#
#
quickworld64: checkq
	cd ${SRCDIR} && \
	    make -j 4 TARGET_ARCH=x86_64 TARGET_PLATFORM=pc64 NO_GCC50=1 \
	    quickworld

quickkernel64: checkq
	cd ${SRCDIR} && \
	    make TARGET_ARCH=x86_64 TARGET_PLATFORM=pc64 KERNCONF=X86_64_GENERIC \
	    quickkernel

# Build and mount an empty filesystem for the emulated root disk
#
# NOTE: root64 must umount when done because a later dependency may
#       have a dependency on mount.
#
root64:	check
	vnconfig -c -T -S ${ROOTSIZE} -s labels \
	    `cat ${QEMUDIR}/vn.which` ${QEMUDIR}/root.img
	dd if=/dev/zero of=/dev/`cat ${QEMUDIR}/vn.which` bs=32k count=4
	fdisk -IB `cat ${QEMUDIR}/vn.which`
	disklabel -r -w `cat ${QEMUDIR}/vn.which`s1 auto
	disklabel `cat ${QEMUDIR}/vn.which`s1 > ${QEMUDIR}/label.tmp
	echo 'a: * 0 4.2BSD' >> ${QEMUDIR}/label.tmp
	disklabel -R `cat ${QEMUDIR}/vn.which`s1 ${QEMUDIR}/label.tmp
	disklabel -B `cat ${QEMUDIR}/vn.which`s1
	newfs /dev/`cat ${QEMUDIR}/vn.which`s1a
	mkdir -p ${QEMUDIR}/root
	vnconfig -u `cat ${QEMUDIR}/vn.which` > /dev/null 2>&1

mount: check
	vnconfig -c -s labels `cat ${QEMUDIR}/vn.which` ${QEMUDIR}/root.img
	fsck -p /dev/`cat ${QEMUDIR}/vn.which`s1a
	mount /dev/`cat ${QEMUDIR}/vn.which`s1a ${QEMUDIR}/root
	@echo "Mounted ${QEMUDIR}/root"

umount: check

# Install a fresh 64 bit world & distribution, and kernel
#
#
install64: mount
	cd ${SRCDIR} && \
	    make -j 4 TARGET_ARCH=x86_64 TARGET_PLATFORM=pc64 \
		 DESTDIR=${QEMUDIR}/root NO_GCC50=1 installworld
	cd ${SRCDIR}/etc && \
	    make -j 4 TARGET_ARCH=x86_64 TARGET_PLATFORM=pc64 \
		 DESTDIR=${QEMUDIR}/root distribution
	echo '/dev/ad0s1a	/	ufs	rw	1 1' > ${QEMUDIR}/root/etc/fstab
	echo 'proc		/proc	procfs	rw	0 0' >> ${QEMUDIR}/root/etc/fstab
	echo 'vfs.root.mountfrom="ufs:ad0s1a"' > ${QEMUDIR}/root/boot/loader.conf
	(egrep -v '^console' ${QEMUDIR}/root/etc/ttys; echo 'console "/usr/libexec/getty Pc"	cons25	on	secure') > ${QEMUDIR}/root/etc/ttys.new
	mv -f ${QEMUDIR}/root/etc/ttys.new ${QEMUDIR}/root/etc/ttys
	cd ${SRCDIR} && \
	    make -j 4 TARGET_ARCH=x86_64 TARGET_PLATFORM=pc64 \
		 DESTDIR=${QEMUDIR}/root KERNCONF=X86_64_GENERIC \
		 installkernel
	@echo "WARNING: 64 bit loader installed, if it doesn't work"
	@echo "         then use 'sysloader' to install the 32 bit one"

# Quick reinstall - just install a new kernel on top of an existing image
#
#
reinstall64: mount
	@echo "NOTE: Installing native 32 bit loader into root.img."
	@echo "      This loader should still be able to boot the"
	@echo "      64 bit kernel"
	cd ${SRCDIR} && \
	    make -j 4 TARGET_ARCH=x86_64 TARGET_PLATFORM=pc64 \
		 DESTDIR=${QEMUDIR}/root NO_GCC50=1 installworld

reinstallkernel64: mount
	cd ${SRCDIR} && \
	    make -j 4 TARGET_ARCH=x86_64 TARGET_PLATFORM=pc64 \
		 DESTDIR=${QEMUDIR}/root KERNCONF=X86_64_GENERIC \
		 installkernel

sysloader: mount
	cp /boot/loader ${QEMUDIR}/root/boot/loader
	sync

# Run qemu on our image in 64 bit mode.  Make sure we are unmounted so
# we do not compete against the emulated kernel when writing to root.img.
# (check does this for us)
#
run: check
	cd ${QEMUDIR} && qemu-system-x86_64 -curses -hda ${QEMUDIR}/root.img \
					    -m 128 -no-fd-bootchk

xrun:
.if !defined(DISPLAY)
	@(echo "Variable DISPLAY not set. Is X running?"; /usr/bin/false)
.endif
	cd ${QEMUDIR} && qemu-system-x86_64 -hda ${QEMUDIR}/root.img \
					    -m 128 -no-fd-bootchk

rungdb: check
	@echo "RUN WITH:  run -curses -hda root.img"
	cd ${QEMUDIR} && gdb qemu-system-x86_64

# build a gdb capable of debugging 64 bit binaries
#
gdb64:
	cd /usr/src/gnu/usr.bin/gdb && (make clean; make TARGET_ARCH=x86_64)
	cp /usr/obj/usr/src/gnu/usr.bin/gdb/gdb/gdb /usr/local/bin/gdb64
	cd /usr/src/gnu/usr.bin/gdb && make clean

# Make sure we are not mounted and the VN device is unconfigured,
#
# Find an unused VN device but do not do anything with it yet.
#
checkq:
.if !defined(QEMUDIR)
	@(echo "must specify QEMUDIR=target or as an environment variable"; /usr/bin/false)
.endif
.if exists(${QEMUDIR})
	@echo "${QEMUDIR} found"
.else
	mkdir -p ${QEMUDIR}
.endif

check: checkq
.if exists(${QEMUDIR}/vn.which)
	-umount ${QEMUDIR}/root > /dev/null 2>&1
	-vnconfig -u `cat ${QEMUDIR}/vn.which` > /dev/null 2>&1
	rm -f ${QEMUDIR}/vn.which
.endif
	(vnconfig -l | fgrep "not in use" > /dev/null) || \
	    (echo "Cannot find unused VN"; /usr/bin/false)
	vnconfig -l | fgrep "not in use" | \
	    cut -f 1 -d : | head -1 > ${QEMUDIR}/vn.which
	egrep '^vn' ${QEMUDIR}/vn.which > /dev/null || \
	    (echo "VN device selection is bad"; /usr/bin/false)
