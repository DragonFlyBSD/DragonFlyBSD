#!/usr/local/www/cgi-bin/tablecg
#
# $DragonFly: site/data/main/Attic/FAQ.cgi,v 1.8 2004/07/18 04:36:50 justin Exp $

$TITLE(DragonFly Frequently Asked Questions)

<p><b>Is DragonFly ready for production?</b><br/>
DragonFly is generally stable and speedy at this point.  DragonFly still uses the FreeBSD ports system for third-party software, and some ports may not build correctly on DragonFly.  You are advised to keep a close eye on the <a href="forums.cgi">forums</a>.</p>

<p><b>What are the <i>potential</i> goals for the next release?</b><br/>
Userland threading and a new packaging system, along with removal of the 
multiprocessing lock (known as the Big Giant Lock) inherited from 
FreeBSD-4.  In addition, we plan to implement asynchronous system call messaging, and a threaded VFS.</p>

<p>For DragonFly news and events, keep an eye on Matthew Dillon's <a href="/status/diary.cgi">diary</a>, 
the <a href="http://www.shiningsilence.com/dbsdlog/">DragonFly BSD Log</a>, and the DragonFly <a href="/main/forums.cgi">mailing lists/newsgroups</a>.</p>

<p><b>I get garbage on the screen when I boot</b> <i>or</i> <b>I can't seem to pause at the initial boot menu.</b><br/>
DragonFly, when booting, outputs to both video and serial ports.  If the booting computer has a 'noisy' serial device connected, it may read data from it during the boot process.  Serial console activation during boot can be disabled by creating the file /boot.config with the contents: '-V'</p>

<p><b>I can't install the XFree86-4 port; it loops forever.</b><br/>
Make sure you have the dfports override for XFree86-4-libraries on your DragonFly machine, and install that individual port.  The XFree86-4 metaport should install correctly after that.</p>

<p>
<b>What will be used to handle third-party applications?
(like ports, RPM, apt-get, etc.)</b><br />

Currently, DragonFly uses the existing ports system from FreeBSD4, with local 
overrides specific to DragonFly located in dfports (default location /usr/dfports).
dfports works the same as ports - you can keep it up to date using cvsup.</p>
<p>
Eventually, DragonFly will have a homegrown port system, using the VFS mechanisms 
that are as of this writing not complete yet.  For more information, check 
the <a href="/goals/packages.cgi">existing packaging description</a>.
</p>

<p><b>What architectures does DragonFly support?</b><br/>
DragonFly is currently targeted at the x86 line of processors; it should work 
on 386 and up, though a 386 is certainly not recommended.  Work is also being 
done on support for the new 64-bit processors from AMD.  There are currently 
no plans for support of other processor types.  </p>
<p>
However, support for Sparc or PowerPC or other systems is possible in the 
future.  If you plan to submit code to the DragonFly project, please keep 
this in mind.</p>
<p>
<b>How can I contribute?</b><br/>
Pick a topic that you enjoy and start working.  Check the 
<a href="/main/team.cgi">Team page</a> to see if there are others 
interested in your topic, or ask around in the 
<a href="/main/forums.cgi">appropriate forum</a>.  You can 
<a href="/main/download.cgi">download 
the source</a> to the operating system and to this site, and send 
patches in unified diff format (<code>diff -uN</code>) to 
'submit at dragonflybsd.org' for review.  Subscribe to that same submit 
<a href="/main/forums.cgi">mailing list/newsgroup</a> to see feedback 
on your patches, and to find if they have been accepted or rejected.</p>

<p>
<b>Will DragonFly use a dynamic /dev filesystem, as in devfs?</b><br/>
Current plans are to keep the existing filesystem model, with the removal of
minor/major numbering.  There may be a 'devd' process to handle dynamic devices.
There are other features to complete first before this is tackled.
</p>

<p><b>Will DragonFly use (insert name here) technology?</b><br/>
Yes and no.  Features must match the existing plan outlined on the site here, 
and there's plenty of existing problems to solve before 'nonessential' work can be done.
However, if you are willing to work on it, it probably can be done.  The 
<a href="/main/forums.cgi">forums</a> are an excellent place to get feedback and to 
find others that may be interested in your topic.  The 
<a href="/main/team.cgi">Team</a> page is also a good place to check.</p>

<p><b>What's the correct way to name this operating system?</b><br/>
It's a BSD variant, called DragonFly.  Note the capitalization on the F, which isn't 
proper English.</p>
