#	@(#)Makefile	5.11 (Berkeley) 9/6/90
# $FreeBSD: src/lib/libc/xdr/Makefile.inc,v 1.15 2006/03/13 01:15:01 deischen Exp $

.PATH: ${.CURDIR}/../libc/xdr ${.CURDIR}/.

CMAPS+=	${.CURDIR}/xdr/Symbol.map

SRCS+=	xdr.c xdr_array.c xdr_float.c xdr_mem.c \
	xdr_rec.c xdr_reference.c xdr_sizeof.c xdr_stdio.c

MAN+=	xdr.3

MLINKS+=rpc_xdr.3 xdr_accepted_reply.3 \
	rpc_xdr.3 xdr_authsys_parms.3 \
	rpc_xdr.3 xdr_callhdr.3 \
	rpc_xdr.3 xdr_callmsg.3 \
	rpc_xdr.3 xdr_opaque_auth.3 \
	rpc_xdr.3 xdr_rejected_reply.3 \
	rpc_xdr.3 xdr_replymsg.3
MLINKS+=xdr.3 xdr_array.3 \
	xdr.3 xdr_bool.3 \
	xdr.3 xdr_bytes.3 \
	xdr.3 xdr_char.3 \
	xdr.3 xdr_destroy.3 \
	xdr.3 xdr_double.3 \
	xdr.3 xdr_enum.3 \
	xdr.3 xdr_float.3 \
	xdr.3 xdr_free.3 \
	xdr.3 xdr_getpos.3 \
	xdr.3 xdr_hyper.3 \
	xdr.3 xdr_inline.3 \
	xdr.3 xdr_int.3 \
	xdr.3 xdr_int16_t.3 \
	xdr.3 xdr_int32_t.3 \
	xdr.3 xdr_int64_t.3 \
	xdr.3 xdr_long.3 \
	xdr.3 xdr_longlong_t.3 \
	xdr.3 xdrmem_create.3 \
	xdr.3 xdr_opaque.3 \
	xdr.3 xdr_pointer.3 \
	xdr.3 xdrrec_create.3 \
	xdr.3 xdrrec_endofrecord.3 \
	xdr.3 xdrrec_eof.3 \
	xdr.3 xdrrec_skiprecord.3 \
	xdr.3 xdr_reference.3 \
	xdr.3 xdr_setpos.3 \
	xdr.3 xdr_short.3 \
	xdr.3 xdr_sizeof.3 \
	xdr.3 xdrstdio_create.3 \
	xdr.3 xdr_string.3 \
	xdr.3 xdr_u_char.3 \
	xdr.3 xdr_u_hyper.3 \
	xdr.3 xdr_u_int.3 \
	xdr.3 xdr_uint16_t.3 \
	xdr.3 xdr_u_int16_t.3 \
	xdr.3 xdr_uint32_t.3 \
	xdr.3 xdr_u_int32_t.3 \
	xdr.3 xdr_uint64_t.3 \
	xdr.3 xdr_u_int64_t.3 \
	xdr.3 xdr_u_long.3 \
	xdr.3 xdr_u_longlong_t.3 \
	xdr.3 xdr_union.3 \
	xdr.3 xdr_u_short.3 \
	xdr.3 xdr_vector.3 \
	xdr.3 xdr_void.3 \
	xdr.3 xdr_wrapstring.3
