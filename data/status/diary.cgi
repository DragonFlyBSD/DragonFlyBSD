#!/usr/local/www/cgi-bin/tablecg
#
# $DragonFly: site/data/status/Attic/diary.cgi,v 1.22 2004/12/21 00:19:27 dillon Exp $

$TITLE(DragonFly - Big-Picture Status)

<h2>Mon 20 December 2004</h2>
<ul>
	<li>The old timeout() API has been completely ripped out and replaced
	    with the newer callout_*() API.
	<li>USB support has been synchronized with FreeBSD and NetBSD
	<li>USB keyboard detachment and reattachment while X is active no
	    longer messes up the translation mode.
	<li>Fix a keyboard lockup issue (the new callout code was not being
	    properly called in an ATKBD hack to deal with lost interrupts).
	<li><b>TCP SACK support is now considered production stable (Jeff).</b>
	<li>A TCP connection closing bug has been fixed, related to changes
	    we've made to the TCPS state values.
	<li><b>Expand TCP's header prediction case to handle common 
	    window updates (Jeff), greatly improving cpu efficiency.</b>
	<li><b>Implement tail-append to sockbufs for the TCP stack, greatly
	    improving cpu efficiencies when dealing with large TCP buffers.</b>
	<li>Lots of miscellanious network layer cleanups (Joerg).
	<li>Fix an IPv6 pcb replication issue that was creating problems
	    with e.g. Apache-2.0.
	<li>kernel event logging ported from FreeBSD (eirikn).
	<li>DCONS (console over firewire) support added (simokawa/from FreeBSD)
	<li>Additional GigE drivers added.
	<li>Major VFS messaging and interfacing progress.  The old namei()
	    and lookup() API has been completely removed.  All high level
	    layers now use the new API and run through a compatibility layer
	    to talk to VFSs which still for the most part implement the old
	    VOP calls.  Use locked namespaces to protect RENAME, REMOVE,
	    MKDIR, and other calls rather then depending on locked vnodes
	    for those protections.
	<li>More VFS work.  Keep track of the current directory with a
	    namecache pointer rather then a vnode pointer.
	<li>More VFS work.  Rewrite the vnode interlock code during 
	    deactivation and disposal.  Begin consolidating v_lock.
	<li>Adjust the boot code to operate more deterministically by always
	    using EDD (linear block number) mode first, only falling back
	    to CHS if EDD fails.  Before it would try to use CHS for
	    cylinders < 1024, resulting in non-deterministic operation on
	    modern machines.
	<li>Separate out loader configuration files for BOOTP vs non-BOOTP
	    boots.
	<li>IPSEC code moved to netproto/ipsec, update, and cleanups
	    (Pawel Biernacki).
	<li>Minor fixes to PPP.
	<li>Performance cleanup of if_em.  Fix alignment requirements to
	    reduce instances where bounce buffers are allocated (from FreeBSD). 
	<li>Kernels built with debug info are now installed with debug info,
	    greatly improving normal enduser's ability to provide useable
	    bug reports.  The backup copy of the kernel is stripped when
	    the copy is made.
	<li><b>A number of bug fixes to the VM system seem to have fixed the few
	    remaining long-term panics in DragonFly.  In particular, a very
	    serious bug in contigmalloc() inherited from FreeBSD-4.x has been
	    fixed  We now consider DragonFly as stable as FreeBSD-4.x.</b>
	<li>Many minor driver bug fixes here and there, including one to the
	    serial driver which was responsible for machine lockups in
	    certain cases.
	<li><b>Major expansion of the checkpoint code API.  You can now
	    re-checkpoint programs that have been checkpoint-restored.  
	    The system call for checkpointing and checkpoint restore
	    functions is now official.  Certain VM area issues have been
	    fixed.  And it is now possible (and easy!) to write 
	    checkpoint-aware programs.</b>
	<li>Abstract kernel structure access via libkern (joerg, from FreeBSD).
	<li>Fix a number of timer issues related to the 8254, sleep/wakeup,
	    and recovery from clock jumps due to high latencies.
	<li>Do better ESTALE checking for NFS clients to reduce instances
	    where ESTALE makes it all the way back to the application layer.
	<li>Improved polling support for UHCI USB (drhodus).
	<li>Fix /boot/loader's handling of extended DOS partitions.  There was
	    an off-by-one issue that prevented the boot loader from passing
	    the proper slice number to the kernel in certain cases (walt).
</ul>

<h2>Sat 18 September 2004</h2>
<ul>
	<li>DragonFly has adopted the 3-clause BSD license as its official
	    license.
	<li>NFS - increase the size of the nfsheur hash table as per a
	    Freenix track paper.  nfsheur is used for sequential I/O
	    heuristic in NFS.  The increase greatly improves clustering
	    under parallel NFS loads.
	<li>More VFS work: make vnode locks mandatory, get rid of nolock
	    kludges in NFS, procfs, nullfs, and many other filesystems. 
	    Remove the vnode_if.m dynamic dispatch algorithms and replace 
	    with a fixed structure (in preparation for upcoming VFS work).
	    Rewrite the VOP table parsing code.  Redo vnode/inode hash table
	    interactions to close race conditions (Matt).  
	<li>Reorganize the boot code to consolidate all fixed ORG directives
	    and other dependancies into a single header file, allowing us
	    to re position the boot code and eventually (not yet) move it
	    out of low BIOS memory.  Change the way the BTX code clears BSS
	    to make it more deterministic and more gcc3 compatible.
	<li>A major cleanup of ipfilter has been done (Hiten).
	<li>Fix a very old but serious bug in the VM system that could result
	    in corrupt user memory when MADV_FREE is used (from Alan Cox).
	<li>Rewrite most of the MBUF allocator and support code.  Get rid
	    of mb_map and back the mbuf allocator with malloc.  Get rid of
	    the old-style m_ext support and replace with new style m_ext
	    support (which is somewhat different then FreeBSD's new code).
	    Cleanup sendfile() to use the new m_ext callback scheme.
	<li>Lots minor/major bug fixes, cleanups, new driver support, etc.
	<li>More GCC3 work.  The system mostly compiles and runs GCC3 builds
	    but the boot/loader code still has some issues (Various people).
	<li>Work on the userland schedule.  Introduce an 'interactivity'
	    measure in an attempt to do a better job assigning time slices.
	    Fix some scheduler interaction bugs which were sometimes resulting
	    in processes being given a full 1/10 second time slice when they
	    shouldn't be.
	<li>A major import of the FreeBSD-5 802_11 infrastructure has been
	    accomplished (Joerg).
	<li>NDis has been ported from FreeBSD, giving DragonFly access to
	    many more 802.11 devices via windoz device drivers (Matt).
	<li>Add thermal control circuit support (Asmodai).
	<li>Generally add throttling support to the system.
	<li><b>Add TCP SACK support (Jeffrey Hsu).  This is still considered
	    experimental.</b>
	<li>Make the syncache per-cpu and dispatch syncache timer events 
	    via LWKT messages directly to the appropriate protocol thread
	    (Jeffrey Hsu).  This removes all race conditions from the syncache
	    code and makes it 95% MP safe.
	<li><b>Greatly reduce the number of ACKs generated on a TCP connection
	    going full-out over a GigE (or other fast) interface by delaying
	    the sending of the ACK until all protocol stack packets have been
	    processed.  Since GiGE interfaces tend to aggregate 8-12(+) 
	    received packets per interrupt, this can cut the ACK rate by 75%
	    (one ack per 8-12 packets instead of one ack per 2 packets), and
	    it does it without violating the TCP spec.  The code takes 
	    advantage of the protocol thread abstraction used to process
	    TCP packets.  (Matt)</b>
	<li><b>Greatly reduce the number of pure window updates that occur over
	    a high speed (typ GigE) TCP connection by recognizing that a
	    pure window update is not always necessary when userland has
	    drained the TCP socket buffer.  (Jeffrey Hsu)</b>
	<li>Rewrite the callout_*() core infrastructure and rip out the old
	    [un]timeout() API (saving ~800K+ of KVM in the process).  The new
	    callout infrastructure uses a DragonFly-friendly per-cpu 
	    implementation and is able to guarentee that callouts will occur
	    on the same cpu they were registered on, a feature that the TCP
	    protocol stack threads are going to soon take major advantage of.
	<li>Cleanup the link layer broadcast address, consolidating many 
	    separate implementations into one ifnet-based implementation
	    (Joerg).
	<li>BUF/BIO progress - start working the XIO vm_page mapping code
	    into the system buffer cache (Hiten).  Remove b_caller2 and
	    b_driver2 field members from the BUF structure (Hiten) (generally
	    we are trying to remove the non-recursive-friendly driver 
	    specific fields from struct buf and friends).
	<li>The release went well!  There were a few gotchas, such as trying
	    to run dual console output to the serial port causing problems on
	    laptops which did not have serial ports.  A bug in the installer
	    was serious enough to have to go to an '1.0A' release a day or two
	    after the 1.0 release.  But, generally speaking, the release did its
	    job!
	<li>More USB fixes.  Clean up some timer races in USB/CAM interactions
	    related to pulling out USB mass storage cards.  Fix a serious bug
	    that could lead to lost transactions and create confusion between
	    the USB code and the device.
	<li>Async syscall work: clean up the sendsys2() syscall API into
	    something that's a bit more reasonable (Eirik Nygaard)
	<li>Add a generic framework for IOCTL mapping (Simon).
	<li>Add VESA mode support, giving us access to bitmapped VESA video
	    modes (Sascha Wildner).
	<li>Fix USB keyboard support by giving the USB keyboard preference
	    even if a normal keyboard is detected earlier in the boot process.
	    This is necessary due to hardware/firmware level PS/2 keyboard
	    emulation that many USB chipsets and BIOSes offer.
	<li><b>Installer Updated: Lots of bug fixes have been made.</b>
	<li>Stability: Spend two weeks stabilizing recent work in preparation
	    for another big push.
</ul>

<h2>Sun 11 July 2004</h2>
<ul>
	<li>Master ISO for the 1.0-RELEASE is now on the FTP site, we
	    release on Monday 12-July-2004.
	<li><b>Major revamping of the boot code.  Support dual-console mode
	    (video and serial).  Properly detect missing serial ports
	    (common for laptops).  Initialize the serial port to 96008N1.</b>
	<li>Bring ACPICA5 uptodate and integrate it into the build as a
	    KLD, just like FreeBSD-5.  Fix additional issues as well.
	    Nearly all of this work was done by YONETANI Tomokazu.  Use
	    the latest INTEL code (20040527).   This greatly improves
	    our laptop support.
	<li>Make the kernel check both CDRom drives for a root filesystem
	    when booted with -C, which allows the boot CD to work in either
	    drive on systems with two drives.
	<li><b>Integrate the DragonFly Installer into the release build.  The
	    DragonFly Installer is a from-scratch design by Chris Pressey,
	    Devon O'Dell, Eirik Nygaard, Hiten Pandya & GeekGod (aka
	    Scott Ullrich).  The design incorporates a worker backend and
	    multiply targetable frontends.  Currently the console frontend
	    is enabled, but there is also a CGI/WWW frontend which is on the
	    CD but still considered highly experimental.</b>
	<li>Fix a bug in the polling backoff code for the VR device
	<li>Implement interrupt livelock detection.  When an interrupt
	    livelock is detected the interrupt thread automatically 
	    throttles itself to a (sysctl settable) rate.  When the interrupt
	    rate drops to half the throttle limit the throttling is removed.
	    This greatly improves debugability, especially on laptops with
	    misrouted interrupts.  While it doesn't necessarily fix the 
	    broken devices it does tend to allow the system to continue to
	    operate with those devices that *do* still work.
	<li>Properly probe for the existance of the serial port in the
	    kernel, which allows us to run a getty on ttyd0 by default for
	    the release CD.  Prior to this fix attempting to use ttyd0 would
	    result in a system deadlock.
	<li>Bring in some pccard driver improvements from FreeBSD, including
	    increasing the CIS area buffer from 1K to 4K and properly 
	    range-checking the CIS parser (which avoids panics and crashes).
	<li>Update our DragonFly Copyright and update date specifications
	    on a number of copyrights.
	<li>Add support for randomized ephermal source ports.
	<li>Do some network driver cleanups.. basically move some common code
	    out of the driver and into ether_ifattach() (work by Joerg).
	<li>Try to be more compatible with laptop touchpads whose aux ports
	    return normally illegal values (Eirik Nygaard).
	<li>Add common functions for computing the ethernet CRC, work 
	    by Joerg, taken from NetBSD.
	<li>Add support for additional AGP bridges - taken from FreeBSD-5.
	<li>Add support for the 're' network device.
	<li>Bring OHCI and EHCI up-to-date with NetBSD.
	<li>Miscellaneous driver fixes to ips, usb (ugen), sound support.
	<li>Miscellaneous Linux emulation work, by David Rhodus, taken
	    from FreeBSD-SA-04:13.linux.
</ul>

<h2>Sun 27 June 2004</h2>
<ul>
	<li>We are going to release 1.0RC1 tonight.
	<li>Implement interrupt livelock detection and rate limiting.  It's
	    still a bit raw, but it does the job (easily testable by plugging
	    and unplugging cardbus cards at a high rate).  The intent is to
	    try to make the system more survivable from interrupt routing and
	    other boot-time configuration issues, and badly written drivers.
	<li>Implement an emergency polling mode for the VR device if its
	    interrupt does not appear to be working.
	<li>Implement dual console (screen and serial port) support by
	    default for boot2 and the loader.
</ul>

<h2>Mon 21 June 2004</h2>
<ul>
	<li>Joerg has brought GCC-3.4-20040618 in and it is now hooked up
	    to the build.  GCC-3.3 will soon be removed.  To use GCC-3.4
	    'setenv CCVER gcc34'.
	<li>The world and kernel now builds with gcc-3.4.  The kernel builds
	    and runs with gcc-3.4 -O3, but this is not an officially supported
	    configuration.
	<li>More M_NOWAIT -> M_INTWAIT work.  Most of the drivers inherited
	    from FreeBSD make aweful assumptions about M_NOWAIT mallocs
	    which cause them to break under DragonFly.
	<li>/usr/bin/ps now reports system thread startup times as the boot
	    time instead of as Jan-1-1970.  The p_start field in pstats has
	    been moved to the thread structure so threads can have a start
	    time in the future - Hiten.
	<li>Zero the itimers on fork() - Hiten / SUSv3 compliance.
	<li>MMX/XMM kernel optimizations are now on by default, greatly
	    improving bcopy/bzero/copyin/copyout performance for large (>4K)
	    buffers.
	<li>A number of revoke() related panics have been fixed, in particular
	    related to 'script' and other pty-using programs.
	<li>The initial MSFBUF scheme (multi-page cached linear buffers)
	    has been committed and is now used for NFS requests.  This is
	    part of the continuing work to eventually make I/O devices
	    responsible for any KVM mappings (because most just set up DMA
	    and don't actually have to make any) - Hiten.
	<li>Continuing ANSIfication work by several people - Chris Pressey.
	<li>Continuing work on the LWKT messaging system.  A number of bugs
	    in the lwkt_abortmsg() path have been fixed.
	<li>The load average is now calculated properly.  Thread sleeps were
	    not being accounted for properly.
	<li>Bring in a number of changes from FreeBSD-5: try the elf image
	    activator first.
	<li>Fix a number of serious ref-counting and ref holding bugs in
	    procfs as part of our use of XIO in procfs - GeekGod and Matt
	<li>Use network predicates for accept() and connect() (using the
	    new message abort functionality to handle PCATCH) - Jeff.
	<li>Convert netproto/ns to use the pr_usrreqs structure.
	<li>Implement markers for traversing PCB lists to fix concurrency
	    problems with sysctl.
	<li>Implement a lwkt_setcpu() API function which moves a thread
	    to a particular cpu.  This will be used by sysctl to iterate
	    across cpus when collecting per-cpu structural information.
	<li>Redo netstat to properly iterate the pcb's across all cpus.
	<li>Significant mbuf cleanup - dtom() has now been removed, and
	    a normal malloc() is used for PCB allocations and in other places
	    where mbufs were being abused for structural allocations.
	<li>dup_sockaddr() now unconditionally uses M_INTWAIT instead of
	    conditionally using M_NOWAIT, making it more reliable.
	<li>Fix a number of USB device ref counting issues and fix issues
	    related to UMASS detaching from CAM while CAM is still active,
	    and vice-versa.
	<li>Optimize kern_getcwd() some to avoid a string shifting bcopy().
	<li>Continued work on asynch syscalls - track pending system calls
	    and make exit1() wait for them (abort support will be forthcoming).
	<li><b>Add a negative lookup cache for NFS.</b>  This makes a huge
	    difference for things like buildworlds where /usr/src is NFS
	    mounted, reducing (post cached) network bandwidth to 1/10 what
	    it was before.
	<li>Add the '-l' option to the 'resident' command, listing all
	    residented programs and their full paths (if available).  -Hiten.
	<li><b>Implement the 'rconfig' utility (see the manual page)</b> - for
	    automatic search/config-script downloading and execution, which
	    makes installing a new DFly box from CDBoot a whole lot easier
	    when you are in a multi-machine environment.
	<li>Revamp the BIO b_dev assignment and revamp the 'disk' layer.
	    Instead of overloading the raw disk device the disk layer now
	    creates a new device on top of the raw disk device and takes over
	    the (user accessible) CDEV (major,minor).  The disk layer does
	    its work and reassigned b_dev to the raw device.  biodone() now
	    unconditionally setes b_dev to NODEV and all I/O ops are required
	    to initialize b_dev prior to initiating the op.  This is
	    precursor work to our DEV layering and messaging goal.
	<li>Fix the rootfs search to specify the correct unit number rather
	    then using unit 0, because CDEVSW lookups now require a valid
	    minor number (CDEVSW's are now registered with a minor number
	    range and the same major number can be overloaded as long as the
	    minor ranges do not conflict).  Fix by Hiroki Sato.
	<li>Fix a wiring related page table memory leak in the VM system.
	<li>Fix a number of ^T related panics.
	<li>Properly ref-count all devices.
</ul>

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
	<li>Major continuing work by Jeff on the threading and partitioning
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
	<li>A longstanding bug in PCI bus assignments which affects larger
	    servers has been fixed.</li>
	<li>The IP checksum code has been rewritten and most of it has been
	    moved to machine-independent sections.</li>
	<li>A general machine-independent CPU synchronization and rendezvous
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
