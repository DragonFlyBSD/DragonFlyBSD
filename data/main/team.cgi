#!/usr/local/www/cgi-bin/tablecg
#
# $DragonFly: site/data/main/Attic/team.cgi,v 1.26 2005/02/01 03:46:27 drhodus Exp $

$TITLE(The DragonFly Team)

<p>
Like many open source projects, DragonFly is a technical project that uses 
a team of geographically separated people.  There are no restrictions on 
who can contribute or how you can contribute, other than peer standards.  
Listed here are some of the people who have made this project possible.</p>

<p>
<strong>Project Leader</strong>:<br />
<a href="mailto:dillon 'at' apollo 'dot' backplane 'dot' com">Matthew Dillon</a>
is known for creating the DICE C compiler on the Amiga, and 
later co-founding BEST Internet in San Francisco.  Matt has also
contributed code to the FreeBSD project and the Linux kernel for
systems such as VM and NFS.</p>

<p>
Matt is the founder of the DragonFly BSD project.  Matt is also the 
principal code contributor to DragonFly, and supports the website and 
other online resources for this project.  He has been working on or 
completed DragonFly projects such as variant symlinks, MPIPE, the slab 
allocator, the namecache, LWKT, dfports, the 'live CD', AMD64 work, 
and much more, including coordination on projects other 
contributors have submitted.
</p>

<p>
<strong>Contributors</strong>:<br />
Many individuals have stepped up to 
contribute various pieces of code, documentation, ideas, and feedback 
to the DragonFly project.  Here's a partial list.
</p>

<table width="100%" cellpadding="3" cellspacing="0" border="1"
style="border-style: flat; border-collapse: collapse; border-color: #BEBEBE;">
<tr bgcolor="#ffcc00">
<th>Name</th><th>Area of Interest/Contribution</th>
</tr>

<tr><td valign="top"><a href="mailto:joe 'at' angerson 'dot' com">Joe Angerson</a></td>
<td valign="top">The DragonFly Mascot Artwork</td>
</tr>

<tr><td valign="top"><a href="mailto:jcoombs 'at' gwi 'dot' net">Joshua Coombs</a></td>
<td valign="top">Sun Grid Engine</td>
</tr>

<tr><td valign="top"><a href="mailto:craig 'at' xlnx-x 'dot' net">Craig Dooley</a></td>
<td valign="top">K&amp;R to ANSI function cleanup, __P() removal, gcc3 building</td>
</tr>

<tr><td valign="top"><a href="mailto:liamfoy 'at' kerneled 'dot' org">Liam J. Foy</a></td>
<td valign="top">Code cleanness and userland utilities (commit access)</td>
</tr>

<tr><td valign="top"><a href="mailto:rgarrett24 'at' cox  'dot' net">Robert Garrett</a> </td>
<td valign="top">RCNG, system installation tool
(commit access)</td>
</tr>

<tr><td valign="top">Jeffrey Hsu </td>
<td valign="top">Multithreading the network stack, RFC compliance
(commit access)</td>
</tr>

<tr><td valign="top"><a href="mailto:virtus 'at' wanadoo 'dot' nl">Douwe Kiela</a></td>
<td valign="top">Code cleanness and standards conformation
</td>
</tr>

<tr><td valign="top"><a href="mailto:saw 'at' online 'dot' de">Sascha Wildner</a></td>
<td valign="top">Syscons driver, code cleanness and userland utilities.
(commit access)</td>
</tr>

<tr><td valign="top"><a href="mailto:coolvibe 'at' hackerheaven 'dot' org">Emiel Kollof</a></td>
<td valign="top">NVIDIA binary driver port override, misc kernel stuff, software porting.
</td>
</tr>

<tr><td valign="top"><a href="mailto:kmacy 'at' fsmware 'dot' com">Kip Macy</a></td>
<td valign="top">Checkpointing
</td>
</tr>

<tr><td valign="top"><a href="mailto:andre 'at' digirati 'dot' com 'dot' br">Andre Nathan</a> </td>
<td valign="top">Code cleanup, 'route show'
</td>
</tr>

<tr><td valign="top"><a href="mailto:eirikn 'at' kerneled 'dot' com">Eirik Nygaard</a> </td>
<td valign="top">Code cleanness and userland utilities
(commit access)</td>
</tr>

<tr><td valign="top"><a href="mailto:hmp 'at' backplane 'dot' com">Hiten Pandya</a> </td>
<td valign="top">Anything and Everything
(commit access)</td></tr>

<tr><td valign="top"><a href="mailto:cpressey 'at' catseye 'dot' mine 'dot' nu">Chris Pressey</a> </td>
<td valign="top">Janitorial work, Installer
(commit access)</td></tr>

<!--
<tr><td valign="top"><a href="mailto:daver 'at' gomerbud 'dot' com">David Reese</a> </td>
<td valign="top">Syscall separation, stackgap allocation removal
(commit access)</td>
</tr>
-->

<tr><td valign="top"><a href="mailto:drhodus 'at' machdep 'dot' com">David Rhodus</a> </td>
<td valign="top">ACPI, ATAng, security upgrades, NFS, tinderbox builds
(commit access)</td>
</tr>

<tr><td valign="top"><a href="mailto:asmodai 'at' wxs 'dot' nl">Jeroen Ruigrok van der Werven</a> </td>
<td valign="top">Regression testing, algorithm testing, subsystems
(PCI, USB, AGP, UDF, ISO9660, etc), compiler and utilities.
(commit access)</td>
</tr>

<tr><td valign="top"><a href="mailto:galen_sampson 'at' yahoo 'dot' com">Galen Sampson</a></td>
<td valign="top">LWKT port to userland
</td>
</tr>

<tr><td valign="top">Hiroki Sato </td>
<td valign="top">Mirror in Japan (AllBSD)
</td>
</tr>

<tr><td valign="top"><a href="mailto:corecode 'at' fs 'dot' ei 'dot' tum 'dot' de">Simon 'corecode' Schubert</a></td>
<td valign="top">Mirror in Germany, daily snapshots
</td>

</tr>

<tr><td valign="top">
<a href="mailto: joerg 'at' bec 'dot' de">J&ouml;rg Sonnenberger</a></td>
<td valign="top">Anything and Everything except web site
(commit access)
</td>
</tr>

<tr><td valign="top">
<a href="mailto:justin 'at' shiningsilence 'dot' com">Justin Sherrill</a></td>
<td valign="top">Secretarial work, documentation, website cleanup
(commit access)</td>
</tr>

<tr><td valign="top"><a href="mailto:geekgod 'at' geekgod 'dot' com">Scott Ullrich</a></td>
<td valign="top">Installer
(commit access)</td>
</tr>

<tr>
<td valign="top"><a href="mailto:okumoto 'at' ucsd 'dot' edu">Max Okumoto</a></td>
<td valign="top">usr.bin/make (commit access)</td>
</tr>

</table>

