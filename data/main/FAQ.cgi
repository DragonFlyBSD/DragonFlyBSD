#!/usr/local/www/cgi-bin/tablecg
#
# $DragonFly: site/data/main/Attic/FAQ.cgi,v 1.12 2005/08/13 00:34:44 swildner Exp $

$TITLE(DragonFly Frequently Asked Questions)

<a name="About_this_FAQ"></a><b> About this FAQ </b>
<p>You can find a copy of the
FAQ on the official DragonFly
website.  In additon, there is
a copy maintained on the wiki
which is perioically synchronized
with the offical FAQ.
</p><p>You can also find translations to other languages at these locations:
<a href="/main/FAQ_Spanish.cgi" title ="DragonFly FAQ Spanish">Español</a> - 
<a href="/main/FAQ_Russian.cgi" title ="DragonFly FAQ Russian">Русский (Russian)</a>
</p>
<a name="Is_DragonFly_ready_for_production.3F"></a><b> Is DragonFly ready for production? </b>
<p>DragonFly is generally stable and speedy at this point. It still uses the FreeBSD ports system for third-party software, and some ports may not build correctly. You are advised to keep a close eye on the forums. Check the <a href='http://www.dragonflybsd.org/main/forums.cgi' class='external' title="http://www.dragonflybsd.org/main/forums.cgi">forums page</a> to find out on how to get access.
</p>
<a name="What_are_the_potential_goals_for_the_next_release.3F"></a><b> What are the potential goals for the next release? </b>
<p>Userland threading and a new packaging system, along with removal of the multiprocessing lock (known as the Big Giant Lock) inherited from FreeBSD-4. In addition, we plan to implement asynchronous system call messaging, and a threaded VFS.
</p><p>For DragonFly news and events, keep an eye on Matthew Dillon's <a href='http://www.dragonflybsd.org/status/diary.cgi' class='external' title="http://www.dragonflybsd.org/status/diary.cgi">diary</a>, the <a href='http://www.shiningsilence.com/dbsdlog/' class='external' title="http://www.shiningsilence.com/dbsdlog/">DragonFly BSD Log</a>, the <a href="http://wiki.dragonflybsd.org" title ="http://wiki.dragonflybsd.org">DragonFly Wiki</a>, and the DragonFly <a href='http://www.dragonflybsd.org/main/forums.cgi' class='external' title="http://www.dragonflybsd.org/main/forums.cgi">mailing lists/newsgroups</a>.
</p>
<a name="Is_there_a_branch_oriented_towards_stability.2C_like_the_FreeBSD.27s_-STABLE.3F"></a><b> Is there a branch oriented towards stability, like the FreeBSD's -STABLE? </b>
<p>Not yet. We intend to emplace most major features on our goals list before we start branching. We do not yet have the development resources required to maintain multiple branches. However, we do have a "DragonFly_Stable" tag which users can synchronize to instead of HEAD. It is just a floating tag indicating a 'reasonably stable point' in development i.e. where the buildworld / buildrelease / buildkernel sequence is likely to work and not producing something that is unusable or too buggy. Keep in mind that "DragonFly_Stable" is a just another tag, so it's not like *BSD's -STABLE. You can use <a href='http://www.dragonflybsd.org/main/dragonfly-preview-supfile' class='external' title="http://www.dragonflybsd.org/main/dragonfly-preview-supfile">this cvsup config file</a> obtaining source tagged "DragonFly_Stable" via cvsup.
</p>
<a name="I_get_garbage_on_the_screen_when_I_boot_or_I_can.27t_seem_to_pause_at_the_initial_boot_menu."></a><b> I get garbage on the screen when I boot or I can't seem to pause at the initial boot menu. </b>
<p>DragonFly, when booting, outputs to both video and serial ports. If the booting computer has a 'noisy' serial device connected, it may read data from it during the boot process. Serial console activation during boot can be disabled by creating the file /boot.config with the contents: '-V'
</p>
<a name="I_can.27t_install_the_XFree86-4_port.3B_it_loops_forever."></a><b> I can't install the XFree86-4 port; it loops forever. </b>
<p>Make sure you have the dfports override for XFree86-4-libraries on your DragonFly machine, and install that individual port. The XFree86-4 metaport should install correctly after that. Or, install the package as root: 'pkg_add -r XFree86'. Using 'pkg_add -r packagename' may often work if a port fails to build.
</p>
<a name="How_can_I_speed_up_my_build_process.3F"></a><b> How can I speed up my build process? </b>
<p>You can use make quickworld instead of make buildworld. This reuses existing tools on disk and speeds this step up considerably. For the kernel there is a similar quickkernel target.
</p>
<a name="But_make_quickworld.2Fquickkernel_fails.21"></a><b> But make quickworld/quickkernel fails! </b>
<p>Try make buildworld or buildkernel instead.
</p>
<a name="What_will_be_used_to_handle_third-party_applications.3F_.28like_ports.2C_RPM.2C_apt-get.2C_etc..29"></a><b> What will be used to handle third-party applications? (like ports, RPM, apt-get, etc.) </b>
<p>Currently, DragonFly uses the existing ports system from FreeBSD4, with local overrides specific to DragonFly located in dfports (default location /usr/dfports). dfports works the same as ports - you can keep it up to date using cvsup. Eventually, DragonFly will have a homegrown port system, using the VFS mechanisms that are as of this writing not complete yet. For more information, check the <a href='http://www.dragonflybsd.org/goals/packages.cgi' class='external' title="http://www.dragonflybsd.org/goals/packages.cgi">existing packaging description</a>.
</p>
<a name="What_architectures_does_DragonFly_support.3F"></a><b> What architectures does DragonFly support? </b>
<p>DragonFly is currently targeted at the x86 line of processors; it should work on 386 and up, though a 386 is certainly not recommended. Work is also being done on support for the new 64-bit processors from AMD. There are currently no plans for support of other processor types. However, support for Sparc or PowerPC or other systems is possible in the future. If you plan to submit code to the DragonFly project, please keep this in mind.
</p>
<a name="How_can_I_contribute.3F"></a><b> How can I contribute? </b>
<p>Pick a topic that you enjoy and start working. Check the <a href='http://www.dragonflybsd.org/main/team.cgi' class='external' title="http://www.dragonflybsd.org/main/team.cgi">team page</a> to see if there are others interested in your topic, or ask around in the <a href='http://www.dragonflybsd.org/main/forums.cgi' class='external' title="http://www.dragonflybsd.org/main/forums.cgi">appropriate forum</a>. You can <a href='http://www.dragonflybsd.org/main/download.cgi' class='external' title="http://www.dragonflybsd.org/main/download.cgi">download the source</a> to the operating system and to the official site, and send patches in unified diff format (diff -uN) to 'submit at dragonflybsd.org' for review. Subscribe to that same submit <a href='http://www.dragonflybsd.org/main/forums.cgi' class='external' title="http://www.dragonflybsd.org/main/forums.cgi">mailing list/newsgroup</a> to see feedback on your patches, and to find if they have been accepted or rejected. In addition, you can update the <a href='http://wiki.dragonflybsd.org' class='external' title="http://wiki.dragonflybsd.org">DragonFly Wiki</a>.
</p><p>Note that you do not have to be a programmer in order to help.  Evangelizing DragonFly and testing it on a variety of hardware, and reporting results can help a great deal.  Try new features and report to the forums on your experiences.  Cleaning up /etc/rc.d only requires shell script experience, for instance, and there's always a need for better documentation.
</p>
<a name="Will_DragonFly_use_a_dynamic_.2Fdev_filesystem.2C_as_in_devfs.3F"></a><b> Will DragonFly use a dynamic /dev filesystem, as in devfs? </b>
<p>Current plans are to keep the existing filesystem model, with the removal of minor/major numbering. There may be a 'devd' process to handle dynamic devices. There are other features to complete first before this is tackled.
</p>
<a name="Will_DragonFly_use_.28insert_name_here.29_technology.3F"></a><b> Will DragonFly use (insert name here) technology? </b>
<p>Yes and no. Features must match the existing plan outlined on the site here, and there's plenty of existing problems to solve before 'nonessential' work can be done. However, if you are willing to work on it, it probably can be done. The forums are an excellent place to get feedback and to find others that may be interested in your topic. The <a href='http://www.dragonflybsd.org/main/team.cgi' class='external' title="http://www.dragonflybsd.org/main/team.cgi">team page</a> is also a good place to check.
</p>
<a name="What.27s_the_correct_way_to_name_this_operating_system.3F"></a><b> What's the correct way to name this operating system? </b>
<p>It's a BSD variant, called DragonFly. Note the capitalization on the F, which isn't proper English.
</p>
<a name="Mplayer_port_build_fails"></a><b> Mplayer port build fails </b>
<p>Copy <a href="http://leaf.dragonflybsd.org/~joerg/mplayer.tgz" class='external' title="http://leaf.dragonflybsd.org/~joerg/mplayer.tgz">http://leaf.dragonflybsd.org/~joerg/mplayer.tgz</a> into your /usr/ports/multimedia and unpack it. Change directory back to /usr/ports/multimedia/mplayer and build it using 'make install'.
</p>
