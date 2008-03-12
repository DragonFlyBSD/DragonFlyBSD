-- Example command names conf file for dfuibe_lua backend.
-- $Id: cmdnames.lua,v 1.8 2005/03/27 23:15:43 cpressey Exp $

local cmd_names = {
	SH		= "bin/sh",
	MKDIR		= "bin/mkdir",
	CHMOD		= "bin/chmod",
	LN		= "bin/ln",
	RM		= "bin/rm",
	CP		= "bin/cp",
	DATE		= "bin/date",
	ECHO		= "bin/echo",
	DD		= "bin/dd",
	MV		= "bin/mv",
	CAT		= "bin/cat",
	TEST		= "bin/test",
	TEST_DEV	= "bin/test -c",
	CPDUP		= "bin/cpdup -o -vvv -I",

	ATACONTROL	= "sbin/atacontrol",
	MOUNT		= "sbin/mount",
	MOUNT_MFS	= "sbin/mount_mfs",
	UMOUNT		= "sbin/umount",
	SWAPON		= "sbin/swapon",
	DISKLABEL	= "sbin/disklabel",
	NEWFS		= "sbin/newfs",
	NEWFS_MSDOS	= "sbin/newfs_msdos",
	FDISK		= "sbin/fdisk",
	DUMPON		= "sbin/dumpon",
	IFCONFIG	= "sbin/ifconfig",
	ROUTE		= "sbin/route",
	DHCLIENT	= "sbin/dhclient",
	SYSCTL		= "sbin/sysctl",

	TOUCH		= "usr/bin/touch",
	YES		= "usr/bin/yes",
	BUNZIP2		= "usr/bin/bunzip2",
	GREP		= "usr/bin/grep",
	KILLALL		= "usr/bin/killall",
	BASENAME	= "usr/bin/basename",
	SORT		= "usr/bin/sort",
	COMM		= "usr/bin/comm",
	AWK		= "usr/bin/awk",
	SED		= "usr/bin/sed",
	BC		= "usr/bin/bc",
	TR		= "usr/bin/tr",
	FIND		= "usr/bin/find",
	CHFLAGS		= "usr/bin/chflags",
	XARGS		= "usr/bin/xargs",

	PWD_MKDB	= "usr/sbin/pwd_mkdb",
	CHROOT		= "usr/sbin/chroot",
	VIDCONTROL	= "usr/sbin/vidcontrol",
	KBDCONTROL	= "usr/sbin/kbdcontrol",
	PW		= "usr/sbin/pw",
	SWAPINFO	= "usr/sbin/swapinfo",
	BOOT0CFG	= "usr/sbin/boot0cfg",
	FDFORMAT	= "usr/sbin/fdformat",
	MTREE		= "usr/sbin/mtree",
	INETD		= "usr/sbin/inetd",
	DHCPD		= "usr/sbin/dhcpd",
	RPCBIND		= "usr/sbin/rpcbind",
	MOUNTD		= "usr/sbin/mountd",
	NFSD		= "usr/sbin/nfsd",

	PKG_ADD		= "usr/sbin/pkg_add",
	PKG_DELETE	= "usr/sbin/pkg_delete",
	PKG_CREATE	= "usr/sbin/pkg_create",
	PKG_INFO	= "usr/sbin/pkg_info",

	TFTPD		= "usr/libexec/tftpd",

	CVSUP		= "usr/local/bin/cvsup",
	MEMTEST		= "usr/local/bin/memtest",

	-- These aren't commands, but they're configurable here nonetheless.

	DMESG_BOOT	= "var/run/dmesg.boot"
}

if App.os.name == "OpenBSD" then
	cmd_names.TEST_DEV = "bin/test -b"
end

if App.os.name ~= "DragonFly" then
	cmd_names.CPDUP = "usr/local/bin/cpdup -o -vvv"
end

return cmd_names
