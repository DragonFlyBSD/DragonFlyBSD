#!/bin/sh
#
# Copyright (c) 2010,2018
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

# Calculate the total size of the given directory, taking care of the
# hard links.
calc_size()
{
	find "$1" -ls | \
	    awk '{ print $7,$1 }' | \
	    sort -n -k 2 | \
	    uniq -f 1 | \
	    awk '{ sum+=$1 } END { print sum }'  # [byte]
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
	# Round initrd size up by MiB
	isize_mb=$(echo "${isize}" | awk '
	    function ceil(x) {
	        y = int(x);
	        return (x>y ? y+1 : y);
	    }
	    {
	        mb = $1/1024/1024;
	        print ceil(mb);
	    }')
	# Add additional 1 MiB
	echo $((${isize_mb} + 1))
}

create_vn()
{
	if [ ! -d "$BUILD_DIR" ]; then
		mkdir -p $BUILD_DIR
		echo "Created build directory $BUILD_DIR"
	fi
	vnconfig -c -S ${INITRD_SIZE}m -Z -T vn ${TMP_DIR}/initrd.img \
	    > ${TMP_DIR}/vndev.mkinitrd || {
		echo "Failed to configure vn device"
		exit 1
	}

	VN_DEV=`cat ${TMP_DIR}/vndev.mkinitrd | cut -f 2 -d ' '`
	rm ${TMP_DIR}/vndev.mkinitrd

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
}

make_hier()
{
	for dir in ${INITRD_DIRS}; do
		mkdir -p ${BUILD_DIR}/${dir}
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

args=`getopt b:c:s:S:t: $*`
test $? -ne 0 && usage

set -- $args
for i; do
	case "$i" in
	-b)	BOOT_DIR="$2"; shift; shift;;
	-c)	CONTENT_DIR="$2"; shift; shift;;
	-s)	INITRD_SIZE="$2"; shift; shift;;
	-S)	INITRD_SIZE_MAX="$2"; shift; shift;;
	-t)	TMP_DIR="$2"; shift; shift;;
	--)	shift; break;
	esac
done
test ! -d ${BOOT_DIR}    && usage
test ! -d ${CONTENT_DIR} && usage
test ! -d ${TMP_DIR}     && usage
test ! -z "$1"           && usage

BUILD_DIR="${TMP_DIR}/initrd"
INITRD_SIZE=${INITRD_SIZE%[mM]}  # [MiB]
INITRD_SIZE_MAX=${INITRD_SIZE_MAX%[mM]}  # [MiB]

CSIZE=$(calc_initrd_size)
echo "Required initrd image size: ${CSIZE} MiB"
if [ -n "${INITRD_SIZE}" -a "${INITRD_SIZE}" != "0" ]; then
	if [ ${CSIZE} -gt ${INITRD_SIZE} ]; then
		echo "Specified initrd size (${INITRD_SIZE} MiB) too small"
		exit 1
	fi
else
	INITRD_SIZE=${CSIZE}
fi
echo "Initrd size: ${INITRD_SIZE} MiB"

if [ -n "${INITRD_SIZE_MAX}" -a "${INITRD_SIZE_MAX}" != "0" ] && \
   [ ${INITRD_SIZE} -gt ${INITRD_SIZE_MAX} ]; then
	echo "Exceeded the specified maximum size (${INITRD_SIZE_MAX} MiB)"
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
echo -n "Copying ${TMP_DIR}/initrd.img.gz to ${BOOT_DIR}/kernel/initrd.img.gz ..."
mv ${TMP_DIR}/initrd.img.gz ${BOOT_DIR}/kernel/initrd.img.gz
echo " OK"
