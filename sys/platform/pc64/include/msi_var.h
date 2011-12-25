#ifndef _ARCH_MSI_VAR_H_
#define _ARCH_MSI_VAR_H_

#ifndef _SYS_TYPES_H_
#include <sys/types.h>
#endif

void	msi_setup(int intr, int cpuid);
void	msi_map(int intr, uint64_t *addr, uint32_t *data, int cpuid);

#endif	/* !_ARCH_MSI_VAR_H_ */
