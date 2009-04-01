# Makefile - manage a qemu system for testing 64 bit DragonFly builds
#
# This Makefile will patch and build qemu, do all necessary world and
# kernel builds, and create a 64 bit root.img disk.
#

SRCDIR ?= /usr/src
QEMU ?= qemu-0.10.1
ROOTSIZE ?= 512M

all: help

scratch: qemu world32 world64 kernel64 root64 install64
	@echo "Run the environment with:"
	@echo "make QEMUDIR=${QEMUDIR} run"

quickw: quickworld64 quickkernel64 reinstall64 reinstallkernel64
	@echo "Run the environment with:"
	@echo "make QEMUDIR=${QEMUDIR} run"

quick: quickkernel64 reinstallkernel64
	@echo "Run the environment with:"
	@echo "make QEMUDIR=${QEMUDIR} run"

help:
	@echo "setenv QEMUDIR target_dir"
	@echo "make [help | clean | scratch | quick | quickw"
	@echo "make [run | mount | umount]"
	@echo "make [gdb64]"
	@echo "    help     - this help"
	@echo "    clean    - clean up"
	@echo "    scratch  - rebuild everything from scratch, including qemu"
	@echo "    quick    - incremental quickkernel & kernel install"
	@echo "    quickw   - incremental world & kernel & install"
	@echo "    run      - run qemu with install image"
	@echo "    mount    - mount image at QEMUDIR/root"
	@echo "    umount   - unmount"
	@echo "    gdb64    - create /usr/loca/bin/gdb64 - debug 64 bit bins"

# Unmount everything, de-configured VN, and clean up.
# (check handles umounting/deconfiguring)
#
clean: check
	rm -rf ${QEMUDIR}/${QEMU}.tar.gz ${QEMUDIR}/${QEMU} \
		${QEMUDIR}/${QEMU}-dragonfly.patch \
		${QEMUDIR}/root.img ${QEMUDIR}/root

# Build qemu for Intel 32 and 64 bit environments
#
# If configuring with sdl support from pkgsrc add the following
# options to the configure command:
#
#	--extra-cflags=-I/usr/pkg/include
#	--extra-ldflags=-L/usr/pkg/lib
#
qemu: checkq
.if !exists(${QEMUDIR}/${QEMU}.tar.gz)
	cd ${QEMUDIR} && \
	    fetch http://download.savannah.gnu.org/releases/qemu/${QEMU}.tar.gz
.endif
.if !exists(${QEMUDIR}/${QEMU}-dragonfly.patch)
	cd ${QEMUDIR} && \
	    fetch http://pkgbox.dragonflybsd.org/patches/${QEMU}-dragonfly.patch
.endif
	rm -rf ${QEMUDIR}/${QEMU}
	cd ${QEMUDIR} && tar xvzpf ${QEMU}.tar.gz
	cd ${QEMUDIR}/${QEMU} && patch -p1 < ${QEMUDIR}/${QEMU}-dragonfly.patch
	cd ${QEMUDIR}/${QEMU} &&  \
	    ./configure --disable-sdl \
			--disable-gfx-check \
			--disable-kqemu \
			--enable-system \
			--disable-aio \
			--disable-kvm \
			--target-list=i386-softmmu,x86_64-softmmu
	cd ${QEMUDIR}/${QEMU} && CCVER=gcc34 gmake
	cd ${QEMUDIR}/${QEMU} && CCVER=gcc34 gmake install

# Build the 32 and 64 bit worlds and the 64 bit kernel
#
#
world32:
	cd ${SRCDIR} && make -j 4 buildworld

world64:
	cd ${SRCDIR} && \
	    make -j 4 TARGET_ARCH=amd64 TARGET_PLATFORM=pc64 buildworld

kernel64:
	cd ${SRCDIR} && \
	    make -j 4 TARGET_ARCH=amd64 TARGET_PLATFORM=pc64 KERNCONF=JG64 \
	    buildkernel

# Quick build - just rebuild the kernel quickly
#
#
quickworld64:
	cd ${SRCDIR} && \
	    make -j 4 TARGET_ARCH=amd64 TARGET_PLATFORM=pc64 quickworld

quickkernel64:
	cd ${SRCDIR} && \
	    make TARGET_ARCH=amd64 TARGET_PLATFORM=pc64 KERNCONF=JG64 \
	    quickkernel

# Build and mount an empty filesystem for the emulated root disk
#
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
	vnconfig -u `cat ${QEMUDIR}/vn.which`
	rm -f ${QEMUDIR}/vn.which

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
	    make -j 4 TARGET_ARCH=amd64 TARGET_PLATFORM=pc64 \
		 DESTDIR=${QEMUDIR}/root installworld
	cd ${SRCDIR}/etc && \
	    make -j 4 TARGET_ARCH=amd64 TARGET_PLATFORM=pc64 \
		 DESTDIR=${QEMUDIR}/root distribution
	echo '/dev/ad1s1a	/	ufs	rw	1 1' > ${QEMUDIR}/root/etc/fstab
	echo 'proc		/proc	procfs	rw	0 0' >> ${QEMUDIR}/root/etc/fstab
	echo 'vfs.root.mountfrom="ufs:ad1s1a"' > ${QEMUDIR}/root/boot/loader.conf
	(egrep -v '^console' ${QEMUDIR}/root/etc/ttys; echo 'console "/usr/libexec/getty Pc"	cons25	on	secure') > ${QEMUDIR}/root/etc/ttys.new
	mv -f ${QEMUDIR}/root/etc/ttys.new ${QEMUDIR}/root/etc/ttys
	cd ${SRCDIR} && \
	    make -j 4 TARGET_ARCH=amd64 TARGET_PLATFORM=pc64 \
		 DESTDIR=${QEMUDIR}/root KERNCONF=JG64 \
		 NO_MODULES= \
		 installkernel

# Quick reinstall - just install a new kernel on top of an existing image
#
#
reinstall64: mount
	cd ${SRCDIR} && \
	    make -j 4 TARGET_ARCH=amd64 TARGET_PLATFORM=pc64 \
		 DESTDIR=${QEMUDIR}/root installworld

reinstallkernel64: mount
	cd ${SRCDIR} && \
	    make -j 4 TARGET_ARCH=amd64 TARGET_PLATFORM=pc64 \
		 DESTDIR=${QEMUDIR}/root KERNCONF=JG64 \
		 NO_MODULES= \
		 installkernel

# Run qemu on our image in 64 bit mode.  Make sure we are unmounted so
# we do not compete against the emulated kernel when writing to root.img.
# (check does this for us)
#
run: check
	cd ${QEMUDIR} && qemu-system-x86_64 -curses -hda ${QEMUDIR}/root.img \
					    -m 128 -no-fd-bootchk

rungdb: check
	@echo "RUN WITH:  run -curses -hda root.img"
	cd ${QEMUDIR} && gdb qemu-system-x86_64

# build a gdb capable of debugging 64 bit binaries
#
gdb64:
	cd /usr/src/gnu/usr.bin/gdb && (make clean; make TARGET_ARCH=amd64)
	cp /usr/obj/usr/src/gnu/usr.bin/gdb/gdb/gdb /usr/local/bin/gdb64
	cd /usr/src/gnu/usr.bin/gdb && make clean

# Make sure we are not mounted and the VN device is unconfigured,
#
# Find an unused VN device but do not do anything with it yet.
#
checkq:
.if !defined(QEMUDIR)
	@(echo "must specify QEMUDIR=target or as environment for run-time build"; exit 1)
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
	    (echo "Cannot find unused VN"; exit 1)
	vnconfig -l | fgrep "not in use" | \
	    cut -f 1 -d : | head -1 > ${QEMUDIR}/vn.which
	egrep '^vn' ${QEMUDIR}/vn.which > /dev/null || \
	    (echo "VN device selection is bad"; exit 1)
