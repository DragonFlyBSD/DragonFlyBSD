# Makefile.i386 -- with config changes.
# Copyright 1990 W. Jolitz
#	from: @(#)Makefile.i386	7.1 5/10/91
# $FreeBSD: src/sys/conf/Makefile.i386,v 1.179.2.16 2002/11/18 11:26:49 ru Exp $
# $DragonFly: src/sys/platform/pc32/conf/Makefile,v 1.2 2006/10/23 18:01:15 dillon Exp $
#
# Makefile for DragonFly BSD
#
# This makefile is constructed from a machine description:
#	config machineid
# Most changes should be made in the machine description
#	/sys/config/``machineid''
# after which you should do
#	 config machineid
# Generic makefile changes should be made in
#	/sys/conf/Makefile.i386
# after which config should be rerun for all machines.
#
# Makefile variables:
#	-DINSTALLSTRIPPED	If debug options are present, install a
#				stripped kernel rather then the full debug
#				kernel.
#
#	-DDESTDIR		Where to install, defaults to /

# Which version of config(8) is required.
%VERSREQ=	400024

.if !defined(S)
.if exists(./@/.)
S=	./@
.else
S=	../..
.endif
.endif

.include "$S/conf/kern.pre.mk"

%BEFORE_DEPEND

%OBJS

%CFILES

%SFILES

%MFILES

%CLEAN

.include "$S/conf/kern.post.mk"

%RULES

# DO NOT DELETE THIS LINE -- make depend uses it
