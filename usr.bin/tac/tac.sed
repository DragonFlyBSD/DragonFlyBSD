#!/usr/bin/sed -nf

# From the GNU sed manual, section 4.6.
# Copyright terms thereof:
#
# This document is released under the terms of the GNU Free Documentation
# License as published by the Free Software Foundation; either version 1.1, or
# (at your option) any later version.
#
# $DragonFly: src/usr.bin/tac/Attic/tac.sed,v 1.1 2007/07/14 11:56:19 corecode Exp $

# reverse all lines of input, i.e. first line became last, ...

# from the second line, the buffer (which contains all previous lines)
# is *appended* to current line, so, the order will be reversed
1! G

# on the last line we're done -- print everything
$ p

# store everything on the buffer again
h
