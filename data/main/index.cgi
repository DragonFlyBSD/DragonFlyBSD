#!/usr/local/www/cgi-bin/tablecg
#
# $DragonFly: site/data/main/Attic/index.cgi,v 1.3 2004/01/06 20:10:52 justin Exp $

$TITLE(The DragonFly BSD Project)

<CENTER>What is it?</CENTER>
<P>
DragonFly is an operating system and environment designed to be the logical
continuation of the FreeBSD-4.x OS series.  These operating systems belong
in the same class as Linux in that they are based on UNIX ideals and APIs.
DragonFly is a fork in the path, so to speak, giving the BSD base an opportunity
to grow in an entirely new direction from the one taken in the FreeBSD-5 series.
<P>
It is our belief that the correct choice of features and algorithms can
yield the potential for excellent scaleability,
robustness, and debuggability in a number of broad system categories.  Not
just for SMP or NUMA, but for everything from a single-node UP system to a
massively clustered system.  It is our belief that a fairly simple
but wide-ranging set of goals will lay the groundwork for future growth.
The existing BSD cores, including FreeBSD-5, are still primarily based on
models which could at best be called 'strained' as they are applied to modern
systems.  The true innovation has given way to basically just laying on hacks
to add features, such as encrypted disks and security layering that in a
better environment could be developed at far less cost and with far greater
flexibility.
<P>
We also believe that it is important to provide API solutions which
allow reasonable backwards and forwards version compatibility, at least
between userland and the kernel, in a mix-and-match environment.  If one
considers the situation from the ultimate in clustering... secure anonymous
system clustering over the internet, the necessity of having properly 
specified APIs becomes apparent.
<P>
Finally, we believe that a fully integrated and feature-full upgrade
mechanism should exist to allow end users and system operators of all
walks of life to easily maintain their systems.  Debian Linux has shown us
the way, but it is possible to do better. 
<P>
DragonFly is going to be a multi-year project at the very least.  Achieving
our goal set will require a great deal of groundwork just to reposition
existing mechanisms to fit the new models.  The Goals link will take you
to a more detailed description of what we hope to accomplish.
