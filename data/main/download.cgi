#!/usr/local/www/cgi-bin/tablecg
#
# $DragonFly: site/data/main/Attic/download.cgi,v 1.73 2005/01/30 21:39:16 liamfoy Exp $

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
<b>NOTE! there is currently a lot of VFS work going on in HEAD.  When
using CVS we recommend that you checkout and track the DragonFly_Stable
tag rather then HEAD.</b></p>

<h2>Other Sites</h2>

<a href="http://gobsd.com">GoBSD.com</a>, a BSD-centric community website, is providing access to thousands of 
<a href="http://gobsd.com/packages">pre-built DragonFly software packages</a>.  These can be added via 
<code>pkg_add -r <i>packagename</i></code>.

<p>
Daily DragonFly events and news are reported at the 
<a href="http://www.shiningsilence.com/dbsdlog/">DragonFly BSD Log</a>.</p>

<h2>Current Working Set</h2>

<p>
DragonFly is under major development so while we do have a release cycle
there are actually better choices.  We have daily snapshots and we have
selected 'reasonably stable' snapshots.  First-time DragonFly users are
probably best served by the most recent 'reasonably stable' snapshot,
which is currently dfly-20041222-stable.iso.gz.</p>
<p>
Releases occur approximately every 6 months and serve as major feature
points in the DragonFly distribution, but as a young project DragonFly
does not yet have the developer resources to maintain multiple release
branches.  Users are best served by downloading one of the recent stable
tagged snapshots.
<p>
We are building both gcc-2.95.x and gcc-3.4 based daily snapshots.  
<b><i>gcc-3.4 based systems are considered experimental and should only
be used for testing and development purposes!</i></b>.  Most DragonFly users
and, indeed, most developers, are using pain-free gcc-2.95.x snapshots.
Both compilers are available for use in all DragonFly snapshots.</p>

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
<a href="http://chlamydia.fs.ei.tum.de/pub/DragonFly/snapshots">HTTP</a>
<a href="ftp://chlamydia.fs.ei.tum.de/pub/DragonFly/snapshots">FTP</a>
</TD>
</TR>

<TR>
<TD>AllBSD.org (Japan)</TD>
<TD>Daily snapshots, official ISOs</TD>
<TD><a href="http://pub.allbsd.org/DragonFly/snapshots/">HTTP</a>,
<a href="ftp://ftp.allbsd.org/pub/DragonFly/snapshots/">FTP</a>, 
rsync
</TD>
</TR>

<TR>
<TD>dragon.BSDTech.com (Norway)</TD>
<TD>Daily snapshots, official ISOs</TD>
<TD>
<a href="http://dragon.bsdtech.com/DragonFly/">HTTP</a>, 
<a href="ftp://dragon.bsdtech.com/DragonFly/">FTP</a>
</TD>
</TR>

<TR>
<TD>Esat.net (UK)</TD>
<TD>Daily snapshots, official ISOs</TD>
<TD>
<a href="http://ftp.esat.net/mirrors/chlamydia.fs.ei.tum.de/pub/DragonFly/">HTTP</a>, 
<a href="ftp://ftp.esat.net/mirrors/chlamydia.fs.ei.tum.de/pub/DragonFly/">FTP</a>, and 
<a href="rsync://ftp.esat.net/mirrors/chlamydia.fs.ei.tum.de/pub/DragonFly/">rysnc</a>. (IPv4 and IPv6)
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

<TR>
<TD>University of Chicago (Illinois, USA)</TD>
<TD>Official ISOs</TD>
<TD><a href="ftp://cvsup.math.uic.edu/dragonflybsd/">FTP</a></TD>
</TR>

<TR>
<TD>Starkast.net (Sweden)</TD>
<TD>Daily snapshots, official ISOs</TD>
<TD>
<a href="http://ftp.starkast.net/pub/DragonFly/">HTTP</a>, 
<a href="ftp://ftp.starkast.net/pub/DragonFly/">FTP</a>
</TD>
</TR>

<TR>
<TD>bgp4.net</TD>
<TD>Daily snapshots, official ISOs</TD>
<TD>
<a href="http://www.bgp4.net/pub/DragonFly/">HTTP</a>, 
<a href="ftp://ftp.bgp4.net/pub/DragonFly/">FTP</a>
</TD>
</TR>

<TR>
<TD>Chung Hua University (Taiwan)</TD>
<TD>code, official ISOs</TD>
<TD>
<a href="http://ftp.csie.chu.edu.tw">HTTP</a>, 
<a href="ftp://ftp.csie.chu.edu.tw">FTP</a>
</TD>
</TR>

<TR>
<TD>Providence University (Taiwan)</TD>
<TD>Daily snapshots, official ISOs</TD>
<TD>
<a href="http://dragonflybsd.cs.pu.edu.tw/">HTTP</a>,
<a href="ftp://dragonflybsd.cs.pu.edu.tw/DragonFLYBSD">FTP</a>
</TD>
</TR>

<TR>
<TD>mirror.isp.net.au (Australia)</TD>
<TD>Daily snapshots</TD>
<TD>
<a href="http://mirror.isp.net.au/ftp/pub/DragonFly/iso-images/">HTTP</a>,
<a href="ftp://mirror.isp.net.au/pub/DragonFly/iso-images/">FTP</a>
</TD>
</TR>

<TR>
<TD>theshell.com (Phoenix Arizona, USA)</TD>
<TD>Daily snapshots, official ISOs</TD>
<TD>
<a href="http://theshell.com/pub/DragonFly/">HTTP</a>,
<a href="ftp://ftp.theshell.com/pub/DragonFly/">FTP</a>, and
<a href="rsync://rsync.theshell.com/pub/DragonFly/">rsync</a>
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
<TD>bsd.vt220.com (Quebec)</TD>
<TD>code, official ISOs</TD>
<TD>
<a href="http://bsd.vt220.com/DragonFly/">HTTP</a>
<a href="ftp://bsd.vt220.com/DragonFly/">FTP</a>
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
<TD>AllBSD.org (Japan)</TD>
<TD>Code</TD>
<TD>
rsync, cvsup, cvsync, cvsweb
</TD>
</TR>

<TR>
<TD>dragon.BSDTech.com (Norway)</TD>
<TD>Code</TD>
<TD>
cvsup, <a href="http://dragon.BSDTech.com/DragonFly/">source/dfports snapshots</a></TD>
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
<TD>dragonfly.the-bofh.org (Holland)</TD>
<TD>Code</TD>
<TD>
cvsup, <a href="http://dragonfly.the-bofh.org/cgi-bin/cvsweb.cgi/">cvsweb</a>
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
<TD>alxl.info (Riga, Latvia)</TD>
<TD>Code</TD>
<TD>
cvsup, <a href="http://alxl.info/cgi-bin/cvsweb.cgi/">cvsweb</a>
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
<TD><A HREF="http://bsd-systems.co.uk/">http://bsd-systems.co.uk/</A>
</TR>


<TR>
<TD><B>Crescent Anchor</B><br>
Crescent Anchor will be providing DragonFly 1.0A on CD-ROM for a
nominal fee.  Part of your purchase from Crescent Anchor will be contributed
back to support continuing DragonFly development via the forthcoming 
nonprofit company.  Please visit the Crescent Anchor site for details, 
pricing, and other offers.  </TD>
<TD><A HREF="http://www.crescentanchor.com/">http://www.crescentanchor.com/</A>
</TR>

<TR>
<TD><B>Ikarios</B><br>

Ikarios is a low-cost supplier (mail-order only) of CDs, shipping in most
European countries.<br>
Each industrially-produced and tested CD-R of "DragonFly BSD 1.0A RELEASE"
CD-R costs at most 1.99 euro (decreasing rate).<br>
</TD>
<TD><A HREF="http://www.ikarios.com/form#dfbsd">http://www.ikarios.com/</A>
</TR>

<TR>
<TD><B>Linux Bazar</B><br>
Linux Bazar is a low-cost supplier of software CDs.  Shipping is only to 
addresses within India.
</TD>
<TD><A HREF="http://www.LinuxBazar.com/">http://www.LinuxBazar.com/</A>
</TR>


</TABLE>
