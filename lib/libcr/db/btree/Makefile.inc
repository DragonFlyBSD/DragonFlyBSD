#	from @(#)Makefile.inc	8.2 (Berkeley) 7/14/94
# $FreeBSD: src/lib/libc/db/btree/Makefile.inc,v 1.3 1999/08/27 23:58:16 peter Exp $
# $DragonFly: src/lib/libcr/db/btree/Attic/Makefile.inc,v 1.3 2003/07/24 21:41:46 dillon Exp $

.PATH: ${.CURDIR}/../libcr/db/btree

SRCS+=	bt_close.c bt_conv.c bt_debug.c bt_delete.c bt_get.c bt_open.c \
	bt_overflow.c bt_page.c bt_put.c bt_search.c bt_seq.c bt_split.c \
	bt_utils.c
