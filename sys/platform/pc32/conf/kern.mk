#
# On the i386, do not align the stack to 16-byte boundaries.  Otherwise GCC
# adds code to the entry and exit point of every function to align the
# stack to 16-byte boundaries -- thus wasting approximately 12 bytes of stack
# per function call.  While the 16-byte alignment may benefit micro benchmarks, 
# it is probably an overall loss as it makes the code bigger (less efficient
# use of code cache tag lines) and uses more stack (less efficient use of data
# cache tag lines)
#
# Prohibit the use of FP registers in the kernel.  The user FP state is
# only saved and restored under strictly managed conditions and mainline
# kernel code cannot safely use the FP system.
#
CFLAGS+=	-fno-stack-protector
CFLAGS+=	-mno-mmx -mno-3dnow -mno-sse -mno-sse2 -mno-sse3

.if ${CCVER:Mgcc*}
CFLAGS+=	-mpreferred-stack-boundary=2
CFLAGS+=	-mno-ssse3 -mno-sse4.1 -mno-sse4.2 -mno-sse4 -mno-sse4a \
		-mno-sse5 
CFLAGS+=	-mno-abm -mno-aes -mno-avx -mno-pclmul -mno-popcnt

.if ${CCVER} == "gcc47"
CFLAGS+=	-mno-avx2 -mno-fsgsbase -mno-rdrnd -mno-f16c
CFLAGS+=	-mno-fma -mno-fma4
CFLAGS+=	-mno-bmi -mno-bmi2
CFLAGS+=	-mno-xop -mno-lwp -mno-lzcnt -mno-tbm
.endif
.endif

CFLAGS+=	-msoft-float

INLINE_LIMIT=	8000
