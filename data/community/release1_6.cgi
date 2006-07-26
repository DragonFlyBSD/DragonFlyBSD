#!/usr/local/www/cgi-bin/tablecg
#
# $DragonFly: site/data/community/Attic/release1_6.cgi,v 1.3 2006/07/26 03:46:27 justin Exp $

$TITLE(DragonFly - July 2006 Release 1.6.x Download)
<h1>Obtaining DragonFly 1.6.x for your system</h1>

<h2>1.6.0 ISO Images for CDs</h2>

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
The installer has a Netboot server option.  You can
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
<br />MD5 (dfly-1.6.0_REL.iso.gz) = ebbcc2f4af5f7ee1e5a4cbf6f3f75bf8

</b><br /> </p>

<h2>1.6.x Release Errata</h2>
<p>
    DragonFly releases are meant to be stable, dependable entities.  We
    backport compatible bug fixes from current development into release
    branches but we do not generally backport new features. 
    The release CD is always a '.0', e.g. 1.6.0.   The most common way to
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
<TD>1.6.0</TD>
<TD>24-Jul-2007</TD>
<TD>RELEASE CD</TD>
<TD>(no current errata)</TD>
</TR>
</TABLE>

<p>
</p>


<h2>1.6.x Release Sites</h2>

<TABLE BORDER="1">
<TR>
<TH>Organization</TH>
<TH>Mirrored Data</TH>
<TH>Access methods</TH>
</TR>

<TR><TD>Fortunaty.net</TD>
<TD>1.6.0_REL image</TD>
<TD><A HREF="http://ftp.fortunaty.net/DragonFly/iso-images/dfly-1.6.0_REL.iso.gz">HTTP</A></TD></TR>

<TR><TD>Chlamydia.fs.ei.tum.de (Germany)</TD>
<TD>1.6.0_REL image</TD>
<TD>
    <A HREF="http://chlamydia.fs.ei.tum.de/pub/DragonFly/iso-images/dfly-1.6.0_REL.iso.gz">HTTP</A>
    <A HREF="ftp://chlamydia.fs.ei.tum.de/pub/DragonFly/iso-images/dfly-1.6.0_REL.iso.gz">FTP</A>
</TD>
</TR>

<!--
<TR><TD>BGP4.net (USA)</TD>
<TD>1.6.0_REL image</TD>
<TD>
    <A HREF="http://mirror.bgp4.net/pub/DragonFly/iso-images/dfly-1.6.0_REL.iso.gz">HTTP</A>
    <A HREF="ftp://mirror.bgp4.net/pub/DragonFly/iso-images/dfly-1.6.0_REL.iso.gz">FTP</A>
</TD></TR>
-->

<TR><TD>TheShell.com</TD>
<TD>1.6.0_REL image</TD>
<TD>
    <A HREF="http://www.theshell.com/pub/DragonFly/iso-images/dfly-1.6.0_REL.iso.gz">HTTP</A>
    <A HREF="ftp://ftp.theshell.com/pub/DragonFly/iso-images/dfly-1.6.0_REL.iso.gz">FTP</A>
    <A HREF="http://www.theshell.com/dfly-1.6.0_REL.iso.gz.torrent">TORRENT</A>
</TD></TR>


<TR><TD>Dragonflybsd.org (USA)</TD>
<TD>1.6.0_REL image</TD>
<TD><A HREF="ftp://ftp.dragonflybsd.org/iso-images/dfly-1.6.0_REL.iso.gz">FTP</A>
(<I>try to find another site first</I>)</TD></TR>

</TABLE>

<h1>Release Notes for DragonFly 1.6.0</h1>

<p>
1.6 is our fourth major DragonFly release.  DragonFly's policy is to
only commit bug fixes to release branches.
</p>
<p>
The biggest user-visible changes in this release are a new random number
generator, a massive reorganization of the 802.11 (wireless) framework,
and extensive bug fixes in the kernel.  We also made significant progress
in pushing the big giant lock inward and made extensive modifications to
the kernel infrastructure with an eye towards DragonFly's main clustering
and userland VFS goals.  We consider 1.6 to be more stable then 1.4.
</p>
<p>
<ul>
	<li>Continued work on LWP/PROC separation.
	<li>Continued pkgsrc integration.
	<li>Fix refcount bugs in the kernel module loader and unloader.
	<li>Lots of netif and serializer cleanups and fixes.
	<li>Lots of softupdates, filesystem, and buffer cache related fixes.
	<li>Remove more of the old ports-related infrastructure.
	<li>Major documentation cleanups.
	<li>Major code cleanups, ansification.
	<li>Change the system #include topology so that each include file
	    is responsible for #include'ing any dependant include files.
	<li>Fix a bug in the PF fragment cache.
	<li>Random number generator: Instead of generating entropy from
	    selected interrupts (and none by default), we now generate
	    entropy from all interrupts by default and rate limit it to
	    not interfere with high performance interrupts. 
	<li>Random number generator: Completely replace the algorithms,
	    remove limitations on returned bytes from /dev/random (which
	    only served to cause programs to not use /dev/random due to its
	    lack of dependability).  Add the ability to seed the RNG.
	    Do some automatic initial seeding at boot.
	<li>Adjust ssh to find the pkgsrc X11BASE instead of the old ports
	    X11BASE.
	<li>Fix some compatibility issues in /bin/sh.
	<li>Fix a small number of critical section enter/exit mismatches.
	<li>Bring in a bunch of new malloc() features from OpenBSD
	    (guard pages, free page protection, pointer guard, etc).
	<li>Clean up the DragonFly build system's automatic .patch handling.
	<li>Bring in openssh 4.3p2.
	<li>Retire libmsun.  It was replaced by NetBSD's libm.
	<li>Fix a bug in the NFS timer/retry code.
	<li>Fix issues related to wide-char support.
	<li><B>Fix a number of private TSS bugs related to threaded programs.</B>
	<li><B>Completely rewrite the user process scheduler and usched APIs.</B>
	<li><B>Add system calls that allow a blocking/non-blocking flag to be
	    passed independant of the O_NONBLOCK state of the descriptor.</B>
	<li><B>Remove all fcntl(... O_NONBLOCK) calls from libc_r, use the
	    new system calls instead.  This solves numerous problems with
	    file descriptors shared between threaded and non-threaded
	    programs getting their non-blocking flag set, and then blowing
	    up the non-threaded program.</B>
	<li>Add additional red-black (RB) tree function support for ranged
	    searches.
	<li>Get rid of gdb -k, replace with a separately built kgdb, and
	    build a separate libgdb as well.
	<li>Implement a VM load heuristic.  Remove the full-process SWAP
	    code which never worked well and replace with page-fault
	    rate-limiting code based on the VM load.
	<li>Fix a serious bug in adjtime()'s microseconds calculation.
	<li>Fix a serious bug in libc's strnstr() function.  The function
	    was testing one byte beyond the specified length limit.  This
	    can cause a seg-fault when, e.g. using strnstr() on a memory
	    mapped file whos last byte abuts the end of the VM page.
	<li>Bring in sendmail 8.13.7.
	<li>Bring in SHA256 support from FreeBSD.
	<li>Implement a hardlink-mirroring option (-H) in cpdup.
	<li>Add missing code needed to detect IPSEC packet replays.
	<li>Enable TCP wrappers in sshd.
	<li>Restrict recursive DNS queries to localhost by default.
	<li><B>Massive reorganization and rewrite of the 802_11 subsystem,
	    with many pieces taken from FreeBSD.</B>
	<li>Fix a number of issues with user-supplied directory offsets
	    blowing up readdir/getdirentries for NFS and UFS.
	<li>Normalize internal kernel I/O byte lengths to 'int' and remove
	    a ton of crazy casts to and from unsigned int or size_t.
	<li>Remove NQNFS support.  The mechanisms are too crude to co-exist
	    with upcoming cache coherency management work and the original
	    implementation hacked up the NFS code pretty severely.
	<li>Remove VOP_GETVOBJECT, VOP_DESTROYVOBJECT, and VOP_CREATEVOBJECT.
	    They never lived up to their billing and DragonFly doesn't
	    need them since DragonFly is capable of aliasing vnodes via
	    the namecache.  Rearrange the VFS code such that VOP_OPEN is
	    now responsible for assocating a VM object with a vnode.
	<li>Formalize open/close counting for the vnode and assert that
	    they are correct.
	<li>Remove the thread_t argument from most VOP and VFS calls.
	    Remove the thread_t argument from many other kernel procedures.
	<li>Integrate FreeBSD's new ifconfig(8) utility.
	<li>Fix a race condition in the floating point fault code that
	    could sometimes result in a kernel assertion.
	<li>Fix a crash in the TCP code when the MTU is too small to
	    support required TCP/IP/IPSEC options and headers.
	<li>Separate EXT2 conditionals from the UFS code, copying the files
	    in question to the EXT2 directory instead of trying to 
	    conditionalize them.  Also remove function hooks and other code
	    mess that had been implemented to allow the UFS code to be
	    used by EXT2.
	<li>Greatly simplify the lockmgr() API.  Remove LK_DRAIN, 
	    LK_INTERLOCK, and many other flags.  Remove the interlock 
	    argument.
	<li>Fix a bug in the POSIX locking code (lockf).  Actually, completely
	    rewrite the POSIX locking code.  The previous code was too
	    complex and mostly unreadable.
	<li>Do a major clean up of all *read*() and *write*() system calls,
	    and iovec handling.
	<li>Replace many instances where LWKT tokens are used with spinlocks.
	<li>Make spinlocks panic-friendly.  Properly handle the detection
	    of indefinite waits and other deadlock issues.
	<li>Improve network performance by embedding the netmsg directly in
	    the mbuf instead of allocating a separate netmsg structure for
	    each packet.
	<li><B>Implement both shared and exclusive spinlocks.
	    Implement a much faster shared spinlock.  Cache the shared
	    state such that no locked bus cycle operation is required in
	    the common case.</B>
	<li>Implement msleep().  Use a novel approach to handling the
	    interlock that greatly improves performance over other 
	    implementations.
	<li>Add cpu-binding support to the scheduler and add a system call
	    to access it.  A user process can be bound to a subset of cpus.
	<li><B>Prefix all syscall functions in the kernel with 'sys_'
	    to reduce function prototype pollution and incompatibilities,
	    and to eventually support virtualized kernels running in
	    userland.</B>
	<li>Port the enhanced SpeedStep driver (EST) for cpu frequency control.
	<li>Remove the asynchronous syscall interface.  It was an idea before
	    its time.  However, keep the formalization of the syscall
	    arguments structures.
	<li>Add a facility which statistically records and dumps program
	    counter tracking data for the kernel.
	<li>Improve kernel SSE2-based bcopies by calling fnclex instead of
	    fninit.
	<li>Major BUF/BIO work - <B>make the entire BUF/BIO subsystem BIO
	    centric.</B>
	<li>Major BUF/BIO work - <B>get rid of block numbers, use 64 bit 
	    byte offsets only.</B>
	<li>Major BUF/BIO work - Clean up structures and compartmentalize
	    driver-specific private fields.  Rewrite and simplify device
	    and vnode strategy APIs.
	<li>Major BUF/BIO work - Remove B_PHYS.  There is no longer any
	    differentiation between physical and non-physical I/O at
	    the strategy layer.
	<li>Major BUF/BIO work - Replace the global buffer cache hash table
	    with a per-vnode RB tree.  Add sanity checks.  Require that all
	    vnode-based buffers be VMIO backed.
	<li>MPSAFE work - <B>Implement the parallel route table algorithm.</B>
	<li>MPSAFE work - <B>Make the user process scheduler MPSAFE.</B>
	<li>MPSAFE work - <B>File descriptor access is now MPASFE.  Many
	    fd related functions, like dup(), close(), etc, are either MPSAFE
	    or mostly MPSAFE.</B>
	<li>MPSAFE work - <B>Push the BGL deeper into the kernel call stack
	    for many system calls.</B>
	<li>MPSAFE work - <B>Make the process list MPSAFE.</B>
	<li>MPSAFE work - <B>Make all cred functions MPASFE.</B>
	<li>NRELEASE - compilable kernel sources are now included on the ISO.
</ul>
</p>
