#!/usr/local/www/cgi-bin/tablecg
#
# $DragonFly: site/data/status/Attic/diary.cgi,v 1.14 2004/05/02 19:41:30 dillon Exp $

$TITLE(DragonFly - Big-Picture Status)

<h2>Sun 2 May 2004</h2>
<ul>
	<li>NEWTOKENs replace old tokens.  The new token abstraction provides
	    a serialization mechanism that persists across blocking conditions.
	    This is not a lock, but more like an MP-capable SPL, in that 
	    if you block you will lose serialization but then regain it when
	    you wakeup again.  This means that newtokens can be used for
	    serialization without having to make lower level subsystems 
	    aware of tokens held by higher level subsystems in the call
	    stack.  This represents a huge simplification over the FreeBSD-5
	    mutex model.
	<li>Support added for the Silicon Image SATA controller.
	<li>DragonFly now supports 16 partitions per slice (up from 8).
	<li>Wildcarded sockets have been split from the TCP/UDP connection
	    hash table, and the listen table is now replicated across 
	    cpus for lockless access.
	<li>UDP sendto() without a connect no longer needs to make a 
	    temporary connect inside the kernel, greatly improving UDP
	    performance in this case.
	<li>NFS performance has been greatly improved.
	<li>Fix some major bugs in the USB/SIM code, greatly improving
	    the reliability of USB disk keys and related devices.
	<li>NEWCARD has been brought in from FreeBSD-5.
	<li>A bunch of userland scheduler performance issues have been fixed.
	<li>Major syscall procedural separation has been completed, separating
	    the user interfacing portion of a syscall from the in-kernel
	    core support, allowing the core support functions to be directly
	    called from emulation code instead of using the stackgap junk.
	<li>An optimized MMX and XMM kernel bcopy has been implemented and
	    tested.  Most of i386/i386/support.s has been rewritten and a
	    number of FP races in that and npxdna() have been closed.
	<li>Brought in the SFBUF facility from FreeBSD-5, including all of
	    Alan Cox's (the FBsd Alan Cox) improvements, plus additional
	    improvements for cpu-localized invlpg calls (and the ability
	    to avoid such calls when they aren't needed).
	<li>A major PIPE code revamp has occured, augmenting the SFBUF
	    direct-copy feature with a linear map.  Peak standard pipe
	    throughput with 32KB buffers on an AMD64 3200+ now exceeds
	    4GBytes/sec.
	<li>Implement XIO, which is a multi-page buffer wrapper for SFBUFs
	    and will eventually replace linear buffer management in the
	    buffer cache as well as provide linear buffer mappings for other
	    parts of the system.
	<li>Added a localized cpu pmap_qenter() called pmap_qenter2() which
	    is capable of maintaining a cpumask, to avoid unnecessary invlpg's
	    on target-side linear maps.  It is currently used by the PIPE
	    code and the CAPS code.
	<li>Joerg has brought in most of the KOBJ extensions from FreeBSD-5.
	<li>Major continuing work by Jeff on the threading and partitioningl
	    of the network stack.  Nearly the whole stack is now
	    theoretically MP safe, with only a few niggling issues before we
	    can actually start turning off the MP lock.
	<li>The last major element of the LWKT messaging system, 
	    lwkt_abortmsg() support, is now in place.  Also it is now possible
	    to wait on a port with PCATCH.
	<li>Hiten has revamped the TCP protocol statistics, making them 
	    per-cpu along the same lines that we have made vmstats per-cpu.
	<li>propolice has been turned on by default in GCC3.
	<li>CAPS (DragonFly userland IPC messaging) has been revamped to
	    use SFBUFs.
	<li>acpica5 now compiles, still needs testing.
	<li>SYSTIMERS added, replacing the original hardclock(), statclock(),
	    and other clocks, and is now used as a basis for timing in the
	    system.  SYSTIMERS provide a fine-grained, MP-capable,
	    cpu-localizable and distributable timer interrupt abstraction.
	<li>Fix RTC write-back issues that were preventing 'ntpdate' changes
	    from being written to the RTC in certain cases.
	<li>Finish most of the namecache topology work.  We no longer need
	    the v_dd and v_ddid junk.  The namecache topology is almost 
	    ready for the next major step, which will be namespace locking
	    via namecache rather then via vnode locks.
	<li>Add ENOENT caching for NFS, greatly reducing the network overhead
	    (by a factor of 5x!!!) required to support things like
	    buildworld's using an NFS-mounted /usr/src.
	<li>Jeff has added predicate messaging requests to the network
	    subsystem, which allows us to convert situations which normally
	    block, like connect(), to use LWKT messages.
	<li>Lots of style cleanups, primarily by Chris Pressey.
</ul>

<h2>Sun 15 February 2004</h2>
<ul>
	<li>Newcard is being integrated.</li>
	<li>A longstanding bug in PCI bus assignments which effects larger
	    servers has been fixed.</li>
	<li>The IP checksum code has been rewriten and most of it has been
	    moved to machine-independant sections.</li>
	<li>A general machine-independant CPU synchronization and rendezvous
	    API has been implemented.  Older hardwired IPIs are slowly being
	    moved to the new API.</li>
	<li>A new 'SysTimer' API has been built which is both MP capable
	    and fine-grained.  8254 Timer 0 is now being used for fine-grained
	    timing, while Timer 2 is being used for timebase.  Various 
	    hardwired clock distribution code is being slowly moved to the
	    new API.  hardclock and statclock have already been moved.</li>
	<li>A long standing bug in the NTP synchronization code has been fixed.
	    NTPD should now lock much more quickly.</li>
	<li>Clock interrupt distribution has been rewritten.  Along with this,
	    IPI messaging now has the ability to pass an interrupt frame
	    pointer to the target function.  Most of the old hardwired 
	    clock distribution code has been ripped out.</li>
	<li>nanosleep() is now a fine-grained sleep.  After all, what's the
	    point of having a nanosleep() system call which is only capable
	    of tick granularity?</li>
	<li>Critical fixes from FreeBSD RELENG_4 integrated by Hiten Pandya.</li>
	<li>Firewire subsystem updated by a patchset from Hidetoshi Shimokawa.</li>
	<li>USB subsystem has been synced with FreeBSD 5 and NetBSD.</li>
	<li>GCC 3.3.3 and Binutils 2.14 have been integrated into base.</li>
	<li>An aggregated Client/Server Directory Services syscall API has
	    been completed.</li>
	<li>An amiga-style 'resident' utility program + kernel support has 
	    been implemented, and prelinking support has been removed 
	    (because the resident utility is much better).  You can make any
	    dynamically loaded ELF binary resident with this utility.  The
	    system will load the program and do all shared library mappings 
	    and relocations, then will snapshot the vmspace.  All future
	    executions of the program simply make a copy of the saved
	    vmspace and skip almost right to main().  Kernel overhead is
	    fairly low, also.  It still isn't as fast as a static binary
	    but it is considerably faster then non-resident dynamic binaries.</li>
</ul>

<h2>Mon 1 December 2003</h2>
<p>A great deal of new infrastructure is starting to come to fruition.</p>
<ul>
	<li>We have a new CD release framework (/usr/src/nrelease) in.
  	    Development on the new framework is proceeding.  Basically the
	    framework is based on a fully functioning live CD system
	    and will allow us to build a new installation infrastructure
	    that is capable of leveraging all the features available in
	    a fully functioning system rather then being forced to use
	    a lobotomized set.</li>
	<li>A new IPC framework to reliably handle things like password
	    file lookups is proceeding.  This framework is intended to
	    remove the need for DLL or statically-linked PAM/NSS and at
	    the same time make maintainance and upgrades easier and more
	    portable.</li>
	<li>The FreeBSD-5 boot infrastructure has been ported and is now
	    the default.</li>
	<li>RCNG from FreeBSD-5 has been ported and is now the default.</li>
	<li>Work is proceeding on bringing in ATAng from FreeBSD-4, with
	    additional modifications required to guarentee I/O in low
	    memory situations.  That is, it isn't going to be a straight
	    port.  The original ATA code from FreeBSD-4.8, which we call
	    ATAold, has been given interim fixes to deal with the low memory
	    and PIO backoff issues so we don't have to rush ATAng.</li>
</ul>

<h2>Sat 18 October 2003</h2>
<p>Wow, October already!  Good progress is being made on several fronts.</p>
<ul>
	<li>K&amp;R function removal.</li>
	<li>VM function cleanups by Hiten Pandya.</li>
	<li>General kernel API cleanups by David Rhodus.</li>
	<li>Syscall Separation work by David Reese.</li>
	<li>Removal of stackgap code in the Linux Emulation by David Reese.</li>
	<li>Networking work by Jeffrey Hsu.</li>
	<li>Interrupt, Slab Allocator stabilization.</li>
	<li>Introduction of _KERNEL_STRUCTURES ... a better way for 
	    userland programs to access kernel header files rather
	    then them setting _KERNEL.</li>
	<li>Bring the system uptodate on security issues (David Rhodus, others)</li>
	<li>NFS peformance improvements by David Rhodus and Hiten Pandya.</li>
	<li>GUPROF and kldload work in the kernel by Hiten Pandya.</li>
	<li>Major progress on the checkpointing code in the kernel
	    primarily by Kip Macy.</li>
	<li>All work through this moment has been stabilized with major
	    input from David Rhodus.</li>
</ul>
<p>Matt's current focus continues to be on rewriting the namecache code.
Several intermediate commits have already been made but the big changes
are still ahead.</p>
<p>Galen has started experimenting with userland threads, by porting the
LWKT subsystem (which is mostly self contained) to userland.</p>

<h2>Wed 27 August 2003 - Slab Allocator, __P removal</h2>
<ul>
    <li>DragonFly now has slab allocator for the kernel!  The allocator is
    about 1/3 the size of FreeBSD-5's slab allocator and features per-cpu
    isolation, mutexless operation, cache sensitivity (locality of reference),
    and optimized zeroing code.</li>
    
    <li>The core of the slab allocator is MP safe but at the moment we still
    use the malloc_type structure for statistics reporting which is not
    yet MP safe, and the backing store (KVM routines) are not MP safe.
    Even, so making the whole thing MP safe is not expected to be difficult.
	</li>
	
    <li>Robert Garrett has made great process removing __P(), Jeffrey has been
    working on the nework stack, David has been backporting bug fixes from
    FreeBSD and doing other cleanups, and Hiten and Jeroen have been
    investigating documentation and source code reorganization.</li>
</ul>

<h2>Sun 10 August 2003 - Source Reorganization</h2>
<ul>
    <li>
    A major source tree reorganization has been accomplished, including 
    separation of device drivers by functionality, moving filesystems into
    a vfs/ subdirectory, and the removal of the modules/ subdirectory with
    an intent to integrate the module makefiles into the normal sys/ 
    tree structure.</li>
    
    <li>Work on syscall messaging is ongoing and we will soon hopefully have
    a fully working demonstration of asynch messaging.</li>
</ul>

<h2>09 July 2003 to 22 July 2003 - Misc work, DEV messaging</h2>
<ul>
    <li>A large number of commits related to the messaging infrastructure have
    been made, and DEV has been message encapsulated (though it still runs
    synchronously).</li>
	
    <li>Announced the official start of work on the userland threading API:
    <a href="misc/uthread.txt">misc/uthread.txt</a></li>
</ul>

<h2>27 June 2003 to 09 July 2003 - MP operation</h2>
<p>This section contains work completed so far.  Some items have not yet
been integrated into the next section up.</p>
<ul>
	<li><b>(done)</b> Get it so user processes can run simultaniously
	    on multiple cpus.  User processes are MP, the kernel is still
	    mostly UP via the MP lock.</li>
	<li><b>(done)</b> Run normal interrupts and software interrupts
	    in their own threads.</li>
	<li><b>(done)</b> Implement interrupt preemption with a
	    block-return-to-original-thread feature (which is more
	    like BSDI and less like 5.x).</li>
	<li><b>(done)</b> Finish separating the userland scheduler
	    from LWKT.  The userland scheduler now schedules one LWKT
	    thread per cpu.  Additionally, the realtime, normal, and
	    idle priority queues work as expected and do not create
	    priority inversions in the kernel.  Implement a strict
	    priority+rr model for LWKTs and assign priorities to
	    interrupts, software interrupts, and user processes running
	    in kernel mode.  Deal with ps, systat, and top.  Fix
	    bugs in the sysctl used to retrieve processes.  Add threads
	    to the information returned by said sysctl.</li>
	<li>Replace the UIO structure with a linked list of VM objects, 
	    offsets, and ranges, which will serve for both system and
	    user I/O requests.</li>
	<li>Move kernel memory management into its own thread(s),
	    then pull in the SLAB allocator from DUX3, implement a
	    per-cpu cache, and get rid of zalloc*().</li>
	<li>Implement virtual cpus, primarily for testing purposes.</li>
	<li>(done) Separate scheduling of user processes from the
	    LWKT scheduler.
	    i.e. only schedule one user process in the LWKT scheduler at
	    a time per cpu.</li>
	<li>Change over to a flat 64 bit I/O path.</li>
	<li>(done) Get real SMP working again... implement the token
	    transfer matrix between cpus, keep the BGL for the moment but
	    physically move the count into the thread structure so it
	    doesn't add (much) to our switching overhead.</li>
	<li>Fix BUF/BIO and turn I/O initiation into a message-passing
	    subsystem.  Move all DEVices to their own threads and
	    implement as message-passing.  VFS needs a major overhaul
	    before VFS devices can be moved into their own threads to
	    the reentrant nature of VFS.</li>
</ul>

<h2>17 June 2003 to 26 June 2003 - Add light weight kernel threads to the tree.</h2>
<p>This work has been completed.  It involved creating a clearly defined
thread/process abstraction.</p>
<ul>
	<li><b>(done)</b> embed a thread structure in the proc structure.</li>
	<li><b>(done)</b> replace the curproc global with curthread, create
	    macros to mimic the old curproc in C code.</li>
	<li><b>(done)</b> Add idlethread.  curthread is never NULL now.</li>
	<li><b>(done)</b> replace the npxproc global with npxthread.</li>
	<li><b>(done)</b> Separate the thread structure from the proc structure.</li>
	<li><b>(done)</b> remove the curpcb global.  Access it via curthread.
	    ('curthread' will be the only global that needs to be
	    changed when switching threads.  Move the PCB to the end
	    of the thread stack.</li>
	<li><b>(done)</b> npxproc becomes npxthread.</li>
	<li><b>(done)</b> cleanup globaldata access.</li>
	<li><b>(done)</b> Separate the heavy weight scheduler from the thread
	    scheduler and make the low level switching function operate
	    directly on threads and only threads.  Heavy weight process
	    switching (involving things like user_ret, timestamps,
	    and so forth) will occur as an abstraction on top of the
	    LWKT scheduler.  swtch.s is almost there already.
	    The LWKT switcher only messes with basic registers and
	    ignores the things that are only required by full blown
	    processes ( debug regs, FP, common_tss, and CR3 ).  The heavy
	    weight scheduler that deals with full blown process contexts
	    handles all save/restore elements.  LWKT switch times are
	    extremely fast.</li>
	<li><b>(done)</b> change all system cals from (proc,uap) to (uap).</li>
	<li><b>(done)</b> change the device interface to take threads instead of
	    procs (d_thread_t is now a thread pointer).  Change the
	    select interface to take thread arguments instead of procs
	    (interface itself still needs to be fixed).  Consolidate
	    p_cred into p_ucred.  Consolidate p_prison into p_ucred.
	    Change suser*() to take ucreds instead of processes.</li>
	<li><b>(done)</b> Move kernel stack management to the thread structure.
	    Note: the kernel stack may not be swapped.  Move the pcb to
	    the top of the kernel stack and point to it from
	    the thread via td_pcb (similar to FreeBSD-5).</li>
	<li><b>(done)</b> Get rid of the idiotic microuptime and related
	    crap from the critical path in the switching code.
	    Implement statistical time statistics from the stat clock
	    interrupt that works for both threads and processes without
	    ruining switching performance (very necessary since there
	    is going to be far more kernel->kernel done with this
	    design, but it also gets rid of a serious eyesore that
	    has been in the FreeBSD tree for a long time).</li>
	<li><b>(done)</b> Translate most proc pointers into thread pointers
	    in the VFS and DEV subsystems (which took all day to do).</li>
	<li><b>(done)</b> Cleanup VFS and BUF.  Remove creds from calls that should
	    not pass them, such as close, getattr, vtruncbuf, fsync, and
	    createvobject.  Remove creds from struct buf.  All of the
	    above operations can be run in any context, passing the
	    originator's cred makes no sense.</li>
	<li><b>(95% done and tested)</b> Remove all use of <b>curproc</b> and
	    <b>curthread</b> from the VFS
	    and DEV subsystems in preparation for moving them to
	    serializable threads.  Add thread arguments as necessary.</li>
	<li><b>(95% done and tested)</b> Finish up using thread references
	    for all subsystems that
	    have no business talking to a process.  e.g. VM, BIO.
	    Deal with UIO_COPY.</li>
	<li><b>(done)</b>Make tsleep/wakeup work with pure threads.</li>
	<li><b>(done, needs more testing. buildworld succeeds on test box)</b>
	    Move kernel threads (which are implemented as heavy-weight
	    processes to LWKT threads).</li>
</ul>

<h2>16 June 2003 - Completed repository fork, $Tag change, and
    various cleanup.</h2>
<ul>
    <li>
    Creating a new repository required a bit of work.  First the RELENG_4
    tree had to be checked out.  Then a new cvs repository was created
    with a new cvs tag.  Then a combination of scripts and manual work
    (taking all day) was used to move the $FreeBSD tags into comments
    and add a new tag to all the source files to represent the new 
    repository.  I decided to cleanup a large number of FBSDID and
    rcsid[] declarations at the same time, basically moving all tag
    descriptions into comments with the intent that a linking support
    program would be written later to collect tags for placement in
    binaries.  Third party tags in static declarations which contained
    copyright information were retained as static declarations.</li>
	
    <li>Some minor adjustments to the syscall generator was required, and
    I also decided to physically remove UUCP from the tree.</li>
	
    <li>Finally, buildworld and buildkernel were made to work again and
    the result was checked in as rev 1.2 (rev 1.1 being the original
    RELENG_4 code).</li>
</ul>
