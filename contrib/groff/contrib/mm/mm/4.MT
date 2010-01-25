.ig

Copyright (C) 1991-2000, 2009
  Free Software Foundation, Inc.
mgm is written by Jörgen Hägg <jh@axis.com>

mgm is free software; you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free
Software Foundation, either version 3 of the License, or
(at your option) any later version.

mgm is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or
FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
for more details.

You should have received a copy of the GNU General Public License
along with this program. If not, see <http://www.gnu.org/licenses/>.

Please send bugreports with examples to jh@axis.com.
..
.
.\"------------
.\" Cover sheet. Memorandum type 4
.\"------------
.de cov@print-title
.if !d cov*title .@error title (.TL) not defined!
.MOVE 2.8c
.S +2
.ad c
.fi
.B
.cov*title
.br
.S
.R
.ad b
..
.\"------------
.de cov@print-authors
.SP 0.5
.I
.S +1
.nr cov*i 0 1
.while \\n+[cov*i]<=\\n[cov*au] \{\
.ce
\\*[cov*au!\\n[cov*i]!1]
.br
.\}
.S
.R
..
.\"------------
.de cov@print-firm
.if d cov*firm \{\
.	SP 0.5
.	ce
\\*[cov*firm]
.\}
..
.\"------------
.de cov@print-abstract
.SP 2
.if d cov*abstract \{\
.	misc@ev-keep cov*ev
.	init@reset
.	if \\n[cov*abs-ind]>0 \{\
.		in +\\n[cov*abs-ind]u
.		ll -\\n[cov*abs-ind]u
.	\}
.	ce
\fI\\*[cov*abs-name]\fP
.	SP 2
.	fi
.	cov*abstract
.	br
.	ev
.\}
..
.\"-----------------
.if d cov*default-firm .if !d cov*firm .ds cov*firm \\*[cov*default-firm]
.if !d cov*mt-printed \{\
.	cov@print-title
.	cov@print-authors
.	cov@print-firm
.	if d cov*abstract \{\
.		if !\n[cov*abs-arg] .cov@print-abstract
.	\}
.	SP 2
.	nr hd*cur-bline \n[nl]
.	ds cov*mt-printed
.	pg@enable-top-trap
.	pg@enable-trap
.\}
.de CS
.pg@disable-top-trap
.SK
.cov@print-title
.cov@print-authors
.cov@print-firm
.cov@print-abstract
..

