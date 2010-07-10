#!/bin/sh

BUILD_DIR="/tmp/initrd"
INITRD_SIZE="15m"
BIN_TOOLS="mkdir rm sh kill"
SBIN_TOOLS="mount mount_devfs mount_hammer mount_nfs mount_null mount_procfs mount_tmpfs umount iscontrol cryptsetup lvm sysctl udevd"
INITRD_DIRS="bin boot dev etc mnt proc sbin tmp var new_root"
CONTENT_DIRS="/usr/share/initrd"

if [ -e /etc/defaults/mkinitrd.conf ]; then
	. /etc/defaults/mkinitrd.conf
	echo "Loaded configuration from /etc/defaults/mkinitrd.conf"
fi


if [ -e /etc/mkinitrd.conf ]; then
	. /etc/mkinitrd.conf
	echo "Loaded configuration from /etc/mkinitrd.conf"
fi

VN_DEV=""

create_vn() {
	if [ ! -d  $BUILD_DIR ]; then
		mkdir -p $BUILD_DIR
		echo "Created build directory $BUILD_DIR"
	fi
	VN_DEV=`vnconfig -c -S ${INITRD_SIZE} -Z -T vn initrd.img | cut -f 2 -d ' '`
	echo "Configured $VN_DEV"
	newfs /dev/${VN_DEV}s0
	echo "Formatted initrd image with UFS"
	mount /dev/${VN_DEV}s0 $BUILD_DIR
	echo "Mounted initrd image on ${BUILD_DIR}"
}

destroy_vn() {
	umount /dev/${VN_DEV}s0
	echo "Unmounted initrd image"
	vnconfig -u $VN_DEV
	echo "Unconfigured $VN_DEV"
}

make_hier() {
	for dir in ${INITRD_DIRS}; do
		mkdir -p ${BUILD_DIR}/${dir}
	done

	echo "Created directory structure"
}

copy_tools() {
	for tool in ${BIN_TOOLS}; do
		objcopy -S /bin/${tool} ${BUILD_DIR}/bin/${tool}
	done

	for tool in ${SBIN_TOOLS}; do
		objcopy -S /sbin/${tool} ${BUILD_DIR}/sbin/${tool}
	done

	echo "Copied essential tools"
}

copy_content() {
	for dir in ${CONTENT_DIRS}; do
		cp -R ${dir}/* ${BUILD_DIR}/
	done
}

print_info() {
	lt ${BUILD_DIR}
	df -h | head -n 1
	df -h | grep $VN_DEV
}

create_vn
make_hier
copy_tools
copy_content
print_info
destroy_vn
mv initrd.img /boot/initrd.img
