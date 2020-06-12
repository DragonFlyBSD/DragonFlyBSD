#!/bin/sh
#
# Copyright (c) 2010, 2018
# 	The DragonFly Project.  All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
#
# 1. Redistributions of source code must retain the above copyright
#    notice, this list of conditions and the following disclaimer.
# 2. Redistributions in binary form must reproduce the above copyright
#    notice, this list of conditions and the following disclaimer in
#    the documentation and/or other materials provided with the
#    distribution.
# 3. Neither the name of The DragonFly Project nor the names of its
#    contributors may be used to endorse or promote products derived
#    from this software without specific, prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
# FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
# COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
# INCIDENTAL, SPECIAL, EXEMPLARY OR CONSEQUENTIAL DAMAGES (INCLUDING,
# BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
# LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
# AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
# OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
# OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
# SUCH DAMAGE.
#

#
# Description
#
# This tool packs the (statically linked) rescue tools (at /rescue by
# default) and contents specified by "-c <content_dirs>", such as the
# necessary etc files, into an UFS-formatted VN image.  This image is
# installed at /boot/kernel/initrd.img.gz and used as the initial ramdisk
# to help mount the real root filesystem when it is encrypted or on LVM.
#


#
# Default configurations
#

# Directory hierarchy on the initrd
#
# * 'new_root' will always be created
# * 'sbin' will be symlinked to 'bin'
# * 'tmp' will be symlinked to 'var/tmp'
#
INITRD_DIRS="bin dev etc mnt var"

# Directory of the statically linked rescue tools which will be copied
# onto the initrd.
RESCUE_DIR="/rescue"

# Specify the location that the initrd will be installed to, i.e.,
# <BOOT_DIR>/kernel/initrd.img.gz
BOOT_DIR="/boot"

# Maximum size (number of MB) allowed for the initrd image
INITRD_SIZE_MAX="15"  # MB

# When run from the buildworld/installworld environment do not require that
# things like uniq, kldload, mount, newfs, etc be in the cross-tools.
# These must run natively for the current system version.
#
PATH=${PATH}:/sbin:/usr/sbin:/bin:/usr/bin

#
# Helper functions
#

log() {
	echo "$@" >&2
}

error() {
	local rc=$1
	shift
	log "$@"
	exit ${rc}
}

check_dirs() {
	for _dir; do
		[ -d "${_dir}" ] ||
		    error 1 "Directory '${_dir}' does not exist"
	done
	return 0
}


#
# Functions
#

calc_initrd_size() {
	log "Calculating required initrd size ..."
	isize=0
	for _dir; do
		csize=$(du -kst ${_dir} | awk '{ print $1 }')  # KB
		log "* ${_dir}: ${csize} KB"
		isize=$((${isize} + ${csize}))
	done
	# Round initrd size up by MB
	isize_mb=$(echo ${isize} | awk '
	    function ceil(x) {
	        y = int(x);
	        return (x>y ? y+1 : y);
	    }
	    {
	        print ceil($1 / 1024);
	    }')
	# Reserve another 1 MB for advanced user to add custom files to the
	# initrd without creating it from scratch.
	echo $((${isize_mb} + 1))
}

create_vn() {
	kldstat -qm vn || kldload -n vn ||
	    error 1 "Failed to load vn kernel module"

	VN_DEV=$(vnconfig -c -S ${INITRD_SIZE}m -Z -T vn ${INITRD_FILE}) &&
	    echo "Configured ${VN_DEV}" ||
	    error 1 "Failed to configure VN device"

	newfs -i 131072 -m 0 /dev/${VN_DEV}s0 &&
	    echo "Formatted initrd image with UFS" ||
	    error 1 "Failed to format the initrd image"
	mount_ufs /dev/${VN_DEV}s0 ${BUILD_DIR} &&
	    echo "Mounted initrd image on ${BUILD_DIR}" ||
	    error 1 "Failed to mount initrd image on ${BUILD_DIR}"
}

destroy_vn() {
	umount /dev/${VN_DEV}s0 &&
	    echo "Unmounted initrd image" ||
	    error 1 "Failed to umount initrd image"
	vnconfig -u ${VN_DEV} &&
	    echo "Unconfigured ${VN_DEV}" ||
	    error 1 "Failed to unconfigure ${VN_DEV}"
}

make_hier() {
	mkdir -p ${BUILD_DIR}/new_root ||
	    error 1 "Failed to mkdir ${BUILD_DIR}/new_root"
	# Symlink 'sbin' to 'bin'
	ln -sf bin ${BUILD_DIR}/sbin
	# Symlink 'tmp' to 'var/tmp', as '/var' will be mounted with
	# tmpfs, saving a second tmpfs been mounted on '/tmp'.
	ln -sf var/tmp ${BUILD_DIR}/tmp
	for _dir in ${INITRD_DIRS}; do
		[ ! -d "${BUILD_DIR}/${_dir}" ] &&
		    mkdir -p ${BUILD_DIR}/${_dir}
	done
	echo "Created directory structure"
}

copy_rescue() {
	cpdup -o -u ${RESCUE_DIR}/ ${BUILD_DIR}/bin/ &&
	    echo "Copied ${RESCUE_DIR} to ${BUILD_DIR}/bin" ||
	    error 1 "Failed to copy ${RESCUE_DIR} to ${BUILD_DIR}/bin"
}

copy_content() {
	for _dir in ${CONTENT_DIRS}; do
		cpdup -o -u ${_dir}/ ${BUILD_DIR}/ &&
		    echo "Copied ${_dir} to ${BUILD_DIR}" ||
		    error 1 "Failed to copy ${dir} to ${BUILD_DIR}"
	done
}

print_info() {
	lt ${BUILD_DIR}
	df -h ${BUILD_DIR}
}

# Check the validity of the created initrd image before moving over.
# This prevents creating an empty and broken initrd image by running
# this tool but without ${CONTENT_DIRS} prepared.
#
# NOTE: Need more improvements.
#
check_initrd()
{
	[ -x "${BUILD_DIR}/sbin/oinit" ] &&
	[ -x "${BUILD_DIR}/bin/sh" ] &&
	[ -x "${BUILD_DIR}/etc/rc" ] || {
		destroy_vn
		error 1 "Ivalid initrd image!"
	}
}

usage() {
	error 2 \
	    "usage: ${0##*/} [-b boot_dir] [-r rescue_dir]" \
	    "[-s size] [-S max_size] -c <content_dirs>"
}


#
# Main
#

while getopts :b:c:hr:s:S: opt; do
	case ${opt} in
	b)
		BOOT_DIR="${OPTARG}"
		;;
	c)
		CONTENT_DIRS="${OPTARG}"
		;;
	h)
		usage
		;;
	r)
		RESCUE_DIR="${OPTARG}"
		;;
	s)
		INITRD_SIZE="${OPTARG}"
		;;
	S)
		INITRD_SIZE_MAX="${OPTARG}"
		;;
	\?)
		log "Invalid option -${OPTARG}"
		usage
		;;
	:)
		log "Option -${OPTARG} requires an argument"
		usage
		;;
	esac
done

shift $((OPTIND - 1))
[ $# -ne 0 ] && usage
[ -z "${BOOT_DIR}" -o -z "${RESCUE_DIR}" -o -z "${CONTENT_DIRS}" ] && usage
check_dirs ${BOOT_DIR} ${RESCUE_DIR} ${CONTENT_DIRS}

VN_DEV=""
INITRD_SIZE=${INITRD_SIZE%[mM]}  # MB
INITRD_SIZE_MAX=${INITRD_SIZE_MAX%[mM]}  # MB

BUILD_DIR=$(mktemp -d -t initrd) || error $? "Cannot create build directory"
echo "Initrd build directory: ${BUILD_DIR}"
INITRD_FILE="${BUILD_DIR}.img"
INITRD_DEST="${BOOT_DIR}/kernel/initrd.img.gz"

CSIZE=$(calc_initrd_size ${RESCUE_DIR} ${CONTENT_DIRS})
echo "Required initrd image size: ${CSIZE} MB"
if [ -n "${INITRD_SIZE}" -a "${INITRD_SIZE}" != "0" ]; then
	if [ ${CSIZE} -gt ${INITRD_SIZE} ]; then
		error 1 "Given initrd size (${INITRD_SIZE} MB) too small"
	fi
else
	INITRD_SIZE=${CSIZE}
fi
echo "Initrd size: ${INITRD_SIZE} MB"

if [ -n "${INITRD_SIZE_MAX}" -a "${INITRD_SIZE_MAX}" != "0" ] && \
   [ ${INITRD_SIZE} -gt ${INITRD_SIZE_MAX} ]; then
	error 1 "Exceeded the maximum size (${INITRD_SIZE_MAX} MB)"
fi

create_vn
make_hier
copy_rescue
copy_content
print_info
destroy_vn
rm -rf ${BUILD_DIR}

echo -n "Compressing ${INITRD_FILE} ..."
gzip -9 ${INITRD_FILE}
echo " OK"

if [ -f "${INITRD_DEST}" ]; then
	echo -n "Backing up ${INITRD_DEST} ..."
	mv ${INITRD_DEST} ${INITRD_DEST}.old
	echo " OK (${INITRD_DEST}.old)"
fi

echo -n "Installing ${INITRD_FILE}.gz to ${INITRD_DEST} ..."
install -o root -g wheel -m 444 ${INITRD_FILE}.gz ${INITRD_DEST}
echo " OK"
rm -f ${INITRD_FILE}.gz
