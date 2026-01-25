/*
 * Placeholder for lwbuf helpers on arm64.
 */

#ifndef _CPU_LWBUF_H_
#define _CPU_LWBUF_H_

#ifndef _SYS_TYPES_H_
#include <sys/types.h>
#endif
#ifndef _SYS_PARAM_H_
#include <sys/param.h>
#endif
#ifndef _VM_VM_PAGE_H_
#include <vm/vm_page.h>
#endif
#ifndef _MACHINE_VMPARAM_H_
#include <machine/vmparam.h>
#endif

struct vm_page;
typedef struct vm_page *vm_page_t;
typedef unsigned long vm_offset_t;

struct lwbuf {
	vm_page_t	m;
	vm_offset_t	kva;
};

static __inline vm_page_t
lwbuf_page(struct lwbuf *lwb)
{
	return (lwb->m);
}

static __inline vm_offset_t
lwbuf_kva(struct lwbuf *lwb)
{
	return (lwb->kva);
}

static __inline struct lwbuf *
lwbuf_alloc(vm_page_t m, struct lwbuf *lwb_cache)
{
	struct lwbuf *lwb = lwb_cache;

	lwb->m = m;
	lwb->kva = PHYS_TO_DMAP(VM_PAGE_TO_PHYS(lwb->m));

	return (lwb);
}

static __inline void
lwbuf_free(struct lwbuf *lwb)
{
	lwb->m = NULL;
}

#define lwbuf_set_global(lwb)

#define LWBUF_IS_OPTIMAL

#endif /* !_CPU_LWBUF_H_ */
