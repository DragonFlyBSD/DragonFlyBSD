# Which version of config(8) is required.
#
%VERSREQ=	400026

.if !defined(S)
.if exists(./dragonfly/.)
S=	./dragonfly
.else
S=	../..
.endif
.endif

.include "$S/conf/kern.pre.mk"

# Override the normal kernel link and link as a normal user program.
SYSTEM_LD= @${CC} ${ELDFLAGS} ${DEBUG} ${LDFLAGS} \
	-o ${.TARGET} ${SYSTEM_OBJS} vers.o -pthread

# Vkernel does not use a linker script.
SYSTEM_DEP:= ${SYSTEM_DEP:N${SYSTEM_LDSCRIPT}}

%BEFORE_DEPEND

%OBJS

%CFILES

%SFILES

%MFILES

%CLEAN

.include "$S/conf/kern.post.mk"

%RULES

# DO NOT DELETE THIS LINE -- make depend uses it
