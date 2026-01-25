/*
 * Placeholder ELF definitions for arm64.
 */

#ifndef _CPU_ELF_H_
#define _CPU_ELF_H_

/*
 * ELF definitions for the aarch64 architecture.
 */
#ifndef EM_AARCH64
#define EM_AARCH64 183
#endif

#ifndef __ELF_WORD_SIZE
#define __ELF_WORD_SIZE	64
#endif
#include <sys/elf32.h>
#include <sys/elf64.h>
#include <sys/elf_generic.h>

#define ELF_ARCH	EM_AARCH64

#if __ELF_WORD_SIZE == 32
#define ELF_TARG_CLASS	ELFCLASS32
#else
#define ELF_TARG_CLASS	ELFCLASS64
#endif
#define ELF_TARG_DATA	ELFDATA2LSB
#define ELF_TARG_MACH	EM_AARCH64
#define ELF_TARG_VER	1

#define ELF_MACHINE_OK(x)	((x) == EM_AARCH64)

/*
 * aarch64 load base for PIE binaries (placeholder).
 */
#define ET_DYN_LOAD_ADDR	0x01021000

#endif /* !_CPU_ELF_H_ */
