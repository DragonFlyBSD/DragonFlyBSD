#!/usr/local/www/cgi-bin/tablecg
#
# $DragonFly: site/data/status/Attic/report-2003.cgi,v 1.8 2004/07/12 04:49:43 hmp Exp $

$TITLE(DragonFly - End of Year Summary for 2003)

<h1>End of Year Summary for 2003</h1>

<p>
<a href="http://www.dragonflybsd.org/">DragonFly BSD</a> was
<a href="http://lists.freebsd.org/pipermail/freebsd-stable/2003-July/002183.html">announced</a>
in July of this year, with Matt Dillon the originator and main weightlifter of this project.
Development has been proceeding at a good clip, with a first release expected in 2004.</p>

<p>Statistics: Here's some data on the amount of activity in the DragonFly source,
as of the end of December 2003.</p>

<table border="1">
<tr><th>Number of commits</th><th>Committer</th></tr>
<tr><td>48288</td><td>dillon</td></tr>
<tr><td>1212</td><td>rob</td></tr>
<tr><td>997</td><td>asmodai</td></tr>
<tr><td>913</td><td>eirikn</td></tr>
<tr><td>636</td><td>drhodus</td></tr>
<tr><td>390</td><td>hmp</td></tr>
<tr><td>149</td><td>daver</td></tr>
<tr><td>53</td><td>joerg</td></tr>
<tr><td>3</td><td>justin</td></tr>
</table>

<h2>ACPI</h2>
<blockquote>
The newer <a href="http://www.acpi.info/">power system</a>, imported by David Rhodus.
It is not on by default.
</blockquote>


<h2>AGP</h2>
<blockquote>
A number of improvements to AGP support from FreeBSD 5 were added by Craig Dooley.
</blockquote>

<h2>AMD64 port</h2>
<blockquote>
	(Matt Dillon, Hiten Pandya, David Rhodus)  AMD's new 64-bit processors are
	nearly supported, with FreeBSD-5's boot code brought in to DragonFly.
	GCC 3.3 support and some code changes are needed first.
	As a side effect of this work, some hooks for PAE support now exist.
	Matt Dillon and David Rhodus has AMD64 hardware to test on.  This
	work has included the merge of boot code from FreeBSD-5
	along with Peter Wemm's linker changes.  (See GCC3 listing)

</blockquote>

<h2>ATAng</h2>
<blockquote>
	ATAng is 'next generation' ATA disk access, which works more efficiently in
	a multithreaded environment.  David Rhodus is merging in newer ATA code
	from the FreeBSD-4x tree and is trying to "work through the abuse of
	M_NOWAIT inside the ATA raid card controllers".
</blockquote>

<h2>Bugzilla</h2>
<blockquote>
	Jeroen Ruigrok has set up a Bugzilla database, and imported a large number
	(over 6,000) of bug reports and feature requests from FreeBSD-4 that may
	still apply to DragonFly.  This database is not yet available.
</blockquote>

<h2>Checkpointing</h2>
<blockquote>
	(Kip Macy)  It is possible to 'freeze' (^E) a copy of a program in
	its current state, and then later 'thaw' (/usr/bin/checkpt) it, restoring the
	state of the program.  Among more exotic applications,
	long-running processes can be saved and restored across reboots.  This
	is mostly complete, and can be loaded with the checkpoint.ko module.
</blockquote>

<h2>Cleanup</h2>
<blockquote>
	Eirik Nygaard and Craig Dooley have been cleaning up the old K&amp;R-style functions
	into ANSI code, along with removal of the __P() macro use from the userland. Robert
	Garrett removed the __P() macro inside the kernel source.  Jeroen Ruigrok has been
	working on the removal of perl from the kernel build process.  David Rhodus and
	Hiten Pandya have been fixing other various long running problems inside the kernel.
	David Rhodus has updated Sendmail (8.12.10), BIND(8.3.7), and also updated other
	applications like OpenSSL and OpenSSH.
</blockquote>

<h2>GCC3</h2>
<blockquote>
	Craig Dooley has submitted many patches to enable DragonFly
	building under GCC3.  David Rhodus has GCC-3.3.3 working in a test
	tree at the moment, for AMD64 support.(In progress)
</blockquote>

<h2>dfports</h2>
<blockquote>
	Matt Dillon created a local override of the existing FreeBSD ports system, to allow
	port installation until a DragonFly-native solution is created.
</blockquote>


<h2>libcaps</h2>
<blockquote>
libcaps has been created.  This is a library that allows userland threading, similar to the existing LWKT.  This is being worked on by Galen Sampson and Matt Dillon.
</blockquote>


<h2>Live CD</h2>
<blockquote>
	DragonFly CDs can now be built as 'live', meaning that a computer can be booted using the CD as the boot disk, and then tools like cpdup can be used to install to hard disk.  This is not yet fully documented; i.e. you will have to run programs like disklabel manually.  /usr/src/nrelease contains the code, and also some instructions.
</blockquote>

<h2>LWKT</h2>
<blockquote>
	Added by Matt Dillon.  LWKT stands for Light-Weight Kernel Threads.  LWKT gives
	each CPU in the system its own scheduler, and threads are locked to that CPU.  This
	removes any need for mutexes to handle interrupts.  More information is available
	<a href="http://www.dragonflybsd.org/goals/threads.cgi">at the DragonFly website</a>.

	Galen Sampson has been working on a port to userland.
</blockquote>

<h2>Logging</h2>
<blockquote>
	Justin Sherrill has been keeping a running news report of DragonFly activity
	at <a href="http://www.shiningsilence.com/dbsdlog/">http://www.shiningsilence.com/dbsdlog/</a>.
</blockquote>

<h2>MPIPE</h2>
<blockquote>
	Quoted from the committ log: "This subsystem is used for 'pipelining' fixed-size
  allocations.  Pipelining is used to avoid lack-of-resource deadlocks by
  still allowing resource allocations to 'block' by guarenteeing that an
  already in-progress operation will soon free memory that will be immediately
  used to satisfy the blocked resource."  Added by Matt Dillon.
</blockquote>

<h2>Make release</h2>
<blockquote>
	Jeroen Ruigrok has been working on make release.  Since the size of the DragonFly
	kernel is in flux, it can exceed the available floppy space.
	(incomplete)
</blockquote>

<h2>Mirrors</h2>
<blockquote>
	Germany - Simon 'corecode' Schubert<br/>
	<a href="http://chlamydia.fs.ei.tum.de/">http://chlamydia.fs.ei.tum.de/</a><br/>
	Ireland - David Burke<br/>
	<a href="http://ftp.esat.net/mirrors/chlamydia.fs.ei.tum.de/pub/DragonFly/">http://ftp.esat.net/mirrors/chlamydia.fs.ei.tum.de/pub/DragonFly/</a><br/>
      	<a href="ftp://ftp.esat.net/mirrors/chlamydia.fs.ei.tum.de/pub/DragonFly/">ftp://ftp.esat.net/mirrors/chlamydia.fs.ei.tum.de/pub/DragonFly/</a><br/>
	Japan - Hiroki Sato<br/>
	<a href="http://pub.allbsd.org/DragonFly/snapshots/i386/">http://pub.allbsd.org/DragonFly/snapshots/i386/</a><br/>
	<a href="ftp://ftp.allbsd.org/pub/DragonFly/snapshots/i386/">ftp://ftp.allbsd.org/pub/DragonFly/snapshots/i386/</a><br/>
	<a href="rsync://rsync.allbsd.org/dragonfly-snapshots/">rsync://rsync.allbsd.org/dragonfly-snapshots/</a><br/>
</blockquote>

<h2>Namecache</h2>
<blockquote>
	Matt Dillon completed this work in September; a writeup of the two stages
	of the work are available
	<a href="http://www.shiningsilence.com/dbsdlog/archives/2003_09.html#000015">here</a>, and
	<a href="http://www.shiningsilence.com/dbsdlog/archives/2003_09.html#000034">here</a>.
</blockquote>

<h2>Network interface aliasing (if_xname)</h2>
<blockquote>
	Max Laier is bringing in code used in the other BSDs to allow
	if_xname, if_dname, and if_dunit.
</blockquote>

<h2>Network stack</h2>
<blockquote>
	Jeffrey Hsu has been moving the network stack to a multithreaded
	model; he also has been making networking match certain RFCs, such as
	<a href="http://netweb.usc.edu/pim/">Protocol Independent Multicast</a>.
</blockquote>

<h2>NEWCARD support</h2>
<blockquote>
	Joerg Sonnenberger has been working on NEWCARD (PCMCIA card support) from FreeBSD 5.
</blockquote>


<h2>NVIDIA binary video card driver</h2>
<blockquote>
	Emiel Kollof got this working in October; a dfports override is available.
</blockquote>

<h2>NFS</h2>
<blockquote>
	David Rhodus and Hiten Pandya have been working on speeding up NFS
	performance. It is now possible to max out a GigE connection via NFSv3
	TCP, on DragonFly.  More tuning to come.
</blockquote>

<h2>PFIL_HOOKS, pf, ALTQ, and CARP</h2>
<blockquote>
	Max Laier is working on bringing in OpenBSD's pf, along with
	<a href="http://www.csl.sony.co.jp/person/kjc/kjc/software.html">ALTQ</a> and
	<a href="http://kerneltrap.org/node/view/1021">CARP</a>.
</blockquote>

<h2>Prelinking</h2>
<blockquote>
Prelinking capability was added to DragonFly by Simon 'corecode'
Schubert, which allows faster loading of applications that use a large
number of dynamic libraries while running, like Qt/KDE.  It is not 
currently hooked into the
system or any port building process.
</blockquote>


<h2>RAID support at install</h2>
<blockquote>
	David Rhodus added the pst driver for Promise cards to the GENERIC kernel, so installation to RAID drives using a Promise controller should be possible.
</blockquote>

<h2>RCNG</h2>
<blockquote>
	RCNG is a new version of the rc configuration system, taken from NetBSD.  More information is available at the <a href="http://www.netbsd.org/Documentation/rc/">NetBSD site</a>.  Robert Garrett merged this in.  Matt Dillon updated RCNG. You can now check the status of, or start, stop, etc. different system services using appropriate single commands like rcstart, rcstop, rcrestart, and so on. varsym -sa will list service status.
</blockquote>

<h2>Sun Grid Engine</h2>
<blockquote>
	Joshua Coombs is working on the sge - more information at:
	<a href="http://gridengine.sunsource.net/">http://gridengine.sunsource.net/</a>.
</blockquote>

<h2>Slab allocator</h2>
<blockquote>
	Matt Dillon added this new kernel memory allocator, which does not require any
	mutexes or blocks for memory assignment.  More information on how a slab allocator work is
	at <a href="http://citeseer.nj.nec.com/bonwick94slab.html">Bonwick94</a>,
	<a href="http://www.usenix.org/event/usenix01/bonwick.html">Bonwick2001</a>,
	and <a href="http://www.linuxhq.com/kernel/file/mm/slab.c">linux/mm/slab.c</a>
	in the Linux kernel.  This project is complete.
</blockquote>

<h2>Stack Protection</h2>
<blockquote>
Ryan Dooley patched libc giving stack-smashing protection.
</blockquote>

<h2>Syscall separation</h2>
<blockquote>
	David P. Reese has been separating system calls into kernel and userland versions.
	He has also removed stackgap allocations for Linux.
</blockquote>

<h2>Tinderbox</h2>
<blockquote>
	David Rhodus (hardware) and Hiten Pandya (setup) has set up
	repeated builds of the DragonFly system; failures
	are sent to the bugs@dragonflybsd.org mailing list.
</blockquote>

<h2>Variant Symlinks</h2>
<blockquote>
	(Matt Dillon)  Variant Symlinks allow links to have variables used
	as part of the path the link points to.  This allows per-user and per-environment
	settings.  This project is complete.  (man varsym for info)
</blockquote>

<h2>Website</h2>
<blockquote>
	In October, the DragonFly website received a facelift from Justin Sherrill
	and Hiten Pandya.  It also now has a <a href="http://www.dragonflybsd.org/main/team.cgi">Team</a> page, and this report.
</blockquote>
