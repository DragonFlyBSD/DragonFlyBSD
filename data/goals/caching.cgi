#!/usr/local/www/cgi-bin/tablecg
#
# $DragonFly: site/data/goals/Attic/caching.cgi,v 1.5 2004/04/16 13:23:37 justin Exp $

$TITLE(DragonFly - A Scaleable Caching Infrastructure)

<h1>Caching Infrastructure Overview</h1>
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
