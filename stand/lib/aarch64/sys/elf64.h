/*
 * Shim for sys/elf64.h - include types before the real header.
 */

#ifndef _AARCH64_SHIM_SYS_ELF64_H_
#define _AARCH64_SHIM_SYS_ELF64_H_

#include <sys/types.h>
#include <stdint.h>

/* Include elf_common first for common definitions */
#include <sys/elf_common.h>

/* Now include the real elf64.h from the kernel tree */
#include "../../../../sys/sys/elf64.h"

#endif /* !_AARCH64_SHIM_SYS_ELF64_H_ */
