# $DragonFly: src/sys/platform/vkernel/conf/Makefile,v 1.4 2006/11/08 16:39:58 dillon Exp $
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

# Override the normal kernel link and link as a normal user program
#
SYSTEM_LD= @${CC} -g -o ${.TARGET} ${SYSTEM_OBJS} vers.o

%BEFORE_DEPEND

%OBJS

%CFILES

%SFILES

%MFILES

%CLEAN

.include "$S/conf/kern.post.mk"

%RULES

# DO NOT DELETE THIS LINE -- make depend uses it
