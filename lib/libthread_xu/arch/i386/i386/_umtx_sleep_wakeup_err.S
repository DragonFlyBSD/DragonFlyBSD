#include <machine/asm.h>
#include <sys/syscall.h>


#define KERNCALL 	int $0x80

#define SYSCALL_ERR(x)						\
			ENTRY(__CONCAT(x, _err));		\
			lea __CONCAT(SYS,x),%eax; KERNCALL; ret; \
			END(__CONCAT(x, _err))


SYSCALL_ERR(_umtx_sleep)
SYSCALL_ERR(_umtx_wakeup)
