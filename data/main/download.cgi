#!/usr/local/www/cgi-bin/tablecg
#
# $DragonFly: site/data/main/Attic/download.cgi,v 1.10 2003/12/14 18:35:16 dillon Exp $

$TITLE(DragonFly - Download)
<P>
You can download a live-boot ISO for intel-compatible architectures (Intel,
AMD, etc) via FTP at the address given below.  This should work with most
machines with IDE CDRom drives but keep in mind that DragonFly is considered
experimental.  This CD will boot into a full DragonFly base system and give
you a login.  Just login as 'root' (no password) and start playing.  Note
that the ISO contains two pre-installed ports, mkisofs, and cvsup, and a
full set of modules, so if you can boot it you should have enough to load
up your hard disk.  This route requires some BSD experience as you need to
use fdisk, boot0cfg, disklabel (don't forget to install bootblocks), newfs,
and mount, and then at the very least 'cpdup' the contents of the CDRom 
into your HD.  Be sure to remove /boot/loader.conf and fixup /etc/fstab
on the HD before trying to reboot through the HD.  The gzip'd ISO is in
the 60-100MB range.
<UL>
    <P><A HREF="ftp://ftp.dragonflybsd.org/iso-images/dfly-20031202.iso.gz">ftp://ftp.dragonflybsd.org/iso-images/dfly-20031202.iso.gz</A>    
</UL>
<P>
The preferred method for obtaining the DragonFly codebase is to get the CVS
repository via cvsup.  If you download the
<A HREF="dragonfly-cvs-supfile">dragonfly-cvs-supfile</A> control file and
run cvsup on it, the repository will be downloaded into the directory
<B>/home/dcvs</B>.  For example:
<UL>
    <P>mkdir /home/dcvs
    <BR>cvsup -g -L 2 dragonfly-cvs-supfile
</UL>
<P>
If you want just the cvs sys hierarchy you can use this supfile:
<A HREF="dragonfly-cvs-sys-supfile">dragonfly-cvs-sys-supfile</A>.
<P>
It is recommended that you run cvsup from once a day to once a week to keep
the cvs tree up to date.  Please do not run cvsup on an automatic basis more
then once a day.  Once downloaded, you can check out the sourcebase using cvs.
If you are already using /usr/src to hold FreeBSD sources you should check
the sourcebase out into a different directory.  Once you have done the
initial checkout you can update the source tree using 'cvs update' from
within the tree.  For example:
<UL>
    <P>cd /usr
    <BR>cvs -d /home/dcvs checkout -d dragonsrc src
</UL>
<P>
You can download portions of the source and/or CVS tree via FTP.  This
facility exists primarily to support casual browsing and is updated
once a day.  Please use the <B>cvsup</B> method above for downloading the
CVS tree.
<UL>
    <P><A HREF="ftp://ftp.dragonflybsd.org/">ftp://ftp.dragonflybsd.org/</A>    
</UL>
<P>
You can browse the CVS archive via cvsweb:
<UL>
    <P><A HREF="http://www.dragonflybsd.org/cgi-bin/cvsweb.cgi">http://www.dragonflybsd.org/cgi-bin/cvsweb.cgi</A>    
</UL>
<P>
<CENTER><H2>News Sites</H2></CENTER>
<P>
Justin Sherrill has set up an ongoing log of DragonFly events.
<UL>
    <P><A HREF="http://www.shiningsilence.com/dbsdlog/">http://www.shiningsilence.com/dbsdlog/</A>
</UL>
<CENTER><H2>Mirror Sites</H2></CENTER>
<P>
The mirror sites are ad-hoc at the moment, but we will formalize them in 
coming weeks.
<UL>
    <P>
    <TABLE BORDER=1>
    <TR>
	<TD>Germany</TD>
	<TD><A HREF="http://grappa.unix-ag.uni-kl.de/cgi-bin/cvsweb/?cvsroot=dragonfly">http://grappa.unix-ag.uni-kl.de/cgi-bin/cvsweb/?cvsroot=dragonfly</A></TD>
	<TD>cvsup, cvsync, rsync, cvsweb, anoncvs</TD>
    </TR>
    <TR>
	<TD>Japan</TD>
	<TD><A HREF="http://www.allbsd.org/">http://www.allbsd.org</A>
	<TD>cvsup, cvsync</TD>
    </TR>
    </TABLE>
</UL>
<CENTER><H2>DAILY SNAPSHOTS</H2></CENTER>
<P>
For people who do not want to go through the repetetive stages of installing
FreeBSD 4.8, doing a DragonFly cvsup etc; well now you have a choice!
</P>
<P>
Simon 'corecode' Schubert (corecode at fs.ei.tum.de) has
offered to provide <B>Daily Snapshots</B> of DragonFly.  The snapshots are
available on FTP and HTTP.  <B>PLEASE NOTE</B>, due to the experimental
nature of this server, it incur some downtime, so please be patient and
do try again in some minutes or hours.
</P>
<P>Download Details:</P>
<UL>
	<LI>FTP: <A HREF="ftp://chlamydia.fs.ei.tum.de/pub/DragonFly/snapshots">ftp://chlamydia.fs.ei.tum.de/pub/DragonFly/snapshots</A>
	<LI>HTTP: <A HREF="http://chlamydia.fs.ei.tum.de/pub/DragonFly/snapshots">http://chlamydia.fs.ei.tum.de/pub/DragonFly/snapshots</A>
</UL>
