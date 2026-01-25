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

#ifndef __ElfType
#define __ElfType(t) Elf64_##t
#endif

typedef struct {
	int	a_type;
	union {
		int	a_val;
	} a_un;
} Elf32_Auxinfo;

typedef struct {
	long	a_type;
	union {
		long	a_val;
		void	*a_ptr;
		void	(*a_fcn)(void);
	} a_un;
} Elf64_Auxinfo;

__ElfType(Auxinfo);

#define	AT_NULL		0
#define	AT_IGNORE	1
#define	AT_EXECFD	2
#define	AT_PHDR		3
#define	AT_PHENT	4
#define	AT_PHNUM	5
#define	AT_PAGESZ	6
#define	AT_BASE		7
#define	AT_FLAGS	8
#define	AT_ENTRY	9
#define	AT_BRK		10
#define	AT_DEBUG	11
#define	AT_NOTELF	10
#define	AT_UID		11
#define	AT_EUID		12
#define	AT_GID		13
#define	AT_EGID		14
#define	AT_EXECPATH	15
#define	AT_CANARY	16
#define	AT_CANARYLEN	17
#define	AT_OSRELDATE	18
#define	AT_NCPUS	19
#define	AT_PAGESIZES	20
#define	AT_PAGESIZESLEN	21
#define	AT_STACKPROT	23
#define	AT_COUNT	24

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

#ifdef _KERNEL
#define ELF_RTLD_ADDR(vmspace) \
	(round_page((vm_offset_t)(vmspace)->vm_daddr + maxdsiz))
#endif

#endif /* !_CPU_ELF_H_ */
