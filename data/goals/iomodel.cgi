#!/usr/local/www/cgi-bin/tablecg
#
# $DragonFly: site/data/goals/Attic/iomodel.cgi,v 1.2 2003/08/11 02:24:47 dillon Exp $

$TITLE(DragonFly - I/O Device Operations)
<CENTER>New I/O Device Model</CENTER>
<P>
I/O is considerably easier to fix then VFS owing to the fact that
most devices operate asynchronously already, despite having a semi-synchronous
API.  The I/O model being contemplated consists of three major pieces of work:
<P>
(1) I/O Data will be represented by ranges of VM Objects instead of ranges of
system or user addresses.  This allows I/O devices to operate entirely
independently of the originating user process.
<P>
(2) Device I/O will be handled through a port/messaging system (see 'ports')
on the left.
<P>
(3) Device I/O will typically be serialized through one or more threads.
Each device will typically be managed by its own thread but certain high
performance devices might be managed by multiple threads (up to one per cpu).
Multithreaded devices would not necessarily compete for resources.  For
example, the TCP stack could be multithreaded with work split up by target
port number mod N, and thus run on multiple threads (and thus multiple cpus)
without contention.
<P>
As part of this work I/O messages will utilize a flat 64 bit byte-offset
rather than block numbers.
<P>
Note that device messages can be acted upon synchronously by the device.
Do not make the mistake of assuming that messages are unconditionally
serialized to the device thread because they aren't.  See the messaging
section on the left for more information.
<P>
It should also be noted that the device interface is being designed with
the flexibility to allow devices to operate as user processes rather then
as kernel-only threads.  Though we probably will not achieve this capability
for some time, the intention is to eventually be able to do it.  There are
innumerable advantages to being able to transparently pull things like 
virtual block devices and even whole filesystems into userspace.
