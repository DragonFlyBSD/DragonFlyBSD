#!/bin/csh
#
# mktemplate systembase templatebase
#
# This creates the Template directory which will be copied onto the
# target worker chroot/jail after various mounts.  The template itself
# does not have any sub-mounts nor does it need to provide mount points,
# those will be created in mount.c.  Most system directories such as /bin
# will be null-mounted later and do not have to be provided by this script.
#
# But any directories with special perms, such as /tmp and /var/tmp, are
# provided by the template, and it is also responsible for providing
# a sanitized /etc.
#
#

if ( $#argv != 2 ) then
	echo "bad argument count"
	echo "mktemplate systembase templatebase"
	exit 1
endif

set sysbase = $argv[1]
set template = $argv[2]
set nonomatch

echo "Creating template from $sysbase to $template"

mkdir -p $template
mkdir -m 1777 -p $template/tmp
mkdir -m 1777 -p $template/var/tmp
mkdir -p $template/etc
cp -Rp $sysbase/etc/. $template/etc

foreach i ( `(cd $sysbase; find /var -type d)` )
	mkdir -p $template/$i
end

mkdir -p $template/var/mail
chown root:mail $template/var/mail
chmod 775 $template/var/mail

mkdir -p $template/var/games
chown root:games $template/var/games
chmod 775 $template/var/games

mkdir -p $template/var/msgs
chown daemon:wheel $template/var/msgs

# Delete sensitive data from /etc
#
# Delete timezone translation so all packages are built in
# GMT.  Fixes at least one package build.
#
rm -f $template/etc/ssh/*key*
rm -f $template/etc/localtime

if ( -f $template/etc/master.passwd ) then
	cat $sysbase/etc/master.passwd | \
		sed -e 's/:[^:]*:/:\*:/1' > $template/etc/master.passwd
endif

mkdir -p $template/root
mkdir -p $template/usr/local/etc
mkdir -p $template/usr/local/etc/pkg
mkdir -p $template/usr/local/bin
mkdir -p $template/usr/local/sbin
mkdir -p $template/usr/local/lib
mkdir -p $template/var/run

cp /var/run/ld-elf.so.hints $template/var/run

#echo > $template/usr/local/etc/pkg.conf
