# Copyright (c) 1998, 2001, Juniper Networks, Inc.
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
#	$FreeBSD: src/lib/libtacplus/Makefile,v 1.2.2.2 2002/10/09 08:50:42 pst Exp $

LIB=		tacplus
SRCS=		taclib.c
INCS=		taclib.h

# XXX sys/md5.h shim errata for bootstrap REMOVE_OPENSSL_FILES
CFLAGS+= -I${_SHLIBDIRPREFIX}/usr/include/priv

DPADD+=		${LIBMD}
LDADD+=		-lmd
SHLIB_MAJOR=	2
WARNS?=		2
MAN=		libtacplus.3 tacplus.conf.5
MLINKS=		libtacplus.3 tac_add_server.3 libtacplus.3 tac_clear_avs.3
MLINKS+=	libtacplus.3 tac_close.3 libtacplus.3 tac_config.3
MLINKS+=	libtacplus.3 tac_create_authen.3
MLINKS+=	libtacplus.3 tac_create_author.3 libtacplus.3 tac_get_av.3
MLINKS+=	libtacplus.3 tac_get_av_value.3 libtacplus.3 tac_get_data.3
MLINKS+=	libtacplus.3 tac_get_msg.3 libtacplus.3 tac_open.3
MLINKS+=	libtacplus.3 tac_send_authen.3 libtacplus.3 tac_send_author.3
MLINKS+=	libtacplus.3 tac_set_av.3 libtacplus.3 tac_set_data.3
MLINKS+=	libtacplus.3 tac_set_msg.3 libtacplus.3 tac_set_port.3
MLINKS+=	libtacplus.3 tac_set_priv.3 libtacplus.3 tac_set_rem_addr.3
MLINKS+=	libtacplus.3 tac_set_user.3 libtacplus.3 tac_strerror.3

.include <bsd.lib.mk>
