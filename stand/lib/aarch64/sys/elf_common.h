/*
 * Shim for sys/elf_common.h - include types before the real header.
 */

#ifndef _SYS_ELF_COMMON_H_
#define _SYS_ELF_COMMON_H_

#include <sys/types.h>
#include <stdint.h>

/* Now include the real elf_common.h from the kernel tree */
#include "../../../../sys/sys/elf_common.h"

#endif /* !_SYS_ELF_COMMON_H_ */
