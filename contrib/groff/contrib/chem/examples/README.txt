This directory contains examples for the `chem' language.

You can view the graphical display of the examples by calling

    groffer <file>

`groffer' calls `chem' automatically.

If you want to transform example files to a different format use the
`roff2*' programs:

`roff2dvi' prints dvi format to standard output,
`roff2html' generates html output,
`roff2pdf' outputs pdf mode,
`roff2ps' produces PostScript output,
`roff2text' generates text output in the groff device `latin1',
`roff2x' prints the output  in  the  groff  device  X  that  is
         suitable  for programs  like `gxditview' or `xditview'.

To get a suitable `groff' output run

    @g@chem <file> | groff -p ...

On the displays, you can see rings consisting of several lines and
bonds (lines).  All points on rings and bonds that do not have a
notation mean a C atom (carbon) filled with H atoms (hydrogen) such
that the valence of 4 is satisfied.

For example, suppose you have just a single line without any
characters.  That means a bond.  It has two points, one at each end of
the line.  So each of these points stands for a C atom, the bond
itself connects these 2 C atoms.  To fulfill the valence of 4, each
points has to carry additionally 3 H atoms.  So the single empty bond
stands for CH3-CH3, though this combination doesn't make much sense
chemically.


####### License

Last update: 5 Jan 2009

Copyright (C) 2006, 2009 Free Software Foundation, Inc.
Written by Bernd Warken.

This file is part of `chem', which is part of `groff'.

`groff' is free software; you can redistribute it and/or modify it
under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

`groff' is distributed in the hope that it will be useful, but WITHOUT
ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
for more details.

You should have received a copy of the GNU General Public License
along with this program. If not, see <http://www.gnu.org/licenses/>.


####### Emacs settings

Local Variables:
mode: text
End:
