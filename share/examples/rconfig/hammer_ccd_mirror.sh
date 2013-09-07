#!/bin/sh
#
# HAMMER INSTALLATION on a CCD disk (mirror)
#
# You should modify disklist to match the disks you want to use.
# Please BE CAREFUL when doing so.
#
#
# set -x

disklist="da0 da1"
bootdisk=""
logfile="/tmp/hammer_ccd_mirror.log"
contents_dir="/tmp/contents"

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
#
ckpreinstall()
{
    # No pre-install checks
}

#
# ccdexit
#
ccdexit()
{
    local status=$1

    if [ $# -ne 0 ]; then
	if [ ${status} -eq 0 ]; then
	        return
		fi
    fi

    echo ""
    echo "ccd operations were not sucessfully performed,"
    echo "it is highly probably you have to reboot this computer now."
    exit 1
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

    disklabel -r -w ${disk}s1 auto >> ${logfile} 2>&1
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
    local param=$2

    disklabel ${disk}s1 > /tmp/label
    ckstatus $? "disklabel"

    # calculate memory
    tmp=`sysctl hw.physmem | cut -w -f 2`
    memtotal=`expr ${tmp} / 1024 / 1024`

    if [ "${param}" = "root" ]; then
	cat >> /tmp/label <<EOF
 b: ${memtotal}m * swap
 d: * * hammer
EOF
    else
	cat >> /tmp/label <<EOF
 a: 768m 0 4.2BSD
 d: * * ccd
EOF
    fi
    disklabel -R ${disk}s1 /tmp/label >> ${logfile} 2>&1
    ckstatus $? "disklabel"
}

#
# ccdops
#
ccdops()
{
    local lst=""
    local tmp=""

    for disk in ${disklist}
    do
	tmp=${lst}
	lst="${tmp} /dev/${disk}s1d"
    done

    ccdconfig ccd0 128 CCDF_MIRROR ${lst}
    ckstatus $? "ccdconfig"
    sleep 3	# Try to settle ops
}

#
# gen_rc_ccd
#
gen_rc_ccd()
{
    local lst=""
    local tmp=""

    for disk in ${disklist}
    do
	tmp=${lst}
	lst="${tmp} /dev/${disk}s1d"
    done

    # dump the config
    ccdconfig -g > ${contents_dir}/etc/ccd.conf
    ckstatus $? "ccdconfig"

    cat >> ${contents_dir}/etc/rc.ccd << EOF
# call ccdconfig to restore the configuration
ccdconfig -C -f /etc/ccd.conf
sleep 5
EOF
    # Make sure it can be executed by initrd rc
    chmod +x ${contents_dir}/etc/rc.ccd
    sync
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

# kldload needed modules and start udevd
echo "* Loading modules"
ckloadmod ccd >> ${logfile} 2>&1

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
done

# ccdconfig operations in the disks
echo "* Performing CCD operations"
ccdops

# prepare the concatenated disk
prepdisk ccd0
sleep 1
mklabel ccd0 root

# Format the volumes
echo "* Formating ccd0"
newfs /dev/${bootdisk}s1a >> ${logfile} 2>&1
ccdexit $?

newfs_hammer -f -L ROOT /dev/ccd0s1d >> ${logfile} 2>&1
ccdexit $?

# Mount it
#
echo "* Mounting everything"
mount_hammer /dev/ccd0s1d /mnt >> ${logfile} 2>&1
ccdexit $?

mkdir /mnt/boot

mount /dev/${bootdisk}s1a /mnt/boot >> ${logfile} 2>&1
ccdexit $?

# Create PFS mount points for nullfs.
pfslist="usr usr.obj var var.crash var.tmp tmp home"

mkdir /mnt/pfs

for pfs in ${pfslist}
do
    hammer pfs-master /mnt/pfs/$pfs >> ${logfile} 2>&1
    ccdexit $?
done

# Mount /usr and /var so that you can add subdirs
mkdir /mnt/usr >> ${logfile} 2>&1
mkdir /mnt/var >> ${logfile} 2>&1
mount_null /mnt/pfs/usr /mnt/usr >> ${logfile} 2>&1
ccdexit $?

mount_null /mnt/pfs/var /mnt/var >> ${logfile} 2>&1
ccdexit $?

mkdir /mnt/usr/obj >> ${logfile} 2>&1
mkdir /mnt/var/tmp >> ${logfile} 2>&1
mkdir /mnt/var/crash >> ${logfile} 2>&1

mkdir /mnt/tmp >> ${logfile} 2>&1
mkdir /mnt/home >> ${logfile} 2>&1

mount_null /mnt/pfs/tmp /mnt/tmp >> ${logfile} 2>&1
ccdexit $?

mount_null /mnt/pfs/home /mnt/home >> ${logfile} 2>&1
ccdexit $?

mount_null /mnt/pfs/var.tmp /mnt/var/tmp >> ${logfile} 2>&1
ccdexit $?

mount_null /mnt/pfs/var.crash /mnt/var/crash >> ${logfile} 2>&1
ccdexit $?

mount_null /mnt/pfs/usr.obj /mnt/usr/obj >> ${logfile} 2>&1
ccdexit $?

chmod 1777 /mnt/tmp >> ${logfile} 2>&1
chmod 1777 /mnt/var/tmp >> ${logfile} 2>&1

# Install the system from the live CD
#
echo "* Starting file copy"
cpdup -vv -o / /mnt >> ${logfile} 2>&1
ccdexit $?

cpdup -vv -o /boot /mnt/boot >> ${logfile} 2>&1
ccdexit $?

cpdup -vv -o /usr /mnt/usr >> ${logfile} 2>&1
ccdexit $?

cpdup -vv -o /var /mnt/var >> ${logfile} 2>&1
ccdexit $?

cpdup -vv -i0 /etc.hdd /mnt/etc >> ${logfile} 2>&1
ccdexit $?

chflags -R nohistory /mnt/tmp >> ${logfile} 2>&1
chflags -R nohistory /mnt/var/tmp >> ${logfile} 2>&1
chflags -R nohistory /mnt/var/crash >> ${logfile} 2>&1
chflags -R nohistory /mnt/usr/obj >> ${logfile} 2>&1

echo "* Adapting fstab and loader.conf"
cat > /mnt/etc/fstab << EOF
# Device		Mountpoint	FStype	Options		Dump	Pass#
/dev/ccd0s1d	/		hammer	rw		1	1
/dev/${bootdisk}s1a	/boot		ufs	rw		1	1
/dev/ccd0s1b	none		swap	sw		0	0
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
ccd_load="YES"
initrd.img_load="YES"
initrd.img_type="md_image"
vfs.root.mountfrom="ufs:md0s0"
vfs.root.realroot="local:hammer:/dev/ccd0s1d"
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
dumpdev="/dev/ccd0s1b"
EOF

# Misc sysctls
#
cat >> /mnt/etc/sysctl.conf << EOF
#net.inet.ip.portrange.first=4000
EOF

# mkinitd image
echo "* Preparing initrd image"
cp /etc/defaults/mkinitrd.conf /etc/mkinitrd.conf
cpdup /usr/share/initrd ${contents_dir}

sed -i.bak '/^BIN_TOOLS/ s/\"$/ sleep\"/' /etc/mkinitrd.conf
sed -i.bak '/^SBIN_TOOLS/ s/\\/ccdconfig \\/' /etc/mkinitrd.conf

# Need to escape the directory so that sed doesn't take the var literally
escaped_var=$(printf '%s\n' "${contents_dir}" | sed 's/[\&/]/\\&/g')
sed -i.bak "s/^CONTENT_DIRS=.*/CONTENT_DIRS=\"${escaped_var}\"/" /etc/mkinitrd.conf

gen_rc_ccd

/sbin/mkinitrd -b /mnt/boot >> ${logfile} 2>&1

# copy installation log
echo "* Saving up installation log"
cp ${logfile} /mnt/tmp/

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
