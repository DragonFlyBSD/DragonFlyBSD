# Which version of config(8) is required.
#
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
# Note that the vkernel requires .hash section to be present as the kernel
# linker uses it to resolve its symbols.
#
# Additionally, the max-page-size is set to 0x200000 regardless of the ld
# default setting.
#
SYSTEM_LD= @${CC} ${DEBUG} ${PROF} -export-dynamic -o ${.TARGET} ${SYSTEM_OBJS} vers.o -pthread
SYSTEM_LD+= -Wl,--hash-style=sysv
SYSTEM_LD+= -z max-page-size=0x200000

%BEFORE_DEPEND

%OBJS

%CFILES

%SFILES

%MFILES

%CLEAN

.include "$S/conf/kern.post.mk"

%RULES

# DO NOT DELETE THIS LINE -- make depend uses it
