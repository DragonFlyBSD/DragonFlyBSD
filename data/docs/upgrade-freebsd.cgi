#!/usr/local/www/cgi-bin/tablecg
#
# $DragonFly: site/data/docs/Attic/upgrade-freebsd.cgi,v 1.3 2004/08/25 18:04:01 justin Exp $

$TITLE(DragonFly - Upgrading From FreeBSD 4)

<P>
If you have a system running FreeBSD 4 (4.9 was the most recent release 
at the time this document was written), it is possible to switch to DragonFly 
"in-place" through downloading the DragonFly source and compiling it.
<P>

<OL>
<LI> First, make sure you are running FreeBSD 4.9-RELEASE or later.  
<code>uname -a</code> will tell you what version you are running.  
Note: FreeBSD 5 versions will <b>not work</b> for this upgrade.
<P>
<LI> Remove any FreeBSD source files from your machine.  
<I>All commands after this point should be run as root.</I>
<P>
<code>
rm -rf /usr/src<BR>
rm -rf /usr/obj<BR>
mkdir /usr/obj
</code>
<P>
<LI> If you do not have cvsup installed, 
these commands will download and install it for you:
<P>
<code>
pkg_add ftp://ftp.freebsd.org/pub/FreeBSD/ports/i386/packages-4-stable/Latest/cvsup-without-gui.tgz<BR>
rehash
</code>
<P>
<LI> Fetch a configuration file for cvsup and run cvsup using that file:
<P>
<code>
fetch http://www.dragonflybsd.org/main/dragonfly-cvs-supfile
</code>
<P>
Comment out the lines that say:
<P>
<code>
*default prefix=/home/dcvs<br>
*default release=cvs
</code>
<P>
and uncomment the lines that say: 
<P>
<code>
 #*default prefix=/usr<br>
 #*default release=cvs tag=.
</code>
<P>
Run cvsup using that config file.
<P>
<code>
cvsup dragonfly-src-supfile
</code>
<P>
You now will have the DragonFly source files in /usr/src.
<P>
<I>Note: the newly created file <code>/usr/src/UPDATING</code> will have the 
most up-to-date instructions on updating your system; double-check it 
before proceeding.</I>
<P>
<LI> You will now want to compile the userland and the kernel. 
<P>
<code>
cd /usr/src<BR>
make buildworld<BR>
make buildkernel
</code>
<P>
<LI> Delete existing FreeBSD include files; having them available may cause 
conflicts later on.
<P>
<code>
rm -rf /usr/include<BR>
mkdir /usr/include
</code>
<P>
<LI> Now, install the new DragonFly userland and kernel:
<P>
<code>
make installworld<BR>
make installkernel
</code>
<P>
<LI> You will want to make sure your /etc directory is cleaned up:
<P>
<code>
make upgrade
</code>
<P>
<LI> You now can sync your disk and reboot your machine.
<P>
<code>
sync<BR>
reboot
</code>
<P>
<LI> Congratulations!  Your computer will boot into DragonFly!
Make sure to check <a href="/main/forums.cgi">the forums</a> 
semi-regularly.  If you have trouble booting, the bugs forum is 
the first place to go.
<P>
You can repeat the cvsup/build/install steps on 
this page to keep your system up to date with the changing 
DragonFly codebase.  (You will not have to repeat the 
<code>make upgrade_etc</code> step.)  

</OL>
