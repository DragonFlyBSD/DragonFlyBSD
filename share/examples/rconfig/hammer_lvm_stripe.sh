#!/bin/sh
#
# This script takes in account a disk list to make a lvm(8) stripe
# which will contain a hammer filesystem and the swap partition.
#
# Be aware that this is just to provide _basic_ installation
# functionalities and it may need actions from the sysadmin to
# properly install and get a consistent installed system.

# set -x

export LVM_SUPPRESS_FD_WARNINGS=1

disklist="da0 da1"
bootdisk=""
logfile="/tmp/hammer_lvm_stripe.log"

#
# err exitval message
#     Display an error and exit
#
err()
{
    exitval=$1
    shift

    echo 1>&2 "$0: ERROR: $*"
    exit $exitval
}

#
# cktmpdir
#
#     Checks whether /tmp is writeable.
#     Exit if not
#
cktmpdir()
{
    mktemp "/tmp/XXX" >> ${logfile} 2>&1

    if [ $? -ne 0 ]; then
	err 1 "/tmp directory must be writeable"
    fi
}

#
# ckpreinstall
#
#     Checks whether lv nodes exist which would may
#     indicate an installation already succeeded or
#     a previous failed one.
#
ckpreinstall()
{

    # Try to activate the volume first
    vgchange -a y main >> ${logfile} 2>&1

    if [ -c /dev/mapper/main-lv_root -o -c /dev/mapper/main-lv_swap ]; then
	echo " ERROR: Either a previous installation failed or your installation"
	echo " is already done. If the former, please follow instructions below."
	echo ""

	lvmexit
    fi
}


#
# ckstatus status progname
#
#     Checks exit status of programs and if it fails
#     it exists with a message
#
ckstatus()
{
    local st=$1
    local prog=$2

    if [ ${st} -ne 0 ]; then
	err 1 "Failed to run $2. Check ${logfile}"
    fi
}

#
# ckloadmod modname
#
#     Checks if a module is loaded, if not
#     it tries to load it. In case of failure
#     it exits
#
ckloadmod()
{
    local mod=$1

    if ! kldstat -q -m ${mod}; then
	kldload $1 >> ${logfile} 2>&1

	if [ $? -ne 0 ]; then
	    err 1 "Could not load ${mod}"
        fi
    fi
}

#
# prepdisk disk
#
#     Clears the first sectors of the disk
#     and then prepares the disk for disklabel.
#     It also installs bootblocks in the first
#     disk of the list.
#
prepdisk()
{
    local disk=$1

    # Hey don't shoot yourself in the foot
    if [ ! "$disk" = "" ]; then
	mount | fgrep ${disk} >> ${logfile} 2>&1

	if [ $? -ne 1 ]; then
	    err 1 "${disk} is already being used, aborting"
	fi
    fi

    if [ "${bootdisk}" = "" ]; then
	bootdisk=${disk}
    fi

    dd if=/dev/zero of=/dev/${disk} bs=32k count=16 >> ${logfile} 2>&1
    ckstatus $? "dd"

    fdisk -IB ${disk} >> ${logfile} 2>&1
    ckstatus $? "fdisk"

    disklabel -r -w ${disk}s1 >> ${logfile} 2>&1
    ckstatus $? "disklabel"

    if [ ! "${bootdisk}" = "" ]; then
	disklabel -B ${disk}s1 >> ${logfile} 2>&1
	ckstatus $? "disklabel"
    fi
}

#
# mklabel disk
#
#     Create same labels for every disk
#     except for the disk that will contain
#     /boot partition
#
mklabel()
{
    local disk=$1

    disklabel ${disk}s1 > /tmp/label
    ckstatus $? "disklabel"

    if [ "${disk}" = "${bootdisk}" ]; then
	cat >> /tmp/label <<EOF
 a: 768m 0 4.2BSD
 d: * * unknown
EOF
    else
	cat >> /tmp/label <<EOF
 d: * * unknown
EOF

    fi

    disklabel -R ${disk}s1 /tmp/label >> ${logfile} 2>&1
    ckstatus $? "disklabel"
}


#
# lvmexit
#
#     lvm exit message
lvmexit()
{
    local status=$1

    if [ $# -ne 0 ]; then
	if [ ${status} -eq 0 ]; then
	    return
	fi
    fi

    echo " There was an error during or after LVM operations for this"
    echo " installation and those operations done cannot be reverted"
    echo " by the script itself. Please revert them manually:"
    echo ""
    echo " Basically you need to perform these steps:"
    echo "   1. Remove all logical volumes in VG main (lv_swap, lv_root)"
    echo "   2. Remove main volume group"
    echo "   3. Remove all the physical volumes. PVs are all in [disk]1d"
    echo "      For example, if you have 2 disks, you need to remove:"
    echo "        /sbin/pvremove /dev/da0s1d"
    echo "        /sbin/pvremove /dev/da1s1d"
    echo ""
    exit 1
}

#
# rev_lvmops
#
#     Revert all lvmops
#
rev_lvmops()
{

    vgchange -a n main >> ${logfile} 2>&1

    lvremove --force lv_swap main >> ${logfile} 2>&1
    if [ $? -ne 0 ]; then
        lvmexit
    fi

    lvremove --force lv_root main >> ${logfile} 2>&1
    if [ $? -ne 0 ]; then
        lvmexit
    fi

    vgremove --force main >> ${logfile} 2>&1
    if [ $? -ne 0 ]; then
        lvmexit
    fi

    for disk in ${disklist}
    do
	pvremove /dev/${disk}s1d >> ${logfile} 2>&1
    done
}

#
# lvmops
#
#     Create physical volumes, volume group 'main' and
#     the ROOT hammer filesystem where everything will
#     be installed. swap is also created as a log. volume.
#
lvmops()
{
    local lst=""
    local tmp=""

    for disk in ${disklist}
    do
	pvcreate /dev/${disk}s1d >> ${logfile} 2>&1
	if [ $? -ne 0 ]; then
	    rev_lvmops
        fi
	tmp=${lst}
	lst="${tmp} /dev/${disk}s1d"

	# Be safe
	sync
	sleep 1
    done

    # Do a full scan in the hope the lvm
    # cache is rebuilt
    pvscan >> ${logfile} 2>&1

    vgcreate main ${lst} >> ${logfile} 2>&1
    if [ $? -ne 0 ]; then
        rev_lvmops
    fi

    # We need to sync and wait for some secs to settle
    # Otherwise /dev/main won't appear
    echo "     Settling LVM operations (5 secs)"
    sync
    sleep 5
    vgscan >> ${logfile} 2>&1

    lvcreate -Z n -n lv_swap -L ${memtotal} main >> ${logfile} 2>&1
    if [ $? -ne 0 ]; then
        rev_lvmops
    fi

    # We sync in every lvm operation
    sync
    lvscan >> ${logfile} 2>&1
    sleep 1

    lvcreate -Z n -n lv_root -l 100%FREE main >> ${logfile} 2>&1
    if [ $? -ne 0 ]; then
        rev_lvmops
    fi

}

echo "ALL DATA IN DISKS ${disklist} WILL BE LOST!"
echo "Press ^C to abort."
for n in 10 9 8 7 6 5 4 3 2 1
do
    echo -n " ${n}"
    sleep 1
done
echo ""

# /tmp has to be writeable
echo "* Checking /tmp is writeable"
cktmpdir

rm "${logfile}" >> ${logfile} 2>&1
echo "* Output to ${logfile}"

# calculate memory
tmp=`sysctl hw.physmem | cut -w -f 2`
memtotal=`expr ${tmp} / 1024 / 1024`

# kldload needed modules and start udevd
echo "* Loading modules"
ckloadmod dm_target_striped >> ${logfile} 2>&1
ckloadmod dm_target_linear >> ${logfile} 2>&1
/sbin/udevd >> ${logfile} 2>&1

# check previous installations
ckpreinstall

# Unmount any prior mounts on /mnt, reverse order to unwind
# sub-directory mounts.
#
for mount in `df | fgrep /mnt | awk '{ print $6; }' | tail -r`
do
    echo " Umounting ${mount}"
    umount ${mount} >> ${logfile} 2>&1
done

# Prepare the disks in the list
for disk in ${disklist}
do
    echo "* Preparing disk ${disk}"
    prepdisk ${disk}
    mklabel ${disk}
    pvscan >> ${logfile} 2>&1
done

# lvm(8) operations in the disks
echo "* Performing LVM operations"
lvmops

# Format the volumes
echo "* Formating ${bootdisk} and LVs lv_root"
newfs /dev/${bootdisk}s1a >> ${logfile} 2>&1
lvmexit $?

newfs_hammer -f -L ROOT /dev/main/lv_root >> ${logfile} 2>&1
lvmexit $?

# Mount it
#
echo "* Mounting everything"
mount_hammer /dev/main/lv_root /mnt >> ${logfile} 2>&1
lvmexit $?

mkdir /mnt/boot

mount /dev/${bootdisk}s1a /mnt/boot >> ${logfile} 2>&1
lvmexit $?

# Create PFS mount points for nullfs.
pfslist="usr usr.obj var var.crash var.tmp tmp home"

mkdir /mnt/pfs

for pfs in ${pfslist}
do
    hammer pfs-master /mnt/pfs/$pfs >> ${logfile} 2>&1
    lvmexit $?
done

# Mount /usr and /var so that you can add subdirs
mkdir /mnt/usr >> ${logfile} 2>&1
mkdir /mnt/var >> ${logfile} 2>&1
mount_null /mnt/pfs/usr /mnt/usr >> ${logfile} 2>&1
lvmexit $?

mount_null /mnt/pfs/var /mnt/var >> ${logfile} 2>&1
lvmexit $?

mkdir /mnt/usr/obj >> ${logfile} 2>&1
mkdir /mnt/var/tmp >> ${logfile} 2>&1
mkdir /mnt/var/crash >> ${logfile} 2>&1

mkdir /mnt/tmp >> ${logfile} 2>&1
mkdir /mnt/home >> ${logfile} 2>&1

mount_null /mnt/pfs/tmp /mnt/tmp >> ${logfile} 2>&1
lvmexit $?

mount_null /mnt/pfs/home /mnt/home >> ${logfile} 2>&1
lvmexit $?

mount_null /mnt/pfs/var.tmp /mnt/var/tmp >> ${logfile} 2>&1
lvmexit $?

mount_null /mnt/pfs/var.crash /mnt/var/crash >> ${logfile} 2>&1
lvmexit $?

mount_null /mnt/pfs/usr.obj /mnt/usr/obj >> ${logfile} 2>&1
lvmexit $?

chmod 1777 /mnt/tmp >> ${logfile} 2>&1
chmod 1777 /mnt/var/tmp >> ${logfile} 2>&1

# Install the system from the live CD
#
echo "* Starting file copy"
cpdup -vv -o / /mnt >> ${logfile} 2>&1
lvmexit $?

cpdup -vv -o /boot /mnt/boot >> ${logfile} 2>&1
lvmexit $?

cpdup -vv -o /usr /mnt/usr >> ${logfile} 2>&1
lvmexit $?

cpdup -vv -o /var /mnt/var >> ${logfile} 2>&1
lvmexit $?

cpdup -vv -i0 /etc.hdd /mnt/etc >> ${logfile} 2>&1
lvmexit $?

chflags -R nohistory /mnt/tmp >> ${logfile} 2>&1
chflags -R nohistory /mnt/var/tmp >> ${logfile} 2>&1
chflags -R nohistory /mnt/var/crash >> ${logfile} 2>&1
chflags -R nohistory /mnt/usr/obj >> ${logfile} 2>&1

echo "* Adapting fstab and loader.conf"
cat > /mnt/etc/fstab << EOF
# Device		Mountpoint	FStype	Options		Dump	Pass#
/dev/main/lv_root	/		hammer	rw		1	1
/dev/${bootdisk}s1a	/boot		ufs	rw		1	1
/dev/main/lv_swap	none		swap	sw		0	0
/pfs/usr		/usr		null	rw		0	0
/pfs/var		/var		null	rw		0	0
/pfs/tmp		/tmp		null	rw		0	0
/pfs/home		/home		null	rw		0	0
/pfs/var.tmp		/var/tmp	null	rw		0	0
/pfs/usr.obj		/usr/obj	null	rw		0	0
/pfs/var.crash		/var/crash	null	rw		0	0
proc			/proc		procfs	rw		0	0
EOF

# Because root is not on the boot partition we have to tell the loader
# to tell the kernel where root is.
#
cat > /mnt/boot/loader.conf << EOF
dm_target_striped_load="YES"
dm_target_linear_load="YES"
initrd.img_load="YES"
initrd.img_type="md_image"
vfs.root.mountfrom="ufs:md0s0"
vfs.root.realroot="local:hammer:/dev/main/lv_root"
EOF

# Setup interface, configuration, sshd
#
echo "* iface setup"
ifc=`route -n get default | fgrep interface | awk '{ print $2; }'`
ip=`ifconfig $ifc | fgrep inet | fgrep -v inet6 | awk '{ print $2; }'`
lip=`echo $ip | awk -F . '{ print $4; }'`

echo -n "ifconfig_$ifc=" >> /mnt/etc/rc.conf
echo '"DHCP"' >> /mnt/etc/rc.conf
cat >> /mnt/etc/rc.conf << EOF
sshd_enable="YES"
dntpd_enable="YES"
hostname="test$lip.MYDOMAIN.XXX"
dumpdev="/dev/main/lv_swap"
EOF

# Misc sysctls
#
cat >> /mnt/etc/sysctl.conf << EOF
#net.inet.ip.portrange.first=4000
EOF

# mkinitd image
echo "* Preparing initrd image"
/sbin/mkinitrd -b /mnt/boot >> ${logfile} 2>&1

# Warn about ssh
echo ""
echo "Warning:"
echo "chroot now to /mnt and change root password and also edit"
echo "/mnt/etc/ssh/sshd_config to allow root login and authentication"
echo "using passwords. Alternatively, you can also just copy your"
echo "~/.ssh/authorized_keys file to allow login using your ssh keys."

echo "Installation finished successfully."


# take CD out and reboot
#
