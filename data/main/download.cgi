#!/usr/local/www/cgi-bin/tablecg
#
# $DragonFly: site/data/main/Attic/download.cgi,v 1.46 2004/07/15 00:01:14 dillon Exp $

$TITLE(DragonFly - Download)
<h1>Obtaining DragonFly for your system</h1>

<h2>CD Images</h2>

<p>
DragonFly CDs are 'live', meaning these CDs will boot your system 
and let you log in as root (no password) and install to your IDE disk-based 
system.  The CD includes an installer that can be run at the console, or 
(experimentally) via a web browser.  Make sure you read the 
<a href="/cgi-bin/cvsweb.cgi/~checkout~/src/nrelease/root/README">README</a> file 
for more information.  
See the 'Download Site' list below for a list of download locations. </p>

<p>
<b>The DragonFly 1.0-RELEASE is here!  See below for download sites!  The MD5s
for the release is:
<br />MD5 (dfly-1.0REL.iso.gz) = c95a378c13257f39420f5f9e4104bd7b
<br />MD5 (dfly-1.0_REL-1.0A_REL.xdelta) = 6001980541a4a2b77505c1845f925d57
<br />MD5 (dfly-1.0A_REL.iso) = ddf5686f828b2ece0b4029ae3ed78c2a
<br />MD5 (dfly-1.0A_REL.iso.gz) = b1c8ded31133960fa58a7b10c300aabd

</b><br /> </p>
<p><b>NOTICE!  RELEASE UPDATED TO 1.0A TO FIX A SERIOUS FDISK/SLICE ISSUE
WITH THE INSTALLER.  An xdelta (/usr/ports/misc/xdelta) patch is available
for people who have downloaded the original 1.0REL iso, and a new ISO is
being propagated to our mirrors.  To apply the delta, unzip the 1.0REL ISO,
apply the delta, and then run md5 on the result to ensure that it matches
the 1.0A_REL (ungzipped) ISO.</b></p>

<h2>Obtaining source via CVS</h2>

<p>
If you prefer to obtain the source via cvsup, you can use 
<a href="dragonfly-cvs-supfile">this cvsup config file</a> to 
pull the source into /home/dcvs, no more than once a day. 
A <a href="dragonfly-cvs-sys-supfile">sys hierarchy only</a> cvsup 
file is available.  See below for suitable cvsup locations.</p>


<h2>Other Sites</h2>

<a href="http://gobsd.com">GoBSD.com</a>, a BSD-centric community website, is providing access to thousands of 
<a href="http://gobsd.com/packages">pre-built DragonFly software packages</a>.  These can be added via 
<code>pkg_add -r <I>packagename</i></code>.

<p>
Daily DragonFly events and news are reported at the 
<a href="http://www.shiningsilence.com/dbsdlog/">DragonFly BSD Log</a>.</p>

<h2>1.0 Release Errata</h2>
<p>
<A HREF="http://www.bsdinstaller.org/errata.html">Installer Errata</A>
</p>
<p>
<b>IMPORTANT ERRATA ADDENDUM: Using the installer on a multi-slice disk
will improperly resize the target slice when it is not the last slice,
to be the same size as the last slice, leading to a corrupt disk!  
1.0A fixes the problem and is now online and there is an xdelta available at:
<A HREF="ftp://ftp.dragonflybsd.org/iso-images/dfly-1.0_REL-1.0A_REL.xdelta">dfly-1.0_REL-1.0A_REL.xdelta (master site)</A>.
The 1.0A ISO is being propogated to our mirrors now.  If you have the original
release iso please use the xdelta program and the above xdelta patch on the
gunzipped ISO to patch it to 1.0A, then check the MD5 of the result.</b>
</p>

<h2>Download Sites</h2>

<TABLE BORDER="0">
<TR>
<TH>Organization</TH>
<TH>Mirrored Data</TH>
<TH>Access methods</TH>
</TR>


<!-- REL links are all grouped together here. -->
<!-- for REL*/1.x releases, please list them here separately, -->
<!-- even if the site's a regular mirror too. -->

<TR BGCOLOR="#EEEEEE"><TD>GoBSD.COM</TD>
<TD>1.0A_REL image</TD>
<TD><A HREF="http://gobsd.com/dfly-1.0A_REL.iso.gz">HTTP</A></TD></TR>

<!-- <TR BGCOLOR="#EEEEEE"><TD>The-BOFH.org (Holland)</TD>
<TD>1.0_REL image</TD>
<TD><A HREF="http://www.the-bofh.org/dfly-1.0REL.iso.gz">HTTP</A></TD></TR> 
-->

<!--
<TR BGCOLOR="#EEEEEE"><TD>Sitetronics.com</TD>
<TD>1.0_REL image</TD>
<TD><A HREF="http://freebsd0.sitetronics.com/~dodell/dfly-1.0REL.iso.gz">HTTP</A></TD></TR>
-->

<TR BGCOLOR="#EEEEEE"><TD>Fortunaty.net (Europe)</TD>
<TD>1.0_REL image</TD>
<TD><A HREF="http://ftp.fortunaty.net/DragonFly/iso-images/dfly-1.0REL.iso.gz">HTTP</A></TD></TR>

<!-- This mirror appears to have an incomplete copy
<TR><TD>univie.ac.at</TD>
<TD>1.0_REL image</TD>
<TD><A HREF="http://ftp.univie.ac.at/systems/DragonFly/iso-images/dfly-1.0REL.iso.gz">HTTP</A></TD></TR>
-->

<TR><TD>hp48.org</TD>
<TD>1.0A_REL image</TD>
<TD><A HREF="http://nibble.hp48.org/dragonfly/iso-images/dfly-1.0A_REL.iso.gz">HTTP</A></TD></TR>

<TR><TD>Starkast.net (Sweden)</TD>
<TD>1.0_REL image</TD>
<TD>
<A HREF="ftp://ftp.starkast.net/pub/DragonFly/iso-images/dfly-1.0REL.iso.gz">FTP</A>
<A HREF="http://ftp.starkast.net/pub/DragonFly/iso-images/dfly-1.0REL.iso.gz">HTTP</A></TD></TR>

<TR BGCOLOR="#EEEEEE"><TD>chlamydia.fs.ei.tum.de (Germany)</TD>
<TD>1.0_REL image</TD>
<TD><A HREF="http://chlamydia.fs.ei.tum.de/pub/DragonFly/iso-images/dfly-1.0REL.iso.gz">HTTP</A>, <A HREF="ftp://chlamydia.fs.ei.tum.de/pub/DragonFly/iso-images/dfly-1.0REL.iso.gz">FTP</A></TD></TR>

<TR BGCOLOR="#EEEEEE"><TD>BSDTech.com (Norway)</TD>
<TD>1.0A_REL image</TD>
<TD>
<A HREF="http://dragon.bsdtech.com/DragonFly/iso-images/dfly-1.0A_REL.iso.gz">HTTP</A>, 
<A HREF="ftp://dragon.bsdtech.com/DragonFly/iso-images/dfly-1.0A_REL.iso.gz">FTP</A></TD></TR>

<TR BGCOLOR="#EEEEEE"><TD>AllBSD.org (Japan)</TD>
<TD>1.0_REL image</TD>
<TD><A HREF="http://pub.allbsd.org/DragonFly/iso-images/dfly-1.0REL.iso.gz">HTTP</A>, 
<A HREF="ftp://ftp.allbsd.org/pub/DragonFly/iso-images/dfly-1.0REL.iso.gz">FTP</A>, 
<A HREF="rsync://rsync.allbsd.org/dragonfly-iso-images/dfly-1.0REL.iso.gz">rysnc</A></TD></TR>

<TR><TD>Pieter from Holland (EU)</TD>
<TD>1.0A_REL image</TD>
<TD><A HREF="http://15pc221.sshunet.nl/DragonFly/iso-images/dfly-1.0A_REL.iso.gz">HTTP</A></TD></TR>

<TR><TD>RPI.edu (NY, USA)</TD>
<TD>1.0A_REL image</TD>
<TD><A HREF="http://www.acm.cs.rpi.edu/~tbw/dfly-1.0A_REL.iso.gz">HTTP</A></TD></TR>

<!--
<TR BGCOLOR="#EEEEEE"><TD>Dragonflybsd.org (California, USA)</TD>
<TD>1.0_REL image</TD>
<TD><A HREF="ftp://ftp.dragonflybsd.org/iso-images/dfly-1.0A_REL.iso.gz">FTP</A>
(<I>try to find another site first)</I></TD></TR>
-->

<TR><TD>EnergyHQ</TD>
<TD>1.0_REL image</TD>
<TD><A HREF="http://www.energyhq.es.eu.org/files/dfly-1.0REL.iso.gz.torrent">BitTorrent</A></TD></TR>

<!-- end of REL links -->
<TR>
<TD COLSPAN="3">&nbsp;</TD>
</TR>


<TR BGCOLOR="#EEEEEE">
<TD>DragonFlyBSD.org (California)</TD>
<TD>Code, ISO master site (<B>NOTE: Please use other sites to download the ISOs!</B>)</TD>
<TD><a href="ftp://ftp.dragonflybsd.org/">FTP</a>, 
<a href="http://www.dragonflybsd.org/cgi-bin/cvsweb.cgi">cvsweb</a>
</TD>
</TR>

<TR BGCOLOR="#EEEEEE">
<TD>chlamydia.fs.ei.tum.de (Germany)</TD>
<TD>Snapshots master site, official ISOs</TD>
<TD>
<a href="http://chlamydia.fs.ei.tum.de/pub/DragonFly/snapshots">HTTP</a>
<a href="ftp://chlamydia.fs.ei.tum.de/pub/DragonFly/snapshots">FTP</a>
</TD>
</TR>

<TR BGCOLOR="#EEEEEE">
<TD>AllBSD.org (Japan)</TD>
<TD>Daily snapshots, official ISOs, code</TD>
<TD><a href="http://pub.allbsd.org/DragonFly/snapshots/">HTTP</a>,
<a href="ftp://ftp.allbsd.org/pub/DragonFly/snapshots/">FTP</a>, 
rsync, cvsup, cvsync, cvsweb
</TD>
</TR>

<TR>
<TD>dragon.BSDTech.com (Norway)</TD>
<TD>Daily snapshots, official ISOs</TD>
<TD>
<a href="http://dragon.bsdtech.com/DragonFly/">HTTP</a>, 
<a href="ftp://dragon.bsdtech.com/DragonFly/">FTP</a>, 
cvsup</TD>
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

<TR BGCOLOR="#EEEEEE">
<TD>The University of Vienna (Austria)</TD>
<TD>Daily snapshots, official ISOs</TD>
<TD>
<a href="http://ftp.univie.ac.at/systems/DragonFly/">HTTP</a>, 
<a href="ftp://ftp.univie.ac.at/systems/DragonFly/">FTP</a>, and
<a href="rsync://ftp.univie.ac.at/DragonFly/">rsync</a>
</TD>
</TR>

<TR BGCOLOR="#EEEEEE">
<TD>University of Chicago (Illinois, USA)</TD>
<TD>official ISOs</TD>
<TD><a href="ftp://cvsup.math.uic.edu/dragonflybsd/">FTP</a></TD>
</TR>

<TR BGCOLOR="#EEEEEE">
<TD>grappa.unix-ag.uni-kl.de (Germany)</TD>
<TD>Code</TD>
<TD>
<a href="http://grappa.unix-ag.uni-kl.de/cgi-bin/cvsweb/?cvsroot=dragonfly">cvsweb</a>, 
cvsup, cvsync, rsync, anoncvs
</TD>
</TR>

<TR BGCOLOR="#EEEEEE">
<TD>dragonfly.the-bofh.org (Holland)</TD>
<TD>Code</TD>
<TD>
<a href="http://dragonfly.the-bofh.org/">HTTP</a>, 
<a href="ftp://dragonfly.the-bofh.org/">FTP</a>, 
cvsup, <a href="http://dragonfly.the-bofh.org/cgi-bin/cvsweb.cgi/">cvsweb</a>
</TD>
</TR>

<TR BGCOLOR="#EEEEEE">
<TD>Starkast.net (Sweden)</TD>
<TD>Code</TD>
<TD>
<a href="http://ftp.starkast.net/pub/DragonFly/">HTTP</a>, 
<a href="ftp://ftp.starkast.net/pub/DragonFly/">FTP</a>
</TD>
</TR>
 
</TABLE>

<h2>Commerce Sites</h2>

<TABLE BORDER="0">
<TR>
<TH>Organization</TH>
<TH>Access methods</TH>
</TR>

<TR>
<TD COLSPAN="2">Any commercial site selling DragonFly related material can be listed in
this section.</TD>
</TR>

<TR BGCOLOR="#EEEEEE">
<TD><B>Crescent Anchor.</B>
Crescent Anchor will be providing DragonFly BSD 1.0 on CD-ROM for a
nominal fee.  Part of your purchase from Crescent Anchor will be contributed
back to support continuing DragonFly development via the forthcoming 
nonprofit
company.  Please visit the Crescent Anchor site for details, pricing, 
and other
offers</TD>
<TD><A HREF="http://www.crescentanchor.com/">http://www.crescentanchor.com/</A>
</TR>

</TABLE>
