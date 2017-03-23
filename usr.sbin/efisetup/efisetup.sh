#!/bin/sh
#
# efisetup [-s swap] [-S serialno] <rawdrive>
#
# Copyright (c) 2017 Matthew Dillon. All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
# 1. Redistributions of source code must retain the above copyright
#    notice, this list of conditions and the following disclaimer.
# 2. Redistributions in binary form must reproduce the above copyright
#    notice, this list of conditions and the following disclaimer in the
#    documentation and/or other materials provided with the distribution.
#
# THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
# IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
# OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
# IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
# INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
# NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
# THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
#
#       Email: Matthew Dillon <dillon@backplane.com>
#

help() {
	echo "WARNING! EFISETUP WILL WIPE THE TARGET DRIVE!"
	echo ""
	echo "efisetup [-s <swap>{m,g}] [-S serialno] <rawdrive>"
	echo "    -s <swap{m,g}     Defaults to 8g"
	echo "    -S serialno       Used to adjust loader.conf, fstab, etc"
	echo "                      If not specified, drive spec is used"
	exit 1
}

fail() {
	echo "failed on $*"
	exit 1
}

var=
fopt=0
swap=8g
serno=

for _switch ; do
	case $_switch in
	-f)
		fopt=1
		;;
	-h)
		help
		;;
	-s)
		var=swap
		;;
	-S)
		var=serno
		;;
	-*)
		echo "Bad option: $_switch"
		exit 1
		;;
	*)
		if [ "x$var" != "x" ]; then
			eval ${var}=$_switch
			var=
		else
			if [ "x$drive" != "x" ]; then
			    echo "Specify only one target drive"
			    echo "WARNING! efisetup will wipe the target drive"
			    exit 1
			fi
			drive=$_switch
		fi
		;;
	esac
done

if [ "x$drive" = "x" ]; then
	help
fi

if [ ! -c $drive ]; then
	if [ ! -c /dev/$drive ]; then
	    echo "efisetup: $drive is not a char-special device"
	    exit 1
	fi
	drive="/dev/$drive"
fi

if [ $fopt == 0 ]; then
	echo -n "This will wipe $drive, are you sure? "
	read ask
	case $ask in
	y)
		;;
	yes)
		;;
	Y)
		;;
	YES)
		;;
	*)
		echo "Aborting command"
		exit 1
		;;
	esac
fi

# Ok, do all the work.  Start by creating a fresh EFI
# partition table
#
gpt destroy $drive > /dev/null 2>&1
dd if=/dev/zero of=$drive bs=32k count=64 > /dev/null 2>&1
gpt create $drive
if [ $? != 0 ]; then
    echo "gpt create failed"
    exit 1
fi

# GPT partitioning
#
#
gpt add -i 0 -s 524288 -t "EFI System" ${drive}
sects=`gpt show /dev/nvme0 | sort -n +1 | tail -1 | awk '{ print $2; }'`
sects=$(($sects / 2048 * 2048))
gpt add -i 1 -s $sects -t "DragonFly Label64" ${drive}
sleep 0.5

mkdir -p /efimnt
if [ $? != 0 ]; then fail "mkdir -p /efimnt"; fi

# GPT s0 - EFI boot setup
#
newfs_msdos ${drive}s0
mount_msdos ${drive}s0 /efimnt
mkdir -p /efimnt/efi/boot
cp /boot/boot1.efi /efimnt/efi/boot/bootx64.efi
umount /efimnt

# GPT s1 - DragonFlyBSD disklabel setup
#
disklabel -r -w ${drive}s1 auto
if [ $? != 0 ]; then fail "initial disklabel"; fi

rm -f /tmp/label.$$
disklabel ${drive}s1 > /tmp/label.$$
cat >> /tmp/label.$$ << EOF
a: 1g	0	4.2BSD
b: ${swap}	*	swap
d: *	*	HAMMER
EOF

disklabel -R ${drive}s1 /tmp/label.$$
if [ $? != 0 ]; then fail "disklabel setup"; fi

#rm -f /tmp/label.$$
sleep 0.5
newfs ${drive}s1a
if [ $? != 0 ]; then fail "newfs ${drive}s1a"; fi
newfs_hammer -L ROOT ${drive}s1d
if [ $? != 0 ]; then fail "newfs_hammer ${drive}s1d"; fi

# DragonFly mounts, setup for installation
#
echo "Mounting DragonFly for copying"
mount ${drive}s1d /efimnt
if [ $? != 0 ]; then fail "mount ${drive}s1d"; fi
mkdir -p /efimnt/boot
mount ${drive}s1a /efimnt/boot
if [ $? != 0 ]; then fail "mount ${drive}s1a"; fi

# INSTALLWORLD SEQUENCE
#
echo "Mounted onto /efimnt and /efimnt/boot"
echo "Type yes to continue with install"
echo "You must have a built the world and kernel already."
echo "^C here if you did not."

echo -n "Continue? "
read ask
case $ask in
y)
	;;
yes)
	;;
Y)
	;;
YES)
	;;
*)
	echo "Stopping here.  /efimnt and /efimnt/boot remain mounted"
	exit 1
	;;
esac

# Setup initial installworld sequence
#
cd /usr/src
make installworld DESTDIR=/efimnt
if [ $? != 0 ]; then fail "make installworld"; fi
make installkernel DESTDIR=/efimnt
if [ $? != 0 ]; then fail "make installkernel"; fi
cd /usr/src/etc
make distribution DESTDIR=/efimnt
if [ $? != 0 ]; then fail "make distribution"; fi

# Calculate base drive path given serial
# number (or no serial number).
#
# serno - full drive path or serial number, sans slice & partition,
#	  including the /dev/, which we use as an intermediate
#	  variable.
#
# mfrom - partial drive path as above except without the /dev/,
#	  allowed in mountfrom and fstab.
#
if [ "x$serno" == "x" ]; then
    serno=${drive}
    mfrom="`echo ${drive} | sed -e 's#/dev/##g'`"
else
    serno="serno/${serno}."
    mfrom="serno/${serno}."
fi

echo "Fixingup files for a ${serno}s1d root"

# Add mountfrom to /efimnt/boot/loader.conf
#
echo "vfs.root.mountfrom=\"hammer:${mfrom}s1d\"" >> /efimnt/boot/loader.conf

# Add dumpdev to /etc/rc.conf
#
echo "dumpdev=\"/dev/${mfrom}s1b\"" >> /efimnt/etc/rc.conf

# Create a fresh /etc/fstab
#
printf "%-20s %-15s hammer\trw\t1 1\n" "${mfrom}s1d" "/" \
			>> /efimnt/etc/fstab
printf "%-20s %-15s ufs\trw\t1 1\n" "${mfrom}s1a" "/boot" \
			>> /efimnt/etc/fstab
printf "%-20s %-15s swap\tsw\t0 0\n" "${mfrom}s1b" "none" \
			>> /efimnt/etc/fstab
printf "%-20s %-15s procfs\trw\t4 4\n" "proc" "/proc" \
			>> /efimnt/etc/fstab

echo "Unmounting /efimnt/boot and /efimnt"
umount /efimnt/boot
umount /efimnt
