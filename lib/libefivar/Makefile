# Copyright 1998 Juniper Networks, Inc.
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
# 1. Redistributions of source code must retain the above copyright
#    notice, this list of conditions and the following disclaimer.
# 2. Redistributions in binary form must reproduce the above copyright
#    notice, this list of conditions and the following disclaimer in the
#    documentation and/or other materials provided with the distribution.
#
# THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
# ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
# ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
# FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
# DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
# OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
# HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
# LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
# OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
# SUCH DAMAGE.
#
# $FreeBSD: head/lib/libefivar/Makefile 326458 2017-12-02 07:29:19Z imp $

EFIBOOT=${.CURDIR}/../../sys/boot/efi
EDK2INC=${.CURDIR}/../../sys/contrib/edk2/Include

.PATH:	${EFIBOOT}/libefi

LIB=		efivar
SRCS=		efivar.c libefivar.c efivar-dp-format.c \
		efivar-dp-parse.c \
		efivar-dp-xlate.c \
		uefi-guid.c uefi-dputil.c
INCS=		efivar.h efivar-dp.h
SHLIB_MAJOR=	1
MAN=		efivar.3

CFLAGS+=	-I${EFIBOOT}/include
CFLAGS+=	-I${.CURDIR} -I${EDK2INC}

.PATH:		${.CURDIR}/../../sbin/gpt
SRCS+=		gpt.c map.c
FLAGS_GROUPS=	gpt
gpt_FLAGS=	-I${.CURDIR}/../../sbin/gpt -D_LIBEFIVAR
gpt_FLAGS_FILES=efivar-dp-xlate.c gpt.c map.c

MLINKS+=efivar.3 efi_variables_supported.3 \
	efivar.3 efi_del_variable.3 \
	efivar.3 efi_get_variable.3 \
	efivar.3 efi_get_variable_attributes.3 \
	efivar.3 efi_get_variable_size.3 \
	efivar.3 efi_append_variable.3 \
	efivar.3 efi_set_variable.3 \
	efivar.3 efi_get_next_variable_name.3 \
	efivar.3 efi_str_to_guid.3 \
	efivar.3 efi_guid_to_str.3 \
	efivar.3 efi_name_to_guid.3 \
	efivar.3 efi_guid_to_name.3 \
	efivar.3 efi_guid_to_symbol.3 \
	efivar.3 libefivar.3

WARNS?=		3

.include <bsd.lib.mk>
