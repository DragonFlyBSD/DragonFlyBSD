#!/usr/local/www/cgi-bin/tablecg
#
# $DragonFly: site/data/docs/Attic/goals.cgi,v 1.1 2006/03/29 01:47:35 justin Exp $

$TITLE(DragonFly - Documentation)

<h1>DragonFly BSD Design Goals</h1>

<p>
Table of contents:
<ul>
<li><a href="#caching">Caching</a>
<li><a href="#iomodel">I/O Model</a>
<li><a href="#packages">Packages</a>
<li><a href="#messaging">Messaging</a>
<li><a href="#threads">Threads</a>
<li><a href="#userapi">User API</a>
<li><a href="#vfsmodel">VFS Model</a>
</ul>
</p>

<hr>

<a name="intro"><h2>What is it?</h2></a>
<p>
DragonFly is going to be a multi-year project.  It will take a lot of groundwork
to even approach the goals we outline here.  By checking our various goal 
links, you can bring up position papers on various nitty-gritty aspects
of kernel and system design which the project hopes to accomplish.</p>
<p>
First and foremost among all of our goals is a desire to be able to
implement them in small bite-sized chunks, while at the same time maintaining
good stability for the system as a whole.  While the goals are not listed
in any particular order, there is a natural order to things which should allow
us to advance piecemeal without compromising the stability of the
system as a whole.  It's a laudable goal that will sit foremost in our
minds, even though we know it is probably not 100% achievable.  The messaging
system is going to be key to the effort.  If we can get that in-place we
will have an excellent (and debuggable) API on top of which the remainder of
the work can be built.</p>


<a name="caching"><h2>Caching Infrastructure Overview</h2></a>
<p>
Our goal is to create a flexible dual-purpose caching infrastructure which
mimics the well known and mature MESI (Modified Exclusive Shared Invalid)
model over a broad range of configurations.  The primary purpose of this
infrastructure will be to protect I/O operations and live memory mappings.
For example, a range-based MESI model would allow multiple processes to
simultaneously operate both reads and writes on different portions of a single
file.  If we implement the infrastructure properly we can extend it into
a networked-clustered environment, getting us a long ways towards achieving
a single-system-image capability.</p>
<p>
Such a caching infrastructure would, for example, protect a write() from a
conflicting ftruncate(), and would preserve atomicy between read() and
write().  The same caching infrastructure would actively invalidate or
reload memory mappings, effectively replacing most of what VNODE locking
is used for now.</p>
<p>
The contemplated infrastructure would utilize two-way messaging and focus
on the VM Object rather than the VNode as the central manager of cached
data.   Some operations, such as a read() or write(), would obtain the
appropriate range lock on the VM object, issue their I/O, then release the
lock.  Long-term caching operations might collapse ranges together to
bound the number of range locks being maintained, which allows the 
infrastructure to maintain locks between operations in a scaleable fashion.
In such cases cache operations such as invalidation or, say,
Exclusive->Shared transitions, would generate a message to the holding 
entity asking it to downgrade or release its range lock.
In otherwords, the caching system being contemplated is 
an <i>actively managed</i> system.</p>

<a name="iomodel"><h2>New I/O Device Model</h2></a>
<p>
I/O is considerably easier to fix than VFS owing to the fact that
most devices operate asynchronously already, despite having a semi-synchronous
API.  The I/O model being contemplated consists of three major pieces of work:</p>

<ol>
<li>I/O Data will be represented by ranges of VM Objects instead of ranges of
system or user addresses.  This allows I/O devices to operate entirely
independently of the originating user process.</li>

<li>Device I/O will be handled through a port/messaging system.
(See 'messaging' goal.)</li>

<li>Device I/O will typically be serialized through one or more threads.
Each device will typically be managed by its own thread but certain high
performance devices might be managed by multiple threads (up to one per cpu).
Multithreaded devices would not necessarily compete for resources.  For
example, the TCP stack could be multithreaded with work split up by target
port number mod N, and thus run on multiple threads (and thus multiple cpus)
without contention.</li>
</ol>

<p>
As part of this work I/O messages will utilize a flat 64 bit byte-offset
rather than block numbers.</p>
<p>
Note that device messages can be acted upon synchronously by the device.
Do not make the mistake of assuming that messages are unconditionally
serialized to the device thread because they aren't.  See the messaging
section for more information.</p>
<p>
It should also be noted that the device interface is being designed with
the flexibility to allow devices to operate as user processes rather than
as kernel-only threads.  Though we probably will not achieve this capability
for some time, the intention is to eventually be able to do it.  There are
innumerable advantages to being able to transparently pull things like 
virtual block devices and even whole filesystems into userspace.</p>

<a name="packages"><h2>Dealing with Package Installation</h2></a>
<p>
Applications are such a god-awful mess these days that it is hard to come
up with a packaging and installation system that can achieve seamless
installation and flawless operation.  We have come to the conclusion that
the crux of the problem is that even seemingly minor updates to third
party libraries (that we have no control over) can screw up an
already-installed application.  A packaging system <em>CAN</em> walk the
dependancy tree and upgrade everything that needs upgrading.  The
problem is that the packaging system might not actually have a new
version of the package or packages that need to be upgraded due to some
third party library being upgraded.</p>
<p>
We need to have the luxury and ability to upgrade only the particular
package we want, without blowing up applications that depend on said package.
This isn't to say that it is desirable.  Instead we say that it is 
necessary because it allows us to do piecemeal upgrades (as well as
piecemeal updates to the packaging system's database itself)
without having to worry about blowing up other things in the process.
<i>Eventually</i> we would synchronize everything up, but there could be
periods of a few days to a few months where a few packages might not be,
and certain very large packages could wind up depending on an old version
of some library for a very long time.  We need to be able to support that.
We also need to be able to support versioned support/configuration directories
that might be hardwired by a port.  Whenever such conflicts occur, the
packaging system needs to version the supporting directories as well. 
If two incompatible versions of package X both need /usr/local/etc/X
we would wind up with /usr/local/etc/X:VERSION1 and /usr/local/etc/X:VERSION2.
</p>
<p>
It is possible to accomplish this goal by explicitly versioning
dependancies and tagging the package binary with an 'environment'...  A
filesystem overlay, you could call it, which applies to supporting
directories like /usr/lib, /usr/local/lib, even /usr/local/etc, which 
makes only the particular version of the particular libraries
and/or files the package needs visible to it.  Everything else would
be invisible to that package.  By enforcing visibility you would 
know very quickly if you specified your 
package dependancies incorrectly, because your package would not
be able to find incorrectly placed libraries or supporting files,  
because they were not made accessible when the package was installed.
For example, if the package says a program depends on version 1.5 of the
ncurses library, then version 1.5 is all that would be visible to the program
(it would appear as just libncurses.* to the program).</p>
<p>
With such a system we would be able to install multiple versions of anything
whether said entities supported fine-grained version control or not, and
even if (in a normal sytem) there would be conflicts with other entities.
The packaging system would be responsible for tagging the binaries and
the operating system would be responsible for the visibility issues.  The
packaging system or possibly even just a cron job would be responsible for
running through the system and locating all the 'cruft' that is removable
after you've updated all the packages that used to depend on it.</p>
<p>
Another real advantage of enforced visibility is that it provides us
with proof-positive that a package does or does not need something.  We
would not have to rely on the packaging system to find out what the
dependancies were; we could just look at the environment tagged to the
binary!</p>

<a name="messaging"><h2>The Port/Messaging Model</h2></a>
<p>
DragonFly will have a lightweight port/messaging API to go along with its
lightweight kernel threads.  The port/messaging API is very simple
in concept; You construct a message, you send it to a target port, and
at some point later you wait for a reply on your reply port.  On this
simple abstraction, we intend to build a high level of capability and
sophistication.  To understand the capabilities of the messaging system,
you must first understand how a message is dispatched.  It basically works
like this:</p>
<pre>
	fubar()
	{
	    FuMsg msg;
	    initFuMsg(&amp;msg, replyPort, ...);
	    error = targetPort->mp_SendMsg(&amp;msg);
	    if (error == EASYNC) {
		  /* now or at some later time, or wait on reply port */
		  error = waitMsg(&amp;msg);	
	    }
	}
</pre>
<p>
The messaging API will wrap this basic mechanism into synchronous and
asynchronous messaging functions.  For example, lwkt_domsg() will send
a message synchronously and wait for a reply.  It will set a flag to hint
to the target port that the message will be blocked on synchronously and
if the target port returns EASYNC, lwkt_domsg() will block.  Likewise
lwkt_sendmsg() would send a message asynchronously, but if the target port
returns a synchronous error code (i.e. anything not EASYNC) lwkt_sendmsg()
will manually queue the now complete message on the reply port itself.</p>
<p>
As you may have guessed, the target port's mp_SendMsg() function has total
control over how it deals with the message.  Regardless of any hints passed
to it in the messaging flags, the target port can decide to act on the
message synchronously (in the context of the caller) and return, or it may
decide to queue the message and return EASYNC.  Messaging operations generally
should not 'block' from the point of view of the initiator.  That is, the
target port should not try to run the message synchronously if doing so would
cause it to block.  Instead, it should queue it to its own thread (or to the
message queue conveniently embedded in the target port structure itself) and
return EASYNC.</p>
<p>
A target port might act on a message synchronously for any
number of reasons.  It is in fact precisely the mp_sendMsg() function for
the target port which deals with per-cpu caches and opportunistic locking
such as try_mplock() in order to deal with the request without having to
resort to more expensive queueing / switching.</p>
<p>
The key thing to remember here is that our best case optimization is direct
execution by mp_SendMsg() with virtually no more overhead then a simple
subroutine call would otherwise entail.  No queueing, no messing around
with the reply port...  If a message can be acted upon synchronously, then we
are talking about an extremely inexpensive operation.  It is this key feature
that allows us to use a messaging interface by design without having to worry
about performance issues.  We are explicitly NOT employing the type of
sophistication that, say, Mach uses.  We are not trying to track memory
mappings or pointers or anything like that, at least not in the low level
messaging interface.  User&lt;-&gt;Kernel messaging interfaces simply employ 
mp_SendMsg() function vectors which do the appropriate translation, so as
far as the sender and recipient are concerned the message will be local to
their VM context.</p>


<a name="threads"><h2>The Light Weight Kernel Threading Model</h2></a>
<p>
DragonFly employs a light weight kernel threading (LWKT) model at its core.
Each process in the system has an associated thread, and most kernel-only
processes are in fact just pure threads.  For example, the pageout daemon
is a pure thread and has no process context.</p>
<p>
The LWKT model has a number of key features that can be counted on no matter
the architecture.  These features are designed to remove or reduce contention
between cpus.</p>
<ol>
    <li><p>
    Each cpu in the system has its own self-contained LWKT scheduler.
    Threads are locked to their cpus by design and can only be moved to other
    cpus under certain special circumstances.  Any LWKT scheduling operation 
    on a particular cpu is only directly executed on that cpu.
    This means that the core LWKT scheduler can schedule, deschedule, 
    and switch between threads within a cpu's domain without any locking 
    whatsoever.  No MP lock, nothing except a simple critical section.</p></li>
    <li><p>
    A thread will never be preemptively moved to another cpu while it is
    running in the kernel, a thread will never be moved between cpus while 
    it is blocked.  The userland scheduler may migrate a thread that is
    running in usermode.  A thread will never be preemptively switched
    to a non-interrupt thread.  If an interrupt thread preempts the current
    thread, then the moment the interrupt thread completes or blocks, the
    preempted thread will resume regardless of its scheduling state.  For
    example, a thread might get preempted after calling
    lwkt_deschedule_self() but before it actually switches out.  This is OK
    because control will be returned to it directly after the interrupt
    thread completes or blocks.</p></li>
    <li><p>
    Due to (2) above, a thread can cache information obtained through the
    per-cpu globaldata structure without having to obtain any locks and, if
    the information is known not to be touched by interrupts, without having to
    enter a critical section.  This allows per-cpu caches for various types of
    information to be implemented with virtually no overhead.</p></li>
    <li><p>
    A cpu which attempts to schedule a thread belonging to another cpu
    will issue an IPI-based message to the target cpu to execute the operation.
    These messages are asynchronous by default and while IPIs may entail some
    latency, they don't necessary waste cpu cycles due to that fact.  Threads
    can block such operations by entering a critical section and, in fact,
    that is what the LWKT scheduler does.  Entering and exiting a critical 
    section are considered to be cheap operations and require no locking
    or locked bus instructions to accomplish.</p></li>
    <li><p>
    The IPI messaging subsystem
    deals with FIFO-full deadlocks by spinning and processing the incoming
    queue while waiting for its outgoing queue to unstall.  The IPI messaging
    subsystem specifically does not switch threads under these circumstances
    which allows the software to treat it as a non-blocking API even though
    some spinning might occassionally occur.</p></li>
</ol>
<p>
In addition to these key features, the LWKT model allows for both FAST
interrupt preemption <em>AND</em> threaded interrupt preemption.  FAST 
interrupts may preempt the current thread when it is not in a critical section.
Threaded interrupts may also preempt the current thread.  The LWKT system
will switch to the threaded interrupt and then switch back to the original
when the threaded interrupt blocks or completes.  IPI functions operate 
in a manner very similar to FAST interrupts and have the same trapframe
capability.  This is used heavily by DragonFly's SYSTIMERS API to distribute
hardclock() and statclock() interrupts to all cpus.</p>

<h3>The IPI Messaging Subsystem</h3>
<p>
The LWKT model implements an asynchronous messaging system for communication
between cpus.  Basically you simply make a call providing the target cpu with
a function pointer and data argument which the target cpu executes 
asynchronously.  Since this is an asynchronous model the caller does not wait
for a synchronous completion, which greatly improves performance, and the 
overhead on the target cpu is roughly equivalent to an interrupt.</p>
<p>
IPI messages operate like FAST Interrupts... meaning that they preempt 
whatever is running on the target cpu (subject to a critical section), run,
and then whatever was running before resumes.  For this reason IPI functions
are not allowed to block in any manner whatsoever.  IPI messages are used
to do things like schedule threads and free memory belonging to other cpus.</p>
<p>
IPI messaging is used heavily by at least half a dozen major LWKT
subsystems, including the per-cpu thread scheduler, the slab allocator,
and messaging subsystems.  Since IPI messaging is a DragonFly-native
subsystem, it does not require and does not use the Big Giant Lock. 
All IPI based functions must therefore be MP-safe (and they are).</p>

<h3>The IPI-based CPU Synchronization Subsystem</h3>
<p>
The LWKT model implements a generalized, machine independent cpu
synchronization API.  The API may be used to place target cpu(s) into a 
known state while one is operating on a sensitive data structure.  This
interface is primarily used to deal with MMU pagetable updates.  For
example, it is not safe to check and clear the modify bit on a page table
entry and then remove the page table entry, even if holding the proper lock.
This is because a userland process running on another cpu may be accessing or
modifying that page, which will create a race between the TLB writeback on the
target cpu and your attempt to clear the page table entry.   The proper
solution is to place all cpus that might be able to issue a writeback
on the page table entry (meaning all cpus in the pmap's pm_active mask)
into a known state first, then make the modification, then release the cpus
with a request to invalidate their TLB.</p>
<p>
The API implemented by DragonFly is deadlock-free.  Multiple cpu
synchronization activities are allowed to operate in parallel and this 
includes any threads which are mastering a cpu synchronization event for
the duration of mastering.  Even with this flexibility, since the cpu 
synchronization interface operates in a controlled environment the callback
functions tend to work just like the callback functions used in the
IPI messaging subsystem.</p>

<h3>Serializing Tokens</h3>
<p>
A serializing token may be held by any number of threads simultaneously.
A thread holding a token is guaranteed that no other thread also
holding that same token will be running at the same time.</p>

<p>A thread may hold any number of serializing tokens.</p>

<p>A thread may hold serializing tokens through a thread yield or blocking
condition, but must understand that another thread holding those tokens
may be allowed to run while the first thread is not running (blocked or
yielded away).</p>

<p>There are theoretically no unresolvable deadlock situations that can
arise with the serializing token mechanism.  However, the initial
implementation can potentially get into livelock issues with multiply
held tokens.</p>

<p>Serializing tokens may also be used to protect threads from preempting
interrupts that attempt to obtain the same token.  This is a slightly
different effect from the Big Giant Lock (also known as the MP lock),
which does not interlock against interrupts on the same cpu.  <i>It is
important to note that token atomicity is maintained through preemptive
conditions, even though preemption involves a temporary switch to another
thread.  It is not necessary to enter a spl() level or critical section
to preserve token atomicity</i>.
</p>

<p>Holding a serializing token does <b>not</b> prevent preemptive interrupts
from occuring, though it might cause some of those interrupts to 
block-reschedule.  Unthreaded FAST and IPI messaging interrupts are not
allowed to use tokens as they have no thread context of their own to operate
in.  These subsystems are instead interlocked through the use of critical
sections.</p>

<a name="userapi"><h2>Creating a Portable User API</h2></a>
<p>
Most standard UNIX systems employ a system call table through which many types
of data, including raw structures, are passed back and forth.   The biggest
obstacle to the ability for user programs to interoperate with kernels
which are older or newer than themselves is the fact that these raw structures
often change.  The worst offenders are things like network interfaces, route
table ioctls, ipfw, and raw process structures for ps, vmstat, etc.  But even
nondescript system calls like stat() and readdir() have issues.  In more
general terms the system call list itself can create portability problems.</p>
<p>
It is a goal of this project to (1) make all actual system calls message-based,
(2) pass structural information through capability and element lists
instead of as raw structures, and (3) implement a generic 'middle layer'
that looks somewhat like an emulation layer, managed by the kernel but loaded
into userspace.  This layer implements all standard system call APIs
and converts them into the appropriate message(s).</p>
<p>For example, Linux emulation would
operate in (kernel-protected) userland rather then in kernelland.  FreeBSD
emulation would work the same way.  In fact, even 'native' programs will
run through an emulation layer in order to see the system call we all know
and love.  The only difference is that native programs will know that the
emulation layer exists and is directly accessible from userland and won't
waste an extra INT0x80 (or whatever) to enter the kernel just to get spit
back out again into the emulation layer.</p>
<p>
Another huge advantage of converting system calls to message-based entities
is that it completely solves the userland threads issue.  One no longer needs
multiple kernel contexts or stacks to deal with multiple userland threads,
one needs only <em>one</em> kernel context and stack per user process.  Userland
threads would still use rfork() to create a real process for each CPU on the
system, but all other operations would use a thread-aware emulation layer.
In fact, nearly all userland upcalls would be issued by the emulation layer
in userland itself, not directly by the kernel.  Here is an example of how
a thread-aware emulation layer would work:</p>
<pre>
	ssize_t
	read(int fd, void *buf, size_t nbytes)
	{
	    syscall_any_msg_t msg;
	    int error;
	
	    /*
	     * Use a convenient mostly pre-built message stored in
	     * the userthread structure for synchronous requests.
	     */
	    msg = &amp;curthread->td_sysmsg;
	    msg->fd = fd;
	    msg->buf = buf;
	    msg->nbytes = bytes;
	    if ((error = lwkt_domsg(&amp;syscall_port, msg)) != 0) {
		curthread->td_errno = error;
		msg->result = -1;
	    }
	    return(msg->result);
	}
</pre>
<p>
And there you have it.  The only 'real' system calls DragonFly would implement
would be message-passing primitives for sending, receiving, and waiting.
Everything else would go through the emulation layer.  Of course, on the
kernel side the message command will wind up hitting a dispatch table almost
as big as the one that exist in FreeBSD 4.x.  But as more and more 
subsystems become message-based, the syscall messages
become more integrated with those subsystems
and the overhead of dealing with a 'message' could actually wind up being
less then the overhead of dealing with discrete system calls.  Portability
becomes far easier to accomplish because the 'emulation layer' provides a
black box which separates what a userland program expects from what the
kernel expects, and the emulation layer can be updated along with the kernel
(or a backwards compatible version can be created) which makes portability
issues transparent to a userland binary.</p>
<p>
Plus, we get all the advantages that a message-passing model provides,
including a very easy way to wedge into system calls for debugging or other
purposes, and a very easy way to create a security layer in the kernel
which could, for example, disable or alter certain classes of system calls
based on the security environment.</p>

<a name="vfsmodel"><h2>The New VFS Model</h2></a>
<p>
Fixing the VFS subsystem is probably the single largest piece of work
we will be doing.  VFS has two serious issues which will require a lot
of reworking.  First, the current VFS API uses a massive reentrancy model
all the way to its core and we want to try to fit it into a threaded
messaging API.  Second, the current VFS API has one of the single most
complex interfaces in the system... VOP_LOOKUP and friends, which resolve
file paths.  Fixing VFS involves two major pieces of work.</p>
<p>
First, the VOP_LOOKUP interface and VFS cache will be completely redone.  All
file paths will be loaded in an unresolved state into the VFS cache by the
kernel before <em>ANY</em> VFS operation is initiated.  The kernel will 
recurse down the VFS cache and when it hits a leaf it will start creating new
entries to represent the unresolved path elements.  The tail of the snake
will then be handed to VFS_LOOKUP() for resolution.  VFS_LOOKUP() will be 
able to return a new VFS pointer if further resolution is required.  For
example, it hits a mount point.  The kernel will then no longer pass random
user supplied strings (and certainly not using user address space!) to the 
VFS subsystem. 
</p>
<p>
Second, the VOP interface in general will be converted to a messaging
interface.  All direct userspace addresses will be resolved into VM object
ranges by the kernel.  The VOP interface will <em>NOT</em> handle direct 
userspace addresses any more.  As a messaging interface VOPs can still operate 
synchronously, and initially that is what we will do.  But the intention is
to thread most of the VOP interface (i.e. replace the massive reentrancy 
model with a serialized threaded messaging model).   For a high performance
filesystem running multiple threads (one per cpu) we can theoretically
achieve the same level of performance that a massively reentrant model can 
achieve.  However, blocking points, such as the bread()'s you see all over 
filesystem code, would either have to be asynchronized, which is difficult,
 or we would have to spawn a lot more threads to handle parallelism.
Initially we can take the (huge) performance hit and serialize the VOP
operations into a single thread, then we can optimize the filesystems we
care about like UFS.  It should be noted that a massive reentrancy model
is not going to perform all that much better then, say, a 16-thread model
for a filesystem because in both cases the bottleneck is the I/O.  As
long as one thread is free to handle non-blocking (cached) requests we can
achieve 95% of the performance of a massive reentrancy model.</p>
<p>
A messaging interface is preferable for many reasons, not the least of
which being that it makes stacking actually work the way it should work,
as independent and opaque elements which stack together to form a whole.
For example, with the new API a capability layer could be slapped onto a
filesystem that otherwise doesn't implement one of its own, and the
enduser would not know the difference.  Filesytems are almost universally
self-contained entities.  A message-based API would allow these entities
to run in userspace for debugging or even in a deployment when one 
absolutely cannot afford a crash.  Why run msdosfs or cd9660 in the
kernel and risk a crash when it would operate just as well in userland?
Debugging and filesystem development are other good reasons for having a
messaging API rather then a massively reentrant API.</p>

