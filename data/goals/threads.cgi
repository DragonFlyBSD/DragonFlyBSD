#!/usr/local/www/cgi-bin/tablecg
#
# $DragonFly: site/data/goals/Attic/threads.cgi,v 1.2 2003/08/11 02:24:47 dillon Exp $

$TITLE(DragonFly - VFS/filesystem Device Operations)
<CENTER>The Light Weight Kernel Threading Model</CENTER>
<P>
DragonFly employs a light weight kernel threading (LWKT) model at its core.
Each process in the system has an associated thread, and most kernel-only
processes are in fact just pure threads.  For example, the pageout daemon
is a pure thread and has no process context.
<P>
The LWKT model has a number of key features that can be counted on no matter
the architecture.  These features are designed to remove or reduce contention
between cpus.
<UL>
    <P>
    (1) Each cpu in the system has its own self-contained LWKT scheduler.
    Threads are locked to their cpus by design and can only be moved to other
    cpus under certain special circumstances.  Any LWKT scheduling operation 
    on a particular cpu is only directly executed on that cpu.
    This means that the core LWKT scheduler can schedule, deschedule, 
    and switch between threads within a cpu's domain without any locking 
    whatsoever.  No MP lock, nothing except a simple critical section.
    <P>
    (2) A Thread will never be moved to another cpu while it is running, and
    a thread will never be preemptive switched to a non-interrupt thread.  If
    an interrupt thread preempts the current thread, then the moment the 
    interrupt thread completes or blocks the preempted thread will resume 
    regardless of its scheduling state.  For example, a thread might get 
    preempted after calling lwkt_deschedule_self() but before it actually
    switches out.  This is ok because control will be returned to it
    directly after the interrupt thread completes or blocks.
    <P>
    (3) Due to (2) above a thread can cache information obtained through the
    per-cpu globaldata structure without having to obtain any locks and, if
    the information is known not to be touched by interrupts, without having to
    enter a critical section.  This allows per-cpu caches for various types of
    information to be implemented with virtually no overhead.
    <P>
    (4) A cpu which attempts to schedule a thread belonging to another cpu
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
    does not switch threads under these circumstances.
</UL>
<P>
In addition to these key features, the LWKT model allows for both FAST
interrupt preemption *AND* threaded interrupt preemption.  FAST interrupts
may preempt the current thread when it is not in a critical section.
Threaded interrupts may also preempt the current thread.  The LWKT system
will switch to the threaded interrupt and then switch back to the original
when the threaded interrupt blocks or completes.  It is our intention to
create an abstraction for FAST software interrupts (with a trapframe) as
well, which will allow traditional hardclock() and statclock() distribution
to operate across all cpus.
<P>
<CENTER>Token Passing Primitives Not Mutexes</CENTER>
<P>
The LWKT model implements a token passing primitive rather then a mutex
primitive.  Tokens are owned by cpus rather then by threads, and a change
of ownership is protected by a critical section (another cpu has to IPI message
you to pull your token away from you).  A token can be made to act almost like
a mutex by releasing it to NOCPU, but generally speaking a token is either
passively released (left owned by the current cpu), or actively handed off to
another cpu.  The release mechanism used depends on the circumstances.  A
token that is ping-ponging (typical in a pipe) is best served by an active
hand off, for example.  The primary advantage of the token over the mutex
is that it costs almost nothing to acquire a token that your cpu already owns,
and costs even *LESS* to release a token because you effectively do not actually
have to release it.  The biggest disadvantage is that acquiring a token owned
by another CPU requires a synchronous IPI message.  This disadvantage can
be partially compensated for by having the release go to NOCPU (then the token
may be acquired through a locked cmpxchgl in IA32-land), but it is our
belief that most token operations can be optimal enough such that the cost
of the occassional IPI will be on average less then the cost of a mutex.
The token code really is a lot less complex then the mutex code.  We don't
have to worry about deadlocks, we don't have to release tokens when we block,
there are a whole lot of special cases that simply do not exist when using
a token model.
