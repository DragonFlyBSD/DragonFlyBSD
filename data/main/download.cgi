#!/usr/local/www/cgi-bin/tablecg
#
# $DragonFly: site/data/main/Attic/download.cgi,v 1.39 2004/07/12 17:41:25 dillon Exp $

$TITLE(DragonFly - Download)
<h1>Obtaining DragonFly for your system</h1>
<p>
There are several ways to install DragonFly on your computer.  
Some of these methods require prior BSD experience with tools like 
disklabel or boot0cfg, and we have no completed installation 
program at this point.</p>

<h2>CD Images</h2>

<p>
DragonFly CDs are 'live', meaning these CDs will boot your system 
and let you log in as root (no password) and install to your IDE disk-based 
system.  Make sure you read the 
<a href="/cgi-bin/cvsweb.cgi/~checkout~/src/nrelease/root/README">README</a> file, as 
this is not an automated installation.  See the 'Download Site' list below 
for a list of download locations. </p>

<p>
<b>The DragonFly 1.0-RELEASE is here!  See below for download sites!  The MD5
for the release is:<br />MD5 (dfly-1.0REL.iso.gz) = c95a378c13257f39420f5f9e4104bd7b
</b><br /> 

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
<TD>REL image</TD>
<TD><A HREF="http://gobsd.com/dfly-1.0REL.iso.gz">HTTP</A></TD></TR>

<!-- <TR BGCOLOR="#EEEEEE"><TD>The-BOFH.org (Holland)</TD>
<TD>REL image</TD>
<TD><A HREF="http://www.the-bofh.org/dfly-1.0REL.iso.gz">HTTP</A></TD></TR> 
-->

<!--
<TR BGCOLOR="#EEEEEE"><TD>Sitetronics.com</TD>
<TD>REL image</TD>
<TD><A HREF="http://freebsd0.sitetronics.com/~dodell/dfly-1.0REL.iso.gz">HTTP</A></TD></TR>
-->

<TR BGCOLOR="#EEEEEE"><TD>Fortunaty.net (Europe)</TD>
<TD>REL image</TD>
<TD><A HREF="http://ftp.fortunaty.net/DragonFly/iso-images/dfly-1.0REL.iso.gz">HTTP</A></TD></TR>

<!-- This mirror appears to have an incomplete copy
<TR><TD>univie.ac.at</TD>
<TD>REL image</TD>
<TD><A HREF="http://ftp.univie.ac.at/systems/DragonFly/iso-images/dfly-1.0REL.iso.gz">HTTP</A></TD></TR>
-->

<TR><TD>hp48.org</TD>
<TD>REL image</TD>
<TD><A HREF="http://nibble.hp48.org/dragonfly/iso-images/dfly-1.0REL.iso.gz">HTTP</A></TD></TR>

<TR><TD>Starkast.net</TD>
<TD>REL image</TD>
<TD>
<A HREF="ftp://ftp.starkast.net/pub/DragonFly/iso-images/dfly-1.0REL.iso.gz">FTP</A>
<A HREF="http://ftp.starkast.net/pub/DragonFly/iso-images/dfly-1.0REL.iso.gz">HTTP</A></TD></TR>

<TR BGCOLOR="#EEEEEE"><TD>chlamydia.fs.ei.tum.de (Germany)</TD>
<TD>REL image</TD>
<TD><A HREF="http://chlamydia.fs.ei.tum.de/pub/DragonFly/iso-images/dfly-1.0REL.iso.gz">HTTP</A>, <A HREF="ftp://chlamydia.fs.ei.tum.de/pub/DragonFly/iso-images/dfly-1.0REL.iso.gz">FTP</A></TD></TR>

<TR BGCOLOR="#EEEEEE"><TD>BSDTech.com (Norway)</TD>
<TD>REL image</TD>
<TD>
<A HREF="http://dragon.bsdtech.com/DragonFly/iso-images/dfly-1.0REL.iso.gz">HTTP</A>, 
<A HREF="ftp://dragon.bsdtech.com/DragonFly/iso-images/dfly-1.0REL.iso.gz">FTP</A></TD></TR>

<TR BGCOLOR="#EEEEEE"><TD>AllBSD.org (Japan)</TD>
<TD>REL image</TD>
<TD><A HREF="http://pub.allbsd.org/DragonFly/iso-images/dfly-1.0REL.iso.gz">HTTP</A>, 
<A HREF="ftp://ftp.allbsd.org/pub/DragonFly/iso-images/dfly-1.0REL.iso.gz">FTP</A>, 
<A HREF="rsync://rsync.allbsd.org/dragonfly-iso-images/dfly-1.0REL.iso.gz">rysnc</A></TD></TR>

<TR><TD>Pieter from Holland</TD>
<TD>REL image</TD>
<TD><A HREF="http://15pc221.sshunet.nl/DragonFly/iso-images/dfly-1.0REL.iso.gz">HTTP</A></TD></TR>

<TR BGCOLOR="#EEEEEE"><TD>Dragonflybsd.org (California, USA)</TD>
<TD>REL image</TD>
<TD><A HREF="ftp://ftp.dragonflybsd.org/iso-images/dfly-1.0REL.iso.gz">FTP</A>
(<I>try to find another site first)</I></TD></TR>

<TR><TD>EnergyHQ</TD>
<TD>REL image</TD>
<TD><A HREF="http://www.energyhq.es.eu.org/files/dfly-1.0REL.iso.gz.torrent">BitTorrent</A></TD></TR>

<!-- end of REL links -->
<TR>
<TD COLSPAN="3">&nbsp;</TD>
</TR>


<TR BGCOLOR="#EEEEEE">
<TD>DragonFlyBSD.org (California)</TD>
<TD>Code, ISO master site</TD>
<TD><a href="ftp://ftp.dragonflybsd.org/">FTP</a>, 
<a href="http://www.dragonflybsd.org/cgi-bin/cvsweb.cgi">cvsweb</a> (preferred)
</TD>
</TR>

<TR BGCOLOR="#EEEEEE">
<TD>chlamydia.fs.ei.tum.de (Germany)</TD>
<TD>Snapshots master site, official ISOs</TD>
<TD>
<a href="http://chlamydia.fs.ei.tum.de/pub/DragonFly/snapshots">HTTP</a>
<a href="ftp://chlamydia.fs.ei.tum.de/pub/DragonFly/snapshots">FTP</a>,
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
<a href="http://ftp.esat.net/mirrors/chlamydia.fs.ei.tum.de/pub/DragonFly/snapshots/">HTTP</a>, 
<a href="ftp://ftp.esat.net/mirrors/chlamydia.fs.ei.tum.de/pub/DragonFly/snapshots/">FTP</a>, and 
<a href="rsync://ftp.esat.net/mirrors/chlamydia.fs.ei.tum.de/pub/DragonFly/snapshots/">rysnc</a>. (IPv4 and IPv6)
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

</TABLE>

<h2>Commerce Sites</h2>

<TABLE BORDER="0">
<TR>
<TH>Organization</TH>
<TH>Access methods</TH>
</TR>

<TR>
<TD COLSPAN="2">NEW SECTION!  Any commercial site selling DragonFly related material can be listed in
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
