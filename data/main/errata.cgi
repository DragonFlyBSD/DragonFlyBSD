#!/usr/local/www/cgi-bin/tablecg
#
# $DragonFly: site/data/main/Attic/errata.cgi,v 1.1 2004/08/31 00:48:17 dillon Exp $

$TITLE(DragonFly - Errata)
<h1>Errata for DragonFly ISOs</h1>
<p>
<CENTER>DragonFly Errata</CENTER>
</p><p>
<table BORDER="1">
<tr>
<th>Id</th>
<th>Versions</th>
<th>Fixed</th>
<th>Description</th>
</tr>

<tr>
<td>#1</td>
<td>REL1.0</td>
<td>REL1.0A</td>
<td>The installer creates a number of files in /etc, /usr/local/bin,
/usr/local/etc, and /usr/local/share/pristine owned by user 1000 and/or
group 1000, including files or directories writable by that user or
group.</td>
</tr>

<tr>
<td>#2</td>
<td>&lt;=REL1.0A</td>
<td>[NextRel]</td>
<td>crond and syslogd are not turned on by default (in /etc/rc.conf) in a
newly installed system.
</tr>

<tr>
<td>#3</td>
<td>&lt;=REL1.0A</td>
<td>[current as of 21Jul2004]</td>
<td>ATA-RAID ('ar') contains stale code that causes the raid to be marked
damaged, making it unusable and requiring the array to be destroyed and
recreated, and the partition tables to be restored using the same settings
in order to fix (the scan_ffs port helps a lot in that regard).  Do not
use the 1.0A ISO with ata-raid.
</tr>

</table>
</p>
