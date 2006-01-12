#!/usr/local/www/cgi-bin/tablecg
#
# $DragonFly: site/data/main/Attic/release1_4.cgi,v 1.2 2006/01/12 18:58:37 dillon Exp $

$TITLE(DragonFly - January 2006 Release 1.4.x Download)
<h1>Obtaining DragonFly 1.4.x for your system</h1>

<h2>1.4.0 ISO Images for CDs</h2>

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
<br />MD5 (dfly-1.4.0_REL.iso.gz) = 5dc706f0dc57d691a8291f44cd19998c

</b><br /> </p>

<h2>1.4.x Release Errata</h2>
<p>
    DragonFly releases are meant to be stable, dependable entities.  We
    backport compatible bug fixes from current development into release
    branches but we do not generally backport new features. 
    The release CD is always a '.0', e.g. 1.4.0.   The most common way to
    track a release is to use cvsup to track the release sources and then
    build and install a new world and kernel to keep your system updated.
</p>
<p>
    <B>We do not always get every last little fix into a release.  Please
    be sure to read the errata page for the release CD!</B>
</p>
<TABLE BORDER="1">
<TR>
<TH>Version</TH>
<TH>Date</TH>
<TH>Comments</TH>
<TH></TH>
</TR>
<TR>
<TD>1.4.0</TD>
<TD>07-Jan-2006</TD>
<TD>RELEASE CD</TD>
<TD><A HREF="errata1_4.cgi">Errata</A></TD>
</TR>
</TABLE>

<p>
</p>


<h2>1.4.x Release Sites</h2>

<TABLE BORDER="1">
<TR>
<TH>Organization</TH>
<TH>Mirrored Data</TH>
<TH>Access methods</TH>
</TR>

<TR><TD>Machdep.com (USA)</TD>
<TD>1.4.0_REL image</TD>
<TD>
    <A HREF="http://dl1.machdep.com/dfly-1.4.0_REL.iso.gz">HTTP (East Coast)</A>
    <br>
    <A HREF="http://dl2.machdep.com/dfly-1.4.0_REL.iso.gz">HTTP (West Coast)</A>
</TD></TR>

<TR><TD>Fortunaty.net</TD>
<TD>1.4.0_REL image</TD>
<TD><A HREF="http://ftp.fortunaty.net/DragonFly/iso-images/dfly-1.4.0_REL.iso.gz">HTTP</A></TD></TR>

<TR><TD>Chlamydia.fs.ei.tum.de (Germany)</TD>
<TD>1.4.0_REL image</TD>
<TD>
    <A HREF="http://chlamydia.fs.ei.tum.de/pub/DragonFly/iso-images/dfly-1.4.0_REL.iso.gz">HTTP</A>
    <A HREF="ftp://chlamydia.fs.ei.tum.de/pub/DragonFly/iso-images/dfly-1.4.0_REL.iso.gz">FTP</A>
</TD>
</TR>

<TR><TD>BGP4.net (USA)</TD>
<TD>1.4.0_REL image</TD>
<TD>
    <A HREF="http://mirror.bgp4.net/pub/DragonFly/iso-images/dfly-1.4.0_REL.iso.gz">HTTP</A>
    <A HREF="ftp://mirror.bgp4.net/pub/DragonFly/iso-images/dfly-1.4.0_REL.iso.gz">FTP</A>
</TD></TR>

<TR><TD>TheShell.com</TD>
<TD>1.4.0_REL image</TD>
<TD>
    <A HREF="http://www.theshell.com/pub/DragonFly/iso-images/dfly-1.4.0_REL.iso.gz">HTTP</A>
    <A HREF="ftp://ftp.theshell.com/pub/DragonFly/iso-images/dfly-1.4.0_REL.iso.gz">FTP</A>
</TD></TR>

<TR><TD>FrostBit.org</TD>
<TD>1.4.0_REL image</TD>
<TD>
    <A HREF="http://torrent.frostbit.org/dfly-1.4.0_REL.iso.gz.torrent">TORRENT</A>
</TD></TR>

<TR><TD>Dragonflybsd.org (USA)</TD>
<TD>1.4.0_REL image</TD>
<TD><A HREF="ftp://ftp.dragonflybsd.org/iso-images/dfly-1.4.0_REL.iso.gz">FTP</A>
(<I>try to find another site first</I>)</TD></TR>

</TABLE>

<h1>Release Notes for DragonFly 1.4.0</h1>

<p>
1.4 is our third major DragonFly release.  DragonFly's policy is to
only commit bug fixes to release branches.
</p>
<p>
The two biggest user-visible changes in this release are (a) A major 
revamping of libc, ctype, and wchar support, as well as changes made
in the kernel which require us to bump the major rev for all of our
shared libraries, and (b) The introduction of PKGSRC to manage third
party applications.  DragonFly no longer supports the FreeBSD PORTS 
system.
</p>
<p>
A huge amount of under-the-hood work has been accomplished with this
release, in particular with network device drivers and protocol stacks,
journaling, and the scheduler.   A phenominal amount of work has gone
into stabilizing major subsystems as part of our effort to remove the
big giant lock.  A lot of obscure race conditions and bugs have been
found and fixed.  From a stability perspective we believe this is our
best release to date!
</p>
<p>
<ul>
	<li><b>Add the closefrom() system call.</b>
	<li><b>GCC 3.4 is now the default compiler.  2.95.x is no longer 
	    supported (it can't handle the new threading storage classes
	    properly).</b>
	<li><b>Import Citrus from NetBSD.</b>
	<li><b>Implement direct TLS support for programs whether threaded or not.</b>
	<li><b>Major library and user-visible system structure changes
	    (dirent, stat, errno, etc), and other work requires a major
	    library bump for libc and other libraries.  libc is now
	    libc.so.6.</b>
	<li><b>stat: inode size now 64 bits, nlink now 32 bits.  new fields, 
	    added pad.</b>
	<li><b>dirent: inode size now 64 bits, various fields disentangled from
	    the UFS dirent.</b>
	<li><b>statfs: new fields, added pad.</b>
	<li>Clean up RC scripts that are not used by DragonFly.
	<li>Remove the OS keyword requirement for RC scripts.
	<li>Add support for unsigned quads to sysctl.
	<li><b>Implement DNTPD, DragonFly's own NTP client time synchronization
	    demon.</b>
	<li>Correct a large number of bugs in the third party ntpd code, but
	    for client-side operations we now recommend you use dntpd.
	<li>Add a framework for aggregating per-cpu structures for user 
	    reporting.
	<li><b>Userland TLS (data spaces for threads) support added.</b>
	<li><b>Create a binary library compatibility infrastructure that
	    allows us to install and/or upgrade older revs of shared
	    libraries on newer machines to maintain compatibility with
	    older programs.</b>
	<li>Fix issues related to the expansion of symbolic links by the
	    bourne shell.
	<li>Many, Many mdoc cleanups and fixes.
	<li>Update cvs, openssl, ssh, sendmail, groff, 
	    and other numerous contributed applications.
	<li><b>Bring in a brand new pam infrastructure.</b>
	<li><b>Introduce pkgsrc support.</b>
	<li>Get rid of libmsun.
	<li>Implement backwards scanning and partial-transaction handling
	    features in jscan.

	<p></p>
	<li>FreeBSD-SA-05:06.iir - major disk access vulnerability for IIR
	<li>FreeBSD-SA-05:04.ifconf - memory disclosure vulnerability
	<li>FreeBSD-SA-05:08.kmem - memory disclosure vulnerability
	<li>FreeBSD-SA-05:16.zlib - possible buffer overflow in zlib
	<li>FreeBSD-SA-05:18.zlib - possible buffer overflow in zlib
	<li>FreeBSD-SA-05:15.tcp - fix TCP RESET window check 
	    (DOS attack vulnerability)
	<li>? - a bzip2 vulnerability

	<p></p>
	<li>Fix a bug in the TCP NewReno algorithm which could result in
	    a large amount of data being unnecessarily retransmitted.
	<li>Fix numerous TCP buffering issues.
	<li>Implement TCP Appropriate Byte Counting
	<li>Bring in ALTQ and reorganize the IF queueing code to remove
	    per-driver depdendancies on ALTQ.
	<li>Strip away numerous TCP hidden indirections that make code hard
	    to read and understand.
	<li>Introduce BPF_MTAP which includes an address family parameter.
	<li>Reimplement network polling with a systimer, allowing the
	    frequency to be adjusted on the fly.
	<li>Remove the really bad hack that was calling the network polling
	    code from the trap code.
	<li>Completely rewrite nework polling support.
	<li>Make the network IF serializer mandatory for all network device
	    driver interrupts, ioctl's, and if_ callbacks.
	<li>Implement a very fast memory object caching infrastructure.  This
	    will eventually replace zalloc() (but not yet).
	<li>Rewrite the mbuf allocator using the new memory object caching
	    infrastructure.  Remove many crazily-large mbuf macros in favor
	    of the new infrastructure.
	<li>Convert all remaining uses of the old mbuf m_ext API to the new
	    API.  Remove support for the old API.
	<li>Reorder the detach sequence in all network drivers.  Unhook the
	    interrupt first rather then last.
	<li>Fix all instances where an mbuf packet header and mbuf data 
	    buffer were being referenced by the wrong name and all instances
	    where the packet header flag was being improperly set or cleared.
	<li>Fix a number of mbuf statistics counting bugs.
	<li>Fix numerous bugs in ipfw/ipfw2 where m_tag data was not being
	    stored in the right place, resulting in a panic.
	<li>Add support for the experiemental SCTP protocol.
	<li>Fix an issue with cloned interfaces being added twice.

	<p></p>
	<li>Add a passive IPIQ call for non-time-critical events such as
	    memory free() calls.
	<li>Add TLS support for threads using the GDT instead of the LDT.
	<li>Greatly simplify and demystify the NTP kernel interface.  Convert
	    most aspects of the interface over to sysctls.
	<li>Implement ranged fsync's in-kernel.  This capability will 
	    eventually replace the write-behind heuristic.

	<li>Introduce MP-safe mountlist scanning code.
	<li>Introduce rip-out-safe red-black tree scanning code.
	<li>Use the new RB scanning code to get rid of VPLACEMARKER and
	    generally use the new RB scanning code to handle all RB tree
	    scanning in a safe way (allowing the scan code callback to block).
	<li>Zoneinfo upgrades
	<li>Rename cpu_mb*() functions to cpu_mfence(), cpu_lfence(), and
	    cpu_sfence() to make their function more apparent.
	<li>Fix bugs in the LWKT token code related to token references
	    being lost due to a preemption or blocking condition.
	<li>Fix bugs in the LWKT rwlock code relating to preemption occuring
	    during token acquisition.
	<li>Fix a bug in the LWKT thread queueing and dequeueing code 
	    related to a preemption.
	<li>Increase the size of the physmap[] array to accomodate newer
	    PC's which have a larger number of memory segments and fix
	    an overflow bug.
	<li>Use the ACPI timer if present instead of one of the other 8254
	    timers (which are not dependable because BIOS calls might 
	    manipulate them).
	<li>Change cpu statistics to be accounted for on a per-cpu basis.
	<li>Make network routing statistics per-cpu.
	<li>Extend the interrupt vector code to pass a frame as a pointer.
	<li>Remove the last vestiges of the old mbuf tagging code.
	<li>Add a serializer API and code (basically blockable mutexes).
	<li>Add interrupt enablement and disablement features to the new
	    serializer module to deal with races against blocked serializer
	    locks when e.g. removing a driver.
	<li>Remove bus_{disable,enable}_intr(), it was not generic enough
	    for our needs.
	<li>Remove all spl*() procedures and convert all uses to critical
	    sections.
	<li>Do not try to completely halt all cpus when panic()ing as this
	    will likely leave the machine in a state that prevents it from
	    being able to do a dump.
	<li>Try to unwind certain conditions when panic()ing from a trap
	    in order to give the machine a better chance to dump its core.
	<li>A number of malloc()'s using M_NOWAIT really needed to be
	    using M_WAITOK.
	<li>Attempt to avoid a livelocked USB interrupt during boot by 
	    delaying the enablement of the EHCI interrupt until after all
	    companion controllers have been attached.
	<li>Reimplement the kernel tracepoint facility (KTR) to greatly
	    reduce the complexity of the API as well as remove all hardwired
	    flags and values.  In addition, record two levels of call
	    backtrace for each entry, if enabled.
	<li>Beef up ktrdump to display symbolic results when possible.
	<li>Beef up the slab allocator build with INVARINTS by adding a
	    bitmap to detect duplicate frees and such.
	<li>Remove the 16 bit count limit for file descriptors.
	<li>Replace the file descriptor allocator with an O(log N) 
	    full-on in-place binary search tree.
	<li>Allow the initial stack pointer for a use process to be
	    randomized.
	<li>Fix numerous scheduling issues that could cause the scheduler
	    to lose track of a reschedule request, resulting in poor 
	    interactive performance.  Rewrite the interactive/batch
	    heuristic.
	<li>Begin to implement a management system to allow multiple 
	    userland schedulers to be configured in a system.
	<li>Add rm -I and add an alias for interactive shells to use it
	    by default.  -I is a less invasive -i.
	<li>Fix a bug in the pipe code that was not handling kernel-space
	    writes correctly.  Such writes can occur whenever the kernel
	    writes KVM-referenced data to a descriptor, such as that
	    journaling code might do.
	<li>Fix many issues with the high level filesystem journaling code.
	    High level journal records are now considered fairly solid.
	<li>Implement the transactional features of the high level journaling
	    subsystem by allowing a journaling record to be written prior to
	    the VFS operation being executed, then aborted if the VFS operation
	    fails.
	<li>Implement UNDO records for most journaling transaction types.
	<li>Implement the journaling code's full-duplex ack protocol feature
	    which allows journals to be broken and restarted without losing
	    data.
	<li>Implement a stat-visible FSMID (filesystem modification id).  This
	    identifier changes whenever any modifying operation on the file
	    or directory occurs, and for directories this identifier also
	    changes if anything in the sub-tree under the directory is
	    modified (recursively).  The FSMID is synthesized for filesystems
	    which do not implement it directly in order to guarentee its 
	    usefulness for at least a subset of operations.
	<li>Implement pesistent storage of the FSMID for UFS.
	<li>Implement shutdown() support for pipes.
	<li>Implement a low level spinlock facility.  Basically the 
	    implementation gives us an MP-safe critical section type of
	    vehicle.  However, being a spinlock the facility may only be
	    used for very short sections of code.
	<li>Fix a bug with USB<->CAM communication for USB mass storage
	    devices.
	<li>Fix numerous bugs in USB, primarily EHCI.
	<li>Fix multiple panics when a fatal trap occurs from an IPI or
	    FAST interrupt.  Interlock panics on multiple cpus so only the
	    first is recognized as the 'real' panic.
	<li>Add a large number of assertions to the scheduler and interrupt
	    subsystems.
	<li>Fix a critical IPI messaging bug (SMP only).
	<li>Do not compile the kernel with the stack protector.  The stack
	    protector generates weird incorrect or unexpected code in some
	    cases which interfere with the C<->assembly interactions in the
	    kernel build
	<li>Various bug fixes to softupdates. 
	<li>Fix a bitmap scanning bug in UFS which could sometimes result
	    in a sanity check panic, but no data corruption.
	<li>Fix a deadlock in UFS's ffs_balloc() related to an incorrect
	    buffer locking order.
	<li>Continued work on the buffer cache.
	<li>Separate out APIC and ICU interrupt management.
	<li>Rewrite the interrupt setup code.
	<li>Major rewriting of the VFS directory scanning code.  Add a new
	    function vop_write_dirent() to create the dirent for return to
	    userland.  The new API is mandatory and filesystem code (not
	    even UFS) may not make assumptions about the size of the
	    userland-returned dirent.
	<li>Major cleanup of the device identification method.
	<li>Lots of driver updates.
	<li>Ansify a great deal more of the codebase.
	<li>Remove the now obsolete smp_rendezvous() mechanism.
	<li>Compile up both the TFTP and the NFS PXE bootp code rather
	    then the (previous) make.conf option to select one or the other.
	<li>Convert the lockmgr interlock from a token to a spinlock, also
	    incidently fixing an issue where non-blocking locks would
	    still potentially issue a thread switch.
	<li>Fix bugs in the interrupt livelock code.
	<li>Rewrite the code handling stopped user processes.
	<li>Rewrite tsleep()/wakeup() to be per-cpu and MPSAFE.  Reorganize
	    the process states (p_stat), removing a number of states but
	    resynthesizing them in eproc for 'ps'.
	<li>Integrate the new if_bridge code from Open/Net/FreeBSD.
	<li>Add an emergency interrupt polling feature that can be used
	    to get an otherwise non-working system working.
</ul>
</p>
