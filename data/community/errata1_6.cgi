#!/usr/local/www/cgi-bin/tablecg
#
# $DragonFly: site/data/community/Attic/errata1_6.cgi,v 1.1 2006/08/03 19:14:08 dillon Exp $

$TITLE(DragonFly - January 2006 Release 1.6.x Errata Page)
<h1>Errata Page for DragonFly 1.6.x</h1>

<p>
General note on errata:  We do not roll a release CD every time we bump
the sub-version.  The standard upgrade method is to build from sources.
The 
<A HREF="http://leaf.dragonflybsd.org/cgi/web-man?command=build&section=ANY">build</A>
manual page gives you information on how to upgrade your system from
sources.  See the <A HREF="download.cgi">download</A> page for information on tracking changes with
cvsup.
</p>

<h2>1.6.1 Errata</h2>
<ul>
    <i>(none as yet)</i>
</ul>

<h2>1.6.0 Errata</h2>
<ul>
    <li><p>
	A bug in the kernel flock code could result in sendmail or
	postfix hanging.  Fixed in 1.6.1.
    </p></li>
</ul>
