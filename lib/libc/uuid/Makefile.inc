# $FreeBSD: src/lib/libc/uuid/Makefile.inc,v 1.5 2008/08/14 22:23:16 emax Exp $

# DCE 1.1 UUID implementation sources

# uuid sources
.PATH: ${.CURDIR}/../libc/uuid

SRCS+=	uuid_compare.c uuid_create.c uuid_create_nil.c uuid_equal.c \
	uuid_from_string.c uuid_hash.c uuid_is_nil.c uuid_name_lookup.c \
	uuid_stream.c uuid_to_string.c
CMAPS+=	${.CURDIR}/uuid/Symbol.map

.if ${LIB} == "c"
MAN+=	uuid.3
MLINKS+=uuid.3 uuid_addr_lookup.3
MLINKS+=uuid.3 uuid_compare.3
MLINKS+=uuid.3 uuid_create.3
MLINKS+=uuid.3 uuid_create_nil.3
MLINKS+=uuid.3 uuid_dec_be.3
MLINKS+=uuid.3 uuid_dec_le.3
MLINKS+=uuid.3 uuid_enc_be.3
MLINKS+=uuid.3 uuid_enc_le.3
MLINKS+=uuid.3 uuid_equal.3
MLINKS+=uuid.3 uuid_from_string.3
MLINKS+=uuid.3 uuid_hash.3
MLINKS+=uuid.3 uuid_is_nil.3
MLINKS+=uuid.3 uuid_name_lookup.3
MLINKS+=uuid.3 uuid_reset_lookup.3
MLINKS+=uuid.3 uuid_to_string.3
.endif
