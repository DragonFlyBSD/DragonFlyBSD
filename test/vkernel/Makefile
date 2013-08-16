# Makefile - set up a vkernel environment for testing the vkernel
#

# require it to be specified
SRCDIR ?= ${.CURDIR}/../..
ROOTSIZE ?= 2048M
PHYSMEM ?= 256m
NCPUS ?= 2

all: help

.if ${MACHINE_ARCH} == "i386"
USEKERNEL=VKERNEL
.else
USEKERNEL=VKERNEL64
.endif

scratch: world kernel root install
	@echo "Run the environment with:"
	@echo "make VKDIR=${VKDIR} run"

quickw: quickworld quickkernel reinstall reinstallkernel
	@echo "Run the environment with:"
	@echo "make VKDIR=${VKDIR} run"

quick: quickkernel reinstallkernel
	@echo "Run the environment with:"
	@echo "make VKDIR=${VKDIR} run"

help:
	@echo "Setup Instructions:"
	@echo ""
	@echo "    setenv ROOTSIZE size (optional)"
	@echo "    setenv VKDIR target_dir"
	@echo ""
	@echo "Meta target components:"
	@echo ""
	@echo "    help     - this help"
	@echo "    clean    - clean up"
	@echo "    scratch  - build and install everything from scratch"
	@echo "               (this is absolutely everything)"
	@echo "    quick    - incremental kernel build & reinstall"
	@echo "    quickw   - incremental world & kernel build & reinstall"
	@echo "    run      - run vkernel with VKDIR/root.img"
	@echo "    mount    - mount VKDIR/root.img at VKDIR/root"
	@echo "    umount   - unmount"
	@echo ""
	@echo "Individual target components:"
	@echo ""
	@echo "    world             - build the world from scratch"
	@echo "    root              - create a new, empty root.img"
	@echo "    install           - install a world & kernel"
	@echo "                        into root.img"
	@echo "    leaf		     - additional customizatio"
	@echo "    kernel   	     - build vkernel"
	@echo "    quickworld        - incremental rebuild world"
	@echo "    quickkernel       - incremental rebuild kernel"
	@echo "    reinstall         - reinstall world into root.img"
	@echo "                        (just the installworld piece)"
	@echo "    reinstallkernel   - reinstall kernel into root.img"
	@echo ""

# Unmount everything, de-configured VN, and clean up.
# (check handles umounting/deconfiguring)
#
clean: check
	rm -rf ${VKDIR}/root.img ${VKDIR}/root

# Build the world and kernel
#
#
world: checkq
	cd ${SRCDIR} && make -j 4 buildworld

kernel: checkq
	cd ${SRCDIR} && make -j 4 KERNCONF=${USEKERNEL} buildkernel

nativekernel: checkq
	cd ${SRCDIR} && make -j 4 KERNCONF=${USEKERNEL} nativekernel

# Quick build - just rebuild the kernel quickly
#
#
quickworld: checkq
	cd ${SRCDIR} && make -j 4 quickworld

quickkernel: checkq
	cd ${SRCDIR} && make -j 4 KERNCONF=${USEKERNEL} quickkernel

# Build and mount an empty filesystem for the emulated root disk
#
# NOTE: root must umount when done because a later dependency may
#       have a dependency on mount.
#
root:	check
	vnconfig -c -T -S ${ROOTSIZE} -s labels \
	    `cat ${VKDIR}/vn.which` ${VKDIR}/root.img
	dd if=/dev/zero of=/dev/`cat ${VKDIR}/vn.which` bs=32k count=4
	fdisk -IB `cat ${VKDIR}/vn.which`
	disklabel -r -w `cat ${VKDIR}/vn.which`s1 auto
	disklabel `cat ${VKDIR}/vn.which`s1 > ${VKDIR}/label.tmp
	echo 'a: * 0 4.2BSD' >> ${VKDIR}/label.tmp
	disklabel -R `cat ${VKDIR}/vn.which`s1 ${VKDIR}/label.tmp
	disklabel -B `cat ${VKDIR}/vn.which`s1
	newfs /dev/`cat ${VKDIR}/vn.which`s1a
	mkdir -p ${VKDIR}/root
	vnconfig -u `cat ${VKDIR}/vn.which` > /dev/null 2>&1

mount: check
	vnconfig -c -s labels `cat ${VKDIR}/vn.which` ${VKDIR}/root.img
	fsck -p /dev/`cat ${VKDIR}/vn.which`s1a
	mount /dev/`cat ${VKDIR}/vn.which`s1a ${VKDIR}/root
	@echo "Mounted ${VKDIR}/root"

umount: check

# Install a fresh world & distribution, and kernel
#
install: mount
	cd ${SRCDIR} && \
	    make -j 4 DESTDIR=${VKDIR}/root installworld
	cd ${SRCDIR}/etc && \
	    make -j 4 DESTDIR=${VKDIR}/root distribution
	echo '/dev/vkd0s1a	/	ufs	rw	1 1' > ${VKDIR}/root/etc/fstab
	echo 'proc		/proc	procfs	rw	0 0' >> ${VKDIR}/root/etc/fstab
	echo 'vfs.root.mountfrom="ufs:vkd0s1a"' > ${VKDIR}/root/boot/loader.conf
	#(egrep -v '^console' ${VKDIR}/root/etc/ttys; echo 'console "/usr/libexec/getty Pc"	cons25	on	secure') > ${VKDIR}/root/etc/ttys.new
	#mv -f ${VKDIR}/root/etc/ttys.new ${VKDIR}/root/etc/ttys
	cd ${SRCDIR} && \
	    make -j 4 \
		 DESTDIR=${VKDIR}/root KERNCONF=${USEKERNEL} \
		 installkernel
	cp ${VKDIR}/root/boot/kernel/kernel ${VKDIR}/vkernel

leaf: mount
	echo 'ifconfig_vke0="up"' > ${VKDIR}/root/etc/rc.conf
	echo 'defaultrouter="10.26.0.1"' >> ${VKDIR}/root/etc/rc.conf

# Quick reinstall - just install a new kernel on top of an existing image
#
#
reinstall: mount
	cd ${SRCDIR} && make -j 4 DESTDIR=${VKDIR}/root installworld

reinstallkernel: mount
	cd ${SRCDIR} && \
	    make -j 4 DESTDIR=${VKDIR}/root KERNCONF=${USEKERNEL} \
		 installkernel
	cp ${VKDIR}/root/boot/kernel/kernel ${VKDIR}/vkernel

sysloader: mount
	cp /boot/loader ${VKDIR}/root/boot/loader
	sync

# Run the vkernel on our image.  Make sure we are unmounted so
# we do not compete against the emulated kernel when writing to root.img.
# (check does this for us)
#
run: check
	mkdir -p /var/vkernel
	sysctl vm.vkernel_enable=1
	cd ${VKDIR} && ./vkernel -m ${PHYSMEM} -n ${NCPUS} \
			-r root.img -U -v \
			-I /var/run/vknet

# When running w/ a NFS root
#
NFS_IP?= 10.0.0.53
NFS_NETMASK?= 255.255.255.0
NFS_ROOT_IP?= 10.0.0.1
NFS_ROOT_PATH?= /netboot2

run_nfsroot: check
	cd ${VKDIR} && ./vkernel -m ${PHYSMEM} -n ${NCPUS} -U -v \
			-I /var/run/vknet \
			-e 'boot.netif.ip=${NFS_IP}:boot.netif.netmask=${NFS_NETMASK}:boot.netif.name=vke0:boot.nfsroot.server=${NFS_ROOT_IP}:boot.nfsroot.path=${NFS_ROOT_PATH}'

# Make sure we are not mounted and the VN device is unconfigured,
#
# Find an unused VN device but do not do anything with it yet.
#
checkq:
.if !defined(VKDIR)
	@(echo "must specify VKDIR=target or as an environment variable"; /usr/bin/false)
.endif
.if exists(${VKDIR})
	@echo "${VKDIR} found"
.else
	mkdir -p ${VKDIR}
.endif

check: checkq
.if exists(${VKDIR}/vn.which)
	-umount ${VKDIR}/root > /dev/null 2>&1
	-vnconfig -u `cat ${VKDIR}/vn.which` > /dev/null 2>&1
	rm -f ${VKDIR}/vn.which
.endif
	(vnconfig -l | fgrep "not in use" > /dev/null) || \
	    (echo "Cannot find unused VN"; /usr/bin/false)
	vnconfig -l | fgrep "not in use" | \
	    cut -f 1 -d : | head -1 > ${VKDIR}/vn.which
	egrep '^vn' ${VKDIR}/vn.which > /dev/null || \
	    (echo "VN device selection is bad"; /usr/bin/false)
