#!/bin/sh
#
# $FreeBSD: src/release/scripts/doc-make.sh,v 1.1.10.1 2002/08/08 08:23:53 ru Exp $
# $DragonFly: src/release/scripts/Attic/doc-make.sh,v 1.2 2003/06/17 04:27:21 dillon Exp $
#

# Create the doc dist.
if [ -d ${RD}/trees/bin/usr/share/doc ]; then
	( cd ${RD}/trees/bin/usr/share/doc;
	find . | cpio -dumpl ${RD}/trees/doc/usr/share/doc ) &&
	rm -rf ${RD}/trees/bin/usr/share/doc
fi
