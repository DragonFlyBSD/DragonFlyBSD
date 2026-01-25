/*
 * Placeholder for lwbuf helpers on arm64.
 */

#ifndef _CPU_LWBUF_H_
#define _CPU_LWBUF_H_

#ifndef _CPU_TYPES_H_
#include <machine/types.h>
#endif

struct vm_page;
typedef struct vm_page *vm_page_t;
typedef unsigned long vm_offset_t;

struct lwbuf {
	vm_page_t	m;
	vm_offset_t	kva;
};

#endif /* !_CPU_LWBUF_H_ */
