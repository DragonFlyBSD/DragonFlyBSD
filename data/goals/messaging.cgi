#!/usr/local/www/cgi-bin/tablecg
#
# $DragonFly: site/data/goals/Attic/messaging.cgi,v 1.5 2004/03/06 14:39:08 hmp Exp $

$TITLE(DragonFly - The Port/Messaging Model)

<h1>The Port/Messaging Model</h1>
<p>
DragonFly will have a lightweight port/messaging API to go along with its
lightweight kernel threads.  The port/messaging API is very simple
in concept; You construct a message, you send it to a target port, and
at some point later you wait for a reply on your reply port.  On this
simple abstraction we intend to build a high level of capability and
sophistication.  To understand the capabilities of the messaging system
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
such as try_mplock() to try to deal with the request without having to
resort to more expensive queueing / switching.</p>
<p>
The key thing to remember here is that our best case optimization is direct
execution by mp_SendMsg() with virtually no more overhead then a simple
subroutine call would otherwise entail.  No queueing, no messing around
with the reply port... if a message can be acted upon synchronously then we
are talking about an extremely inexpensive operation.  It is this key feature
that allows us to use a messaging interface by design without having to worry
about performance issues.  We are explicitly NOT employing the type of
sophistication that, say, Mach uses.  We are not trying to track memory
mappings or pointers or anything like that, at least not in the low level
messaging interface.  User&lt;-&gt;Kernel messaging interfaces simply employ 
mp_SendMsg() function vectors which do the appropriate translation, so as
far as the sender and recipient are concerned the message will be local to
their VM context.</p>
