#!/usr/local/www/cgi-bin/tablecg
#
# $DragonFly: site/data/community/Attic/download.cgi,v 1.4 2006/09/02 03:52:27 justin Exp $

$TITLE(DragonFly - Download)
<h1>Obtaining DragonFly for your system</h1>

<h2>CD Images</h2>

<p>
DragonFly CDs are 'live', meaning these CDs will boot your system 
and let you log in as root (no password).  You can use this feature to
check for hardware compatibility and play with DragonFly a little before
actually installing it on your hard drive.  The CD includes an installer
that can be run at the console, or (experimentally) via a web browser.
Make sure you read the
<a href="/cgi-bin/cvsweb.cgi/~checkout~/src/nrelease/root/README">README</a>
file for more information.   To activate the installer, boot the CD and
login as 'installer'.
See the 'Download Site' list below for a list of download locations. </p>

<h2>Obtaining source via CVS</h2>

<p>
If you prefer to obtain the source via cvsup, you can use 
<a href="dragonfly-cvs-supfile">this cvsup config file</a> to 
pull the source into /home/dcvs, no more than once a day. 
A <a href="dragonfly-cvs-sys-supfile">sys hierarchy only</a> cvsup 
file is available.  See below for suitable cvsup locations.
</p>

<p>
You typically extract sources from the repository into /usr/src using
something like this:
</p>
<ul>
<pre>
cd /usr
rm -rf src
cvs -d /home/dcvs checkout -rDragonFly_Preview src</pre>
</ul>

<p>
When extracting sources from the cvs repository please note the meaning of
the following tags:
<ul>
    <table BORDER="1">
    <tr><td><b>CVS Tag</b></td><td><b>Description</b></td></tr>
    <tr>
	<td>HEAD</td>
	<td>Represents the bleeding edge of DragonFly development.  Kernels
	    compiled from these sources will be marked DEVELOPMENT.</td>
    </tr>
    <tr>
	<td>DragonFly_Preview</td>
	<td>Represents a recent snapshot of the development tree.  Kernels
	    compiled from these sources are still highly developmental and
	    may be unstable, but are likely a lot more stable then HEAD.</td>
    </tr>
    <tr>
	<td>DragonFly_RELEASE_X_Y_Slip</td>
	<td>Represents code tracking a particular DragonFly release.  The
	    actual branch tag is the same tag without the '_Slip' suffix,
	    but since the subversion is only bumped once a day you should
	    use the full tag to extract sources so the subversion reported
	    by the kernel matches the actual subversion of the code and so
	    you do not catch the codebase in the middle of a commit.
	</td>
    </tr>
    </table>
</ul>
</p>

<h2>Other Sites</h2>

<p>
Daily DragonFly events and news are reported at the 
<a href="http://www.shiningsilence.com/dbsdlog/">DragonFly BSD Digest</a>.</p>
</p>

<h2>Current Working Set</h2>

<p>
DragonFly is under major development so while we do have a release cycle
there are often better choices.  We have daily snapshots and we have
preview snapshots of current development.  First-time DragonFly users are
probably best served using a preview snapshot.  These snapshots can be
located at <a href="ftp://ftp.dragonflybsd.org/iso-images/">ftp://ftp.dragonflybsd.org/iso-images</a>. 
Daily snapshots are available at <a href="http://chlamydia.fs.ei.tum.de/pub/DragonFly/snapshots/">http://chlamydia.fs.ei.tum.de/pub/DragonFly/snapshots/</a>.
</p>

<p>
DragonFly systems based on current development are labeled PREVIEW or
DEVELOPMENT.  People who want to keep up with recent developments but who
do not want the absolute bleeding-edge-likely-to-crash-in-bad-ways work
should use PREVIEW.  People who want the bleeding edge should use the
HEAD of the CVS tree which are marked DEVELOPMENT.  <i>We used to have
something called STABLE and CURRENT.  Those no longer exist.  STABLE has
been renamed to PREVIEW and CURRENT has been renamed to DEVELOPMENT.</i>
</p>

<h2>DragonFly Releases</h2>

<p>
Releases occur approximately twice a year.  DragonFly release branches
<b>only contain bug and security fixes</b> and are designed for people
running production systems who don't want any surprises.  Brand-new 
features often discussed on the mailing lists are typically not
in release branches.
</p>
<p>
The current release is <a href="release1_6.cgi">DragonFly 1.6.X-RELEASE</a>.
</p>

<p>
DragonFly systems based on releases are labeled RELEASE.  For example,
you might be running <b>DragonFly X.Y.Z-RELEASE</b>.  The subversion is
bumped on a daily basis when commits have been made to the release branch
that day and can become quite large.  For example, 1.4.75.
</p>

<h2>Download Sites</h2>

<TABLE BORDER="0">
<TR>
<TH>Organization</TH>
<TH>Mirrored Data</TH>
<TH>Access methods</TH>
</TR>

<!--
<TR><TD CLASS="mirrorsection" COLSPAN="3">Official Releases</TD></TR>

<TR><TD>July 2004 [Older Release]</TD>
<TD>1.0A Release</TD>
<TD><A HREF="release1_0.cgi">1.0A Release Page</A></TD></TR>

<TR>
<TD COLSPAN="3">&nbsp;</TD>
</TR>
-->

<TR><TD CLASS="mirrorsection" COLSPAN="3">Snapshots and ISO images</TD></TR>

<TR>
<TD>DragonFlyBSD.org (California)</TD>
<TD>Code, ISO master site (<B>NOTE: Use this site as a last resort!)</B>)</TD>
<TD><a href="ftp://ftp.dragonflybsd.org/">FTP</a>
</TD>
</TR>

<TR>
<TD>chlamydia.fs.ei.tum.de (Germany)</TD>
<TD>Snapshots master site, official ISOs</TD>
<TD>
<a href="http://chlamydia.fs.ei.tum.de/pub/DragonFly/">HTTP</a>,
<a href="ftp://chlamydia.fs.ei.tum.de/pub/DragonFly/">FTP</a>,
<a href="http://chlamydia.fs.ei.tum.de/services/#rsync">rsync</a>
</TD>
</TR>

<TR>
<TD>AllBSD.org (Japan)</TD>
<TD>Daily snapshots, official ISOs</TD>
<TD><a href="http://pub.allbsd.org/DragonFly/">HTTP</a>,
<a href="ftp://ftp.allbsd.org/pub/DragonFly/">FTP</a>, 
rsync
</TD>
</TR>

<TR>
<TD>Esat.net (IE)</TD>
<TD>Daily snapshots, official ISOs</TD>
<TD>
<a href="http://ftp.esat.net/mirrors/chlamydia.fs.ei.tum.de/pub/DragonFly/">HTTP</a>, 
<a href="ftp://ftp.esat.net/mirrors/chlamydia.fs.ei.tum.de/pub/DragonFly/">FTP</a>, and 
<a href="rsync://ftp.esat.net/mirrors/chlamydia.fs.ei.tum.de/pub/DragonFly/">rsync</a> (IPv4 and IPv6)
</TD>
</TR>

<TR>
<TD>Fortunaty.net (Europe)</TD>
<TD>Daily snapshots, official ISOs</TD>
<TD><a href="http://ftp.fortunaty.net/DragonFly/">HTTP</a>,
<a href="ftp://ftp.fortunaty.net/DragonFly/">FTP</a>
</TD>
</TR>

<TR>
<TD>The University of Vienna (Austria)</TD>
<TD>Daily snapshots, official ISOs</TD>
<TD>
<a href="http://ftp.univie.ac.at/systems/DragonFly/">HTTP</a>, 
<a href="ftp://ftp.univie.ac.at/systems/DragonFly/">FTP</a>, and
<a href="rsync://ftp.univie.ac.at/DragonFly/">rsync</a>
</TD>
</TR>

<!--
<TR>
<TD>University of Illinois at Chicago (Illinois, USA)</TD>
<TD>Official ISOs</TD>
<TD><a href="ftp://cvsup.math.uic.edu/dragonflybsd/">FTP</a></TD>
</TR>
-->

<TR>
<TD>Starkast.net (Sweden)</TD>
<TD>Daily snapshots, official ISOs</TD>
<TD>
<a href="http://ftp.starkast.net/pub/DragonFly/">HTTP</a>, 
<a href="ftp://ftp.starkast.net/pub/DragonFly/">FTP</a>
</TD>
</TR>

<!--
<TR>
<TD>bgp4.net</TD>
<TD>Daily snapshots, official ISOs</TD>
<TD>
<a href="http://mirror.bgp4.net/pub/DragonFly/">HTTP</a>, 
<a href="ftp://mirror.bgp4.net/pub/DragonFly/">FTP</a>
</TD>
</TR>
-->

<!--
<TR>
<TD>Chung Hua University (Taiwan)</TD>
<TD>code, official ISOs</TD>
<TD>
<a href="http://ftp.csie.chu.edu.tw">HTTP</a>, 
<a href="ftp://ftp.csie.chu.edu.tw">FTP</a>
</TD>
</TR>
-->

<TR>
<TD>Providence University (Taiwan)</TD>
<TD>Daily snapshots, official ISOs</TD>
<TD>
<a href="http://dragonflybsd.cs.pu.edu.tw/">HTTP</a>,
<a href="ftp://dragonflybsd.cs.pu.edu.tw/DragonFLYBSD">FTP</a>
</TD>
</TR>

<TR>
<TD>Japan Advanced Institute of Science and Technology (JAIST) (Ishikawa, Japan)</TD>
<TD>Daily snapshots, official ISOs</TD>
<TD>
<a href="http://ftp.jaist.ac.jp/pub/DragonFly/">HTTP</a>,
<a href="ftp://ftp.jaist.ac.jp/pub/DragonFly/">FTP</a>,
<a href="rsync://ftp.jaist.ac.jp/pub/DragonFly/">rsync</a>
</TD>
</TR>

<!--
<TR>
<TD>mirror.isp.net.au (Australia)</TD>
<TD>Daily snapshots</TD>
<TD>
<a href="http://mirror.isp.net.au/ftp/pub/DragonFly/iso-images/">HTTP</a>,
<a href="ftp://mirror.isp.net.au/pub/DragonFly/iso-images/">FTP</a>
</TD>
</TR>
-->

<TR>
<TD>TheShell.com (Phoenix AZ, USA)</TD>
<TD>Code, official ISOs</TD>
<TD>
<a href="http://theshell.com/pub/DragonFly/">HTTP</a>,
<a href="ftp://ftp.theshell.com/pub/DragonFly/">FTP</a>, and
<a href="rsync://rsync.theshell.com/pub/DragonFly/">rsync</a> (IPv4 and IPv6)
</TD>
</TR>

<TR>
<TD>mirror.macomnet.net (Moscow, Russia)</TD>
<TD>Daily snapshots, official ISOs</TD>
<TD>
<a href="http://mirror.macomnet.net/pub/DragonFlyBSD/">HTTP</a>,
<a href="ftp://mirror.macomnet.net/pub/DragonFlyBSD/">FTP</a>, and
<a href="rsync://mirror.macomnet.net/pub/DragonFlyBSD/">rsync</a>
</TD>
</TR>

<TR>
<TD>ftp.tu-clausthal.de (Germany)</TD>
<TD>Official ISOs</TD>
<TD>
<a href="http://ftp.tu-clausthal.de/pub/DragonFly/">HTTP</a>,
<a href="ftp://ftp.tu-clausthal.de/pub/DragonFly/">FTP</a>
</TD>
</TR>

<TR>
<TD>Demokritos University of Thrace (Greece)</TD>
<TD>Daily Snapshots, Official ISOs</TD>
<TD>
<a href="http://ftp.duth.gr/pub/DragonflyBSD/">HTTP</a>,
<a href="ftp://ftp.duth.gr/pub/DragonflyBSD/">FTP</a>
<a href="rsync://ftp.duth.gr/DragonflyBSD/">rsync</a>
</TD>
</TR>

<!-- binary packages after this -->
<TR>
<TD COLSPAN="3">&nbsp;</TD>
</TR>

<TR><TD CLASS="mirrorsection" COLSPAN="3">pkgsrc binary mirrors</TD></TR>

<TR>
<TD>packages.stura.uni-rostock.de (Germany)</TD>
<TD>Master site for binary pkgsrc packages</TD>
<TD><a href="ftp://packages.stura.uni-rostock.de/pkgsrc-current/DragonFly/RELEASE/i386/All/">i386 packages</a>,
<a href="ftp://packages.stura.uni-rostock.de/pkgsrc-current/DragonFly/RELEASE/bootstrap/">bootstrap kits</a>
</TD>
</TR>

<TR>
<TD>chlamydia.fs.ei.tum.de (Germany)</TD>
<TD>binary pkgsrc packages </TD>
<TD><a href="http://chlamydia.fs.ei.tum.de/pub/DragonFly/packages/RELEASE/i386/All/">i386 packages</a>, 
<a href="http://chlamydia.fs.ei.tum.de/pub/DragonFly/packages/RELEASE/bootstrap/">bootstrap kits</a>
</TD>
</TR>

<TR>
<TD>ftp.pvvntnu.no (Norway)</TD>
<TD>binary pkgsrc packages </TD>
<TD><a href="ftp://ftp.pvv.ntnu.no/pub/DragonFly/packages/pkgsrc-current/DragonFly/RELEASE/i386/All/">i386 packages</a>, 
<a href="ftp://ftp.pvv.ntnu.no/pub/DragonFly/packages/pkgsrc-current/DragonFly/RELEASE/bootstrap/">bootstrap kits</a>
</TD>
</TR>

<TR>
<TD>TheShell.com (Phoenix, AZ, USA)</TD>
<TD>binary pkgsrc packages </TD>
<TD><a href="http://theshell.com/pub/DragonFly/packages/RELEASE/i386/All/">i386 packages</a>, 
<a href="http://theshell.com/pub/DragonFly/packages/RELEASE/bootstrap/">bootstrap kits</a>
</TD>
</TR>

<TR>
<TD>Japan Advanced Institute of Science and Technology (JAIST) (Ishikawa, Japan)</TD>
<TD>binary packages</TD>
<TD>
<a href="ftp://ftp.jaist.ac.jp/pub/DragonFly/packages/DEVELOPMENT/i386/">i386 packages</a>,
<a href="ftp://ftp.jaist.ac.jp/pub/DragonFly/packages/DEVELOPMENT/bootstrap/">bootstrap kits</a>
</TD>
</TR>

<TR>
<TD>Demokritos University of Thrace (Greece)</TD>
<TD>binary pkgsrc packages</TD>
<TD>
<a href="ftp://ftp.duth.gr/pub/DragonflyBSD/packages/RELEASE/i386/All/">i386 packages</a>
<a href="ftp://ftp.duth.gr/pub/DragonflyBSD/packages/RELEASE/bootstrap/">bootstrap kits</a>
</TD>
</TR>

<!-- source only after this -->

<TR>
<TD COLSPAN="3">&nbsp;</TD>
</TR>

<TR><TD CLASS="mirrorsection" COLSPAN="3">Source mirrors</TD></TR>

<TR>
<TD>DragonFlyBSD.org (California)</TD>
<TD>Code master site (<B>NOTE: Use other sites if possible!</B>)</TD>
<TD> 
<a href="http://www.dragonflybsd.org/cgi-bin/cvsweb.cgi">cvsweb</a>
</TD>
</TR>

<TR>
<TD>chlamydia.fs.ei.tum.de (Germany)</TD>
<TD>Code</TD>
<TD>
<a href="http://chlamydia.fs.ei.tum.de/services/#anoncvs">anoncvs</a>,
<a href="http://chlamydia.fs.ei.tum.de/services/#cvsup">cvsup</a>
</TD>
</TR>

<TR>
<TD>AllBSD.org (Japan)</TD>
<TD>Code</TD>
<TD>
<a href="http://www.allbsd.org/#pub-anoncvs">anoncvs</a>,
rsync, cvsup, cvsync, cvsweb
</TD>
</TR>

<TR>
<TD>grappa.unix-ag.uni-kl.de (Germany)</TD>
<TD>Code</TD>
<TD>
<a href="http://grappa.unix-ag.uni-kl.de/cgi-bin/cvsweb/?cvsroot=dragonfly">cvsweb</a>, 
cvsup, cvsync, rsync, anoncvs
</TD>
</TR>

<TR>
<TD>mirror.isp.net.au (Australia)</TD>
<TD>Code</TD>
<TD>
cvsup, rsync, <a href="http://mirror.isp.net.au/cgi-bin/cvsweb.cgi/?cvsroot=dragonfly">cvsweb</a>
</TD>
</TR>

<TR>
<TD>TheShell.com (Phoenix, AZ, USA)</TD>
<TD>Code</TD>
<TD>
<a href="http://cvsweb.theshell.com/">cvsweb</a>,
cvsup, cvsync, rsync
</TD>
</TR>

<!--
<TR>
<TD>alxl.info (Riga, Latvia)</TD>
<TD>Code</TD>
<TD>
cvsup, <a href="http://alxl.info/cgi-bin/cvsweb.cgi/">cvsweb</a>
</TD>
</TR>
-->

<TR>
<TD>dragonflybsd.delphij.net (China)</TD>
<TD>Code (<B>NOTE: China mirror only</B>)</TD>
<TD>
cvsup
</TD>
</TR>

<TR>
<TD>fred.acm.cs.rpi.edu (Troy, New York)</TD>
<TD>Code</TD>
<TD>
cvsup
</TD>
</TR>

</TABLE>

<h2>Commercial Sites</h2>

<TABLE BORDER="0">

<TR>
<TD COLSPAN="2">Any commercial site selling DragonFly related material can be listed in
this section.</TD>
</TR>

<TR>
<TD COLSPAN="2">
&nbsp;
</TD>
</TR>

<TR>
<TH>Organization</TH>
<TH>Access methods</TH>
</TR>

<TR>
<TD><B>BSD-Systems</B><br>
BSD-Systems.co.uk offers high quality Open Source Operating Systems 
available for purchase on CD, and will ship to UK or international 
addresses.
</TD>
<TD><A HREF="http://bsd-systems.co.uk/">http://bsd-systems.co.uk/</A></TD>
</TR>

<TR>
<TD><B>Ikarios</B><br>
Ikarios is a low-cost supplier (mail-order only) of CDs, shipping in most
European countries.<br>
Each industrially-produced and tested CD costs at most 1.99 euro (decreasing rate).<br>
</TD>
<TD><A HREF="http://www.ikarios.com/form#dfbsd">http://www.ikarios.com/</A></TD>
</TR>

<TR>
<TD><B>Lehmanns</B><br>
Lehmanns is a bookshop located in Germany, offering various
BSD and Linux distributions.  Their FreeBSD DVDs also
contain the latest version of DragonFly BSD, which can
be started straight from the boot menu of the DVD.
</TD>
<TD><A HREF="http://www.lob.de/">http://www.lob.de/</A></TD>
</TR>

<TR>
<TD><B>Linux Bazar</B><br>
Linux Bazar is a low-cost supplier of software CDs.  Shipping is only to 
addresses within India.
</TD>
<TD><A HREF="http://www.LinuxBazar.com/">http://www.LinuxBazar.com/</A></TD>
</TR>

<tr>
<td><b>Linux CD Mall</b><br/>
Linux CD Mall, a family business operating from Auckland, NZ, is run by
Chris Hope and The Electric Toolbox Ltd to distribute Linux and BSD
distributions on CD and DVD to New Zealand and the rest of the world.
</td>
<td><a href="http://www.linuxcdmall.com/dragonfly.html">http://www.linuxcdmall.com/dragonfly.html</a></td>
</tr>

<tr>
<td><b>Tetrad Digital Integrity</b><br/>
TDI is an information assurance company in the Washington DC area, which
can provide consulting services for deploying DragonFly BSD in all areas of
enterprise use - desktop, server and infrastructure (router/firewall
etc)</td>
<td><a href="http://www.tdisecurity.com/">http://www.tdisecurity.com</a><br/>
dfbsd@tdisecurity.com</td>
</tr>


</TABLE>
