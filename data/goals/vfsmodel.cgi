#!/usr/local/www/cgi-bin/tablecg
#
# $DragonFly: site/data/goals/Attic/vfsmodel.cgi,v 1.4 2004/03/06 14:39:08 hmp Exp $

$TITLE(DragonFly - VFS/filesystem Device Operations)

<h1>The New VFS Model</h1>
<p>
Fixing the VFS subsystem is probably the single largest piece of work
we will be doing.  VFS has two serious issues which will require a lot
of reworking.  First, the current VFS API uses a massive reentrancy model
all the way to its core and we want to try to fit it into a threaded
messaging API.  Second, the current VFS API has one of the single most
complex interfaces in the system... VOP_LOOKUP and friends which resolve
file paths.  Fixing VFS involves two major pieces of work.</p>
<p>
First, the VOP_LOOKUP interface and VFS cache will be completely redone.  All
file paths will be loaded in an unresolved state into the VFS cache by the
kernel before *ANY* VFS operation is initiated.  The kernel will basically
recurse down the VFS cache and when it hits a leaf it will start creating new
entries to represent the unresolved path elements.  The tail of the snake
will then be handed to VFS_LOOKUP() for resolution.  VFS_LOOKUP() will be 
able to return a new VFS pointer if further resolution is required (for
example, it hits a mount point.  The kernel will no longer pass random
user supplied strings (and certainly not using user address space!) to the
VFS subsystem.</p>
<p>
Second, the VOP interface in general will be converted to a messaging
interface.  All direct userspace addresses will be resolved into VM object
ranges by the kernel.  The VOP interface will *NOT* handle direct userspace
addresses any more.  As a messaging interface VOPs can still operate 
synchronously, and initially that is what we will do.  But the intention is
to thread most of the VOP interface (i.e. replace the massive reentrancy 
model with a serialized threaded messaging model).   For a high performance
filesystem running multiple threads (one per cpu) we can theoretically
achieve the same level of performance that a massively reentrant model can 
achieve, but blocking points (like the bread()'s you see all over filesystem
code) would either have to be asynchronized, which is difficult to say the
least, or we would have to spawn a lot more threads to handle parallelism.
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
as independant and opaque elements which stack together to form a whole.
For example, with the new API a capability layer could be slapped onto a
filesystem that otherwise doesn't implement one of its own, and the
enduser would not know the differences.  Filesytems are almost universally
self-contained entities.  A message-based API would allow these entities
to run in userspace for debugging or even in a deployment when one 
absolutely cannot afford a crash.  Why run msdosfs or cd9660 in the
kernel and risk a crash when it would operate just as well in userland?
Debugging and filesystem development are other good reasons for having a
messaging API rather then a massively reentrant API.</p>
