#!/usr/local/www/cgi-bin/tablecg
#
# $DragonFly: site/data/community/Attic/errata1_4_4.cgi,v 1.1 2006/04/23 21:33:57 dillon Exp $

$TITLE(DragonFly - January 2006 Release 1.4.x Errata Page)
<h1>Errata Page for DragonFly 1.4.x</h1>

<h2>1.4.4 Errata</h2>
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
	Binary packages are available
	<A HREF="http://chlamydia.fs.ei.tum.de/pub/DragonFly/packages/RELEASE/i386/All/">here</A>.
    </p></li>
</ul>
