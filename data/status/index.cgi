#!/usr/local/www/cgi-bin/tablecg
#
# $DragonFly: site/data/status/Attic/index.cgi,v 1.10 2004/03/06 14:39:08 hmp Exp $

$TITLE(DragonFly - Big-Picture Status)

<p>
This section lists project (self) assignments and sub-projects that we have
brainstormed about that developers could dig their teeth in to.  Related 
discussions will be in the kernel list and submissions should be posted to
the submit list.</p>

<ul>
	<li>Caching - not started</li>
	<li>IOModel - in progress</li>
	<li>VFSModel - not started</li>
	<li>Messaging - in progress</li>
	<li>Threads - mostly done</li>
	<li>UserAPI - not started</li>
	<li>Packages - not started</li>
</ul>

<table border="1" cellpadding="3" width="100%">
<tr>
    <td><b>Label</b></td>
    <td><b>Work Unit</b></td>
    <td><b>Developer</b></td>
    <td><b>Requirements</b></td>
    <td><b>StartDate</b></td>
    <td><b>EndDate</b></td>
    <td><b>Status</b></td>
</tr>
<tr>
    <td>RCNG</td>
    <td>Merge 5.x RCNG</td>
    <td>Robert Garrett</td>
    <td>None</td>
    <td>17-Jul-2003</td>
    <td>xx-Jul-2003</td>
    <td>Completed</td>
</tr>
<tr>
    <td>DEV1</td>
    <td>Core Messaging / DEV Wrapper</td>
    <td>Matthew Dillon</td>
    <td>None</td>
    <td>17-Jul-2003</td>
    <td>None</td>
    <td>In Progress</td>
</tr>
<tr>
    <td>SYSCALL1</td>
    <td>Syscall Messaging &amp; Wrapper</td>
    <td>Matthew Dillon</td>
    <td>DEV1</td>
    <td>17-Jul-2003</td>
    <td>None</td>
    <td>In Progress</td>
</tr>
<tr>
    <td>VFS1</td>
    <td>VFS Messaging & Wrapper</td>
    <td>Matthew Dillon</td>
    <td>SYSCALL1</td>
    <td>17-Jul-2003</td>
    <td>None</td>
    <td>Not started</td>
</tr>
<tr>
    <td>NOSECURE</td>
    <td>NOSECURE build variable removal</td>
    <td>Jeroen Ruigrok van der Werven</td>
    <td>None</td>
    <td>2-Aug-2003</td>
    <td>3-Aug-2003</td>
    <td>Completed</td>
</tr>
<tr>
    <td>KERBEROSIV</td>
    <td>Kerberos IV removal</td>
    <td>Jeroen Ruigrok van der Werven</td>
    <td>NOSECURE</td>
    <td>5-Aug-2003</td>
    <td>10-Aug-2003</td>
    <td>Completed</td>
</tr>
<tr>
    <td>SLAB</td>
    <td>Slab Allocator for Kernel</td>
    <td>Matt Dillon</td>
    <td>&nbsp;</td>
    <td>25-Aug-2003</td>
    <td>31-Aug-2003</td>
    <td>Completed</td>
</tr>
<tr>
    <td>NETNS</td>
    <td>Fix NETNS</td>
    <td>David Rhodus</td>
    <td>&nbsp;</td>
    <td>6-Sept-2003</td>
    <td>7-Sept-2003</td>
    <td>Completed</td>
</tr>
</table>

