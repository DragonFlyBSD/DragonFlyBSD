# $DragonFly: src/sys/platform/vkernel/conf/Makefile,v 1.1 2006/10/22 16:09:14 dillon Exp $
#
# Which version of config(8) is required.
%VERSREQ=	400023

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
