#       from @(#)Makefile.inc	8.1 (Berkeley) 6/4/93
# $FreeBSD: src/lib/libc/db/man/Makefile.inc,v 1.8.2.2 2001/04/25 10:04:04 ru Exp $
# $DragonFly: src/lib/libc/db/man/Makefile.inc,v 1.2 2003/06/17 04:26:41 dillon Exp $

.PATH: ${.CURDIR}/../libc/db/man

.if ${LIB} == "c"
MAN+=	btree.3 dbm.3 dbopen.3 hash.3 mpool.3 recno.3

MLINKS+= dbm.3 dbm_clearerr.3
MLINKS+= dbm.3 dbm_close.3
MLINKS+= dbm.3 dbm_delete.3
MLINKS+= dbm.3 dbm_dirnfo.3
MLINKS+= dbm.3 dbm_error.3
MLINKS+= dbm.3 dbm_fetch.3
MLINKS+= dbm.3 dbm_firstkey.3
MLINKS+= dbm.3 dbm_nextkey.3
MLINKS+= dbm.3 dbm_open.3
MLINKS+= dbm.3 dbm_store.3
MLINKS+= dbopen.3 db.3
.endif
