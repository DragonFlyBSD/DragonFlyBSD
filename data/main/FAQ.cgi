#!/usr/local/www/cgi-bin/tablecg
#
# $DragonFly: site/data/main/Attic/FAQ.cgi,v 1.1 2004/01/07 18:18:00 justin Exp $

$TITLE(DragonFly Frequently Asked Questions)

<P>

<B>What is the release date for DragonFly 1.0?</B><BR>
Somewhere in 2004. Stable vs. unstable releases will be in single tree.
There will be no separated trees as with some other BSDs.
<P>
<B>What will be used to handle third-party applications?  
(like ports, RPM, apt-get, etc.)</B><BR>

Currently, DragonFly uses the existing ports system from FreeBSD4, with local 
overrides specific to DragonFly located in dfports (default location /usr/dfports).
dfports works the same as ports - you can keep it up to date using cvsup.
<P>
Eventually, DragonFly will have a homegrown port system, using the VFS mechanisms 
that are as of this writing not complete yet.  For more information, check 
the <a href="/Goals/packages.cgi">existing packaging description</a>.
<P>

<B>Will DragonFly use a dynamic /dev filesystem, as in devfs?</B><BR>
Current plans are to keep the existing filesystem model, with the removal of 
minor/major numbering.  There may be a 'devd' process to handle dynamic devices.  
There are other features to complete first before this is tackled.
<P>
<B>Will DragonFly use (insert name here) technology?</B><BR>
Yes and no.  Features must match the existing plan outlined on the site here, 
and there's plenty of existing problems to solve before 'nonessential' work can be done.
However, if you are willing to work on it, it probably can be done.  The 
<a href="/Main/forums.cgi">forums</a> are an excellent place to get feedback and to 
find others that may be interested in your topic.  The 
<a href="/Main/team.cgi">Team</a> page is also a good place to check.
<P>
<B>What's the correct way to name this operating system?</B><BR>
It's a BSD variant, called DragonFly.  Note the capitalization on the F, which isn't 
proper English.  
