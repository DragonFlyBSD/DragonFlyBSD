# $DragonFly: src/sys/platform/vkernel/conf/Makefile,v 1.2 2006/10/23 18:01:15 dillon Exp $
#
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
