#!/usr/local/www/cgi-bin/tablecg
#
# $DragonFly: site/data/main/Attic/release1_0.cgi,v 1.1 2004/09/07 19:37:57 dillon Exp $

$TITLE(DragonFly - July 2004 Release 1.0A Download)
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

<p>
<b>The MD5s for the release is:
<br />MD5 (dfly-1.0REL.iso.gz) = c95a378c13257f39420f5f9e4104bd7b
<br />MD5 (dfly-1.0_REL-1.0A_REL.xdelta) = 6001980541a4a2b77505c1845f925d57
<br />MD5 (dfly-1.0A_REL.iso) = ddf5686f828b2ece0b4029ae3ed78c2a
<br />MD5 (dfly-1.0A_REL.iso.gz) = b1c8ded31133960fa58a7b10c300aabd

</b><br /> </p>
<p>NOTICE!  RELEASE UPDATED TO 1.0A TO FIX A SERIOUS FDISK/SLICE ISSUE
WITH THE INSTALLER.  An xdelta (/usr/ports/misc/xdelta) patch is available
for people who have downloaded the original 1.0REL iso, and a new ISO has
been propagated to our mirrors.  To apply the delta, unzip the 1.0REL ISO,
apply the delta, and then run md5 on the result to ensure that it matches
the 1.0A_REL (ungzipped) ISO.</p>
<p><b>NOTICE!  The 1.0A release is fairly old relative to more recent 
development, at this point it is recommended that you download a recent
live CD ISO image (see the main download page).</b></p>

<h2>1.0 Release Errata</h2>
<p>
<A HREF="http://www.bsdinstaller.org/errata.html">Installer Errata</A>
<br /><A HREF="errata.cgi">General Errata</A>
</p>
<p>
<b>IMPORTANT ERRATA ADDENDUM: Using the installer on a multi-slice disk
will improperly resize the target slice when it is not the last slice,
to be the same size as the last slice, leading to a corrupt disk!  
1.0A fixes the problem and is now online and there is an xdelta available at:
<A HREF="ftp://ftp.dragonflybsd.org/iso-images/dfly-1.0_REL-1.0A_REL.xdelta">dfly-1.0_REL-1.0A_REL.xdelta (master site)</A>.
The 1.0A ISO has been propagated to our mirrors.  If you have the original
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

<TR><TD CLASS="mirrorsection" COLSPAN="3">1.0A Release Sites</TD></TR>

<TR><TD>GoBSD.COM</TD>
<TD>1.0A_REL image</TD>
<TD><A HREF="http://gobsd.com/dfly-1.0A_REL.iso.gz">HTTP</A></TD></TR>

<TR><TD>Fortunaty.net (Europe)</TD>
<TD>1.0A_REL image</TD>
<TD><A HREF="http://ftp.fortunaty.net/DragonFly/iso-images/dfly-1.0A_REL.iso.gz">HTTP</A></TD></TR>

<TR><TD>univie.ac.at</TD>
<TD>1.0A_REL image</TD>
<TD><A HREF="http://ftp.univie.ac.at/systems/DragonFly/iso-images/dfly-1.0A_REL.iso.gz">HTTP</A></TD></TR>

<TR><TD>hp48.org</TD>
<TD>1.0A_REL image</TD>
<TD><A HREF="http://nibble.hp48.org/dragonfly/iso-images/dfly-1.0A_REL.iso.gz">HTTP</A></TD></TR>

<TR><TD>Starkast.net (Sweden)</TD>
<TD>1.0A_REL image</TD>
<TD>
<A HREF="ftp://ftp.starkast.net/pub/DragonFly/iso-images/dfly-1.0A_REL.iso.gz">FTP</A>
<A HREF="http://ftp.starkast.net/pub/DragonFly/iso-images/dfly-1.0A_REL.iso.gz">HTTP</A></TD></TR>

<TR><TD>chlamydia.fs.ei.tum.de (Germany)</TD>
<TD>1.0A_REL image</TD>
<TD><A HREF="http://chlamydia.fs.ei.tum.de/pub/DragonFly/iso-images/dfly-1.0A_REL.iso.gz">HTTP</A>, <A HREF="ftp://chlamydia.fs.ei.tum.de/pub/DragonFly/iso-images/dfly-1.0A_REL.iso.gz">FTP</A></TD></TR>

<TR><TD>BSDTech.com (Norway)</TD>
<TD>1.0A_REL image</TD>
<TD>
<A HREF="http://dragon.bsdtech.com/DragonFly/iso-images/dfly-1.0A_REL.iso.gz">HTTP</A>, 
<A HREF="ftp://dragon.bsdtech.com/DragonFly/iso-images/dfly-1.0A_REL.iso.gz">FTP</A></TD></TR>

<TR><TD>AllBSD.org (Japan)</TD>
<TD>1.0A_REL image</TD>
<TD><A HREF="http://pub.allbsd.org/DragonFly/iso-images/dfly-1.0A_REL.iso.gz">HTTP</A>, 
<A HREF="ftp://ftp.allbsd.org/pub/DragonFly/iso-images/dfly-1.0A_REL.iso.gz">FTP</A>, 
<A HREF="rsync://rsync.allbsd.org/dragonfly-iso-images/dfly-1.0A_REL.iso.gz">rysnc</A></TD></TR>

<TR><TD>Pieter from Holland (EU)</TD>
<TD>1.0A_REL image</TD>
<TD><A HREF="http://15pc221.sshunet.nl/DragonFly/iso-images/dfly-1.0A_REL.iso.gz">HTTP</A></TD></TR>

<TR><TD>RPI.edu (NY, USA)</TD>
<TD>1.0A_REL image</TD>
<TD><A HREF="http://www.acm.cs.rpi.edu/~tbw/dfly-1.0A_REL.iso.gz">HTTP</A></TD></TR>

<TR><TD>Dragonflybsd.org (California, USA)</TD>
<TD>1.0A_REL image</TD>
<TD><A HREF="ftp://ftp.dragonflybsd.org/iso-images/dfly-1.0A_REL.iso.gz">FTP</A>
(<I>try to find another site first)</I></TD></TR>

<!-- end of REL links -->
<TR>
<TD COLSPAN="3">&nbsp;</TD>
</TR>

</TABLE>

