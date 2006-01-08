#!/usr/local/www/cgi-bin/tablecg
#
# $DragonFly: site/data/main/Attic/errata1_4.cgi,v 1.1 2006/01/08 19:02:45 dillon Exp $

$TITLE(DragonFly - January 2006 Release 1.4.x Errata Page)
<h1>Errata Page for DragonFly 1.4.x</h1>

<h2>1.4.0 Errata</h2>
<ul>
    <li><p>
	European timezones may not be properly set by the installer.  You
	will have to go in post-install and set the timezone by copying the
	correct file from /usr/share/zoneinfo to /etc/localtime.  Do not
	use a softlink or the default time will not use the correct timezone
	in single-user mode.
    </p></li>

    <li><p>
	When upgrading a system a new /etc/pam.d directory is created
	but not populated.  You must run ./convert.sh from the /etc/pam.d
	directory.  PAM will not work properly until you do.</p>
    </p></li>
    <li><p>
	Due to our switch from ports to pkgsrc and the mistake (which we 
	cannot take back now) of using /var/db/pkg for both, you may have
	to clean out the /var/db/pkg directory to avoid conflicts.  Note
	that the actual install location for ports was /usr/local, and
	the install location for pkgsrc is /usr/pkg, so the actual installed
	packages should not theoretically conflict.
    </p></li>
    <li><p>
	A larger problem is that third party libraries used by ports or
	pkgsrc will be geared towards either the old libc.so.4 or the new
	libc.so.6 and cannot be mixed and matched.  A complete reinstall of
	third party packages via the new pkgsrc system is recommended.  That
	means basically backing up and then wiping /usr/local and starting
	fresh with pkgsrc.  However, we realize that pkgsrc coversage is
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
</ul>
