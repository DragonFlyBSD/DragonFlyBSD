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

. /etc/defaults/mkinitrd.conf

if [ -r /etc/mkinitrd.conf ]; then
	. /etc/mkinitrd.conf
	echo "Loaded configuration from /etc/mkinitrd.conf"
fi

VN_DEV=""

check_dirs()
{
	for _dir; do
		[ -d "${_dir}" ] || {
			echo "Directory '${_dir}' does not exist"
			return 1
		}
	done
	return 0
}

# Calculate the total size of the given directory, taking care of the
# hard links.
calc_size()
{
	find "$1" -ls | \
	    awk '{ print $7,$1 }' | \
	    sort -n -k 2 | \
	    uniq -f 1 | \
	    awk '{ sum+=$1 } END { print sum }'  # byte
}

calc_initrd_size()
{
	echo "Contents directories:" >&2
	isize=0
	for dir in ${CONTENT_DIRS}; do
		csize=$(calc_size ${dir})
		echo "* ${dir}: ${csize} bytes" >&2
		isize=$((${isize} + ${csize}))
	done
	# Round initrd size up by MB
	isize_mb=$(echo "${isize}" | awk '
	    function ceil(x) {
	        y = int(x);
	        return (x>y ? y+1 : y);
	    }
	    {
	        mb = $1/1024/1024;
	        print ceil(mb);
	    }')
	# Add additional 1 MB
	echo $((${isize_mb} + 1))
}

create_vn()
{
	local _vndev
	_vndev=${TMP_DIR}/vndev.$$

	if [ ! -d "$BUILD_DIR" ]; then
		mkdir -p $BUILD_DIR
		echo "Created build directory $BUILD_DIR"
	fi
	vnconfig -c -S ${INITRD_SIZE}m -Z -T vn ${TMP_DIR}/initrd.img \
	    > ${_vndev} || {
		echo "Failed to configure vn device"
		exit 1
	}

	VN_DEV=`cat ${_vndev} | cut -f 2 -d ' '`
	rm ${_vndev}

	echo "Configured $VN_DEV"
	newfs -i 131072 -m 0 /dev/${VN_DEV}s0
	echo "Formatted initrd image with UFS"
	mount -t ufs /dev/${VN_DEV}s0 $BUILD_DIR
	echo "Mounted initrd image on ${BUILD_DIR}"
}

destroy_vn()
{
	umount /dev/${VN_DEV}s0
	echo "Unmounted initrd image"
	vnconfig -u $VN_DEV
	echo "Unconfigured $VN_DEV"
	rmdir ${BUILD_DIR}
}

make_hier()
{
	mkdir -p ${BUILD_DIR}/new_root
	# symlink 'tmp' to 'var/tmp', as '/var' will be mounted with
	# tmpfs, saving a second tmpfs been mounted on '/tmp'.
	ln -sf var/tmp ${BUILD_DIR}/tmp
	for _dir in ${INITRD_DIRS}; do
	    [ ! -d "${BUILD_DIR}/${_dir}" ] &&
		mkdir -p ${BUILD_DIR}/${_dir}
	done
	echo "Created directory structure"
}

copy_content()
{
	for dir in ${CONTENT_DIRS}; do
		cpdup -o -u ${dir}/ ${BUILD_DIR}/ || {
			echo "Failed to copy ${dir} to ${BUILD_DIR}"
			exit 1
		}
	done
}

print_info()
{
	lt ${BUILD_DIR}
	df -h ${BUILD_DIR}
}

usage()
{
	echo "usage: ${0##*/} [-b bootdir] [-c contentsdir] [-t tmpdir]" \
	     "[-s size] [-S max_size]"
	exit 2
}


#
# Main
#

while getopts :b:c:hs:S:t: opt; do
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
	s)
		INITRD_SIZE="${OPTARG}"
		;;
	S)
		INITRD_SIZE_MAX="${OPTARG}"
		;;
	t)
		TMP_DIR="${OPTARG}"
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
[ -z "${BOOT_DIR}" -o -z "${CONTENT_DIRS}"  -o -z "${TMP_DIR}" ] && usage
check_dirs ${BOOT_DIR} ${CONTENT_DIRS} ${TMP_DIR}

BUILD_DIR="${TMP_DIR}/initrd.$$"
INITRD_SIZE=${INITRD_SIZE%[mM]}  # MB
INITRD_SIZE_MAX=${INITRD_SIZE_MAX%[mM]}  # MB

CSIZE=$(calc_initrd_size)
echo "Required initrd image size: ${CSIZE} MB"
if [ -n "${INITRD_SIZE}" -a "${INITRD_SIZE}" != "0" ]; then
	if [ ${CSIZE} -gt ${INITRD_SIZE} ]; then
		echo "Specified initrd size (${INITRD_SIZE} MB) too small"
		exit 1
	fi
else
	INITRD_SIZE=${CSIZE}
fi
echo "Initrd size: ${INITRD_SIZE} MB"

if [ -n "${INITRD_SIZE_MAX}" -a "${INITRD_SIZE_MAX}" != "0" ] && \
   [ ${INITRD_SIZE} -gt ${INITRD_SIZE_MAX} ]; then
	echo "Exceeded the specified maximum size (${INITRD_SIZE_MAX} MB)"
	exit 1
fi

create_vn
copy_content
make_hier
print_info
destroy_vn

echo -n "Compressing ${TMP_DIR}/initrd.img ..."
gzip -9 ${TMP_DIR}/initrd.img
echo " OK"

DEST="${BOOT_DIR}/kernel/initrd.img.gz"
if [ -f "${DEST}" ]; then
	echo -n "Backup ${DEST} ..."
	mv ${DEST} ${DEST}.old
	echo " OK (${DEST}.old)"
fi

echo -n "Copying ${TMP_DIR}/initrd.img.gz to ${DEST} ..."
mv ${TMP_DIR}/initrd.img.gz ${DEST}
echo " OK"
