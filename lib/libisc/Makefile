# $FreeBSD: src/lib/libisc/Makefile,v 1.1.2.6 2002/07/22 14:21:49 ru Exp $
# $DragonFly: src/lib/libisc/Makefile,v 1.5 2004/09/20 15:32:59 asmodai Exp $

BIND_DIR=	${.CURDIR}/../../contrib/bind-9.2.4rc7

.include "${.CURDIR}/../../usr.sbin/named/Makefile.inc"
.include "${BIND_DIR}/lib/isc/api"

LIB=		isc
SHLIB_MAJOR=	1
SHLIB_MINOR=	0

.PATH:		${BIND_DIR}/lib
SRCS+=		isc/nls/msgcat.c

SRCS+=		isc/unix/app.c isc/unix/dir.c isc/unix/entropy.c \
		isc/unix/errno2result.c isc/unix/file.c isc/unix/fsaccess.c \
		isc/unix/interfaceiter.c isc/unix/keyboard.c isc/unix/net.c \
		isc/unix/os.c isc/unix/resource.c isc/unix/socket.c \
		isc/unix/stdio.c isc/unix/stdtime.c isc/unix/strerror.c \
		isc/unix/syslog.c isc/unix/time.c

SRCS+=		isc/assertions.c isc/base64.c isc/bitstring.c isc/buffer.c \
		isc/bufferlist.c isc/commandline.c isc/error.c isc/event.c \
		isc/hash.c isc/heap.c isc/hex.c isc/hmacmd5.c \
		isc/inet_pton.c \
		isc/lex.c isc/lfsr.c isc/lib.c isc/log.c \
		isc/md5.c isc/mem.c isc/mutexblock.c isc/netaddr.c \
		isc/ondestroy.c isc/quota.c isc/random.c \
		isc/ratelimiter.c isc/result.c isc/rwlock.c \
		isc/serial.c isc/sha1.c isc/sockaddr.c isc/string.c \
		isc/symtab.c isc/task.c isc/taskpool.c isc/timer.c \
		isc/version.c

SRCS+=		isccfg/check.c isccfg/log.c isccfg/parser.c

SRCS+=		dns/a6.c dns/acl.c dns/adb.c dns/byaddr.c \
		dns/cache.c dns/callbacks.c dns/compress.c \
		dns/db.c dns/dbiterator.c dns/dbtable.c dns/diff.c \
		dns/dispatch.c dns/dnssec.c dns/forward.c dns/journal.c \
		dns/keytable.c dns/lib.c dns/log.c dns/lookup.c \
		dns/master.c dns/masterdump.c dns/message.c \
		dns/name.c dns/ncache.c dns/nxt.c dns/peer.c \
		dns/rbt.c dns/rbtdb.c dns/rbtdb64.c dns/rdata.c \
		dns/rdatalist.c dns/rdataset.c dns/rdatasetiter.c \
		dns/rdataslab.c dns/request.c dns/resolver.c dns/result.c \
		dns/rootns.c dns/sdb.c dns/soa.c dns/ssu.c \
		dns/stats.c dns/tcpmsg.c dns/time.c dns/timer.c dns/tkey.c \
		dns/tsig.c dns/ttl.c dns/validator.c \
		dns/version.c dns/view.c dns/xfrin.c dns/zone.c \
		dns/zonekey.c dns/zt.c

SRCS+=		dns/sec/dst/dst_api.c dns/sec/dst/dst_lib.c \
		dns/sec/dst/dst_parse.c dns/sec/dst/dst_result.c \
		dns/sec/dst/gssapi_link.c dns/sec/dst/gssapictx.c \
		dns/sec/dst/hmac_link.c dns/sec/dst/key.c \
		dns/sec/dst/openssl_link.c dns/sec/dst/openssldh_link.c \
		dns/sec/dst/openssldsa_link.c dns/sec/dst/opensslrsa_link.c

SRCS+=		isccc/alist.c isccc/base64.c isccc/cc.c isccc/ccmsg.c \
		isccc/lib.c isccc/result.c isccc/sexpr.c isccc/symtab.c \
		isccc/version.c

SRCS+=		lwres/context.c lwres/gai_strerror.c lwres/getaddrinfo.c \
		lwres/gethost.c lwres/getipnode.c lwres/getnameinfo.c \
		lwres/getrrset.c lwres/herror.c lwres/lwbuffer.c \
		lwres/lwconfig.c lwres/lwpacket.c lwres/lwresutil.c \
		lwres/lwres_gabn.c lwres/lwres_gnba.c lwres/lwres_grbn.c \
		lwres/lwres_noop.c lwres/lwinetaton.c lwres/lwinetpton.c \
		lwres/lwinetntop.c

# contrib/bind/include/* must not override any real system includes
CFLAGS+=	-I${.CURDIR}/../../usr.sbin/named
CFLAGS+=	-I${.CURDIR}/../../usr.sbin/named/include

CFLAGS+=	-Wall -Wmissing-prototypes

CFLAGS+=	-DLIBINTERFACE=${LIBINTERFACE} \
		-DLIBREVISION=${LIBREVISION} \
		-DLIBAGE=${LIBAGE}

HEADERS =	app.h assertions.h base64.h bitstring.h boolean.h buffer.h \
		bufferlist.h commandline.h entropy.h error.h event.h \
		eventclass.h \
		file.h formatcheck.h fsaccess.h heap.h hex.h hmacmd5.h \
		interfaceiter.h ipv6.h lang.h lex.h \
		lfsr.h lib.h list.h log.h magic.h md5.h mem.h msgcat.h msgs.h \
		mutexblock.h netaddr.h ondestroy.h os.h \
		print.h quota.h random.h ratelimiter.h \
		refcount.h region.h resource.h \
		result.h resultclass.h rwlock.h serial.h sha1.h sockaddr.h \
		socket.h stdio.h string.h symtab.h task.h taskpool.h timer.h \
		types.h util.h

INCS=		${HEADERS:S/^/${BIND_DIR}\/lib\/isc\/include\/isc\//g}
INCSDIR=	${INCLUDEDIR}/isc

# there are a lot of duplicate names in the libraries we are combining,
# give each library its own subdirectory.
#
beforedepend: isc isccc dns dns/sec/dst isc/nls isc/unix isccfg lwres

isc isccc dns dns/sec/dst isc/nls isc/unix isccfg lwres:
		mkdir -p ${.TARGET}

CLEANDIRS+=	isc isccc dns isc/nls isc/unix isccfg lwres

#MAN=		assertions.3 bitncmp.3 eventlib.3 heap.3 \
#		logging.3 memcluster.3 tree.3
#CLEANFILES+=	${MAN}

#MLINKS+=	assertions.3 ENSURE.3 \
#		assertions.3 ENSURE_ERR.3 \
#		assertions.3 INSIST.3 \
#		assertions.3 INSIST_ERR.3 \
#		assertions.3 INVARIANT.3 \
#		assertions.3 INVARIANT_ERR.3 \
#		assertions.3 REQUIRE.3 \
#		assertions.3 REQUIRE_ERR.3 \
#		assertions.3 assertion_type_to_text.3 \
#		assertions.3 set_assertion_failure_callback.3 \
#		eventlib.3 evAddTime.3 \
#		eventlib.3 evCancelConn.3 \
#		eventlib.3 evCancelRW.3 \
#		eventlib.3 evClearIdleTimer.3 \
#		eventlib.3 evClearTimer.3 \
#		eventlib.3 evCmpTime.3 \
#		eventlib.3 evConnFunc.3 \
#		eventlib.3 evConnect.3 \
#		eventlib.3 evConsIovec.3 \
#		eventlib.3 evConsTime.3 \
#		eventlib.3 evCreate.3 \
#		eventlib.3 evDefer.3 \
#		eventlib.3 evDeselectFD.3 \
#		eventlib.3 evDestroy.3 \
#		eventlib.3 evDispatch.3 \
#		eventlib.3 evDo.3 \
#		eventlib.3 evDrop.3 \
#		eventlib.3 evFileFunc.3 \
#		eventlib.3 evGetNext.3 \
#		eventlib.3 evHold.3 \
#		eventlib.3 evInitID.3 \
#		eventlib.3 evLastEventTime.3 \
#		eventlib.3 evListen.3 \
#		eventlib.3 evMainLoop.3 \
#		eventlib.3 evNowTime.3 \
#		eventlib.3 evPrintf.3 \
#		eventlib.3 evRead.3 \
#		eventlib.3 evResetTimer.3 \
#		eventlib.3 evSelectFD.3 \
#		eventlib.3 evSetDebug.3 \
#		eventlib.3 evSetIdleTimer.3 \
#		eventlib.3 evSetTimer.3 \
#		eventlib.3 evStreamFunc.3 \
#		eventlib.3 evSubTime.3 \
#		eventlib.3 evTestID.3 \
#		eventlib.3 evTimeRW.3 \
#		eventlib.3 evTimeSpec.3 \
#		eventlib.3 evTimeVal.3 \
#		eventlib.3 evTimerFunc.3 \
#		eventlib.3 evTouchIdleTimer.3 \
#		eventlib.3 evTryAccept.3 \
#		eventlib.3 evUnhold.3 \
#		eventlib.3 evUntimeRW.3 \
#		eventlib.3 evUnwait.3 \
#		eventlib.3 evWaitFor.3 \
#		eventlib.3 evWaitFunc.3 \
#		eventlib.3 evWrite.3 \
#		heap.3 heap_decreased.3 \
#		heap.3 heap_delete.3 \
#		heap.3 heap_element.3 \
#		heap.3 heap_for_each.3 \
#		heap.3 heap_free.3 \
#		heap.3 heap_increased.3 \
#		heap.3 heap_insert.3 \
#		heap.3 heap_new.3 \
#		logging.3 log_add_channel.3 \
#		logging.3 log_category_is_active.3 \
#		logging.3 log_close_stream.3 \
#		logging.3 log_dec_references.3 \
#		logging.3 log_free_channel.3 \
#		logging.3 log_free_context.3 \
#		logging.3 log_get_filename.3 \
#		logging.3 log_get_stream.3 \
#		logging.3 log_inc_references.3 \
#		logging.3 log_new_context.3 \
#		logging.3 log_new_file_channel.3 \
#		logging.3 log_new_null_channel.3 \
#		logging.3 log_new_syslog_channel.3 \
#		logging.3 log_open_stream.3 \
#		logging.3 log_option.3 \
#		logging.3 log_remove_channel.3 \
#		logging.3 log_set_file_owner.3 \
#		logging.3 log_vwrite.3 \
#		logging.3 log_write.3 \
#		memcluster.3 memget.3 \
#		memcluster.3 memput.3 \
#		memcluster.3 memstats.3 \
#		tree.3 tree_add.3 \
#		tree.3 tree_delete.3 \
#		tree.3 tree_init.3 \
#		tree.3 tree_mung.3 \
#		tree.3 tree_srch.3 \
#		tree.3 tree_trav.3

.include <bsd.lib.mk>

# This has to come after the .include <bsd.lib.mk>. Not sure why.

.SUFFIXES:	.mdoc .3
.mdoc.3:
		sed -e 's/@INDOT@//g' \
		    -e 's/@SYSCALL_EXT@/2/g' \
		    -e 's/@SYS_OPS_EXT@/8/g' \
		    < ${.ALLSRC} > ${.TARGET}

