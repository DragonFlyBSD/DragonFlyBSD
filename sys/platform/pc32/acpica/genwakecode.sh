#!/bin/sh
# $FreeBSD: src/sys/i386/acpica/genwakecode.sh,v 1.1 2002/05/01 21:52:34 peter Exp $
#
echo "/* generated from `pwd`/acpi_wakecode.o */"
echo 'static char wakecode[] = {';
hexdump -bv acpi_wakecode.bin | \
    sed -e 's/^[0-9a-f][0-9a-f]*//' -e 's/\([[:digit:]]\{1,\}\) */0\1,/g'
echo '};'

nm -n acpi_wakecode.o | while read offset dummy what
do
    echo "#define ${what}	0x${offset}"
done

exit 0
