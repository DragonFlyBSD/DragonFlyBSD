# $DragonFly: src/sys/platform/vkernel/conf/Makefile,v 1.7 2007/07/01 02:51:41 dillon Exp $
#
# Which version of config(8) is required.
%VERSREQ=	400026

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
SYSTEM_LD= @${CC} -g -export-dynamic -o ${.TARGET} ${SYSTEM_OBJS} vers.o -pthread

%BEFORE_DEPEND

%OBJS

%CFILES

%SFILES

%MFILES

%CLEAN

.include "$S/conf/kern.post.mk"

%RULES

# DO NOT DELETE THIS LINE -- make depend uses it
