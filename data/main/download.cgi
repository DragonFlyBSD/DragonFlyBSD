#!/usr/local/www/cgi-bin/tablecg
#
# $DragonFly: site/data/main/Attic/download.cgi,v 1.34 2004/07/07 16:42:52 dillon Exp $

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
this is not an automated installation.</p>

<p>
<B>The DragonFly 1.0-RC2 SNAP is now Ready!</B> You can download the release
candidate via: <a href="ftp://ftp.dragonflybsd.org/iso-images/dfly-1.0RC2.iso.gz">dfly-1.0RC2.iso.gz</a>
(2004-07-04) is available.
</p>
<!--<p>The <b>RC1 SNAP</b> is mirrored at:
<a href="http://gobsd.com/dfly-1.0RC1.iso.gz">http://gobsd.com/dfly-1.0RC1.iso.gz</a></p> -->
<p><a href="./MD5SUM">MD5 checksums</a> for the RC2 snapshot.</p>
<p>
RC2 Mirrors may be found here: <a href="mirrors.html">mirrors.html</a>
</p>
<p>
If you are looking for the most recent version possible,
daily snapshots are available from Simon 'corecode' Schubert via
<a href="ftp://chlamydia.fs.ei.tum.de/pub/DragonFly/snapshots">FTP</a>
and <a href="http://chlamydia.fs.ei.tum.de/pub/DragonFly/snapshots">HTTP</a>.
</p>
<p>
Esat.net (UK) is mirroring these snapshots via IPv4 and IPv6 on
<a href="ftp://ftp.esat.net/mirrors/chlamydia.fs.ei.tum.de/pub/DragonFly/snapshots/">FTP</a>, 
<a href="http://ftp.esat.net/mirrors/chlamydia.fs.ei.tum.de/pub/DragonFly/snapshots/">HTTP</a>, and 
<a href="rsync://ftp.esat.net/mirrors/chlamydia.fs.ei.tum.de/pub/DragonFly/snapshots/">rysnc</a>.</p>
<p>
AllBSD.org (Japan) also mirrors these images via 
<a href="http://pub.allbsd.org/DragonFly/snapshots/">HTTP</a> and 
<a href="ftp://ftp.allbsd.org/pub/DragonFly/snapshots/">FTP</a>.</p>
<p>
Fortunaty.net (Europe) offers mirrors via 
<a href="http://ftp.fortunaty.net/">HTTP</a> and
<a href="ftp://ftp.fortunaty.net/">FTP</a>.</p>
<p>
The University of Vienna (Austria) offers mirrors via  
<a href="ftp://ftp.univie.ac.at/systems/DragonFly/">FTP</a>,
<a href="http://ftp.univie.ac.at/systems/DragonFly/">HTTP</a>, and
<a href="rsync://ftp.univie.ac.at/DragonFly/">rsync</a>.</p>
<p>
Vladimir Egorin at UIC.EDU mirrors the stable ISO CDs via
<a href="ftp://cvsup.math.uic.edu/dragonflybsd/">FTP</a>.</p>

<h2>Obtaining source via CVS</h2>

<p>
If you prefer to obtain the source via cvsup, you can use 
<a href="dragonfly-cvs-supfile">this cvsup config file</a> to 
pull the source into /home/dcvs. 
A <a href="dragonfly-cvs-sys-supfile">sys hierarchy only</a> cvsup 
file is available.</p>
<p>
Please do not pull down source via cvsup more than once a day.  Cvsup is the preferred 
retrieval method, though you can use 
<a href="ftp://ftp.dragonflybsd.org/">FTP</a>, or browse 
<a href="http://www.dragonflybsd.org/cgi-bin/cvsweb.cgi">via cvsweb</a>.
</p>

<h2>Package Downloads</h2>
GoBSD.com is providing access to thousands of pre-built DragonFly software
packages.<br/>
&nbsp;&nbsp;&nbsp;&nbsp;<a href="http://gobsd.com/packages">http://gobsd.com/packages</a>

<h2>Mirror Sites</h2>

<p>
Germany - cvsup, cvsync, rsync, cvsweb, anoncvs <br/>
&nbsp;&nbsp;&nbsp;&nbsp;<a href="http://grappa.unix-ag.uni-kl.de/cgi-bin/cvsweb/?cvsroot=dragonfly">http://grappa.unix-ag.uni-kl.de/cgi-bin/cvsweb/?cvsroot=dragonfly</a></p>
<p>	
Japan - rsync, cvsup, cvsync, cvsweb <br/>
&nbsp;&nbsp;&nbsp;&nbsp;<a href="http://www.allbsd.org/">http://www.allbsd.org</a>
</p>

<h2>Other Sites</h2>
<p>
GoBSD.com is a BSD-centric community website.
&nbsp;&nbsp;&nbsp;&nbsp;<a href="http://gobsd.com">http://gobsd.com</a></p>
<p>
Justin Sherrill has set up an ongoing log of DragonFly events.
&nbsp;&nbsp;&nbsp;&nbsp;<a href="http://www.shiningsilence.com/dbsdlog/">http://www.shiningsilence.com/dbsdlog/</a></p>
