#!/usr/local/www/cgi-bin/tablecg
#
# $DragonFly: site/data/docs/nanosleep/Attic/index.cgi,v 1.1 2004/01/22 21:55:58 justin Exp $

$TITLE(DragonFly and nanosleep&#40;&#41;)

<body>
<center><H2><span style="font-family: helvetica,arial,sans-serif;">
How long does <i>your</i> nanosleep() really sleep?</span></H2></center>
<p>
<UL>
<LI><a href="#intro">Introduction</a></LI>
<LI><a href="#problem">Problem</a></LI>
<LI><a href="#observe">Observations</a></LI>
<LI><a href="#alias">Clock Aliasing</a></LI>
<LI><a href="#concl">Conclusion</a></LI>
</UL>

<blockquote>
<i>
Committer's note: This was originally written by Paul Herman in 
January 2004.  He has graciously allowed it to be posted on 
the DragonFly site.
</i>
</blockquote>

<a name="intro">
<b style="font-family: helvetica,arial,sans-serif;">Introduction</b></a>
<br>
Have you ever wonder why the command:<pre>
  time sleep 1
</pre>
never returns exactly 1 second? You might know the
answer, but if you think you do, <a href="#preemt">you might also be wrong</a>.</p>
<p>
Accurate time keeping and event triggering is becoming more and more affordable
as PC hardware becomes increasingly faster.  Network communication is now a matter
of microseconds and system call round trip times are accomplished within nanoseconds.
This document examines the accuracy of the <tt>nanosleep()</tt>/<tt>tsleep()</tt>
kernel functions under DragonFly BSD 1.0-CURRENT.
</p>
<p>
<a name="problem">
<b style="font-family: helvetica,arial,sans-serif;">Problem</b></a><br>
Suppose we wish to trigger an event at an <i>exact time</i> on the clock, i.e.
every second just as the second hand moves.  In pseudo code, we wish to do
something like:<pre>
	forever
		now = look_at_clock()
		s = <i>next second</i> - now
		sleep(s)
		trigger_event()
	end
</pre>
<p>
So, if our clock reads 10:45:22 and the second hand reads 22.874358,
then by the time our sleep function wakes up, it should be <i>exactly</i>
10:45:23.  Similarly, the next call to <tt>trigger_event()</tt> will happen
at <i>exactly</i> 10:45:24. So much for the theory, let's try this in the
real world and see how close we can get.
</p>
<p>
<a name="observe">
<b style="font-family: helvetica,arial,sans-serif;">Observations</b></a><br>
I quickly coded up <a href="wakeup_latency.c">wakeup_latency.c</a> which sleeps until the next
second and then prints the difference in seconds between the actual time and the
time expected. On a GHz machine, I expected this to be very accurate, but to my
astonishment, I got the following:<pre>
  0.019496
  0.019506
  0.019516
  0.019525
  0.019535
</pre>
i.e. 19ms later than expected!  This is is nearly <i>twice</i> the default system
tick length of 10ms based on 100 Hz!!  That is unexpected and certainly undesireable.
I then ran the program on:
<UL>
<LI>DragonFlyBSD 1.0-CURRENT (i386)
<LI>Darwin 7.0.0 / Panther (iMac G4)
<LI>FreeBSD 4.9-STABLE (i386)
<LI>Linux 2.4.21 (i386)
<LI>Solaris 8 (sparc)
</UL>
All OSes had their default Hz unchanged (which I'm pretty sure is 100
in all cases.)
The following figure shows the output of
<a href="wakeup_latency.c">wakeup_latency.c</a> running for 15 minutes
(900 seconds), i.e. the number of seconds that usleep() additionally slept:<br>
<center><img src="wakeup_lat_by_os.png"></center><br>
<p>
Well, there are quite a few things to notice here indeed.
<UL>
<LI>All seem to guarantee to sleep <i>at least</i> as long as requested (all values are positive,
a requirement by the Open Group's SUSv2.)</LI>
<LI>All save Darwin don't seem to even make the effort to return when they should, which should be immediately after the second hand tick.</LI>
<LI>Most return after <i>at least</i> one kernel tick has passed.</LI>
<LI>FreeBSD &amp; DFly express some wierd sawtooth action.  Is it clock aliasing? Interesing note here,
this delay was process independent.  No matter how many process were running, all <tt>usleep()</tt>
calls at the same time took the same time.  Strange indeed.</LI>
</UL>
</p>
<p>
What happens when you keep the OS &amp; hardware constant and change the Hz?
A good question with even more unexpected results.
This following figure shows what happens on DFly 1.0.<br>
<center><img src="wakeup_lat_3d_by_hz.png"></center><br>
Although the sawtooth wave is not easily seen here, we see that increasing kern.hz
does help, but only up to around 1000.  Beyond that the speed improvement breaks down.
The following figure shows this in finer detail.<br>
<center><img src="wakeup_lat_by_hz.png"></center><br>
</p>
<p>
Now that we've characterized the problem, let's see if we can solve it.
</p>
<p>
<a name="soln">
<b style="font-family: helvetica,arial,sans-serif;">Solutions?</b></a><br>
The first place I looked was <tt>kern/kern_time.c:nanosleep()</tt>.  Although
it does truncate the precision of the request from nanoseconds to microseconds (and then from
microseconds to ticks via <tt>tvtohz()</tt>), that alone shouldn't account for the differences
we are seeing.  However, a closer examination shows a bug in <tt>tvtohz()</tt>
(<tt>kern/kern_clock.c</tt> rev 1.12).
</p>
<p>
With hz=100 and ticks=10000, I get:<br>
<center><table width=%40 border=2 cellpadding=2>
<tr align=center><td>seconds</td>	<td>theoretical ticks <tt>rint(seconds*Hz)</tt></td>	<td><pre>tvtohz()</pre></td></tr>
<tr align=center><td>0.900000</td>	<td>90</td>	<td>91</td></tr>
<tr align=center><td>0.990000</td>	<td>99</td>	<td>100</td></tr>
<tr align=center><td>0.999000</td>	<td>100</td>	<td>101</td></tr>
<tr align=center><td>1.000000</td>	<td>100</td>	<td>101</td></tr>
<tr align=center><td>1.000001</td>	<td>100</td>	<td>101</td></tr>
<tr align=center><td>1.000002</td>	<td>100</td>	<td>102</td></tr>
<tr align=center><td>1.001000</td>	<td>100</td>	<td>102</td></tr>
</table></center>
<p>

This change was apparently introduced into FreeBSD rev 1.11 in 1994.  This should be corrected, and
the bug that it was meant to fix (<tt>sleep(1)</tt> exiting too soon) should be
properly fixed.  I did with <a href="tvtohz.patch">this patch</a>, and now our <tt>usleep()</tt>
delays look a bit better:

</p>
<center><img src="wakeup_lat_tvtohz_fix.png"></center><br>
<p>
<a name="alias">
<b style="font-family: helvetica,arial,sans-serif;">Clock Aliasing</b></a><br>
So what about the sawtooth patterns?  The kernel clock shouldn't behave that way,
and something is definitely rotten in Denmark.</p>
<p>
A keen eye will see in the last figure that the change to <tt>tvtohz()</tt> produced
a slightly steeper or quicker frequency of the sawtooth wave.  What does this mean?
Well, now that for any given timeval,
<tt>tvtohz()</tt> returns fewer ticks, the system requires fewer ticks in order to cover
the same time span, or to put it differently, fewer "microseconds" to cover the same
wall clock time, we have effectively (at least from the view of the Hz timer) sped up
the system clock.  Interesting.  It seems the Hz timer is out of sync with the system
hardware clock producing aliasing.  This is not unlike the aliasing you see in movies
when automobile wheels appear to turn backwards.  In that case, the camera shutter
frequency is slightly out of sync with the turning of the wheels.
</p>
<p>
To confirm that our "wheels" our indeed out of sync, I ran the same program I've been
using all along, and at the same time tinkered with the clock frequency using
<tt>ntptime [ -f freq ]</tt> (like adjusting the speed of the car.)  Sure enough,
I was able to tame the observed delays any way I pleased.  Other evidence of a
software error is that the Hz timer is clocked off of the i8254 clock which is
the same clock as the hardware timecounter, i.e. the i8254 based timecounter appears
to run faster than the i8254 Hz timer!  There <b>must</b> be a software bug.
</p>
<p>
Upon further investigation, Matt Dillon suggested that the drift could be caused
by the cumulative effect of a system tick not being an integral multiple of an
i8254 timer tick, which in effect speeds up the Hz timer to a speed that is
faster than it should be.  The solution was to periodically slow down the
Hz timer by a small fraction so that on the average, the Hz timer would tick
exactly as it should.
</p>
<p>
This initial change produced very promising results, which led to Matt to improve the
design and develop a PLL loop in clkintr() which slowly adjusts the frequency of the
Hz timer against the wall clock timer (be it i8254 or TSC) to get it as close to the
target Hz as possible. This change can is refelcted in <a
href="http://www.dragonflybsd.org/cgi-bin/cvsweb.cgi/src/sys/i386/isa/clock.c.diff?r1=1.7&r2=1.10">
Revisions 1.8 - 1.10 in clock.c</a>.  A nice side effect of this change is that because
the Hz timer is skewed against the wall clock, any clock disciplining done to the wall
clock time (by <tt>ntpd</tt>, for example) will also adjust the Hz rate accordingly.
</p>
<p>
The improvement is reflected in the following figure using the same old
<a href="wakeup_latency.c">wakeup_latency.c</a> tests (with slight changes to improve
precision.)  Due to the nature of the PLL which takes a few minutes to stablize, one
can see that there is clock aliasing until a lock on the frequency is attained.  After
that, the Hz timer appears very accurate, indeed.
</p>
<center><img src="wakeup_lat_alias_fix.png"></center><br>
<p>
Here is a closer look, both with the PLL patch at different Hz rates (using the
TSC as the wall clock timecounter.)
</p>
<center><img src="wakeup_lat_alias_fix_byhz.png"></center><br>
<p>
There is some slight improvement with higher Hz rates, but both seem to perform very
well now, generally staying within 10 microseconds of the expected time (about
a thousand times better than some other operating systems.)  At this fine
resolution, you can even see how other processes affect latency, such as
the periodic syncer daemon.  The difference is like looking through the hubble
telescope rather than binoculars.
</p>
<br>
<p>
<a name="concl">
<b style="font-family: helvetica,arial,sans-serif;">Conclusion</b></a><br>
</p>
<p>
As with everything, answering some questions will potentially open up new ones.
One can naturally ask "why are we limited to around 10 microseconds when the
i8254 has about an 0.8 microsecond resolution?", but the changes already made
represent a <em>significant</em> improvement in the standard Hz timer, and one
can only be pleased with the results.
</p>
<p>
In conclusion, we see that
after asking a rather academic question such as "why isn't sleep
very accurate?" which may have little practical application, some minor deficiencies
were uncovered that potentialy have very <i>practical</i> applications, such as accurate
bandwidth shaping as measured against a well disciplined wall clock.
</p>
<br>
<p>
<a name="preemt">
<b style="font-family: helvetica,arial,sans-serif;">Appendix: Still Questions?</b></a><br>
Finally, I'd like to stave off some possible questions you may have at this point.</p>
<p>
<i><b>Q: Why don't you renice process to a higher priority so the process will
wake up sooner?</b></i><br>
A: I did, no difference.<br>
<br>
<i><b>Q: That's easy to explain, <tt>gettimeofday()</tt> is a system call, and system calls
have lots of overhead.  That's why it takes so long.</b></i><br>
A: Nope.  I've found that <tt>clock_gettime()</tt> takes only a total of
about 500-800 nanoseconds when called from userland on a GHz machine, and
<tt>nanotime()</tt> takes only about 60 ns when called directly from inside the kernel.<br>
<br>
<b><i>Q: Just increase kern.hz and your delay will go away.</i></b><br>
A: No they won't. Sure, <tt>usleep()</tt> will return faster, but there is still a definite delay problem.
See the pretty graphs above.<br>
<br>
<b><i>Q: Use the TSC timer instead of the i8254. It has a much higher resolution.</i></b><br>
A: I tried both, no difference.<br>
</p>
<br>
<br>
<hr align=left width="66%">
<address>
Paul Herman<br>
Professor at the University of my Living Room Sofa<br>
Senior Researcher of the Kitchen Refrigerator<br>
Guest monthly lectures at the local laundrymat<br>
<br>
pherman@frenchfries.net<br>
January 8, 2004<br>
</address>
</BODY>
</HTML>
