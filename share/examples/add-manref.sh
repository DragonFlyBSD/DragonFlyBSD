#!/bin/sh
#
# Script to properly insert an entry into the man-refs.ent file.
# Originally by Dima Dorfman, public domain, 2001.
#
# This makes *a lot* of assumptions about the format; particularly it
# requires that all man pages in a section are in one contiguous
# block, and that everything is sorted alphabetically within the
# aforementioned block.  This will edit the file in place, but will
# not commit it; since it doesn't do a good job of handling corner
# cases, the user should review its work before committing its
# changes.
#
# This is also very ugly, but that's the price of not having Tcl in
# the base system!
#
# Tested with doc/share/sgml/man-refs.ent rev. 1.85 (dd, 2001/06/30).
#
# Exit codes: 0- added; 1- exists; 2- something else;
#
# $DragonFly: doc/share/examples/add-manref.sh,v 1.1.1.1 2004/04/02 09:36:23 hmp Exp $
#

# Input: $wol_l - line
# Output: $wol_w - word
word_on_line () {
    wol_w=`grep -n . $i | grep "^${wol_l}:" | \
	sed -E 's/^[[:digit:]]*:<\\!ENTITY man[.]([-[:alnum:].]*)[.][[:digit:]]* \".*$$/\1/'`;
}

if [ X$3 = X ]; then
    echo "usage: $0 file name section";
    exit 2;
fi

i=$1;
n=$2;
s=$3;
k=`echo $n | sed 's/_/./g'`;

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

l=$firstline;
u=$lastline;
while [ $u -ge $l ]; do
    r=`expr $l + $u`;
    m=`expr $r / 2`;
    wol_l=$m;
    word_on_line;
    km=$wol_w;
    if [ "$k" = "$km" ]; then
	echo "$n($s) already exists; line $m";
	exit 1;
    elif [ "$k" '<' "$km" ]; then
	u=`expr $m - 1`;
    elif [ "$k" '>' "$km" ]; then
	l=`expr $m + 1`;
    else
	echo "Thou shalt not defy the laws of mathematics!";
	echo "Thou shalt not make silly bugs in thy shell scripts!";
	exit 2;
    fi
done;

t="<!ENTITY man.$k.$s \"<citerefentry/<refentrytitle/$n/<manvolnum/$s//\">";
echo "Inserting line $l:";
echo $t;
echo -n "Last chance to interrupt (or press enter to continue)> ";
read okay;
ed -s $i <<EOF
${l}i
$t
.
w
q
EOF

echo "All done!";
exit 0;
