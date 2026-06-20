# Flags for compiling the VKERNEL.
#
# -fno-strict-aliasing required for -O2 compilation.
#
CFLAGS+=	-D_KERNEL_VIRTUAL
CFLAGS+=	-fno-stack-protector -fno-strict-aliasing
CFLAGS+=	-fno-strict-overflow
CFLAGS+=	-fno-omit-frame-pointer

# Prohibit the use of FP registers in the kernel.  The user FP state is
# only saved and restored under strictly managed conditions and mainline
# kernel code cannot safely use the FP system.
#
CFLAGS+=	-mno-mmx -mno-sse
CFLAGS+=	-msoft-float
CFLAGS+=	-mno-fp-ret-in-387

# Retpoline spectre protection
.if ${CCVER:Mgcc*} && ${CCVER:S/gcc//} >= 80
CFLAGS+=	-mindirect-branch=thunk-inline
.endif

# Remove the dynamic library hack for now
SYSTEM_OBJS:= ${SYSTEM_OBJS:Nhack.So}
