#!/usr/local/www/cgi-bin/tablecg
#
# $DragonFly: site/data/goals/Attic/threads.cgi,v 1.8 2004/03/06 14:39:08 hmp Exp $

$TITLE(DragonFly - Light Weight Kernel Threading Model)

<h2>The Light Weight Kernel Threading Model</h2>
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
    <li>
    Each cpu in the system has its own self-contained LWKT scheduler.
    Threads are locked to their cpus by design and can only be moved to other
    cpus under certain special circumstances.  Any LWKT scheduling operation 
    on a particular cpu is only directly executed on that cpu.
    This means that the core LWKT scheduler can schedule, deschedule, 
    and switch between threads within a cpu's domain without any locking 
    whatsoever.  No MP lock, nothing except a simple critical section.</li>
    <li>
    A thread will never be moved to another cpu while it is running, and
    a thread will never be preemptively switched to a non-interrupt thread.  If
    an interrupt thread preempts the current thread, then the moment the 
    interrupt thread completes or blocks, the preempted thread will resume 
    regardless of its scheduling state.  For example, a thread might get 
    preempted after calling lwkt_deschedule_self() but before it actually
    switches out.  This is ok because control will be returned to it
    directly after the interrupt thread completes or blocks.</li>
    <li>
    Due to (2) above, a thread can cache information obtained through the
    per-cpu globaldata structure without having to obtain any locks and, if
    the information is known not to be touched by interrupts, without having to
    enter a critical section.  This allows per-cpu caches for various types of
    information to be implemented with virtually no overhead.</li>
    <li>
    A cpu which attempts to schedule a thread belonging to another cpu
    will issue an IPI-based message to the target cpu to execute the operation.
    These messages are asynchronous by default and while IPIs may entail some
    latency, they don't necessary waste cpu cycles due to that fact.  Threads
    can block such operations by entering a critical section and, in fact,
    that is what the LWKT scheduler does.  Entering and exiting a critical 
    section are considered to be cheap operations and should require no 
    locking or locked bus instructions to accomplish.  The IPI messaging
    subsystem deals with FIFO-full deadlocks by spinning with interrupts 
    enabled and processing its incoming queue while waiting for its 
    outgoing queue to unstall.  The IPI messaging subsystem specifically
    does not switch threads under these circumstances.</li>
</ol>
<p>
In addition to these key features, the LWKT model allows for both FAST
interrupt preemption *AND* threaded interrupt preemption.  FAST interrupts
may preempt the current thread when it is not in a critical section.
Threaded interrupts may also preempt the current thread.  The LWKT system
will switch to the threaded interrupt and then switch back to the original
when the threaded interrupt blocks or completes.  It is our intention to
create an abstraction for FAST software interrupts (with a trapframe) as
well, which will allow traditional hardclock() and statclock() distribution
to operate across all cpus.</p>

<h2>The IPI Messaging Subsystem</h2>
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

<h2>The IPI-based CPU Synchronization Subsystem</h2>
<p>
The LWKT model implements a generalized, machine independant cpu
synchronization API.  The API may be used to place target cpu(s) into a 
known state while one is operating on a sensitive data structure.  This
interface is primarily used to deal with MMU pagetable updates.  For
example, it is not safe to check and clear the modify bit on a page table
entry and then remove the page table entry, even if holding the proper lock,
because a userland process running on another cpu may be accessing or
modifying that page and create a race between the TLB writeback on the
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
synchronization interface operates in a controlled environment the call
back functions tend to work just like the callback functions used in the
IPI messaging subsystem.</p>

<h2>Serializing Tokens</h2>
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
which does not interlock against interrupts on the same cpu.</p>

<p>Holding a serializing token does <b>not</b> prevent preemptive interrupts
from occuring, though it might cause some of those interrupts to 
block-reschedule.</p>
