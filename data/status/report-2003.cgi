#!/usr/local/www/cgi-bin/tablecg
#
# $DragonFly: site/data/status/Attic/report-2003.cgi,v 1.6 2004/02/16 05:16:42 dillon Exp $

$TITLE(DragonFly - Activity Summary)

<H1>DragonFly Year-End summary for 2003</H1>

<P>
<A HREF="http://www.dragonflybsd.org/">DragonFly BSD</a> was
<a href="http://lists.freebsd.org/pipermail/freebsd-stable/2003-July/002183.html">announced</A>
in July of this year, with Matt Dillon the originator and main weightlifter of this project.
Development has been proceeding at a good clip, with a first release expected in 2004.
</P>

<P>
Statistics: Here's some data on the amount of activity in the DragonFly source,
as of the end of December 2003.
</P>

<P>
<TABLE BORDER="1">
<TR><TH>Number of commits</TH><TH>Committer</TH></TR>
<TR><TD>48288</TD><TD>dillon</TD></TR>
<TR><TD>1212</TD><TD>rob</TD></TR>
<TR><TD>997</TD><TD>asmodai</TD></TR>
<TR><TD>913</TD><TD>eirikn</TD></TR>
<TR><TD>636</TD><TD>drhodus</TD></TR>
<TR><TD>390</TD><TD>hmp</TD></TR>
<TR><TD>149</TD><TD>daver</TD></TR>
<TR><TD>53</TD><TD>joerg</TD></TR>
<TR><TD>3</TD><TD>justin</TD></TR>
</TABLE>
</P>



<H2>ACPI</H2>
<BLOCKQUOTE>
	The newer <A HREF="http://www.acpi.info/">power system</A>, imported by David Rhodus.
	It is not on by default.
</BLOCKQUOTE>


<H2>AGP</H2>
<BLOCKQUOTE>
A number of improvements to AGP support from FreeBSD 5 were added by Craig Dooley.
</BLOCKQUOTE>

<H2>AMD64 port</H2>
<BLOCKQUOTE>
	(Matt Dillon, Hiten Pandya, David Rhodus)  AMD's new 64-bit processors are
	nearly supported, with FreeBSD-5's boot code brought in to DragonFly.
	GCC 3.3 support and some code changes are needed first.
	As a side effect of this work, some hooks for PAE support now exist.
	Matt Dillon and David Rhodus has AMD64 hardware to test on.  This
	work has included the merge of boot code from FreeBSD-5
	along with Peter Wemm's linker changes.  (See GCC3 listing)

</BLOCKQUOTE>

<H2>ATAng</H2>
<BLOCKQUOTE>
	ATAng is 'next generation' ATA disk access, which works more efficiently in
	a multithreaded environment.  David Rhodus is merging in newer ATA code
	from the FreeBSD-4x tree and is trying to "work through the abuse of
	M_NOWAIT inside the ATA raid card controllers".
</BLOCKQUOTE>

<H2>Bugzilla</H2>
<BLOCKQUOTE>
	Jeroen Ruigrok has set up a Bugzilla database, and imported a large number
	(over 6,000) of bug reports and feature requests from FreeBSD-4 that may
	still apply to DragonFly.  This database is not yet available.
</BLOCKQUOTE>

<H2>Checkpointing</H2>
<BLOCKQUOTE>
	(Kip Macy)  It is possible to 'freeze' (^E) a copy of a program in
	its current state, and then later 'thaw' (/usr/bin/checkpt) it, restoring the
	state of the program.  Among more exotic applications,
	long-running processes can be saved and restored across reboots.  This
	is mostly complete, and can be loaded with the checkpoint.ko module.
</BLOCKQUOTE>

<H2>Cleanup</H2>
<BLOCKQUOTE>
	Eirik Nygaard and Craig Dooley have been cleaning up the old K&amp;R-style functions
	into ANSI code, along with removal of the __P() macro use from the userland. Robert
	Garrett removed the __P() macro inside the kernel source.  Jeroen Ruigrok has been
	working on the removal of perl from the kernel build process.  David Rhodus and
	Hiten Pandya have been fixing other various long running problems inside the kernel.
	David Rhodus has updated Sendmail (8.12.10), BIND(8.3.7), and also updated other
	applications like OpenSSL and OpenSSH.
</BLOCKQUOTE>

<H2>GCC3</H2>
<BLOCKQUOTE>
	Craig Dooley has submitted many patches to enable DragonFly
	building under GCC3.  David Rhodus has GCC-3.3.3 working in a test
	tree at the moment, for AMD64 support.(In progress)
</BLOCKQUOTE>

<H2>dfports</H2>
<BLOCKQUOTE>
	Matt Dillon created a local override of the existing FreeBSD ports system, to allow
	port installation until a DragonFly-native solution is created.
</BLOCKQUOTE>


<H2>libcaps</H2>
<BLOCKQUOTE>
libcaps has been created.  This is a library that allows userland threading, similar to the existing LWKT.  This is being worked on by Galen Sampson and Matt Dillon.
</BLOCKQUOTE>


<H2>Live CD</H2>
<BLOCKQUOTE>
	DragonFly CDs can now be built as 'live', meaning that a computer can be booted using the CD as the boot disk, and then tools like cpdup can be used to install to hard disk.  This is not yet fully documented; i.e. you will have to run programs like disklabel manually.  /usr/src/nrelease contains the code, and also some instructions.
</BLOCKQUOTE>

<H2>LWKT</H2>
<BLOCKQUOTE>
	Added by Matt Dillon.  LWKT stands for Light-Weight Kernel Threads.  LWKT gives
	each CPU in the system its own scheduler, and threads are locked to that CPU.  This
	removes any need for mutexes to handle interrupts.  More information is available
	<A HREF="http://www.dragonflybsd.org/goals/threads.cgi">at the DragonFly website</A>.

	Galen Sampson has been working on a port to userland.
</BLOCKQUOTE>

<H2>Logging</H2>
<BLOCKQUOTE>
	Justin Sherrill has been keeping a running news report of DragonFly activity
	at <A HREF="http://www.shiningsilence.com/dbsdlog/">http://www.shiningsilence.com/dbsdlog/</A>.
</BLOCKQUOTE>

<H2>MPIPE</H2>
<BLOCKQUOTE>
	Quoted from the committ log: "This subsystem is used for 'pipelining' fixed-size
  allocations.  Pipelining is used to avoid lack-of-resource deadlocks by
  still allowing resource allocations to 'block' by guarenteeing that an
  already in-progress operation will soon free memory that will be immediately
  used to satisfy the blocked resource."  Added by Matt Dillon.
</BLOCKQUOTE>

<H2>Make release</H2>
<BLOCKQUOTE>
	Jeroen Ruigrok has been working on make release.  Since the size of the DragonFly
	kernel is in flux, it can exceed the available floppy space.
	(incomplete)
</BLOCKQUOTE>

<H2>Mirrors</H2>
<BLOCKQUOTE>
	Germany - Simon 'corecode' Schubert<BR>
	<a href="http://chlamydia.fs.ei.tum.de/">http://chlamydia.fs.ei.tum.de/</A><BR>
	Ireland - David Burke<BR>
	<a href="http://ftp.esat.net/mirrors/chlamydia.fs.ei.tum.de/pub/DragonFly/">http://ftp.esat.net/mirrors/chlamydia.fs.ei.tum.de/pub/DragonFly/</A><BR>
      	<a href="ftp://ftp.esat.net/mirrors/chlamydia.fs.ei.tum.de/pub/DragonFly/">ftp://ftp.esat.net/mirrors/chlamydia.fs.ei.tum.de/pub/DragonFly/</A><BR>
	Japan - Hiroki Sato<BR>
	<a href="http://pub.allbsd.org/DragonFly/snapshots/i386/">http://pub.allbsd.org/DragonFly/snapshots/i386/</A><BR>
	<a href="ftp://ftp.allbsd.org/pub/DragonFly/snapshots/i386/">ftp://ftp.allbsd.org/pub/DragonFly/snapshots/i386/</A><BR>
	<a href="rsync://rsync.allbsd.org/dragonfly-snapshots/">rsync://rsync.allbsd.org/dragonfly-snapshots/</A><BR>
</BLOCKQUOTE>

<H2>Namecache</H2>
<BLOCKQUOTE>
	Matt Dillon completed this work in September; a writeup of the two stages
	of the work are available
	<A HREF="http://www.shiningsilence.com/dbsdlog/archives/2003_09.html#000015">here</A>, and
	<A HREF="http://www.shiningsilence.com/dbsdlog/archives/2003_09.html#000034">here</A>.
</BLOCKQUOTE>

<H2>Network interface aliasing (if_xname)</H2>
<BLOCKQUOTE>
	Max Laier is bringing in code used in the other BSDs to allow
	if_xname, if_dname, and if_dunit.
</BLOCKQUOTE>

<H2>Network stack</H2>
<BLOCKQUOTE>
	Jeffrey Hsu has been moving the network stack to a multithreaded
	model; he also has been making networking match certain RFCs, such as
	<A HREF="http://netweb.usc.edu/pim/">Protocol Independent Multicast</a>.
</BLOCKQUOTE>

<H2>NEWCARD support</H2>
<BLOCKQUOTE>
	Joerg Sonnenberger has been working on NEWCARD (PCMCIA card support) from FreeBSD 5.
</BLOCKQUOTE>


<H2>NVIDIA binary video card driver</H2>
<BLOCKQUOTE>
	Emiel Kollof got this working in October; a dfports override is available.
</BLOCKQUOTE>

<H2>NFS</H2>
<BLOCKQUOTE>
	David Rhodus and Hiten Pandya have been working on speeding up NFS
	performance. It is now possible to max out a GigE connection via NFSv3
	TCP, on DragonFly.  More tuning to come.
</BLOCKQUOTE>

<H2>PFIL_HOOKS, pf, ALTQ, and CARP</H2>
<BLOCKQUOTE>
	Max Laier is working on bringing in OpenBSD's pf, along with
	<a href="http://www.csl.sony.co.jp/person/kjc/kjc/software.html">ALTQ</a> and
	<A HREF="http://kerneltrap.org/node/view/1021">CARP</A>.
</BLOCKQUOTE>

<H2>Prelinking</H2>
<BLOCKQUOTE>
Prelinking capability was added to DragonFly by Simon 'corecode'
Schubert, which allows faster loading of applications that use a large
number of dynamic libraries while running, like Qt/KDE.  It is not 
currently hooked into the
system or any port building process.
</BLOCKQUOTE>


<H2>RAID support at install</H2>
<BLOCKQUOTE>
	David Rhodus added the pst driver for Promise cards to the GENERIC kernel, so installation to RAID drives using a Promise controller should be possible.
</BLOCKQUOTE>

<H2>RCNG</H2>
<BLOCKQUOTE>
	RCNG is a new version of the rc configuration system, taken from NetBSD.  More information is available at the <A HREF="http://www.netbsd.org/Documentation/rc/">NetBSD site</a>.  Robert Garrett merged this in.  Matt Dillon updated RCNG. You can now check the status of, or start, stop, etc. different system services using appropriate single commands like rcstart, rcstop, rcrestart, and so on. varsym -sa will list service status.
</BLOCKQUOTE>

<H2>Sun Grid Engine</H2>
<BLOCKQUOTE>
	Joshua Coombs is working on the sge - more information at:
	<a href="http://gridengine.sunsource.net/">http://gridengine.sunsource.net/</a>.
</BLOCKQUOTE>

<H2>Slab allocator</H2>
<BLOCKQUOTE>
	Matt Dillon added this new kernel memory allocator, which does not require any
	mutexes or blocks for memory assignment.  More information on how a slab allocator work is
	at <a href="http://citeseer.nj.nec.com/bonwick94slab.html">Bonwick94</a>,
	<a href="http://www.usenix.org/event/usenix01/bonwick.html">Bonwick2001</a>,
	and <a href="http://www.linuxhq.com/kernel/file/mm/slab.c">linux/mm/slab.c</a>
	in the Linux kernel.  This project is complete.
</BLOCKQUOTE>

<H2>Stack Protection</H2>
<BLOCKQUOTE>
Ryan Dooley patched libc giving stack-smashing protection.
</BLOCKQUOTE>

<H2>Syscall separation</H2>
<BLOCKQUOTE>
	David P. Reese has been separating system calls into kernel and userland versions.
	He has also removed stackgap allocations for Linux.
</BLOCKQUOTE>

<H2>Tinderbox</H2>
<BLOCKQUOTE>
	David Rhodus (hardware) and Hiten Pandya (setup) has set up
	repeated builds of the DragonFly system; failures
	are sent to the bugs@dragonflybsd.org mailing list.
</BLOCKQUOTE>

<H2>Variant Symlinks</H2>
<BLOCKQUOTE>
	(Matt Dillon)  Variant Symlinks allow links to have variables used
	as part of the path the link points to.  This allows per-user and per-environment
	settings.  This project is complete.  (man varsym for info)
</BLOCKQUOTE>

<H2>Website</H2>
<BLOCKQUOTE>
	In October, the DragonFly website received a facelift from Justin Sherrill
	and Hiten Pandya.  It also now has a <a href="http://www.dragonflybsd.org/main/team.cgi">Team</a> page, and this report.
</BLOCKQUOTE>
