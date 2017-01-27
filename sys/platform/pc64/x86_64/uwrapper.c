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
fubyte(const uint8_t *base)
{
	return curthread->td_proc->p_vmspace->vm_pmap.fubyte(base);
}

int
subyte(uint8_t *base, uint8_t byte)
{
	return curthread->td_proc->p_vmspace->vm_pmap.subyte(base, byte);
}

int32_t
fuword32(const uint32_t *base)
{
	return curthread->td_proc->p_vmspace->vm_pmap.fuword32(base);
}

int64_t
fuword64(const uint64_t *base)
{
	return curthread->td_proc->p_vmspace->vm_pmap.fuword64(base);
}

int
suword64(uint64_t *base, uint64_t word)
{
	return curthread->td_proc->p_vmspace->vm_pmap.suword64(base, word);
}

int
suword32(uint32_t *base, int word)
{
	return curthread->td_proc->p_vmspace->vm_pmap.suword32(base, word);
}

uint32_t
swapu32(volatile uint32_t *base, uint32_t v)
{
	return curthread->td_proc->p_vmspace->vm_pmap.swapu32(base, v);
}

uint64_t
swapu64(volatile uint64_t *base, uint64_t v)
{
	return curthread->td_proc->p_vmspace->vm_pmap.swapu64(base, v);
}
