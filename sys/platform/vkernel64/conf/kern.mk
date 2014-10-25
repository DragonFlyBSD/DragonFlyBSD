#
# Prohibit the use of FP registers in the kernel.  The user FP state is
# only saved and restored under strictly managed conditions and mainline
# kernel code cannot safely use the FP system.
#
.if ${CCVER:Mgcc*}
CFLAGS+=	-mpreferred-stack-boundary=4
.endif
CFLAGS+=	-fno-stack-protector
CFLAGS+=	-mno-mmx -mno-3dnow -mno-sse -mno-sse2 -mno-sse3
CFLAGS+=	-D_KERNEL_VIRTUAL
CFLAGS+=	-fno-omit-frame-pointer

# Remove the dynamic library hack for now
#
SYSTEM_OBJS:= ${SYSTEM_OBJS:Nhack.So}

INLINE_LIMIT=	8000
