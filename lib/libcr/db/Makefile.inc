#	from @(#)Makefile.inc	8.2 (Berkeley) 2/21/94
# $FreeBSD: src/lib/libc/db/Makefile.inc,v 1.3 1999/08/27 23:58:15 peter Exp $
# $DragonFly: src/lib/libcr/db/Attic/Makefile.inc,v 1.3 2003/07/24 21:41:46 dillon Exp $
#
CFLAGS+=-D__DBINTERFACE_PRIVATE

.include "${.CURDIR}/../libcr/db/btree/Makefile.inc"
.include "${.CURDIR}/../libcr/db/db/Makefile.inc"
.include "${.CURDIR}/../libcr/db/hash/Makefile.inc"
.include "${.CURDIR}/../libcr/db/man/Makefile.inc"
.include "${.CURDIR}/../libcr/db/mpool/Makefile.inc"
.include "${.CURDIR}/../libcr/db/recno/Makefile.inc"
