#!/bin/sh
#
# Script to assert that the man-refs.ent file is in lexicographical
# order by section number then by name.  Originally by Dima Dorfman,
# public domain, 2001.
#
# This assumes that all the man pages for one section are in one
# contiguous block.  It will scan the block and make sure that the man
# pages are in lexicographical order.  Note that it only looks at the
# man page entity, not the name, so the dots (.) used in place of
# special characters (e.g., "_", "[") won't get expanded.  It will not
# edit the file in place; only alert the operator to the discrepancy.
# Furthermore, once it found something wrong, it will stop, so it may
# be necessary to run it more than once per section to make sure
# everything is clean.
#
# Used to create doc/share/sgml/man-refs.ent rev. 1.85 (dd, 2001/06/30).
#
# Exit codes: 0- okay; 1- something out of order; 2- something else;
#
# $DragonFly: doc/share/examples/check-manref.sh,v 1.1.1.1 2004/04/02 09:36:25 hmp Exp $

# Input: $wol_l - line
# Output: $wol_w - word
word_on_line () {
    wol_w=`grep -n . $i | grep "^${wol_l}:" | \
	sed -E 's/^[[:digit:]]*:<\\!ENTITY man[.]([-[:alnum:].]*)[.][[:digit:]]* \".*$$/\1/'`;
}

if [ X$2 = X ]; then
    echo "usage: $0 file section";
    exit 2;
fi

i=$1;
s=$2;

firstline=`grep -n "^<\\!ENTITY man[.][-[:alnum:].]*[.]$s \"" $i | \
    head -1 | cut -d: -f1`;
if [ "X$firstline" = "X" ]; then
    echo "Can't find first line of section $s.";
    exit 2;
fi
echo "First line of section $s is $firstline.";
lastline=`grep -n "^<\\!ENTITY man[.][-[:alnum:].]*[.]$s \"" $i | \
    tail -1 | cut -d: -f1`;
if [ "X$lastline" = "X" ]; then
    echo "Can't find last line of section $s.";
    exit 2;
fi
echo "Last line of section $s is $lastline.";

x=$firstline;
while [ $x != $lastline ]; do
    wol_l=$x;
    word_on_line;
    if [ "$last" ]; then
	if [ "$last" = "$wol_w" ]; then
	    echo "Duplicate \"$last\" (l. $x).";
	    exit 1;
	elif [ "$last" '>' "$wol_w" ]; then
	    echo "Out of order: \"$wol_w\" after \"$last\" (l. $x).";
	    exit 1;
	fi
    fi
    last=$wol_w;
    x=`expr $x + 1`;
done;

exit 0;
