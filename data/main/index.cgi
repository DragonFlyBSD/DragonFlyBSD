#!/usr/local/www/cgi-bin/tablecg
#
# $DragonFly: site/data/main/Attic/index.cgi,v 1.11 2004/12/21 01:38:49 hmp Exp $

$TITLE(The DragonFly BSD Project)

<table border="0" width="100%" bgcolor="white">
<tr><td align="center">
	<h1>Ongoing DragonFly News</h1></td></tr>
<tr><td>
	<p>The <a href="http://www.shiningsilence.com/dbsdlog/">DragonFly Digest</a>
    has up to date information on recent events and changes.</p>

	<p>A <a href="http://www.sitetronics.com/dfwiki/">Wiki</a> has been
	started by the
	<a href="http://www.bsdinstaller.org/">DragonFly Installer Team</a> for
	things related to DragonFly.</a></p>
</td></tr>
</table>
<table border="0" width="100%" bgcolor="white">
<tr><td align="center">
    <h1>DragonFly-1.0 RELEASED!</h1>
    <h1>12 July 2004</h1>
</td></tr></table>

    <p>
One year after starting the project as a fork off the FreeBSD-4.x tree,
the <a href="http://www.dragonflybsd.org/main/team.cgi">DragonFly Team</a>
is pleased to announce our 1.0 release!</p>

    <p>
We've made remarkable progress in our first year.  We have replaced nearly
all of the core threading, process, interrupt, and network infrastructure 
with DragonFly native subsystems.  We have our own MP-friendly slab allocator,
a Light Weight Kernel Threading (LWKT) system that is separate from the
dynamic userland scheduler, a fine-grained system timer abstraction for
kernel use, a fully integrated light weight messaging system, and a core
IPI (Inter Processor Interrupts) messaging system for inter-processor
communications.</p>

    <p>
We have managed to retain 4.x's vaunted stability throughout the development
process, despite ripping out and replacing major subsystems, and we have
a demonstratively superior coding model which is both UP (Uni-Processor) and
MP (Multi-Processor) friendly and which is nearly as efficient on UP systems
as the original 4.x UP-centric code is on UP systems.</p>

    <p>
We have made excellent progress bringing in those pieces from FreeBSD, NetBSD,
and OpenBSD that fit our model.  For example, NEWBUS/BUS_DMA, the USB 
infrastructure, RCNG (next generation system startup infrastructure), and
so forth.  We have made an excellent start on reformulating the build
and release infrastructure including an excellent new system installer
which, while still in its infancy for the 1.0 release, has been coded in
a manner that will allow us to greatly improve and expand its capabilities
in coming months.</p>

    <p>
We have done so much that it cannot all be listed here.  Please check out the
<a href="http://www.dragonflybsd.org/status/diary.cgi">Diary</a> for
technical details.</p>

    <p>
The two largest user-visible subsystems that still have major work pending
are the userland threading and ports/packages subsystems.  People will find
that the DragonFly-1.0 release is still using the old 4.x pthreads model,
and at the moment we are relying on the FreeBSD ports tree with DragonFly
specific overrides for third party application support... about as severe a
hack as it is possible to have.  These two stop-gap items will be at the
forefront of the work for the next year, along with a major move to start
removing the BGL (Big Giant Lock, also known as the MP lock) from code
inherited from 4.x, threading the VFS (Virtual File System) subsystem (the
network subsystem is already threaded as of 1.0), and implementing
asynchronously messaged system calls.  And that is just the tip of the iceberg,
for we will be achieving far more in the coming year!</p>

<h1>What is DragonFly BSD?</h1>
<p>
DragonFly is an operating system and environment designed to be the logical
continuation of the FreeBSD-4.x OS series.  These operating systems belong
in the same class as Linux in that they are based on UNIX ideals and APIs.
DragonFly is a fork in the path, so to speak, giving the BSD base an opportunity
to grow in an entirely new direction from the one taken in the FreeBSD-5 series.</p>
<p>
It is our belief that the correct choice of features and algorithms can
yield the potential for excellent scalability,
robustness, and debuggability in a number of broad system categories.  Not
just for SMP or NUMA, but for everything from a single-node UP system to a
massively clustered system.  It is our belief that a fairly simple
but wide-ranging set of goals will lay the groundwork for future growth.
The existing BSD cores, including FreeBSD-5, are still primarily based on
models which could at best be called 'strained' as they are applied to modern
systems.  The true innovation has given way to basically just laying on hacks
to add features, such as encrypted disks and security layering that in a
better environment could be developed at far less cost and with far greater
flexibility.</p>
<p>
We also believe that it is important to provide API solutions which
allow reasonable backwards and forwards version compatibility, at least
between userland and the kernel, in a mix-and-match environment.  If one
considers the situation from the ultimate in clustering... secure anonymous
system clustering over the internet, the necessity of having properly 
specified APIs becomes apparent.</p>
<p>
Finally, we believe that a fully integrated and feature-full upgrade
mechanism should exist to allow end users and system operators of all
walks of life to easily maintain their systems.  Debian Linux has shown us
the way, but it is possible to do better.</p>
<p>
DragonFly is going to be a multi-year project at the very least.  Achieving
our goal set will require a great deal of groundwork just to reposition
existing mechanisms to fit the new models.  The goals link will take you
to a more detailed description of what we hope to accomplish.</p>
