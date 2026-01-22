#ifndef _MACHINE_ELF_H_
#define _MACHINE_ELF_H_

/*
 * ELF definitions for the AArch64 architecture.
 */

#ifndef __ELF_WORD_SIZE
#define	__ELF_WORD_SIZE	64	/* Used by <sys/elf_generic.h> */
#endif

#include <sys/elf32.h>
#include <sys/elf64.h>
#include <sys/elf_generic.h>

#define	ELF_ARCH	EM_AARCH64

#define	ELF_MACHINE_OK(x) ((x) == EM_AARCH64)

/* Define "machine" characteristics */
#define	ELF_TARG_CLASS	ELFCLASS64
#define	ELF_TARG_DATA	ELFDATA2LSB
#define	ELF_TARG_MACH	EM_AARCH64
#define	ELF_TARG_VER	1

/*
 * AArch64 relocation types
 */
#define	R_AARCH64_NONE		0
#define	R_AARCH64_ABS64		257
#define	R_AARCH64_ABS32		258
#define	R_AARCH64_RELATIVE	1027

#endif /* !_MACHINE_ELF_H_ */
