#!/usr/local/www/cgi-bin/tablecg
#
# $DragonFly: site/data/main/Attic/release1_2.cgi,v 1.6 2005/08/01 07:12:57 dillon Exp $

$TITLE(DragonFly - April 2005 Release 1.2.x Download)
<h1>Obtaining DragonFly 1.2.x for your system</h1>

<h2>1.2.0 ISO Images for CDs</h2>

<p>
DragonFly CDs are 'live', which means that the CD will boot your system 
and let you log in as root (no password).  You can use this feature to
check for hardware compatibility and play with DragonFly a little before
actually installing it on your hard drive. 
</p>
<p>
The CD includes an installer that can be run at the console, or
(experimentally) via a web browser.  Make sure you read the
<a href="/cgi-bin/cvsweb.cgi/~checkout~/src/nrelease/root/README">README</a>
file for more information.   To activate the installer, boot the CD and
login as 'installer'.
</p>
<p>
As of this release the installer has a Netboot server option.  You can
boot the CD on one machine, enable the feature via the installer, and
then PXEBoot other systems and do a network based install.  System
operators should be aware that running the netboot server hardwires
a 10.1.0.X network and runs a DHCP server which might interfere with
other DHCP servers on your LAN.
</p>
<p>
See the 'Download Site' list below for a list of download locations. 
</p>

<p>
<b>The MD5 for the release is:
<br />MD5 (dfly-1.2.0_REL.iso.gz) = e0faf2e1dd31763cad5eda2dd8e56ef8

</b><br /> </p>

<h2>1.2.x Release Errata</h2>
<p>
    DragonFly releases are meant to be stable, dependable entities.  We
    backport compatible bug fixes from current development into release
    branches but we do not generally backport new features. 
    The release CD is always a '.0', e.g. 1.2.0.   The most common way to
    track a release is to use cvsup to track the release sources and then
    build and install a new world and kernel to keep your system updated.
</p>
<TABLE BORDER="1">
<TR>
<TH>Version</TH>
<TH>Date</TH>
<TH>Comments</TH>
</TR>
<TR>
<TD>1.2.0</TD>
<TD>08-Apr-2005</TD>
<TD>RELEASE CD</TD>
</TR>
<TR>
<TD>1.2.1</TD>
<TD>22-Apr-2005</TD>
<TD>(unspecified)</TD>
</TR>
<TR>
<TD>1.2.2</TD>
<TD>09-May-2005</TD>
<TD>A kernel TLS support bug was fixed.</TD>
</TR>
<TR>
<TD>1.2.3</TD>
<TD>29-Jun-2005</TD>
<TD>Compiler, bootstrap, security, fork/^Z race, nfile limit, token,
    and packet filter fixes.</TD>
</TR>
<TR>
<TD>1.2.4</TD>
<TD>07-Jul-2005</TD>
<TD>Wildcard expansion bug in /bin/sh, bzip2 security issue, 
    core dumping to NFS mounts, updated leapseconds, bridging fixes.</TD>
</TR>
<TR>
<TD>1.2.5</TD>
<TD>31-Jul-2005</TD>
<TD>Numerous critical SMP fixes: critical section and spl code, 
    IPI messaging, LWKT tokens, and a ktrace fix.</TD>
</TR>
</TABLE>

<h2>1.2.x Release Sites</h2>

<TABLE BORDER="1">
<TR>
<TH>Organization</TH>
<TH>Mirrored Data</TH>
<TH>Access methods</TH>
</TR>

<TR><TD>GoBSD.COM (USA)</TD>
<TD>1.2.0_REL image</TD>
<TD>
    <A HREF="http://gobsd.com/dfly-1.2.0_REL.iso.gz">HTTP</A>
    <A HREF="ftp://gobsd.com/dfly-1.2.0_REL.iso.gz">FTP</A>
</TD></TR>

<TR><TD>Fortunaty.net</TD>
<TD>1.2.0_REL image</TD>
<TD><A HREF="http://ftp.fortunaty.net/DragonFly/iso-images/dfly-1.2.0_REL.iso.gz">HTTP</A></TD></TR>

<TR><TD>PFSense.com</TD>
<TD>1.2.0_REL image</TD>
<TD><A HREF="http://www.pfsense.com/dfly/dfly-1.2.0_REL.iso.gz">HTTP</A></TD></TR>

<TR><TD>Chlamydia.fs.ei.tum.de (Germany)</TD>
<TD>1.2.0_REL image</TD>
<TD>
    <A HREF="http://chlamydia.fs.ei.tum.de/pub/DragonFly/iso-images/dfly-1.2.0_REL.iso.gz">HTTP</A>
    <A HREF="ftp://chlamydia.fs.ei.tum.de/pub/DragonFly/iso-images/dfly-1.2.0_REL.iso.gz">FTP</A>
</TD>
</TR>

<TR><TD>Starkast.net (Sweden)</TD>
<TD>1.2.0_REL image</TD>
<TD>
    <A HREF="http://ftp.starkast.net/pub/DragonFly/iso-images/dfly-1.2.0_REL.iso.gz">HTTP</A>
    <A HREF="ftp://ftp.starkast.net/pub/DragonFly/iso-images/dfly-1.2.0_REL.iso.gz">FTP</A>
</TD></TR>

<TR><TD>BGP4.net (USA)</TD>
<TD>1.2.0_REL image</TD>
<TD>
    <A HREF="http://mirror.bgp4.net/pub/DragonFly/iso-images/dfly-1.2.0_REL.iso.gz">HTTP</A>
    <A HREF="ftp://mirror.bgp4.net/pub/DragonFly/iso-images/dfly-1.2.0_REL.iso.gz">FTP</A>
</TD></TR>

<TR><TD>TheShell.com</TD>
<TD>1.2.0_REL image</TD>
<TD>
    <A HREF="http://www.theshell.com/pub/DragonFly/iso-images/dfly-1.2.0_REL.iso.gz">HTTP</A>
    <A HREF="ftp://ftp.theshell.com/pub/DragonFly/iso-images/dfly-1.2.0_REL.iso.gz">FTP</A>
</TD></TR>

<TR><TD>FictionPress.com (USA)</TD>
<TD>1.2.0_REL image</TD>
<TD>
    <A HREF="http://mirror.fictionpress.com/df/dfly-1.2.0_REL.iso.gz">HTTP</A>
</TD></TR>

<TR><TD>SourceForge (USA)</TD>
<TD>1.2.0_REL image</TD>
<TD>
    <A HREF="http://dragonflybsd.sf.net/dfly-1.2.0_REL.iso.gz">HTTP</A>
</TD></TR>

<TR><TD>Hup.hu (Hungary)</TD>
<TD>1.2.0_REL image</TD>
<TD>
    <A HREF="http://www.hup.hu/~trey/DragonFly/iso-images/dfly-1.2.0_REL.iso.gz">HTTP</A>
</TD></TR>

<TR><TD>Bit Torrent</TD>
<TD>1.2.0_REL image</TD>
<TD>
    <A HREF="http://torrent.bitslush.org/dfly-1.2.0_REL.iso.gz.torrent">TORRENT</A>
</TD></TR>

<TR><TD>Dragonflybsd.org (USA)</TD>
<TD>1.2.0_REL image</TD>
<TD><A HREF="ftp://ftp.dragonflybsd.org/iso-images/dfly-1.2.0_REL.iso.gz">FTP</A>
(<I>try to find another site first</I>)</TD></TR>

</TABLE>

<h1>Release Notes for DragonFly 1.2.0</h1>

<p>
1.2.x is our second major DragonFly release and the first one which
we have created a separate CVS branch for.  DragonFly's policy is to
only commit bug fixes to release branches.
</p>

<p>
This release represents a significant milestone in our efforts to
improve the kernel infrastructure.  DragonFly is still running under
the Big Giant Lock, but this will probably be the last release where
that is the case.
</p>
<p>
The greatest progress has been made in the
network subsystem.  The TCP stack is now almost fully threaded (and
will likely be the first subsystem we remove the BGL from in coming
months).  The TCP stack now fully supports the SACK protocol and a
large number of bug and performance fixes have gone in, especially
in regard to GigE performance over LANs.
</p>
<p>
The namecache has been completely rewritten and is now considered 
to be production-ready with this release.  The rewrite will greatly
simplify future filesystem work and is a necessary precursor for our
ultimate goal of creating a clusterable OS.
</p>
<p>
This will be last release that uses GCC 2.95.x as the default compiler.
Both GCC 3.4.x and GCC 2.95.x are supported in this release through the
use of the CCVER environment variable ('gcc2' or 'gcc34').  GCC 2.95.x is to
be retired soon due to its lack of TLS support.  The current development
branch will soon start depending heavily on TLS support and __thread both
within the kernel and in libc and other libraries.  This release fully
supports TLS segments for programs compiled with gcc-3.4.x.
</p>
<p>
It goes without saying that this release is far more stable then our 1.0A
release.  A huge number of bug fixes, performance improvements, and
design changes have been made since the 1.0A release.
</p>
<ul>
    <li>TCP SACK in, tested, and on by default.
    <li>TCP Performance tuning (header prediction now works properly,
	ACK aggregation when operating at GiGE speeds, fewer pure window
	update packets).
    <li>Major network protocol stack threading and other infrastructure work.
    <li>ALTQ and PF (Packet Filter).
    <li>TLS (Thread Local Storage) support.
    <li>DCONS support (console over firewire).
    <li>IPv6 improvements.
    <li>Namecache infrastructure rewritten.
    <li>Improved checkpointing support.
    <li>NFSv3 greatly improved.
    <li>Kernel callout_*() infrastructure revamped.
    <li>A lot of USB fixes.
    <li>VESA console and X support.
    <li>A large number of general maintainance items, such as driver updates,
	bug fixes, and so forth.
    <li>Upgraded installer includes an option to turn it into a standalone
	netboot server for mass installs, bug fixes, and other new features.
    <li>The RELEASE is now branched in the cvs repository.
</ul>
<p>
Other minor or incremental improvements:
</p>
<ul>
    <li>Better boot-time diagnostics.
    <li>Improved UP and SMP scheduler.
    <li>The Minix MINED editor is now a standard part of /bin, intended for
	use as an emergency editor in single-user mode.
    <li>GDB-6 now the default.
    <li>BIND-9 now the default.
    <li>OpenSSH updated to 3.9p1
    <li>ncurses updated to 5.4
    <li>dhcp udpated to 3.0
    <li>CVS 1.12.11 base with additional FreeBSD and DragonFly hacks
</ul>
