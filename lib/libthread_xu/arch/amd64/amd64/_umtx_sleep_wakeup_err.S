#include <machine/asm.h>
#include <sys/syscall.h>


#define KERNCALL        movq %rcx, %r10; syscall

#define SYSCALL_ERR(x)						\
			ENTRY(__CONCAT(x, _err));		\
			mov __CONCAT($SYS,x),%rax; KERNCALL; ret


SYSCALL_ERR(_umtx_sleep)
SYSCALL_ERR(_umtx_wakeup)
