#! /bin/sh
# $DragonFly: src/tools/tools/local_syms/local_syms.sh,v 1.1 2005/01/10 22:13:45 joerg Exp $

# Process all object files in the current directory and build a list
# of symbols which are used only locally. Those are good candidates
# for static.

tempfoo=`basename $0`
TMPFILE=`mktemp -d -t ${tempfoo}` || exit 1

for a in *.o
do
	nm -gpu "$a" | sed 's/^ *U //'
done | sort | uniq > ${TMPFILE}/external_syms

for a in *.o
do
	nm -pg --defined-only $a | awk '{ print $3 ; }' | sort > ${TMPFILE}/defined_syms
	comm -23 ${TMPFILE}/defined_syms ${TMPFILE}/external_syms > ${TMPFILE}/uniq_syms
	if test `wc -l < ${TMPFILE}/uniq_syms` -gt 0
	then
		echo "$a:"
		cat ${TMPFILE}/uniq_syms
		echo
	fi
done

rm -R ${TMPFILE}
