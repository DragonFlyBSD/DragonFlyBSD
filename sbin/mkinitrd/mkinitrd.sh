#!/bin/sh
#
# Copyright (c) 2010
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

create_vn()
{
	if [ ! -d  $BUILD_DIR ]; then
		mkdir -p $BUILD_DIR
		echo "Created build directory $BUILD_DIR"
	fi
	vnconfig -c -S ${INITRD_SIZE} -Z -T vn ${TMP_DIR}/initrd.img \
	    > ${TMP_DIR}/vndev.mkinitrd
	if [ $? -ne 0 ]; then
	    echo "Failed to configure vn device"
	    exit 1
	fi

	VN_DEV=`cat ${TMP_DIR}/vndev.mkinitrd | cut -f 2 -d ' '`
	[ -f ${TMP_DIR}/vndev.mkinitrd ] && rm ${TMP_DIR}/vndev.mkinitrd

	echo "Configured $VN_DEV"
	newfs /dev/${VN_DEV}s0
	echo "Formatted initrd image with UFS"
	mount /dev/${VN_DEV}s0 $BUILD_DIR
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

copy_tools()
{
	for tool in ${BIN_TOOLS}; do
		objcopy -S /bin/${tool} ${BUILD_DIR}/bin/${tool}
	done

	for tool in ${SBIN_TOOLS}; do
		objcopy -S /sbin/${tool} ${BUILD_DIR}/sbin/${tool}
	done

	echo "Copied essential tools"
}

copy_content()
{
	for dir in ${CONTENT_DIRS}; do
		cpdup ${dir}/ ${BUILD_DIR}/
	done
}

print_info()
{
	lt ${BUILD_DIR}
	df -h | head -n 1
	df -h | grep $VN_DEV
}

usage()
{
	echo "usage: $0 [-b bootdir] [-t tmpdir]"
	exit 2
}

args=`getopt b:t: $*`
test $? -ne 0 && usage

set -- $args
for i; do
	case "$i" in
	-b)	BOOT_DIR="$2"; shift; shift;;
	-t)	TMP_DIR="$2"; shift; shift;;
	--)	shift; break;
	esac
done
test ! -d ${BOOT_DIR} && usage
test ! -d ${TMP_DIR} && usage
test ! -z $1 && usage
BUILD_DIR="${TMP_DIR}/initrd"

create_vn
copy_content
make_hier
copy_tools
print_info
destroy_vn
mv ${TMP_DIR}/initrd.img ${BOOT_DIR}/kernel/initrd.img
