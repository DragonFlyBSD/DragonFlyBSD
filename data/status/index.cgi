#!/usr/local/www/cgi-bin/tablecg
#
# $DragonFly: site/data/status/Attic/index.cgi,v 1.3 2003/08/11 02:24:52 dillon Exp $

$TITLE(DragonFly - Big-Picture Status)
<P><B>Caching</B> - not started
<BR><B>IOModel</B> - in progress
<BR><B>VFSModel</B> - not started
<BR><B>Messaging</B> - in progress 
<BR><B>Threads</B> - Mostly done
<BR><B>UserAPI</B> - not started
<BR><B>Packages</B> - not started
<P>
This section lists project (self) assignments and sub-projects that we have
brainstormed about that developers could dig their teeth in to.  Related 
discussions will be in the kernel list and submissions should be posted to
the submit list.
<UL>
    <TABLE BORDER=1>
	<TR>
	    <TD><B>Label</B></TD>
	    <TD><B>Work Unit</B></TD>
	    <TD><B>Developer</B></TD>
	    <TD><B>Requirements</B></TD>
	    <TD><B>StartDate</B></TD>
	    <TD><B>EndDate</B></TD>
	    <TD><B>Status</B></TD>
	</TR>
	<TR>
	    <TD>RCNG</TD>
	    <TD>Merge 5.x RCNG</TD>
	    <TD>Robert Garrett</TD>
	    <TD>None</TD>
	    <TD>17-Jul-2003</TD>
	    <TD>xx-Jul-2003</TD>
	    <TD>Completed</TD>
	</TR>
	<TR>
	    <TD>DEV1</TD>
	    <TD>Core Messaging / DEV Wrapper</TD>
	    <TD>Matthew Dillon</TD>
	    <TD>None</TD>
	    <TD>17-Jul-2003</TD>
	    <TD>None</TD>
	    <TD>In Progress</TD>
	</TR>
	<TR>
	    <TD>SYSCALL1</TD>
	    <TD>Syscall Messaging & Wrapper</TD>
	    <TD>Matthew Dillon</TD>
	    <TD>DEV1</TD>
	    <TD>17-Jul-2003</TD>
	    <TD>None</TD>
	    <TD>In Progress</TD>
	</TR>
	<TR>
	    <TD>VFS1</TD>
	    <TD>VFS Messaging & Wrapper</TD>
	    <TD>Matthew Dillon</TD>
	    <TD>SYSCALL1</TD>
	    <TD>17-Jul-2003</TD>
	    <TD>None</TD>
	    <TD>Not started</TD>
	</TR>
	<TR>
	    <TD>NOSECURE</TD>
	    <TD>NOSECURE build variable removal</TD>
	    <TD>Jeroen Ruigrok van der Werven</TD>
	    <TD>None</TD>
	    <TD>2-Aug-2003</TD>
	    <TD>3-Aug-2003</TD>
	    <TD>Completed</TD>
	</TR>
	<TR>
	    <TD>KERBEROSIV</TD>
	    <TD>Kerberos IV removal</TD>
	    <TD>Jeroen Ruigrok van der Werven</TD>
	    <TD>NOSECURE</TD>
	    <TD>5-Aug-2003</TD>
	    <TD>10-Aug-2003</TD>
	    <TD>Completed</TD>
	</TR>
    </TABLE>
</UL>
