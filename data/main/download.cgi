#!/usr/local/www/cgi-bin/tablecg
#
# $DragonFly: site/data/main/Attic/download.cgi,v 1.20 2004/03/05 18:30:17 dillon Exp $

$TITLE(DragonFly - Download)
<P>
<H1>Obtaining DragonFly for your system</H1>
There are several ways to install DragonFly on your computer.  
Some of these methods require prior BSD experience with tools like 
disklabel or boot0cfg, and we have no completed installation 
program at this point.

<H2>CD Images</H2>

DragonFly CDs are 'live', meaning these CDs will boot your system 
and let you log in as root (no password) and install to your IDE disk-based 
system.  Make sure you read the 
<A HREF="/cgi-bin/cvsweb.cgi/~checkout~/src/nrelease/root/README?rev=1.9">/README</A> file, as 
this is not an automated installation.
<P>

The 
most recent <A HREF="ftp://ftp.dragonflybsd.org/iso-images/dfly-20040305.iso.gz">'known good' image</A>
(2004-03-05) is available.   
If you are looking for the most recent version possible, 
daily snapshots are available from Simon 'corecode' Schubert via
<A HREF="ftp://chlamydia.fs.ei.tum.de/pub/DragonFly/snapshots">FTP</A>
and <A HREF="http://chlamydia.fs.ei.tum.de/pub/DragonFly/snapshots">HTTP</A>.
<P>
Esat.net (UK) is mirroring these snapshots via IPv4 and IPv6 on
<a href="ftp://ftp.esat.net/mirrors/chlamydia.fs.ei.tum.de/pub/DragonFly/snapshots/">FTP</a>, 
<a href="http://ftp.esat.net/mirrors/chlamydia.fs.ei.tum.de/pub/DragonFly/snapshots/">HTTP</a>, and 
<a href="rsync://ftp.esat.net/mirrors/chlamydia.fs.ei.tum.de/pub/DragonFly/snapshots/">rysnc</a>.  <BR>

AllBSD.org (Japan) also mirrors these images via 
<A HREF="http://pub.allbsd.org/DragonFly/snapshots/">HTTP</A> and 
<A HREF="ftp://ftp.allbsd.org/pub/DragonFly/snapshots/">FTP</A>. <BR>

Fortunaty.net (Europe) offers mirrors via 
<A HREF="http://ftp.fortunaty.net/">HTTP</A> and
<A HREF="ftp://ftp.fortunaty.net/">FTP</A>.

<H2>Obtaining source via CVS</H2>

<P>
If you prefer to obtain the source via cvsup, you can use 
<A HREF="dragonfly-cvs-supfile">this cvsup config file</A> to 
pull the source into /home/dcvs.  A 
<A HREF="dragonfly-cvs-sys-supfile">sys hierarchy only</A> cvsup 
file is available.
<P>
Please do not pull down source via cvsup more than once a day.  Cvsup is the preferred 
retrieval method, though you can use 
<A HREF="ftp://ftp.dragonflybsd.org/">FTP</A>, or browse 
<A HREF="http://www.dragonflybsd.org/cgi-bin/cvsweb.cgi">via cvsweb</A>.  

<H2>Mirror Sites</H2>

<P>
Germany - cvsup, cvsync, rsync, cvsweb, anoncvs<BR>
&nbsp;&nbsp;&nbsp;&nbsp;<A HREF="http://grappa.unix-ag.uni-kl.de/cgi-bin/cvsweb/?cvsroot=dragonfly">http://grappa.unix-ag.uni-kl.de/cgi-bin/cvsweb/?cvsroot=dragonfly</A><BR>
<BR>	
Japan - rsync, cvsup, cvsync, cvsweb<BR>
&nbsp;&nbsp;&nbsp;&nbsp;<A HREF="http://www.allbsd.org/">http://www.allbsd.org</A><BR>


<P>
<H2>News Sites</H2>
<P>
Justin Sherrill has set up an ongoing log of DragonFly events.
&nbsp;&nbsp;&nbsp;&nbsp;<A HREF="http://www.shiningsilence.com/dbsdlog/">http://www.shiningsilence.com/dbsdlog/</A>
