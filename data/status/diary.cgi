#!/usr/local/www/cgi-bin/tablecg
#
# $DragonFly: site/data/status/Attic/diary.cgi,v 1.3 2003/08/27 17:34:44 dillon Exp $

$TITLE(DragonFly - Big-Picture Status)

<P>
<LI>Thru 27 August 2003 - Slab Allocator, __P removal
<UL>
    <P>
    DragonFly now has slab allocator for the kernel!  The allocator is
    about 1/3 the size of FreeBSD-5's slab allocator and features per-cpu
    isolation, mutexless operation, cache sensitivity (locality of reference),
    and optimized zeroing code.
    <P>
    The core of the slab allocator is MP safe but at the moment we still
    use the malloc_type structure for statistics reporting which is not
    yet MP safe, and the backing store (KVM routines) are not MP safe.
    Even, so making the whole thing MP safe is not expected to be difficult.
    <P>
    Robert Garrett has made great process removing __P(), Jeffrey has been
    working on the nework stack, David has been backporting bug fixes from
    FreeBSD and doing other cleanups, and Hiten and Jeroen have been
    investigating documentation and source code reorganization.
</UL>
<P>
<LI>Thru 10 August 2003 - Source Reorganization
<UL>
    <P>
    A major source tree reorganization has been accomplished, including 
    separation of device drivers by functionality, moving filesystems into
    a vfs/ subdirectory, and the removal of the modules/ subdirectory with
    an intent to integrate the module makefiles into the normal sys/ 
    tree structure.
    <P>
    Work on syscall messaging is ongoing and we will soon hopefully have
    a fully working demonstration of asynch messaging.
</UL>
<LI>09 July 2003 to 22 July 2003 - Misc work, DEV messaging
<UL>
    <P>
    A large number of commits related to the messaging infrastructure have
    been made, and DEV has been message encapsulated (though it still runs
    synchronously).
    <P>
    Announced the official start of work on the userland threading API:
    <A HREF="misc/uthread.txt">misc/uthread.txt</A>
</UL>
<LI>27 June 2003 to 09 July 2003 - MP operation
<UL>
    <P>
    This section contains work completed so far.  Some items have not yet
    been integrated into the next section up.
    <UL>
	<P>
	<LI><B>(done)</B> Get it so user processes can run simultaniously
	    on multiple cpus.  User processes are MP, the kernel is still
	    mostly UP via the MP lock.
	<LI><B>(done)</B> Run normal interrupts and software interrupts
	    in their own threads.
	<LI><B>(done)</B> Implement interrupt preemption with a
	    block-return-to-original-thread feature (which is more
	    like BSDI and less like 5.x).
	<LI><B>(done)</B> Finish separating the userland scheduler
	    from LWKT.  The userland scheduler now schedules one LWKT
	    thread per cpu.  Additionally, the realtime, normal, and
	    idle priority queues work as expected and do not create
	    priority inversions in the kernel.  Implement a strict
	    priority+rr model for LWKTs and assign priorities to
	    interrupts, software interrupts, and user processes running
	    in kernel mode.  Deal with ps, systat, and top.  Fix
	    bugs in the sysctl used to retrieve processes.  Add threads
	    to the information returned by said sysctl.
	<LI>Replace the UIO structure with a linked list of VM objects, 
	    offsets, and ranges, which will serve for both system and
	    user I/O requests.
	<LI>Move kernel memory management into its own thread(s),
	    then pull in the SLAB allocator from DUX3, implement a
	    per-cpu cache, and get rid of zalloc*().
	<LI>Implement virtual cpus, primarily for testing purposes.
	<LI>(done) Separate scheduling of user processes from the 
	    LWKT scheduler.
	    i.e. only schedule one user process in the LWKT scheduler at
	    a time per cpu.
	<LI>Change over to a flat 64 bit I/O path.
	<LI>(done) Get real SMP working again... implement the token
	    transfer matrix between cpus, keep the BGL for the moment but
	    physically move the count into the thread structure so it
	    doesn't add (much) to our switching overhead.
	<LI>Fix BUF/BIO and turn I/O initiation into a message-passing
	    subsystem.  Move all DEVices to their own threads and
	    implement as message-passing.  VFS needs a major overhaul
	    before VFS devices can be moved into their own threads to
	    the reentrant nature of VFS.
    </UL>
</UL>
<P>
<LI>17 June 2003 to 26 June 2003 - Add light weight kernel threads to the tree.
<UL>
    <P>
    This work has been completed.  It involved creating a clearly defined
    thread/process abstraction.
    <UL>
	<P>
	<LI><B>(done)</B> embed a thread structure in the proc structure
	<LI><B>(done)</B> replace the curproc global with curthread, create
	    macros to mimic the old curproc in C code.
	<LI><B>(done)</B> Add idlethread.  curthread is never NULL now.
	<LI><B>(done)</B> replace the npxproc global with npxthread.
	<LI><B>(done)</B> Separate the thread structure from the proc structure
	<LI><B>(done)</B> remove the curpcb global.  Access it via curthread.
	    ('curthread' will be the only global that needs to be
	    changed when switching threads.  Move the PCB to the end
	    of the thread stack.
	<LI><B>(done)</B> npxproc becomes npxthread
	<LI><B>(done)</B> cleanup globaldata access
	<LI><B>(done)</B> Separate the heavy weight scheduler from the thread
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
	    extremely fast.
	<LI><B>(done)</B> change all system cals from (proc,uap) to (uap).
	<LI><B>(done)</B> change the device interface to take threads instead of
	    procs (d_thread_t is now a thread pointer).  Change the
	    select interface to take thread arguments instead of procs
	    (interface itself still needs to be fixed).  Consolidate
	    p_cred into p_ucred.  Consolidate p_prison into p_ucred.
	    Change suser*() to take ucreds instead of processes.
	<LI><B>(done)</B> Move kernel stack management to the thread structure.
	    Note: the kernel stack may not be swapped.  Move the pcb to
	    the top of the kernel stack and point to it from
	    the thread via td_pcb (similar to FreeBSD-5).
	<LI><B>(done)</B> Get rid of the idiotic microuptime and related
	    crap from the critical path in the switching code.
	    Implement statistical time statistics from the stat clock
	    interrupt that works for both threads and processes without
	    ruining switching performance (very necessary since there
	    is going to be far more kernel->kernel done with this
	    design, but it also gets rid of a serious eyesore that
	    has been in the FreeBSD tree for a long time).
	<LI><B>(done)</B> Translate most proc pointers into thread pointers
	    in the VFS and DEV subsystems (which took all day to do).
	<LI><B>(done)</B>Cleanup VFS and BUF.  Remove creds from calls that should
	    not pass them, such as close, getattr, vtruncbuf, fsync, and
	    createvobject.  Remove creds from struct buf.  All of the
	    above operations can be run in any context, passing the
	    originator's cred makes no sense.
	<LI><B>(95% done and tested)</B> Remove all use of 'curproc' and
	    'curthread' from the VFS
	    and DEV subsystems in preparation for moving them to
	    serializable threads.  Add thread arguments as necessary.
	<LI><B>(95% done and tested)</B> Finish up using thread references
	    for all subsystems that
	    have no business talking to a process.  e.g. VM, BIO.
	    Deal with UIO_COPY.
	<LI><B>(done)</B>Make tsleep/wakeup work with pure threads
	<LI><B>(done, needs more testing. buildworld succeeds on test box)</B>
	    Move kernel threads (which are implemented as heavy-weight
	    processes to LWKT threads).
    </UL>
</UL>
<P>
<LI>16 June 2003 - Completed repository fork, $Tag change, and 
    various cleanup.
<UL>
    <P>
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
    copyright information were retained as static declarations.
    <P>
    Some minor adjustments to the syscall generator was required, and
    I also decided to physically remove uucp from the tree.
    <P>
    Finally, buildworld and buildkernel were made to work again and
    the result was checked in as rev 1.2 (rev 1.1 being the original
    RELENG_4 code).
</UL>
