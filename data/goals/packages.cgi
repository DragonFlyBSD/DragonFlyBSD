#!/usr/local/www/cgi-bin/tablecg
#
# $DragonFly: site/data/goals/Attic/packages.cgi,v 1.3 2004/01/06 20:44:50 justin Exp $

$TITLE(DragonFly - Packaging up the UserLand)
<CENTER>Dealing with Package Installation</CENTER>
<P>
Applications are such a godawful mess these days that it is hard to come
up with a packaging and installation system that can achieve seamless
installation and flawless operation.  I have come to the conclusion that
the crux of the problem is that even seemingly minor updates to third
party libraries (that we have no control over) can screw up an
already-installed application.  A packaging system *CAN* walk the
dependancy tree and upgrade everything that needs upgrading.  The
problem is that the packaging system might not actually have a new
version of the package or packages that need to be upgraded due to some
third party library being upgraded.
<P>
We need to have the luxury and ability to upgrade only the particular
package we want without blowing up applications that depend on said package.
This isn't to say that it is desirable.  Instead we say that it is 
necessary because it allows us to do piecemeal upgrades (as well as
piecemeal updates to the packaging system's database itself)
without having to worry about blowing up other things in the process.
<I>Eventually</I> we would synchronize everything up, but there could be
periods of a few days to a few months where a few packages might not be,
and certain very large packages could wind up depending on an old version
of some library for a very long time.  We need to be able to support that.
We also need to be able to support versioned support/configuration directories
that might be hardwired by a port.  Whenever such conflicts occur, the
packaging system needs to version the supporting directories as well. 
If two incompatible versions of package X both need /usr/local/etc/X
we would wind up with /usr/local/etc/X:VERSION1 and /usr/local/etc/X:VERSION2.
<P>
I believe it is possible to accomplish this goal by explicitly versioning
dependancies and tagging the package binary with an 'environment'... a 
filesystem overlay you could call it, which applies to supporting
directories like /usr/lib, /usr/local/lib, even /usr/local/etc, and
basically makes only the particular version of the particular libraries
and/or files the package needs visible to it, and everything else would
be invisible.  By enforcing visibility you would know very quickly if you
specified your package dependancies incorrectly because your package would not
be able to find libraries or supporting files that exist, but that
you did not realize it needed.
For example, if the package says a program depends on version 1.5 of the
ncurses library, then version 1.5 is all that would be visible to the program
(it would appear as just libncurses.* to the program).  
<P>
With such a system we would be able to install multiple versions of anything
whether said entities supported fine-grained version control or not, and
even if (in a normal sytem) there would be conflicts with other entities.
The packaging system would be responsible for tagging the binaries and
the operating system would be responsible for the visibility issues.  The
packaging system or possibly even just a cron job would be responsible for
running through the system and locating all the 'cruft' that is removable
after you've updated all the packages that used to depend on it.
<P>
Another real advantage of enforced visibility is that it provides us
with proof-positive that a package does or does not need something.  We
would not have to rely on the packaging system to find out what the
dependancies were; we could just look at the environment tagged to the
binary!
