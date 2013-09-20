#include <sys/systm.h>
#include <sys/thread.h>
#include <sys/types.h>
#include <sys/proc.h>

#include <vm/vm_map.h>

int
copyinstr(const void *udaddr, void *kaddr, size_t len, size_t *res)
{
	return curthread->td_proc->p_vmspace->vm_pmap.copyinstr(udaddr, kaddr, len, res);
}

 int
copyin(const void *udaddr, void *kaddr, size_t len)
{
	return curthread->td_proc->p_vmspace->vm_pmap.copyin(udaddr, kaddr, len);
}

int
copyout(const void *kaddr, void *udaddr, size_t len)
{
	return curthread->td_proc->p_vmspace->vm_pmap.copyout(kaddr, udaddr, len);

}

int
fubyte(const void *base)
{
	return curthread->td_proc->p_vmspace->vm_pmap.fubyte(base);
}

int
subyte (void *base, int byte)
{
	return curthread->td_proc->p_vmspace->vm_pmap.subyte(base, byte);
}

long
fuword(const void *base)
{
	return curthread->td_proc->p_vmspace->vm_pmap.fuword(base);
}

int
suword(void *base, long word)
{
	return curthread->td_proc->p_vmspace->vm_pmap.suword(base, word);
}

int
suword32(void *base, int word)
{
	return curthread->td_proc->p_vmspace->vm_pmap.suword32(base, word);
}
