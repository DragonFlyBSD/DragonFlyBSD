#!/bin/sh
#
# This script tests compatibility between dm_target_crypt and
# dm_target_crypt_ng.
#
# It creates an encrypted disk with one implementation, then copies
# files to it. Then it mounts it using the other implementation and
# verifies that the copied files are still the same (using mtree).
#
# It does this procedure in both directions and various configurations
# (e.g. with or w/o AESNI enabled).

set -e

FILETREE=/usr/distfiles
FILETREE_MTREE=/tmp/reference.mtree
DISKIMAGE=/tmp/diskimage
VN=vn0
KEYFILE=/tmp/keyfile
PASSWORD=dragonfly
MTREE_KEYS="uid,gid,sha1,size,type"
TMP_MTREE_DIFF=/tmp/mtree.diff

create_mtree() {
	mtree -c -p $1 -k ${MTREE_KEYS}
}

verify_mtree() {
	rm -f ${TMP_MTREE_DIFF}
	mtree -p ${1} -f ${2} -k "${MTREE_KEYS}" > ${TMP_MTREE_DIFF}
	if [ -z "`cat ${TMP_MTREE_DIFF}`" ]; then
		echo "OK"
	else
		echo "FAIL"
		cat ${TMP_MTREE_DIFF}
		return 1
	fi
}

create_diskimage() {
	local cipher="${1}"

	rm -f ${DISKIMAGE}
	dd if=/dev/zero of=${DISKIMAGE} bs=1m count=1500
	vnconfig -c ${VN} ${DISKIMAGE}
	echo "${PASSWORD}" > ${KEYFILE}

	cryptsetup luksFormat \
		--batch-mode --cipher ${cipher} /dev/${VN} ${KEYFILE}
	cryptsetup luksOpen \
		--batch-mode --key-file ${KEYFILE} /dev/${VN} testdisk
	newfs /dev/mapper/testdisk
	mount /dev/mapper/testdisk /mnt
	cpdup -I ${FILETREE} /mnt/filetree
	verify_mtree /mnt/filetree ${FILETREE_MTREE}
	umount /mnt
	cryptsetup luksClose testdisk
	vnconfig -u vn0
}

verify_diskimage() {
	local cipher="${1}"

	vnconfig -c ${VN} ${DISKIMAGE}
	echo "${PASSWORD}" > ${KEYFILE}
	cryptsetup luksOpen \
		--batch-mode --key-file ${KEYFILE} /dev/${VN} testdisk
	mount /dev/mapper/testdisk /mnt

	verify_mtree /mnt/filetree ${FILETREE_MTREE}

	umount /mnt
	cryptsetup luksClose testdisk
	vnconfig -u ${VN}
}

runtest() {
	local cipher="${1}"
	local modulea="${2}"
	local sysctla="${3}"
	local moduleb="${4}"
	local sysctlb="${5}"

	echo "--------------------------------"
	echo Testing cipher ${cipher}
	echo "--------------------------------"

	echo "Create diskimage with ${modulea} (sysctl ${sysctla})"
	kldload ${modulea} 
	if [ "${sysctla}" ]; then
		echo "setting sysctl ${sysctla}"
		sysctl "${sysctla}"
	fi

	create_diskimage ${cipher}
	kldunload ${modulea} 

	echo "Verify diskimage with ${moduleb} (sysctl ${sysctlb})"
	kldload ${moduleb} 
	if [ "${sysctlb}" ]; then
		echo "setting sysctl ${sysctlb}"
		sysctl "${sysctlb}"
	fi

	verify_diskimage ${cipher}
	kldunload ${moduleb} 
}

echo Generating reference filetree of ${FILETREE} in ${FILETREE_MTREE}
create_mtree ${FILETREE} > ${FILETREE_MTREE}

runtest aes-cbc-essiv:sha256 dm_target_crypt "" dm_target_crypt_ng "hw.aesni_disable=0"
runtest aes-cbc-essiv:sha256 dm_target_crypt "" dm_target_crypt_ng "hw.aesni_disable=1"
runtest aes-cbc-essiv:sha256 dm_target_crypt_ng "hw.aesni_disable=0" dm_target_crypt ""
runtest aes-cbc-essiv:sha256 dm_target_crypt_ng "hw.aesni_disable=1" dm_target_crypt ""
runtest aes-cbc-essiv:sha256 dm_target_crypt_ng "hw.aesni_disable=0" dm_target_crypt_ng "hw.aesni_disable=1"
runtest aes-cbc-essiv:sha256 dm_target_crypt_ng "hw.aesni_disable=1" dm_target_crypt_ng "hw.aesni_disable=0"

runtest aes-xts-essiv:sha256 dm_target_crypt "" dm_target_crypt_ng "hw.aesni_disable=0"
runtest aes-xts-essiv:sha256 dm_target_crypt "" dm_target_crypt_ng "hw.aesni_disable=1"
runtest aes-xts-essiv:sha256 dm_target_crypt_ng "hw.aesni_disable=0" dm_target_crypt ""
runtest aes-xts-essiv:sha256 dm_target_crypt_ng "hw.aesni_disable=1" dm_target_crypt ""
runtest aes-xts-essiv:sha256 dm_target_crypt_ng "hw.aesni_disable=0" dm_target_crypt_ng "hw.aesni_disable=1"
runtest aes-xts-essiv:sha256 dm_target_crypt_ng "hw.aesni_disable=1" dm_target_crypt_ng "hw.aesni_disable=0"

runtest serpent-cbc-essiv:sha256 dm_target_crypt "" dm_target_crypt_ng ""
runtest serpent-cbc-essiv:sha256 dm_target_crypt_ng "" dm_target_crypt ""
runtest serpent-xts-essiv:sha256 dm_target_crypt "" dm_target_crypt_ng ""
runtest serpent-xts-essiv:sha256 dm_target_crypt_ng "" dm_target_crypt ""

runtest twofish-cbc-essiv:sha256 dm_target_crypt "" dm_target_crypt_ng ""
runtest twofish-cbc-essiv:sha256 dm_target_crypt_ng "" dm_target_crypt ""
runtest twofish-xts-essiv:sha256 dm_target_crypt "" dm_target_crypt_ng ""
runtest twofish-xts-essiv:sha256 dm_target_crypt_ng "" dm_target_crypt ""

