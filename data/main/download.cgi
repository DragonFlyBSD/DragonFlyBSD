#!/usr/local/www/cgi-bin/tablecg
#
# $DragonFly: site/data/main/Attic/download.cgi,v 1.25 2004/05/04 17:24:43 justin Exp $

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
<a href="/cgi-bin/cvsweb.cgi/~checkout~/src/nrelease/root/README?rev=1.12">README</a> file, as 
this is not an automated installation.</p>

<p>
The 
most recent <a href="ftp://ftp.dragonflybsd.org/iso-images/dfly-20040422.iso.gz">'known good' image</a>
(2004-04-22) is available.
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
The University of Vienna (Germany) offers mirrors via  
<a href="ftp://ftp.univie.ac.at/systems/DragonFly/">FTP</a>,
<a href="http://ftp.univie.ac.at/systems/DragonFly/">HTTP</a>, and
<a href="rsync://ftp.univie.ac.at/DragonFly/</a>.</p>
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

<h2>Mirror Sites</h2>

<p>
Germany - cvsup, cvsync, rsync, cvsweb, anoncvs <br/>
&nbsp;&nbsp;&nbsp;&nbsp;<a href="http://grappa.unix-ag.uni-kl.de/cgi-bin/cvsweb/?cvsroot=dragonfly">http://grappa.unix-ag.uni-kl.de/cgi-bin/cvsweb/?cvsroot=dragonfly</a></p>
<p>	
Japan - rsync, cvsup, cvsync, cvsweb <br/>
&nbsp;&nbsp;&nbsp;&nbsp;<a href="http://www.allbsd.org/">http://www.allbsd.org</a>
</p>

<h2>News Sites</h2>
<p>
Justin Sherrill has set up an ongoing log of DragonFly events.
&nbsp;&nbsp;&nbsp;&nbsp;<a href="http://www.shiningsilence.com/dbsdlog/">http://www.shiningsilence.com/dbsdlog/</a></p>
