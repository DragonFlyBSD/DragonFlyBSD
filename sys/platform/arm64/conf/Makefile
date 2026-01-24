#
# Which version of config(8) is required.
%VERSREQ=	400026

.if !defined(S)
.if exists(./dragonfly/.)
S=	./dragonfly
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
