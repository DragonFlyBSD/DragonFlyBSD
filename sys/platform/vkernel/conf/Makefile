# $DragonFly: src/sys/platform/vkernel/conf/Makefile,v 1.3 2006/11/07 06:43:25 dillon Exp $
#
# Which version of config(8) is required.
%VERSREQ=	400025

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
