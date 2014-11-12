#!/bin/csh
#
# A file like this is typically copied to /usr/local/etc/rconfig/auto.sh on
# the rconfig server and the rconfig demon is run via 'rconfig -s -a'.  When
# you boot the DragonFly CD you have to bring up the network, typically
# via 'dhclient interfacename', then run 'rconfig -a' or 
# 'rconfig -a ip_of_server' if the server is not on the same LAN.
#
# WARNING!  THIS SCRIPT WILL COMPLETELY WIPE THE DISK!
#
# $DragonFly: src/share/examples/rconfig/auto.sh,v 1.2 2008/09/03 02:22:25 dillon Exp $

set disk = ad0
set slice = s1
set xdisk = $disk$slice

# Refuse to do anything if the machine wasn't booted from CD
#
set cdboot = 0
foreach i ( `df / | awk '{ print $1; }'` )
    if ( $i =~ acd* ) then
	set cdboot = 1
    endif
end

if ( $cdboot == 0 ) then
    echo "Aborting auto init script, machine was not booted from CD"
    exit 1
endif

# Wipe the disk entirely
#

echo "FDISK - ALL DATA ON THE DRIVE WILL BE LOST"
foreach i ( 5 4 3 2 1 )
    echo -n " $i"
    sleep 1
end

dd if=/dev/zero of=/dev/$disk bs=32k count=16
fdisk -IB $disk
boot0cfg -B $disk
boot0cfg -v $disk
dd if=/dev/zero of=/dev/$xdisk bs=32k count=16

echo "DISKLABEL"
sleep 1

disklabel -B -r -w $xdisk auto
disklabel $xdisk > /tmp/disklabel.$xdisk
cat >> /tmp/disklabel.$xdisk << EOF
a: 256m * 4.2BSD
b: 1024m * swap
d: 256m * 4.2BSD
e: 256m * 4.2BSD
f: 6144m * 4.2BSD
g: * * 4.2BSD
EOF
disklabel -R $xdisk /tmp/disklabel.$xdisk
disklabel $xdisk

echo "NEWFS"
sleep 1

newfs /dev/${xdisk}a
newfs -U /dev/${xdisk}d
newfs -U /dev/${xdisk}e
newfs -U /dev/${xdisk}f
newfs -U /dev/${xdisk}g

echo "MOUNT"
sleep 1

mount /dev/${xdisk}a /mnt
mkdir /mnt/var
mkdir /mnt/tmp
mkdir /mnt/usr
mkdir /mnt/home

mount /dev/${xdisk}d /mnt/var
mount /dev/${xdisk}e /mnt/tmp
mount /dev/${xdisk}f /mnt/usr
mount /dev/${xdisk}g /mnt/home

echo "CPDUP ROOT"
cpdup / /mnt
echo "CPDUP VAR"
cpdup /var /mnt/var
echo "CPDUP ETC"
cpdup /etc.hdd /mnt/etc
echo "CPDUP DEV"
cpdup /dev /mnt/dev
echo "CPDUP USR"
cpdup /usr /mnt/usr

echo "CLEANUP"
chmod 1777 /mnt/tmp
rm -rf /mnt/var/tmp
ln -s /tmp /mnt/var/tmp

cat >/mnt/etc/fstab << EOF
# Example fstab based on /README.
#
# Device                Mountpoint      FStype  Options         Dump    Pass#
/dev/${xdisk}a		/		ufs	rw		1	1
/dev/${xdisk}b		none		swap	sw		0	0
/dev/${xdisk}d		/var		ufs	rw		2	2
/dev/${xdisk}e		/tmp		ufs	rw		2	2
/dev/${xdisk}f		/usr		ufs	rw		2	2
/dev/${xdisk}g		/home		ufs	rw		2	2
proc			/proc		procfs	rw		0	0
# example MFS remount (for a pristine MFS filesystem do not use -C)
#swap			/mnt		mfs	rw,-C,-s=4000	0	0
EOF

cat >/mnt/etc/rc.conf << EOF
ifconfig_em0="DHCP"
sshd_enable="YES"
sendmail_enable="NONE"
dumpdev="/dev/${xdisk}b"
EOF

if ( ! -d /mnt/root/.ssh ) then
    mkdir /mnt/root/.ssh
endif
cat > /mnt/root/.ssh/authorized_keys << EOF
# put your ssh public keys here so you can ssh into the 
# newly configured machine
EOF

# Allow public-key-only access to the root account
#
sed -e 's/#PermitRootLogin no/PermitRootLogin without-password/' < /mnt/etc/ssh/sshd_config > /mnt/etc/ssh/sshd_config.new
mv -f /mnt/etc/ssh/sshd_config.new /mnt/etc/ssh/sshd_config

