#!/bin/sh
# $FreeBSD: src/sys/i386/acpica/genwakecode.sh,v 1.1 2002/05/01 21:52:34 peter Exp $
# $DragonFly: src/sys/platform/pc32/acpica5/genwakecode.sh,v 1.1 2004/02/21 06:48:05 dillon Exp $
#
echo "/* generated from `pwd`/acpi_wakecode.o */"
echo 'static char wakecode[] = {';
hexdump -Cv acpi_wakecode.bin | \
    sed -e 's/^[0-9a-f][0-9a-f]*//' -e 's/\|.*$//' | \
    while read line
    do
	for code in ${line}
	do
	    echo -n "0x${code},";
	done
    done
echo '};'

nm -n acpi_wakecode.o | while read offset dummy what
do
    echo "#define ${what}	0x${offset}"
done

exit 0
