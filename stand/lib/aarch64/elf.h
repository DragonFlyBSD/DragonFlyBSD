/*
 * Freestanding elf.h for aarch64 EFI loader build.
 */

#ifndef _ELF_H_
#define _ELF_H_

#include <sys/types.h>
#include <stdint.h>
#include <sys/elf64.h>

/* Generic ELF types for 64-bit */
typedef Elf64_Addr	Elf_Addr;
typedef Elf64_Half	Elf_Half;
typedef Elf64_Off	Elf_Off;
typedef Elf64_Sword	Elf_Sword;
typedef Elf64_Word	Elf_Word;
typedef Elf64_Xword	Elf_Xword;
typedef Elf64_Sxword	Elf_Sxword;
typedef Elf64_Size	Elf_Size;
typedef Elf64_Dyn	Elf_Dyn;
typedef Elf64_Rel	Elf_Rel;
typedef Elf64_Rela	Elf_Rela;
typedef Elf64_Sym	Elf_Sym;
typedef Elf64_Phdr	Elf_Phdr;
typedef Elf64_Ehdr	Elf_Ehdr;
typedef Elf64_Shdr	Elf_Shdr;

#define ELF_R_TYPE	ELF64_R_TYPE
#define ELF_R_SYM	ELF64_R_SYM

#endif /* !_ELF_H_ */
