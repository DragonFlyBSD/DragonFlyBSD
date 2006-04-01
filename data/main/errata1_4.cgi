#!/usr/local/www/cgi-bin/tablecg
#
# $DragonFly: site/data/main/Attic/errata1_4.cgi,v 1.4 2006/04/01 23:04:45 justin Exp $

$TITLE(DragonFly - January 2006 Release 1.4.x Errata Page)
<h1>Errata Page for DragonFly 1.4.x</h1>

<h2>1.4.0 Errata</h2>
<ul>
    <li><p>
	European timezones may not be properly set by the installer.  You
	will have to go in post-install and set the timezone by running
	the 'tzsetup' program.
	<pre>
tzsetup
	</pre>
    </p></li>

    <li><p>
	When upgrading a system a new /etc/pam.d directory is created
	but not populated.  You must run ./convert.sh from the /etc/pam.d
	directory.  PAM will not work properly until you do.  For example,
	you would not be able to login to X using xdm.</p>
	<pre>
cd /etc/pam.d
sh ./convert.sh
	</pre>
    </p></li>
    <li><p>
	A larger problem is that third party libraries used by ports or
	pkgsrc will be geared towards either the old libc.so.4 or the new
	libc.so.6 and cannot be mixed and matched.  A complete reinstall of
	third party packages via the new pkgsrc system is recommended.  That
	means basically backing up and then wiping /usr/local and starting
	fresh with pkgsrc.  However, we realize that pkgsrc coverage is
	not (yet) as extensive as ports coverage and there will be a number
	of packages that cannot be easily installed via the pkgsrc system.
    </p></li>
    <li><p>
	If you intend to use CVS we recommend that you create a ~/.cvsrc
	file in your home directory and in root's home directory with the
	following contents.  The -P is particularly important because some
	parts of the build get confused by empty directories.
	<pre>
cvs -q
diff -u
update -Pd
checkout -P
	</pre>
    </p></li>
    <li><p>
	The installer fails to install the file /etc/mk.conf.  The following
	is recommended, but you have two choices.  If you are upgrading an
	older system that has installed ports, you may want to change 
	pkgsrc's notion of the pkgsrc database from "/var/db/pkg" to, say
	"/var/db/pkgsrc", to avoid conflicts with the ports database.
	Otherwise just stick with "/var/db/pkg".  The two key items here
	is the defaulting of X to 'xorg' and setting a WRKOBJDIR to avoid
	cluttering the package source tree.
	<pre>
.ifdef BSD_PKG_MK

PKG_DBDIR=/var/db/pkg
LOCALBASE=/usr/pkg
VARBASE=/var
FETCH_CMD=/usr/pkg/bin/ftp
PAX=/usr/pkg/bin/pax
X11_TYPE=xorg
WRKOBJDIR=/usr/obj/pkgsrc

.endif
	</pre>
    </p></li>
    <li><p>
    Packages pre-installed by the CD have incorrect paths in their +CONTENTS
    files.  The following commands will clean these up:
    <pre>
find /var/db/pkg -name +CONTENTS -type f -exec \
	sed -i '' -e 's,/usr/release/root,,' -- {} \;
pkg_admin rebuild
    </pre>
    </p></li>
    <li><p>
	Binary packages are available
	<A HREF="http://chlamydia.fs.ei.tum.de/pub/DragonFly/packages/RELEASE/i386/All/">here</A>.
    </p></li>
    <li><p>
	The newaliases command must be run as root, otherwise sendmail will
	complain about not being able to find the mail aliases database.
	<pre>
newaliases
	</pre>
    </p></li>
    <li><p>
	The skeleton .cshrc and .profile for newly added users does not
	contain the pkgsrc binary paths, and rc.shutdown also did not contain
	the required path.  The commit and diff is available at this
	URL: <A HREF="http://leaf.dragonflybsd.org/mailarchive/commits/2006-01/msg00038.html">SkeletonFixes</A>
    </p></li>
</ul>
