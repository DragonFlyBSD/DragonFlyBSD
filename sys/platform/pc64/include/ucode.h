/*
 * CPU microcode applied from an image the loader preloaded, early enough to
 * change what identify_cpu() sees.  See platform/pc64/x86_64/ucode.c.
 */

#ifndef _MACHINE_UCODE_H_
#define	_MACHINE_UCODE_H_

#ifdef _KERNEL

void	ucode_load_bsp(void);	/* BSP, before identify_cpu() */
void	ucode_apply(void);	/* each AP, from initializecpu() */

#endif	/* _KERNEL */

#endif	/* !_MACHINE_UCODE_H_ */
