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
.\" Cover sheet. Memorandum type 5
.\"------------
.nr cov*mt0-ind 1.1c
.de cov@print-title
.B
.ll 9c
.fi
.cov*title
.R
.ll
.nf
.if d cov*title-charge-case \fBCharge Case \\*[cov*title-charge-case]\fP
.if d cov*title-file-case \fBFile Case \\*[cov*title-file-case]\fP
.fi
..
.\"------------
.de cov@print-date
.rj 1
\f[\\*[@sdf_font]]\\*[cov*new-date]\fP
.br
..
.\"------------
.if !d cov*mt-printed \{\
.	SP 1.9c
.	cov@print-title 
.	SP 1.2c
.	cov@print-date 
.	SP 3
.	pg@enable-top-trap
.	pg@enable-trap
.	ds cov*mt-printed
.\}
