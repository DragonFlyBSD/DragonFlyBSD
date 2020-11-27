#!/bin/sh

export SH=sh

TESTS=$(find -Es . -regex ".*\.[0-9]+")

# Header
printf '# %-25s\t%-15s\t%-40s\t%s\n' "Testcase" "Type" "Options" "Args" > sh.runlist


# Tests
for i in ${TESTS} ; do
	printf "%-27s\tuserland\tnobuild,interpreter=%s,rc=%d\n" ${i##./} ${SH} ${i##*.} >> sh.runlist
done
