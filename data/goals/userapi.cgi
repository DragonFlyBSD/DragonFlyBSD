#!/usr/local/www/cgi-bin/tablecg
#
# $DragonFly: site/data/goals/Attic/userapi.cgi,v 1.2 2003/08/11 02:24:47 dillon Exp $

$TITLE(DragonFly - User API)
<CENTER>Creating a Portable User API</CENTER>
<P>
Most standard UNIXes employ a system call table through which many types
of data, including raw structures, are passed back and forth.   The biggest
obstacle to the ability for user programs to interoperate with kernels
which are older or newer than themselves is the fact that these raw structures
often change.  The worst offenders are things like network interfaces, route
table ioctls, ipfw, and raw process structures for ps, vmstat, etc.  But even
non-descript system calls like stat() and readdir() have issues.  In more
general terms the system call list itself can create portability problems.
<P>
It is a goal of this project to (1) make all actual system calls message-based,
(2) pass structural information through capability and element lists
instead of as raw structures, and (3) implement a generic 'middle layer'
that looks kinda like an emulation layer, managed by the kernel but loaded
into userspace.  This layer layer implements all standard system call APIs
and converts them into the appropriate message(s).
For example, linux emulation would
operate in (kernel-protected) userland rather then in kernelland.  FreeBSD
emulation would work the same way.  In fact, even 'native' programs will
run through an emulation layer in order to see the system call we all know
and love.  The only difference is that native programs will know that the
emulation layer exists and is directly accessible from userland and won't
waste an extra INT0x80 (or whatever) to enter the kernel just to get spit
back out again into the emulation layer.
<P>
Another huge advantage of converting system calls to message-based entities
is that it completely solves the userland threads issue.  One no longer needs
multiple kernel contexts or stacks to deal with multiple userland threads,
one needs only *one* kernel context and stack per user process.  Userland
threads would still use rfork() to create a real process for each cpu on the
system, but all other operations would use a thread-aware emulation layer.
In fact, nearly all userland upcalls would be issued by the emulation layer
in userland itself, not directly by the kernel.  Here is an example of how
a thread-aware emulation layer would work:
<UL><PRE>
    <P>
ssize_t
read(int fd, void *buf, size_t nbytes)
{
    syscall_any_msg_t msg;
    int error;

    /*
    * Use the convenient mostly pre-built message stored in the
    * userthread structure
    */
    msg = &curthread->td_sysmsg;
    msg->fd = fd;
    msg->buf = buf;
    msg->nbytes = bytes;
    error = lwkt_domsg(&syscall_port, msg);
    curthread->td_errno = error;
    if (error)
	msg->result = -1;
    return(msg->result);
}
</PRE></UL>
<P>
And there you have it.  The only 'real' system calls DragonFly would implement
would be message-passing primitives for sending, receiving, and waiting.
Everything else would go through the emulation layer.  Of course on the
kernel side the message command will wind up hitting a dispatch table almost
as big as the one we have in 4.x.  But as more and more subsystems become
message-based the syscall messages become more integrated with those subsystems
and the overhead of dealing with a 'message' could actually wind up being
less then the overhead of dealing with discrete system calls.  Portability
becomes far easier to accomplish because the 'emulation layer' provides a
black box which separates what a userland program expects from what the
kernel expects, and the emulation layer can be updated along with the kernel
(or a backwards compatible version can be created) which makes portability
issues transparent to a userland binary.
<P>
Plus we get all the advantages that a message-passing model provides,
including a very easy way to wedge into system calls for debugging or other
purposes, and a very easy way to create a security layer in the kernel
which could, for example, disable or alter certain classes of system calls
based on the security environment.
