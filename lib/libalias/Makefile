# $FreeBSD: src/lib/libalias/Makefile,v 1.13.2.5 2002/07/24 03:21:24 luigi Exp $

LIB=		alias
SHLIB_MAJOR=	5
SHLIBDIR?=	/lib
SRCS=		alias.c alias_cuseeme.c alias_db.c alias_ftp.c alias_irc.c \
		alias_nbt.c alias_pptp.c alias_proxy.c alias_smedia.c \
		alias_util.c
INCS=		alias.h
WARNS?=		2
MAN=		libalias.3

MLINKS=		libalias.3 PacketAliasInit.3 \
		libalias.3 PacketAliasUninit.3 \
		libalias.3 PacketAliasSetAddress.3 \
		libalias.3 PacketAliasSetMode.3 \
		libalias.3 PacketAliasSetFWBase.3 \
		libalias.3 PacketAliasIn.3 \
		libalias.3 PacketAliasOut.3 \
		libalias.3 PacketAliasRedirectPort.3 \
		libalias.3 PacketAliasRedirectAddr.3 \
		libalias.3 PacketAliasAddServer.3 \
		libalias.3 PacketAliasRedirectDelete.3 \
		libalias.3 PacketAliasProxyRule.3 \
		libalias.3 PacketAliasRedirectProto.3 \
		libalias.3 PacketAliasSaveFragment.3 \
		libalias.3 PacketAliasGetFragment.3 \
		libalias.3 PacketAliasFragmentIn.3 \
		libalias.3 PacketAliasSetTarget.3 \
		libalias.3 PacketAliasCheckNewLink.3 \
		libalias.3 PacketAliasInternetChecksum.3 \
		libalias.3 PacketUnaliasOut.3

.include <bsd.lib.mk>
