/*-
 * Copyright (c) 2006,2008-2010 Joseph Koshy
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ifndef _PRIVATE_LIBELF_H_
#define	_PRIVATE_LIBELF_H_

#include <sys/param.h>
#include <sys/endian.h>
#include <sys/queue.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <unistd.h>

/* _libelf_config.h */
#if defined(__DragonFly__)
#define	LIBELF_ARCH		EM_X86_64
#define	LIBELF_BYTEORDER	ELFDATA2LSB
#define	LIBELF_CLASS		ELFCLASS64
#endif	/* __DragonFly__ */

/* _elftc.h */
#if defined(__DragonFly__)
#define	ELFTC_BYTE_ORDER			_BYTE_ORDER
#define	ELFTC_BYTE_ORDER_LITTLE_ENDIAN		_LITTLE_ENDIAN
#define	ELFTC_BYTE_ORDER_BIG_ENDIAN		_BIG_ENDIAN
#define	ELFTC_HAVE_MMAP				1
#define	STAILQ_FOREACH_SAFE			STAILQ_FOREACH_MUTABLE
#endif

#if 1
#define	_libelf_allocate_data		__ei_libelf_allocate_data
#define	_libelf_allocate_elf		__ei_libelf_allocate_elf
#define	_libelf_allocate_scn		__ei_libelf_allocate_scn
#define	_libelf_cvt_ADDR64_tom		__ei_libelf_cvt_ADDR64_tom
#define	_libelf_cvt_BYTE_tox		__ei_libelf_cvt_BYTE_tox
#define	_libelf_cvt_CAP64_tom		__ei_libelf_cvt_CAP64_tom
#define	_libelf_cvt_DYN64_tom		__ei_libelf_cvt_DYN64_tom
#define	_libelf_cvt_EHDR64_tom		__ei_libelf_cvt_EHDR64_tom
#define	_libelf_cvt_GNUHASH64_tom	__ei_libelf_cvt_GNUHASH64_tom
#define	_libelf_cvt_HALF_tom		__ei_libelf_cvt_HALF_tom
#define	_libelf_cvt_LWORD_tom		__ei_libelf_cvt_LWORD_tom
#define	_libelf_cvt_MOVE64_tom		__ei_libelf_cvt_MOVE64_tom
#define	_libelf_cvt_NOTE_tom		__ei_libelf_cvt_NOTE_tom
#define	_libelf_cvt_OFF64_tom		__ei_libelf_cvt_OFF64_tom
#define	_libelf_cvt_PHDR64_tom		__ei_libelf_cvt_PHDR64_tom
#define	_libelf_cvt_REL64_tom		__ei_libelf_cvt_REL64_tom
#define	_libelf_cvt_RELA64_tom		__ei_libelf_cvt_RELA64_tom
#define	_libelf_cvt_SHDR64_tom		__ei_libelf_cvt_SHDR64_tom
#define	_libelf_cvt_SWORD_tom		__ei_libelf_cvt_SWORD_tom
#define	_libelf_cvt_SXWORD_tom		__ei_libelf_cvt_SXWORD_tom
#define	_libelf_cvt_SYM64_tom		__ei_libelf_cvt_SYM64_tom
#define	_libelf_cvt_SYMINFO64_tom	__ei_libelf_cvt_SYMINFO64_tom
#define	_libelf_cvt_VDEF64_tom		__ei_libelf_cvt_VDEF64_tom
#define	_libelf_cvt_VNEED64_tom		__ei_libelf_cvt_VNEED64_tom
#define	_libelf_cvt_WORD_tom		__ei_libelf_cvt_WORD_tom
#define	_libelf_cvt_XWORD_tom		__ei_libelf_cvt_XWORD_tom
#define	_libelf_ehdr			__ei_libelf_ehdr
#define	_libelf_fsize			__ei_libelf_fsize
#define	_libelf_get_translator		__ei_libelf_get_translator
#define	_libelf_getshdr			__ei_libelf_getshdr
#define	_libelf_init_elf		__ei_libelf_init_elf
#define	_libelf_load_extended		__ei_libelf_load_extended
#define	_libelf_load_section_headers	__ei_libelf_load_section_headers
#define	_libelf_memory			__ei_libelf_memory
#define	_libelf_msize			__ei_libelf_msize
#define	_libelf_open_object		__ei_libelf_open_object
#define	_libelf_read_special_file	__ei_libelf_read_special_file
#define	_libelf_release_data		__ei_libelf_release_data
#define	_libelf_release_elf		__ei_libelf_release_elf
#define	_libelf_release_scn		__ei_libelf_release_scn
#define	_libelf_xlate_shtype		__ei_libelf_xlate_shtype
#define	_libelf		__ei_libelf

#define	elf64_fsize	__ei_elf64_fsize
#define	elf_getscn	__ei_elf_getscn

#define	elf_begin	_ei_elf_begin
#define	elf_end		_ei_elf_end
#define	elf_errmsg	_ei_elf_errmsg
#define	elf_errno	_ei_elf_errno
#define	elf_getdata	_ei_elf_getdata
#define	elf_nextscn	_ei_elf_nextscn
#define	elf_strptr	_ei_elf_strptr
#define	elf_version	_ei_elf_version
#define	gelf_getshdr	_ei_gelf_getshdr
#define	gelf_getsym	_ei_gelf_getsym
#endif

/* elfdefinitions.h */
/*
 * Offsets in the `ei_ident[]` field of an ELF executable header.
 */
#define	_ELF_DEFINE_EI_OFFSETS()			\
_ELF_DEFINE_EI(EI_MAG0,     0, "magic number")		\
_ELF_DEFINE_EI(EI_MAG1,     1, "magic number")		\
_ELF_DEFINE_EI(EI_MAG2,     2, "magic number")		\
_ELF_DEFINE_EI(EI_MAG3,     3, "magic number")		\
_ELF_DEFINE_EI(EI_CLASS,    4, "file class")		\
_ELF_DEFINE_EI(EI_DATA,     5, "data encoding")		\
_ELF_DEFINE_EI(EI_VERSION,  6, "file version")		\
_ELF_DEFINE_EI(EI_OSABI,    7, "OS ABI kind")		\
_ELF_DEFINE_EI(EI_ABIVERSION, 8, "OS ABI version")	\
_ELF_DEFINE_EI(EI_PAD,	    9, "padding start")		\
_ELF_DEFINE_EI(EI_NIDENT,  16, "total size")

#undef	_ELF_DEFINE_EI
#define	_ELF_DEFINE_EI(N, V, DESCR)	N = V ,
enum {
	_ELF_DEFINE_EI_OFFSETS()
	EI__LAST__
};

/*
 * The ELF class of an object.
 */
#define	_ELF_DEFINE_ELFCLASS()				\
_ELF_DEFINE_EC(ELFCLASSNONE, 0, "Unknown ELF class")	\
_ELF_DEFINE_EC(ELFCLASS32,   1, "32 bit objects")	\
_ELF_DEFINE_EC(ELFCLASS64,   2, "64 bit objects")

#undef	_ELF_DEFINE_EC
#define	_ELF_DEFINE_EC(N, V, DESCR)	N = V ,
enum {
	_ELF_DEFINE_ELFCLASS()
	EC__LAST__
};

/*
 * Endianness of data in an ELF object.
 */

#define	_ELF_DEFINE_ELF_DATA_ENDIANNESS()			\
_ELF_DEFINE_ED(ELFDATANONE, 0, "Unknown data endianness")	\
_ELF_DEFINE_ED(ELFDATA2LSB, 1, "little endian")			\
_ELF_DEFINE_ED(ELFDATA2MSB, 2, "big endian")

#undef	_ELF_DEFINE_ED
#define	_ELF_DEFINE_ED(N, V, DESCR)	N = V ,
enum {
	_ELF_DEFINE_ELF_DATA_ENDIANNESS()
	ED__LAST__
};

/*
 * Values of the magic numbers used in identification array.
 */
#define	_ELF_DEFINE_ELF_MAGIC()			\
_ELF_DEFINE_EMAG(ELFMAG0, 0x7FU)		\
_ELF_DEFINE_EMAG(ELFMAG1, 'E')			\
_ELF_DEFINE_EMAG(ELFMAG2, 'L')			\
_ELF_DEFINE_EMAG(ELFMAG3, 'F')

#undef	_ELF_DEFINE_EMAG
#define	_ELF_DEFINE_EMAG(N, V)		N = V ,
enum {
	_ELF_DEFINE_ELF_MAGIC()
	ELFMAG__LAST__
};

/*
 * ELF Machine types: (EM_*).
 */
#define	_ELF_DEFINE_ELF_MACHINES()					\
_ELF_DEFINE_EM(EM_NONE,             0, "No machine")			\
_ELF_DEFINE_EM(EM_386,              3, "Intel 80386")			\
_ELF_DEFINE_EM(EM_X86_64,           62, "AMD x86-64 architecture")

#undef	_ELF_DEFINE_EM
#define	_ELF_DEFINE_EM(N, V, DESCR)	N = V ,
enum {
	_ELF_DEFINE_ELF_MACHINES()
	EM__LAST__
};

/* ELF file format version numbers. */
#define	EV_NONE		0
#define	EV_CURRENT	1

/*
 * Special section indices.
 */
#define _ELF_DEFINE_SECTION_INDICES()					\
_ELF_DEFINE_SHN(SHN_UNDEF,	0,	 "undefined section")		\
_ELF_DEFINE_SHN(SHN_LORESERVE,	0xFF00U, "start of reserved area")	\
_ELF_DEFINE_SHN(SHN_LOPROC,	0xFF00U,				\
	"start of processor-specific range")				\
_ELF_DEFINE_SHN(SHN_BEFORE,	0xFF00U, "used for section ordering")	\
_ELF_DEFINE_SHN(SHN_AFTER,	0xFF01U, "used for section ordering")	\
_ELF_DEFINE_SHN(SHN_AMD64_LCOMMON, 0xFF02U, "large common block label") \
_ELF_DEFINE_SHN(SHN_MIPS_ACOMMON, 0xFF00U,				\
	"allocated common symbols in a DSO")				\
_ELF_DEFINE_SHN(SHN_MIPS_TEXT,	0xFF01U, "Reserved (obsolete)")		\
_ELF_DEFINE_SHN(SHN_MIPS_DATA,	0xFF02U, "Reserved (obsolete)")		\
_ELF_DEFINE_SHN(SHN_MIPS_SCOMMON, 0xFF03U,				\
	"gp-addressable common symbols")				\
_ELF_DEFINE_SHN(SHN_MIPS_SUNDEFINED, 0xFF04U,				\
	"gp-addressable undefined symbols")				\
_ELF_DEFINE_SHN(SHN_MIPS_LCOMMON, 0xFF05U, "local common symbols")	\
_ELF_DEFINE_SHN(SHN_MIPS_LUNDEFINED, 0xFF06U,				\
	"local undefined symbols")					\
_ELF_DEFINE_SHN(SHN_HIPROC,	0xFF1FU,				\
	"end of processor-specific range")				\
_ELF_DEFINE_SHN(SHN_LOOS,	0xFF20U,				\
	"start of OS-specific range")					\
_ELF_DEFINE_SHN(SHN_SUNW_IGNORE, 0xFF3FU, "used by dtrace")		\
_ELF_DEFINE_SHN(SHN_HIOS,	0xFF3FU,				\
	"end of OS-specific range")					\
_ELF_DEFINE_SHN(SHN_ABS,	0xFFF1U, "absolute references")		\
_ELF_DEFINE_SHN(SHN_COMMON,	0xFFF2U, "references to COMMON areas")	\
_ELF_DEFINE_SHN(SHN_XINDEX,	0xFFFFU, "extended index")		\
_ELF_DEFINE_SHN(SHN_HIRESERVE,	0xFFFFU, "end of reserved area")

#undef	_ELF_DEFINE_SHN
#define	_ELF_DEFINE_SHN(N, V, DESCR)	N = V ,
enum {
	_ELF_DEFINE_SECTION_INDICES()
	SHN__LAST__
};

/*
 * Section types.
 */

#define	_ELF_DEFINE_SECTION_TYPES()					\
_ELF_DEFINE_SHT(SHT_NULL,            0, "inactive header")		\
_ELF_DEFINE_SHT(SHT_PROGBITS,        1, "program defined information")	\
_ELF_DEFINE_SHT(SHT_SYMTAB,          2, "symbol table")			\
_ELF_DEFINE_SHT(SHT_STRTAB,          3, "string table")			\
_ELF_DEFINE_SHT(SHT_RELA,            4,					\
	"relocation entries with addends")				\
_ELF_DEFINE_SHT(SHT_HASH,            5, "symbol hash table")		\
_ELF_DEFINE_SHT(SHT_DYNAMIC,         6,					\
	"information for dynamic linking")				\
_ELF_DEFINE_SHT(SHT_NOTE,            7, "additional notes")		\
_ELF_DEFINE_SHT(SHT_NOBITS,          8, "section occupying no space")	\
_ELF_DEFINE_SHT(SHT_REL,             9,					\
	"relocation entries without addends")				\
_ELF_DEFINE_SHT(SHT_SHLIB,           10, "reserved")			\
_ELF_DEFINE_SHT(SHT_DYNSYM,          11, "symbol table")		\
_ELF_DEFINE_SHT(SHT_INIT_ARRAY,      14,				\
	"pointers to initialization functions")				\
_ELF_DEFINE_SHT(SHT_FINI_ARRAY,      15,				\
	"pointers to termination functions")				\
_ELF_DEFINE_SHT(SHT_PREINIT_ARRAY,   16,				\
	"pointers to functions called before initialization")		\
_ELF_DEFINE_SHT(SHT_GROUP,           17, "defines a section group")	\
_ELF_DEFINE_SHT(SHT_SYMTAB_SHNDX,    18,				\
	"used for extended section numbering")				\
_ELF_DEFINE_SHT(SHT_LOOS,            0x60000000UL,			\
	"start of OS-specific range")					\
_ELF_DEFINE_SHT(SHT_SUNW_dof,	     0x6FFFFFF4UL,			\
	"used by dtrace")						\
_ELF_DEFINE_SHT(SHT_SUNW_cap,	     0x6FFFFFF5UL,			\
	"capability requirements")					\
_ELF_DEFINE_SHT(SHT_GNU_ATTRIBUTES,  0x6FFFFFF5UL,			\
	"object attributes")						\
_ELF_DEFINE_SHT(SHT_SUNW_SIGNATURE,  0x6FFFFFF6UL,			\
	"module verification signature")				\
_ELF_DEFINE_SHT(SHT_GNU_HASH,	     0x6FFFFFF6UL,			\
	"GNU Hash sections")						\
_ELF_DEFINE_SHT(SHT_GNU_LIBLIST,     0x6FFFFFF7UL,			\
	"List of libraries to be prelinked")				\
_ELF_DEFINE_SHT(SHT_SUNW_ANNOTATE,   0x6FFFFFF7UL,			\
	"special section where unresolved references are allowed")	\
_ELF_DEFINE_SHT(SHT_SUNW_DEBUGSTR,   0x6FFFFFF8UL,			\
	"debugging information")					\
_ELF_DEFINE_SHT(SHT_CHECKSUM,	     0x6FFFFFF8UL,			\
	"checksum for dynamic shared objects")				\
_ELF_DEFINE_SHT(SHT_SUNW_DEBUG,      0x6FFFFFF9UL,			\
	"debugging information")					\
_ELF_DEFINE_SHT(SHT_SUNW_move,       0x6FFFFFFAUL,			\
	"information to handle partially initialized symbols")		\
_ELF_DEFINE_SHT(SHT_SUNW_COMDAT,     0x6FFFFFFBUL,			\
	"section supporting merging of multiple copies of data")	\
_ELF_DEFINE_SHT(SHT_SUNW_syminfo,    0x6FFFFFFCUL,			\
	"additional symbol information")				\
_ELF_DEFINE_SHT(SHT_SUNW_verdef,     0x6FFFFFFDUL,			\
	"symbol versioning information")				\
_ELF_DEFINE_SHT(SHT_SUNW_verneed,    0x6FFFFFFEUL,			\
	"symbol versioning requirements")				\
_ELF_DEFINE_SHT(SHT_SUNW_versym,     0x6FFFFFFFUL,			\
	"symbol versioning table")					\
_ELF_DEFINE_SHT(SHT_HIOS,            0x6FFFFFFFUL,			\
	"end of OS-specific range")					\
_ELF_DEFINE_SHT(SHT_LOPROC,          0x70000000UL,			\
	"start of processor-specific range")				\
_ELF_DEFINE_SHT(SHT_ARM_EXIDX,       0x70000001UL,			\
	"exception index table")					\
_ELF_DEFINE_SHT(SHT_ARM_PREEMPTMAP,  0x70000002UL,			\
	"BPABI DLL dynamic linking preemption map")			\
_ELF_DEFINE_SHT(SHT_ARM_ATTRIBUTES,  0x70000003UL,			\
	"object file compatibility attributes")				\
_ELF_DEFINE_SHT(SHT_ARM_DEBUGOVERLAY, 0x70000004UL,			\
	"overlay debug information")					\
_ELF_DEFINE_SHT(SHT_ARM_OVERLAYSECTION, 0x70000005UL,			\
	"overlay debug information")					\
_ELF_DEFINE_SHT(SHT_MIPS_LIBLIST,    0x70000000UL,			\
	"DSO library information used in link")				\
_ELF_DEFINE_SHT(SHT_MIPS_MSYM,       0x70000001UL,			\
	"MIPS symbol table extension")					\
_ELF_DEFINE_SHT(SHT_MIPS_CONFLICT,   0x70000002UL,			\
	"symbol conflicting with DSO-defined symbols ")			\
_ELF_DEFINE_SHT(SHT_MIPS_GPTAB,      0x70000003UL,			\
	"global pointer table")						\
_ELF_DEFINE_SHT(SHT_MIPS_UCODE,      0x70000004UL,			\
	"reserved")							\
_ELF_DEFINE_SHT(SHT_MIPS_DEBUG,      0x70000005UL,			\
	"reserved (obsolete debug information)")			\
_ELF_DEFINE_SHT(SHT_MIPS_REGINFO,    0x70000006UL,			\
	"register usage information")					\
_ELF_DEFINE_SHT(SHT_MIPS_PACKAGE,    0x70000007UL,			\
	"OSF reserved")							\
_ELF_DEFINE_SHT(SHT_MIPS_PACKSYM,    0x70000008UL,			\
	"OSF reserved")							\
_ELF_DEFINE_SHT(SHT_MIPS_RELD,       0x70000009UL,			\
	"dynamic relocation")						\
_ELF_DEFINE_SHT(SHT_MIPS_IFACE,      0x7000000BUL,			\
	"subprogram interface information")				\
_ELF_DEFINE_SHT(SHT_MIPS_CONTENT,    0x7000000CUL,			\
	"section content classification")				\
_ELF_DEFINE_SHT(SHT_MIPS_OPTIONS,     0x7000000DUL,			\
	"general options")						\
_ELF_DEFINE_SHT(SHT_MIPS_DELTASYM,   0x7000001BUL,			\
	"Delta C++: symbol table")					\
_ELF_DEFINE_SHT(SHT_MIPS_DELTAINST,  0x7000001CUL,			\
	"Delta C++: instance table")					\
_ELF_DEFINE_SHT(SHT_MIPS_DELTACLASS, 0x7000001DUL,			\
	"Delta C++: class table")					\
_ELF_DEFINE_SHT(SHT_MIPS_DWARF,      0x7000001EUL,			\
	"DWARF debug information")					\
_ELF_DEFINE_SHT(SHT_MIPS_DELTADECL,  0x7000001FUL,			\
	"Delta C++: declarations")					\
_ELF_DEFINE_SHT(SHT_MIPS_SYMBOL_LIB, 0x70000020UL,			\
	"symbol-to-library mapping")					\
_ELF_DEFINE_SHT(SHT_MIPS_EVENTS,     0x70000021UL,			\
	"event locations")						\
_ELF_DEFINE_SHT(SHT_MIPS_TRANSLATE,  0x70000022UL,			\
	"???")								\
_ELF_DEFINE_SHT(SHT_MIPS_PIXIE,      0x70000023UL,			\
	"special pixie sections")					\
_ELF_DEFINE_SHT(SHT_MIPS_XLATE,      0x70000024UL,			\
	"address translation table")					\
_ELF_DEFINE_SHT(SHT_MIPS_XLATE_DEBUG, 0x70000025UL,			\
	"SGI internal address translation table")			\
_ELF_DEFINE_SHT(SHT_MIPS_WHIRL,      0x70000026UL,			\
	"intermediate code")						\
_ELF_DEFINE_SHT(SHT_MIPS_EH_REGION,  0x70000027UL,			\
	"C++ exception handling region info")				\
_ELF_DEFINE_SHT(SHT_MIPS_XLATE_OLD,  0x70000028UL,			\
	"obsolete")							\
_ELF_DEFINE_SHT(SHT_MIPS_PDR_EXCEPTION, 0x70000029UL,			\
	"runtime procedure descriptor table exception information")	\
_ELF_DEFINE_SHT(SHT_MIPS_ABIFLAGS,   0x7000002AUL,			\
	"ABI flags")							\
_ELF_DEFINE_SHT(SHT_SPARC_GOTDATA,   0x70000000UL,			\
	"SPARC-specific data")						\
_ELF_DEFINE_SHT(SHT_AMD64_UNWIND,    0x70000001UL,			\
	"unwind tables for the AMD64")					\
_ELF_DEFINE_SHT(SHT_ORDERED,         0x7FFFFFFFUL,			\
	"sort entries in the section")					\
_ELF_DEFINE_SHT(SHT_HIPROC,          0x7FFFFFFFUL,			\
	"end of processor-specific range")				\
_ELF_DEFINE_SHT(SHT_LOUSER,          0x80000000UL,			\
	"start of application-specific range")				\
_ELF_DEFINE_SHT(SHT_HIUSER,          0xFFFFFFFFUL,			\
	"end of application-specific range")

#undef	_ELF_DEFINE_SHT
#define	_ELF_DEFINE_SHT(N, V, DESCR)	N = V ,
enum {
	_ELF_DEFINE_SECTION_TYPES()
	SHT__LAST__ = SHT_HIUSER
};

#define	PN_XNUM			0xFFFFU /* Use extended section numbering. */

/**
 ** ELF Types.
 **/

typedef uint64_t	Elf64_Addr;	/* Program address. */
typedef uint16_t	Elf64_Half;	/* Unsigned medium integer. */
typedef uint64_t	Elf64_Off;	/* File offset. */
typedef int32_t		Elf64_Sword;	/* Signed integer. */
typedef uint32_t	Elf64_Word;	/* Unsigned integer. */
typedef uint64_t	Elf64_Lword;	/* Unsigned long integer. */
typedef uint64_t	Elf64_Xword;	/* Unsigned long integer. */
typedef int64_t		Elf64_Sxword;	/* Signed long integer. */

/*
 * Capability descriptors.
 */
typedef struct {
	Elf64_Xword	c_tag;	     /* Type of entry. */
	union {
		Elf64_Xword	c_val; /* Integer value. */
		Elf64_Addr	c_ptr; /* Pointer value. */
	} c_un;
} Elf64_Cap;

/*
 * Dynamic section entries.
 */
typedef struct {
	Elf64_Sxword	d_tag;	     /* Type of entry. */
	union {
		Elf64_Xword	d_val; /* Integer value. */
		Elf64_Addr	d_ptr; /* Pointer value; */
	} d_un;
} Elf64_Dyn;

/*
 * The executable header (EHDR).
 */
typedef struct {
	unsigned char   e_ident[EI_NIDENT]; /* ELF identification. */
	Elf64_Half      e_type;	     /* Object file type (ET_*). */
	Elf64_Half      e_machine;   /* Machine type (EM_*). */
	Elf64_Word      e_version;   /* File format version (EV_*). */
	Elf64_Addr      e_entry;     /* Start address. */
	Elf64_Off       e_phoff;     /* File offset to the PHDR table. */
	Elf64_Off       e_shoff;     /* File offset to the SHDRheader. */
	Elf64_Word      e_flags;     /* Flags (EF_*). */
	Elf64_Half      e_ehsize;    /* Elf header size in bytes. */
	Elf64_Half      e_phentsize; /* PHDR table entry size in bytes. */
	Elf64_Half      e_phnum;     /* Number of PHDR entries. */
	Elf64_Half      e_shentsize; /* SHDR table entry size in bytes. */
	Elf64_Half      e_shnum;     /* Number of SHDR entries. */
	Elf64_Half      e_shstrndx;  /* Index of section name string table. */
} Elf64_Ehdr;

/*
 * Note descriptors.
 */

typedef	struct {
	uint32_t	n_namesz;    /* Length of note's name. */
	uint32_t	n_descsz;    /* Length of note's value. */
	uint32_t	n_type;	     /* Type of note. */
} Elf_Note;

/*
 * Program Header Table (PHDR) entries.
 */
typedef struct {
	Elf64_Word	p_type;	     /* Type of segment. */
	Elf64_Word	p_flags;     /* Segment flags. */
	Elf64_Off	p_offset;    /* File offset to segment. */
	Elf64_Addr	p_vaddr;     /* Virtual address in memory. */
	Elf64_Addr	p_paddr;     /* Physical address (if relevant). */
	Elf64_Xword	p_filesz;    /* Size of segment in file. */
	Elf64_Xword	p_memsz;     /* Size of segment in memory. */
	Elf64_Xword	p_align;     /* Alignment constraints. */
} Elf64_Phdr;

/*
 * Move entries, for describing data in COMMON blocks in a compact
 * manner.
 */
typedef struct {
	Elf64_Lword	m_value;     /* Initialization value. */
	Elf64_Xword	m_info;	     /* Encoded size and index. */
	Elf64_Xword	m_poffset;   /* Offset relative to symbol. */
	Elf64_Half	m_repeat;    /* Repeat count. */
	Elf64_Half	m_stride;    /* Number of units to skip. */
} Elf64_Move;

/*
 * Section Header Table (SHDR) entries.
 */
typedef struct {
	Elf64_Word	sh_name;     /* index of section name */
	Elf64_Word	sh_type;     /* section type */
	Elf64_Xword	sh_flags;    /* section flags */
	Elf64_Addr	sh_addr;     /* in-memory address of section */
	Elf64_Off	sh_offset;   /* file offset of section */
	Elf64_Xword	sh_size;     /* section size in bytes */
	Elf64_Word	sh_link;     /* section header table link */
	Elf64_Word	sh_info;     /* extra information */
	Elf64_Xword	sh_addralign; /* alignment constraint */
	Elf64_Xword	sh_entsize;  /* size for fixed-size entries */
} Elf64_Shdr;

/*
 * Symbol table entries.
 */
typedef struct {
	Elf64_Word	st_name;     /* index of symbol's name */
	unsigned char	st_info;     /* type and binding attributes */
	unsigned char	st_other;    /* visibility */
	Elf64_Half	st_shndx;    /* index of related section */
	Elf64_Addr	st_value;    /* value for the symbol */
	Elf64_Xword	st_size;     /* size of associated data */
} Elf64_Sym;

/*
 * Syminfo descriptors, containing additional symbol information.
 */
typedef struct {
	Elf64_Half	si_boundto;  /* Entry index with additional flags. */
	Elf64_Half	si_flags;    /* Flags. */
} Elf64_Syminfo;

/*
 * Relocation descriptors.
 */
typedef struct {
	Elf64_Addr	r_offset;    /* location to apply relocation to */
	Elf64_Xword	r_info;      /* type+section for relocation */
} Elf64_Rel;

typedef struct {
	Elf64_Addr	r_offset;    /* location to apply relocation to */
	Elf64_Xword	r_info;      /* type+section for relocation */
	Elf64_Sxword	r_addend;    /* constant addend */
} Elf64_Rela;

/*
 * Symbol versioning structures.
 */
typedef struct {
	Elf64_Word	vda_name;    /* Index to name. */
	Elf64_Word	vda_next;    /* Offset to next entry. */
} Elf64_Verdaux;

typedef struct {
	Elf64_Word	vna_hash;    /* Hash value of dependency name. */
	Elf64_Half	vna_flags;   /* Flags. */
	Elf64_Half	vna_other;   /* Unused. */
	Elf64_Word	vna_name;    /* Offset to dependency name. */
	Elf64_Word	vna_next;    /* Offset to next vernaux entry. */
} Elf64_Vernaux;

typedef struct {
	Elf64_Half	vd_version;  /* Version information. */
	Elf64_Half	vd_flags;    /* Flags. */
	Elf64_Half	vd_ndx;	     /* Index into the versym section. */
	Elf64_Half	vd_cnt;	     /* Number of aux entries. */
	Elf64_Word	vd_hash;     /* Hash value of name. */
	Elf64_Word	vd_aux;	     /* Offset to aux entries. */
	Elf64_Word	vd_next;     /* Offset to next version definition. */
} Elf64_Verdef;

typedef struct {
	Elf64_Half	vn_version;  /* Version number. */
	Elf64_Half	vn_cnt;	     /* Number of aux entries. */
	Elf64_Word	vn_file;     /* Offset of associated file name. */
	Elf64_Word	vn_aux;	     /* Offset of vernaux array. */
	Elf64_Word	vn_next;     /* Offset of next verneed entry. */
} Elf64_Verneed;

/*
 * The header for GNU-style hash sections.
 */

typedef struct {
	uint32_t	gh_nbuckets;	/* Number of hash buckets. */
	uint32_t	gh_symndx;	/* First visible symbol in .dynsym. */
	uint32_t	gh_maskwords;	/* #maskwords used in bloom filter. */
	uint32_t	gh_shift2;	/* Bloom filter shift count. */
} Elf_GNU_Hash_Header;

/* libelf.h */
/* Library private data structures */
typedef struct _Elf Elf;
typedef struct _Elf_Scn Elf_Scn;

/* File types */
typedef enum {
	ELF_K_NONE = 0,
	ELF_K_AR,	/* `ar' archives */
	ELF_K_COFF,	/* COFF files (unsupported) */
	ELF_K_ELF,	/* ELF files */
	ELF_K_NUM
} Elf_Kind;

/* Data types */
typedef enum {
	ELF_T_ADDR,
	ELF_T_BYTE,
	ELF_T_CAP,
	ELF_T_DYN,
	ELF_T_EHDR,
	ELF_T_HALF,
	ELF_T_LWORD,
	ELF_T_MOVE,
	ELF_T_MOVEP,
	ELF_T_NOTE,
	ELF_T_OFF,
	ELF_T_PHDR,
	ELF_T_REL,
	ELF_T_RELA,
	ELF_T_SHDR,
	ELF_T_SWORD,
	ELF_T_SXWORD,
	ELF_T_SYMINFO,
	ELF_T_SYM,
	ELF_T_VDEF,
	ELF_T_VNEED,
	ELF_T_WORD,
	ELF_T_XWORD,
	ELF_T_GNUHASH,	/* GNU style hash tables. */
	ELF_T_NUM
} Elf_Type;

#define	ELF_T_FIRST	ELF_T_ADDR
#define	ELF_T_LAST	ELF_T_GNUHASH

/* Commands */
typedef enum {
	ELF_C_NULL = 0,
	ELF_C_CLR,
	ELF_C_FDDONE,
	ELF_C_FDREAD,
	ELF_C_RDWR,
	ELF_C_READ,
	ELF_C_SET,
	ELF_C_WRITE,
	ELF_C_NUM
} Elf_Cmd;

/*
 * An `Elf_Data' structure describes data in an
 * ELF section.
 */
typedef struct _Elf_Data {
	/*
	 * `Public' members that are part of the ELF(3) API.
	 */
	uint64_t	d_align;
	void		*d_buf;
	uint64_t	d_off;
	uint64_t	d_size;
	Elf_Type	d_type;
	unsigned int	d_version;
} Elf_Data;

/*
 * An `Elf_Arhdr' structure describes an archive
 * header.
 */
typedef struct {
	time_t		ar_date;
	char		*ar_name;	/* archive member name */
	gid_t		ar_gid;
	mode_t		ar_mode;
	char		*ar_rawname;	/* 'raw' member name */
	size_t		ar_size;
	uid_t		ar_uid;

	/*
	 * Members that are not part of the public API.
	 */
	unsigned int	ar_flags;
} Elf_Arhdr;

/*
 * An `Elf_Arsym' describes an entry in the archive
 * symbol table.
 */
typedef struct {
	off_t		as_off;		/* byte offset to member's header */
	unsigned long	as_hash;	/* elf_hash() value for name */
	char		*as_name;	/* null terminated symbol name */
} Elf_Arsym;

/*
 * Error numbers.
 */

enum Elf_Error {
	ELF_E_NONE,	/* No error */
	ELF_E_ARCHIVE,	/* Malformed ar(1) archive */
	ELF_E_ARGUMENT,	/* Invalid argument */
	ELF_E_CLASS,	/* Mismatched ELF class */
	ELF_E_DATA,	/* Invalid data descriptor */
	ELF_E_HEADER,	/* Missing or malformed ELF header */
	ELF_E_IO,	/* I/O error */
	ELF_E_LAYOUT,	/* Layout constraint violation */
	ELF_E_MODE,	/* Wrong mode for ELF descriptor */
	ELF_E_RANGE,	/* Value out of range */
	ELF_E_RESOURCE,	/* Resource exhaustion */
	ELF_E_SECTION,	/* Invalid section descriptor */
	ELF_E_SEQUENCE,	/* API calls out of sequence */
	ELF_E_UNIMPL,	/* Feature is unimplemented */
	ELF_E_VERSION,	/* Unknown API version */
	ELF_E_NUM	/* Max error number */
};

/*
 * Flags defined by the API.
 */

#define	ELF_F_LAYOUT	0x001U	/* application will layout the file */
#define	ELF_F_DIRTY	0x002U	/* a section or ELF file is dirty */

#ifdef __cplusplus
extern "C" {
#endif
static
Elf		*elf_begin(int _fd, Elf_Cmd _cmd, Elf *_elf);
static
int		elf_end(Elf *_elf);
static
const char	*elf_errmsg(int _error);
static
int		elf_errno(void);
static
Elf_Data	*elf_getdata(Elf_Scn *, Elf_Data *);
static
char		*elf_strptr(Elf *_elf, size_t _section, size_t _offset);
static
unsigned int	elf_version(unsigned int _version);
static
Elf_Scn		*elf_nextscn(Elf *_elf, Elf_Scn *_scn);
#ifdef __cplusplus
}
#endif

/* gelf.h */
typedef Elf64_Shdr	GElf_Shdr;	/* Section header */
typedef Elf64_Sym	GElf_Sym;	/* Symbol table entries */

#ifdef __cplusplus
extern "C" {
#endif
static
GElf_Shdr	*gelf_getshdr(Elf_Scn *_scn, GElf_Shdr *_dst);
static
GElf_Sym	*gelf_getsym(Elf_Data *_src, int _index, GElf_Sym *_dst);
#ifdef __cplusplus
}
#endif

/* _libelf.h */
/*
 * Library-private data structures.
 */

#define LIBELF_MSG_SIZE	256

struct _libelf_globals {
	int		libelf_arch;
	unsigned int	libelf_byteorder;
	int		libelf_class;
	int		libelf_error;
	int		libelf_fillchar;
	unsigned int	libelf_version;
	unsigned char	libelf_msg[LIBELF_MSG_SIZE];
};

#if 0
extern struct _libelf_globals _libelf;
#endif

#define	LIBELF_PRIVATE(N)	(_libelf.libelf_##N)

#define	LIBELF_ELF_ERROR_MASK			0xFF
#define	LIBELF_OS_ERROR_SHIFT			8

#define	LIBELF_ERROR(E, O) (((E) & LIBELF_ELF_ERROR_MASK) |	\
	((O) << LIBELF_OS_ERROR_SHIFT))

#define	LIBELF_SET_ERROR(E, O) do {					\
		LIBELF_PRIVATE(error) = LIBELF_ERROR(ELF_E_##E, (O));	\
	} while (0)

/*
 * Flags for library internal use.  These use the upper 16 bits of the
 * `e_flags' field.
 */

#define	LIBELF_F_DATA_MALLOCED	0x040000U /* whether data was malloc'ed */
#define	LIBELF_F_RAWFILE_MALLOC	0x080000U /* whether e_rawfile was malloc'ed */
#define	LIBELF_F_RAWFILE_MMAP	0x100000U /* whether e_rawfile was mmap'ed */
#define	LIBELF_F_SHDRS_LOADED	0x200000U /* whether all shdrs were read in */
#define	LIBELF_F_SPECIAL_FILE	0x400000U /* non-regular file */

struct _Elf {
	int		e_activations;	/* activation count */
	unsigned int	e_byteorder;	/* ELFDATA* */
	int		e_class;	/* ELFCLASS*  */
	Elf_Cmd		e_cmd;		/* ELF_C_* used at creation time */
	int		e_fd;		/* associated file descriptor */
	unsigned int	e_flags;	/* ELF_F_* & LIBELF_F_* flags */
	Elf_Kind	e_kind;		/* ELF_K_* */
	Elf		*e_parent;	/* non-NULL for archive members */
	unsigned char	*e_rawfile;	/* uninterpreted bytes */
	size_t		e_rawsize;	/* size of uninterpreted bytes */
	unsigned int	e_version;	/* file version */

	/*
	 * Header information for archive members.  See the
	 * LIBELF_F_AR_HEADER flag.
	 */
	union {
		Elf_Arhdr	*e_arhdr;	/* translated header */
		unsigned char	*e_rawhdr;	/* untranslated header */
	} e_hdr;

	union {
		struct {		/* ar(1) archives */
			off_t	e_next;	/* set by elf_rand()/elf_next() */
			int	e_nchildren;
			unsigned char *e_rawstrtab; /* file name strings */
			size_t	e_rawstrtabsz;
			unsigned char *e_rawsymtab;	/* symbol table */
			size_t	e_rawsymtabsz;
			Elf_Arsym *e_symtab;
			size_t	e_symtabsz;
		} e_ar;
		struct {		/* regular ELF files */
			union {
#if 0
				Elf32_Ehdr *e_ehdr32;
#endif
				Elf64_Ehdr *e_ehdr64;
			} e_ehdr;
			union {
#if 0
				Elf32_Phdr *e_phdr32;
#endif
				Elf64_Phdr *e_phdr64;
			} e_phdr;
			STAILQ_HEAD(, _Elf_Scn)	e_scn;	/* section list */
			size_t	e_nphdr;	/* number of Phdr entries */
			size_t	e_nscn;		/* number of sections */
			size_t	e_strndx;	/* string table section index */
		} e_elf;
	} e_u;
};

/*
 * The internal descriptor wrapping the "Elf_Data" type.
 */
struct _Libelf_Data {
	Elf_Data	d_data;		/* The exported descriptor. */
	Elf_Scn		*d_scn;		/* The containing section */
	unsigned int	d_flags;
	STAILQ_ENTRY(_Libelf_Data) d_next;
};

struct _Elf_Scn {
	union {
#if 0
		Elf32_Shdr	s_shdr32;
#endif
		Elf64_Shdr	s_shdr64;
	} s_shdr;
	STAILQ_HEAD(, _Libelf_Data) s_data;	/* translated data */
	STAILQ_HEAD(, _Libelf_Data) s_rawdata;	/* raw data */
	STAILQ_ENTRY(_Elf_Scn) s_next;
	struct _Elf	*s_elf;		/* parent ELF descriptor */
	unsigned int	s_flags;	/* flags for the section as a whole */
	size_t		s_ndx;		/* index# for this section */
	uint64_t	s_offset;	/* managed by elf_update() */
	uint64_t	s_rawoff;	/* original offset in the file */
	uint64_t	s_size;		/* managed by elf_update() */
};

enum {
	ELF_TOFILE,
	ELF_TOMEMORY
};

/* PRIVATE */

/* elf.c */
static struct _libelf_globals _libelf = {
	.libelf_arch		= LIBELF_ARCH,
	.libelf_byteorder	= LIBELF_BYTEORDER,
	.libelf_class		= LIBELF_CLASS,
	.libelf_error		= 0,
	.libelf_fillchar	= 0,
	.libelf_version		= EV_NONE
};

/* libelf_msize.c */
struct msize {
	size_t	msz64;
};

static struct msize msize[ELF_T_NUM] = {
[ELF_T_ADDR] = { .msz64 = sizeof(Elf64_Addr) },
[ELF_T_BYTE] = { .msz64 = 1 },
[ELF_T_CAP] = { .msz64 = sizeof(Elf64_Cap) },
[ELF_T_DYN] = { .msz64 = sizeof(Elf64_Dyn) },
[ELF_T_EHDR] = { .msz64 = sizeof(Elf64_Ehdr) },
[ELF_T_GNUHASH] = { .msz64 = 1 },
[ELF_T_HALF] = { .msz64 = sizeof(Elf64_Half) },
[ELF_T_LWORD] = { .msz64 = sizeof(Elf64_Lword) },
[ELF_T_MOVE] = { .msz64 = sizeof(Elf64_Move) },
[ELF_T_MOVEP] = { .msz64 = 0 },
[ELF_T_NOTE] = { .msz64 = 1 },
[ELF_T_OFF] = { .msz64 = sizeof(Elf64_Off) },
[ELF_T_PHDR] = { .msz64 = sizeof(Elf64_Phdr) },
[ELF_T_REL] = { .msz64 = sizeof(Elf64_Rel) },
[ELF_T_RELA] = { .msz64 = sizeof(Elf64_Rela) },
[ELF_T_SHDR] = { .msz64 = sizeof(Elf64_Shdr) },
[ELF_T_SWORD] = { .msz64 = sizeof(Elf64_Sword) },
[ELF_T_SXWORD] = { .msz64 = sizeof(Elf64_Sxword) },
[ELF_T_SYMINFO] = { .msz64 = sizeof(Elf64_Syminfo) },
[ELF_T_SYM] = { .msz64 = sizeof(Elf64_Sym) },
[ELF_T_VDEF] = { .msz64 = 1 },
[ELF_T_VNEED] = { .msz64 = 1 },
[ELF_T_WORD] = { .msz64 = sizeof(Elf64_Word) },
[ELF_T_XWORD] = { .msz64 = sizeof(Elf64_Xword) },
};

static size_t
_libelf_msize(Elf_Type t, int elfclass, unsigned int version)
{
	size_t sz;

	assert(/*elfclass == ELFCLASS32 ||*/ elfclass == ELFCLASS64);
	assert((signed) t >= ELF_T_FIRST && t <= ELF_T_LAST);

	if (version != EV_CURRENT) {
		LIBELF_SET_ERROR(VERSION, 0);
		return (0);
	}

	sz = /* (elfclass == ELFCLASS32) ? msize[t].msz32 : */ msize[t].msz64;

	return (sz);
}

/* libelf_fsize.c */
struct tfsize {
	size_t fsz64;
};

static struct tfsize tfsize[ELF_T_NUM] = {
[ELF_T_ADDR] =    { .fsz64 = sizeof(Elf64_Addr) },
[ELF_T_BYTE] =    { .fsz64 = 1 },
[ELF_T_CAP] =     { .fsz64 = sizeof(Elf64_Xword)+sizeof(Elf64_Xword)+0 },
[ELF_T_DYN] =     { .fsz64 = sizeof(Elf64_Sxword)+sizeof(Elf64_Xword)+0 },
[ELF_T_EHDR] =    { .fsz64 = EI_NIDENT
			    +sizeof(Elf64_Half)+sizeof(Elf64_Half)
			    +sizeof(Elf64_Word)+sizeof(Elf64_Addr)
			    +sizeof(Elf64_Off)+ sizeof(Elf64_Off)
			    +sizeof(Elf64_Word)+sizeof(Elf64_Half)
			    +sizeof(Elf64_Half)+sizeof(Elf64_Half)
			    +sizeof(Elf64_Half)+sizeof(Elf64_Half)
			    +sizeof(Elf64_Half)+0 },
[ELF_T_GNUHASH] = { .fsz64 = 1 },
[ELF_T_HALF] =    { .fsz64 = sizeof(Elf64_Half) },
[ELF_T_LWORD] =   { .fsz64 = sizeof(Elf64_Lword) },
[ELF_T_MOVE] =    { .fsz64 = sizeof(Elf64_Lword)+sizeof(Elf64_Xword)
			    +sizeof(Elf64_Xword)+sizeof(Elf64_Half)
			    +sizeof(Elf64_Half)+0 },
[ELF_T_MOVEP] =   { .fsz64 = 0 },
[ELF_T_NOTE] =    { .fsz64 = 1 },
[ELF_T_OFF] =     { .fsz64 = sizeof(Elf64_Off) },
[ELF_T_PHDR] =    { .fsz64 = sizeof(Elf64_Word)+sizeof(Elf64_Word)
			    +sizeof(Elf64_Off)+ sizeof(Elf64_Addr)
			    +sizeof(Elf64_Addr)+sizeof(Elf64_Xword)
			    +sizeof(Elf64_Xword)+sizeof(Elf64_Xword)+0 },
[ELF_T_REL] =     { .fsz64 = sizeof(Elf64_Addr)+sizeof(Elf64_Xword)+0 },
[ELF_T_RELA] =    { .fsz64 = sizeof(Elf64_Addr)+sizeof(Elf64_Xword)
			    +sizeof(Elf64_Sxword)+0 },
[ELF_T_SHDR] =    { .fsz64 = sizeof(Elf64_Word)+sizeof(Elf64_Word)
			    +sizeof(Elf64_Xword)+sizeof(Elf64_Addr)
			    +sizeof(Elf64_Off)+ sizeof(Elf64_Xword)
			    +sizeof(Elf64_Word)+sizeof(Elf64_Word)
			    +sizeof(Elf64_Xword)+sizeof(Elf64_Xword)+0 },
[ELF_T_SWORD] =   { .fsz64 = sizeof(Elf64_Sword) },
[ELF_T_SXWORD] =  { .fsz64 = sizeof(Elf64_Sxword) },
[ELF_T_SYMINFO] = { .fsz64 = sizeof(Elf64_Half)+sizeof(Elf64_Half)+0 },
[ELF_T_SYM] =     { .fsz64 = sizeof(Elf64_Word)+1+1+sizeof(Elf64_Half)
			    +sizeof(Elf64_Addr)+sizeof(Elf64_Xword)+0 },
[ELF_T_VDEF] =    { .fsz64 = 1 },
[ELF_T_VNEED] =   { .fsz64 = 1 },
[ELF_T_WORD] =    { .fsz64 = sizeof(Elf64_Word) },
[ELF_T_XWORD] =   { .fsz64 = sizeof(Elf64_Xword) },
};

static size_t
_libelf_fsize(Elf_Type t, int ec, unsigned int v, size_t c)
{
	size_t sz;

	sz = 0;
	if (v != EV_CURRENT)
		LIBELF_SET_ERROR(VERSION, 0);
	else if ((int) t < ELF_T_FIRST || t > ELF_T_LAST)
		LIBELF_SET_ERROR(ARGUMENT, 0);
	else {
		sz = ec == ELFCLASS64 ? tfsize[t].fsz64 : /* tfsize[t].fsz32 */ 0;
		if (sz == 0)
			LIBELF_SET_ERROR(UNIMPL, 0);
	}

	return (sz*c);
}

/* gelf_fsize.c */
static size_t
elf64_fsize(Elf_Type t, size_t c, unsigned int v)
{
	return (_libelf_fsize(t, ELFCLASS64, v, c));
}

/* libelf_allocate.h */
static Elf *
_libelf_allocate_elf(void)
{
	Elf *e;

	if ((e = malloc(sizeof(*e))) == NULL) {
		LIBELF_SET_ERROR(RESOURCE, errno);
		return NULL;
	}

	e->e_activations = 1;
	e->e_hdr.e_rawhdr = NULL;
	e->e_byteorder   = ELFDATANONE;
	e->e_class       = ELFCLASSNONE;
	e->e_cmd         = ELF_C_NULL;
	e->e_fd          = -1;
	e->e_flags	 = 0;
	e->e_kind        = ELF_K_NONE;
	e->e_parent      = NULL;
	e->e_rawfile     = NULL;
	e->e_rawsize     = 0;
	e->e_version     = LIBELF_PRIVATE(version);

	(void) memset(&e->e_u, 0, sizeof(e->e_u));

	return (e);
}

static void
_libelf_init_elf(Elf *e, Elf_Kind kind)
{
	assert(e != NULL);
	assert(e->e_kind == ELF_K_NONE);

	e->e_kind = kind;

	switch (kind) {
	case ELF_K_ELF:
		STAILQ_INIT(&e->e_u.e_elf.e_scn);
		break;
	default:
		break;
	}
}

#define	FREE(P)		do {				\
		if (P)					\
			free(P);			\
	} while (0)

static Elf *
_libelf_release_elf(Elf *e)
{
#if 0
	Elf_Arhdr *arh;
#endif

	switch (e->e_kind) {
#if 0
	case ELF_K_AR:
		FREE(e->e_u.e_ar.e_symtab);
		break;
#endif

	case ELF_K_ELF:
		switch (e->e_class) {
#if 0
		case ELFCLASS32:
			FREE(e->e_u.e_elf.e_ehdr.e_ehdr32);
			FREE(e->e_u.e_elf.e_phdr.e_phdr32);
			break;
#endif
		case ELFCLASS64:
			FREE(e->e_u.e_elf.e_ehdr.e_ehdr64);
			FREE(e->e_u.e_elf.e_phdr.e_phdr64);
			break;
		}

		assert(STAILQ_EMPTY(&e->e_u.e_elf.e_scn));

#if 0
		if (e->e_flags & LIBELF_F_AR_HEADER) {
			arh = e->e_hdr.e_arhdr;
			FREE(arh->ar_name);
			FREE(arh->ar_rawname);
			free(arh);
		}
#endif

		break;

	default:
		break;
	}

	free(e);

	return (NULL);
}

#undef FREE

static struct _Libelf_Data *
_libelf_allocate_data(Elf_Scn *s)
{
	struct _Libelf_Data *d;

	if ((d = calloc((size_t) 1, sizeof(*d))) == NULL) {
		LIBELF_SET_ERROR(RESOURCE, 0);
		return (NULL);
	}

	d->d_scn = s;

	return (d);
}

static struct _Libelf_Data *
_libelf_release_data(struct _Libelf_Data *d)
{

	if (d->d_flags & LIBELF_F_DATA_MALLOCED)
		free(d->d_data.d_buf);

	free(d);

	return (NULL);
}

static Elf_Scn *
_libelf_allocate_scn(Elf *e, size_t ndx)
{
	Elf_Scn *s;

	if ((s = calloc((size_t) 1, sizeof(Elf_Scn))) == NULL) {
		LIBELF_SET_ERROR(RESOURCE, errno);
		return (NULL);
	}

	s->s_elf = e;
	s->s_ndx = ndx;

	STAILQ_INIT(&s->s_data);
	STAILQ_INIT(&s->s_rawdata);

	STAILQ_INSERT_TAIL(&e->e_u.e_elf.e_scn, s, s_next);

	return (s);
}

static Elf_Scn *
_libelf_release_scn(Elf_Scn *s)
{
	Elf *e;
	struct _Libelf_Data *d, *td;

	assert(s != NULL);

	STAILQ_FOREACH_SAFE(d, &s->s_data, d_next, td) {
		STAILQ_REMOVE(&s->s_data, d, _Libelf_Data, d_next);
		d = _libelf_release_data(d);
	}

	STAILQ_FOREACH_SAFE(d, &s->s_rawdata, d_next, td) {
		assert((d->d_flags & LIBELF_F_DATA_MALLOCED) == 0);
		STAILQ_REMOVE(&s->s_rawdata, d, _Libelf_Data, d_next);
		d = _libelf_release_data(d);
	}

	e = s->s_elf;

	assert(e != NULL);

	STAILQ_REMOVE(&e->e_u.e_elf.e_scn, s, _Elf_Scn, s_next);

	free(s);

	return (NULL);
}

/* libelf_data.c */
static int
_libelf_xlate_shtype(uint32_t sht)
{
	/*
	 * Look for known section types.
	 */
	switch (sht) {
	case SHT_DYNAMIC:
		return (ELF_T_DYN);
	case SHT_DYNSYM:
		return (ELF_T_SYM);
	case SHT_FINI_ARRAY:
		return (ELF_T_ADDR);
	case SHT_GNU_HASH:
		return (ELF_T_GNUHASH);
	case SHT_GNU_LIBLIST:
		return (ELF_T_WORD);
	case SHT_GROUP:
		return (ELF_T_WORD);
	case SHT_HASH:
		return (ELF_T_WORD);
	case SHT_INIT_ARRAY:
		return (ELF_T_ADDR);
	case SHT_NOBITS:
		return (ELF_T_BYTE);
	case SHT_NOTE:
		return (ELF_T_NOTE);
	case SHT_PREINIT_ARRAY:
		return (ELF_T_ADDR);
	case SHT_PROGBITS:
		return (ELF_T_BYTE);
	case SHT_REL:
		return (ELF_T_REL);
	case SHT_RELA:
		return (ELF_T_RELA);
	case SHT_STRTAB:
		return (ELF_T_BYTE);
	case SHT_SYMTAB:
		return (ELF_T_SYM);
	case SHT_SYMTAB_SHNDX:
		return (ELF_T_WORD);
	case SHT_SUNW_dof:
		return (ELF_T_BYTE);
	case SHT_SUNW_move:
		return (ELF_T_MOVE);
	case SHT_SUNW_syminfo:
		return (ELF_T_SYMINFO);
	case SHT_SUNW_verdef:	/* == SHT_GNU_verdef */
		return (ELF_T_VDEF);
	case SHT_SUNW_verneed:	/* == SHT_GNU_verneed */
		return (ELF_T_VNEED);
	case SHT_SUNW_versym:	/* == SHT_GNU_versym */
		return (ELF_T_HALF);
	default:
		/*
		 * Values in the range [SHT_LOOS..SHT_HIUSER] (i.e.,
		 * OS, processor and user-defined section types) are
		 * legal, but since we do not know anything more about
		 * their semantics, we return a type of ELF_T_BYTE.
		 */
		if (sht >= SHT_LOOS && sht <= SHT_HIUSER)
			return (ELF_T_BYTE);

		/*
		 * Other values are unsupported.
		 */
		return (-1);
	}
}

/* libelf_convert.c */
#define	SWAP_BYTE(X)	do { (void) (X); } while (0)
#define	SWAP_IDENT(X)	do { (void) (X); } while (0)
#define	SWAP_HALF(X)	do {						\
		uint16_t _x = (uint16_t) (X);				\
		uint32_t _t = _x & 0xFFU;				\
		_t <<= 8U; _x >>= 8U; _t |= _x & 0xFFU;			\
		(X) = (uint16_t) _t;					\
	} while (0)
#define	_SWAP_WORD(X, T) do {						\
		uint32_t _x = (uint32_t) (X);				\
		uint32_t _t = _x & 0xFF;				\
		_t <<= 8; _x >>= 8; _t |= _x & 0xFF;			\
		_t <<= 8; _x >>= 8; _t |= _x & 0xFF;			\
		_t <<= 8; _x >>= 8; _t |= _x & 0xFF;			\
		(X) = (T) _t;						\
	} while (0)
#define	SWAP_SWORD(X)	_SWAP_WORD(X, /* Elf32_Sword */ Elf64_Sword)
#define	SWAP_WORD(X)	_SWAP_WORD(X, /* Elf32_Word */ Elf64_Word)
#define	_SWAP_WORD64(X, T) do {						\
		uint64_t _x = (uint64_t) (X);				\
		uint64_t _t = _x & 0xFF;				\
		_t <<= 8; _x >>= 8; _t |= _x & 0xFF;			\
		_t <<= 8; _x >>= 8; _t |= _x & 0xFF;			\
		_t <<= 8; _x >>= 8; _t |= _x & 0xFF;			\
		_t <<= 8; _x >>= 8; _t |= _x & 0xFF;			\
		_t <<= 8; _x >>= 8; _t |= _x & 0xFF;			\
		_t <<= 8; _x >>= 8; _t |= _x & 0xFF;			\
		_t <<= 8; _x >>= 8; _t |= _x & 0xFF;			\
		(X) = (T) _t;						\
	} while (0)
#define	SWAP_ADDR64(X)	_SWAP_WORD64(X, Elf64_Addr)
#define	SWAP_LWORD(X)	_SWAP_WORD64(X, Elf64_Lword)
#define	SWAP_OFF64(X)	_SWAP_WORD64(X, Elf64_Off)
#define	SWAP_SXWORD(X)	_SWAP_WORD64(X, Elf64_Sxword)
#define	SWAP_XWORD(X)	_SWAP_WORD64(X, Elf64_Xword)

#define	READ_BYTE(P,X)	do {						\
		const unsigned char *const _p =				\
			(const unsigned char *) (P);			\
		(X)		= _p[0];				\
		(P)		= (P) + 1;				\
	} while (0)
#define	READ_HALF(P,X)	do {						\
		uint16_t _t;						\
		unsigned char *const _q = (unsigned char *) &_t;	\
		const unsigned char *const _p =				\
			(const unsigned char *) (P);			\
		_q[0]		= _p[0];				\
		_q[1]		= _p[1];				\
		(P)		= (P) + 2;				\
		(X)		= _t;					\
	} while (0)
#define	_READ_WORD(P,X,T) do {						\
		uint32_t _t;						\
		unsigned char *const _q = (unsigned char *) &_t;	\
		const unsigned char *const _p =				\
			(const unsigned char *) (P);			\
		_q[0]		= _p[0];				\
		_q[1]		= _p[1];				\
		_q[2]		= _p[2];				\
		_q[3]		= _p[3];				\
		(P)		= (P) + 4;				\
		(X)		= (T) _t;				\
	} while (0)
#define	READ_SWORD(P,X)		_READ_WORD(P, X, /*Elf32_Sword*/ Elf64_Sword)
#define	READ_WORD(P,X)		_READ_WORD(P, X, /*Elf32_Word*/ Elf64_Word)
#define	_READ_WORD64(P,X,T)	do {					\
		uint64_t _t;						\
		unsigned char *const _q = (unsigned char *) &_t;	\
		const unsigned char *const _p =				\
			(const unsigned char *) (P);			\
		_q[0]		= _p[0];				\
		_q[1]		= _p[1];				\
		_q[2]		= _p[2];				\
		_q[3]		= _p[3];				\
		_q[4]		= _p[4];				\
		_q[5]		= _p[5];				\
		_q[6]		= _p[6];				\
		_q[7]		= _p[7];				\
		(P)		= (P) + 8;				\
		(X)		= (T) _t;				\
	} while (0)
#define	READ_ADDR64(P,X)	_READ_WORD64(P, X, Elf64_Addr)
#define	READ_LWORD(P,X)		_READ_WORD64(P, X, Elf64_Lword)
#define	READ_OFF64(P,X)		_READ_WORD64(P, X, Elf64_Off)
#define	READ_SXWORD(P,X)	_READ_WORD64(P, X, Elf64_Sxword)
#define	READ_XWORD(P,X)		_READ_WORD64(P, X, Elf64_Xword)
#define	READ_IDENT(P,X)		do {					\
		(void) memcpy((X), (P), sizeof((X)));			\
		(P)		= (P) + EI_NIDENT;			\
	} while (0)

#define	ROUNDUP2(V,N)	(V) = ((((V) + (N) - 1)) & ~((N) - 1))

static int
_libelf_cvt_ADDR64_tom(unsigned char *dst, size_t dsz, unsigned char *src,
    size_t count, int byteswap)
{
	Elf64_Addr t, *d = (Elf64_Addr *) (uintptr_t) dst;
	size_t c;

	if (dsz < count * sizeof(Elf64_Addr))
		return (0);

	if (!byteswap) {
		(void) memcpy(dst, src, count * sizeof(*d));
		return (1);
	}

	for (c = 0; c < count; c++) {
		READ_ADDR64(src,t);
		SWAP_ADDR64(t);
		*d++ = t;
	}

	return (1);
}

static int
_libelf_cvt_CAP64_tom(unsigned char *dst, size_t dsz, unsigned char *src,
    size_t count, int byteswap)
{
	Elf64_Cap	t, *d;
	unsigned char	*s,*s0;
	size_t		fsz;

	fsz = elf64_fsize(ELF_T_CAP, (size_t) 1, EV_CURRENT);
	d   = ((Elf64_Cap *) (uintptr_t) dst) + (count - 1);
	s0  = src + (count - 1) * fsz;

	if (dsz < count * sizeof(Elf64_Cap))
		return (0);

	while (count--) {
		s = s0;
		/* Read an Elf64_Cap */
		READ_XWORD(s,t.c_tag);
		READ_XWORD(s,t.c_un.c_val);
		/**/
		if (byteswap) {
			/* Swap an Elf64_Cap */
			SWAP_XWORD(t.c_tag);
			SWAP_XWORD(t.c_un.c_val);
			/**/
		}
		*d-- = t; s0 -= fsz;
	}

	return (1);
}

static int
_libelf_cvt_DYN64_tom(unsigned char *dst, size_t dsz, unsigned char *src,
    size_t count, int byteswap)
{
	Elf64_Dyn	t, *d;
	unsigned char	*s,*s0;
	size_t		fsz;

	fsz = elf64_fsize(ELF_T_DYN, (size_t) 1, EV_CURRENT);
	d   = ((Elf64_Dyn *) (uintptr_t) dst) + (count - 1);
	s0  = src + (count - 1) * fsz;

	if (dsz < count * sizeof(Elf64_Dyn))
		return (0);

	while (count--) {
		s = s0;
		/* Read an Elf64_Dyn */
		READ_SXWORD(s,t.d_tag);
		READ_XWORD(s,t.d_un.d_ptr);
		/**/
		if (byteswap) {
			/* Swap an Elf64_Dyn */
			SWAP_SXWORD(t.d_tag);
			SWAP_XWORD(t.d_un.d_ptr);
			/**/
		}
		*d-- = t; s0 -= fsz;
	}

	return (1);
}

static int
_libelf_cvt_EHDR64_tom(unsigned char *dst, size_t dsz, unsigned char *src,
    size_t count, int byteswap)
{
	Elf64_Ehdr	t, *d;
	unsigned char	*s,*s0;
	size_t		fsz;

	fsz = elf64_fsize(ELF_T_EHDR, (size_t) 1, EV_CURRENT);
	d   = ((Elf64_Ehdr *) (uintptr_t) dst) + (count - 1);
	s0  = src + (count - 1) * fsz;

	if (dsz < count * sizeof(Elf64_Ehdr))
		return (0);

	while (count--) {
		s = s0;
		/* Read an Elf64_Ehdr */
		READ_IDENT(s,t.e_ident);
		READ_HALF(s,t.e_type);
		READ_HALF(s,t.e_machine);
		READ_WORD(s,t.e_version);
		READ_ADDR64(s,t.e_entry);
		READ_OFF64(s,t.e_phoff);
		READ_OFF64(s,t.e_shoff);
		READ_WORD(s,t.e_flags);
		READ_HALF(s,t.e_ehsize);
		READ_HALF(s,t.e_phentsize);
		READ_HALF(s,t.e_phnum);
		READ_HALF(s,t.e_shentsize);
		READ_HALF(s,t.e_shnum);
		READ_HALF(s,t.e_shstrndx);
		/**/
		if (byteswap) {
			/* Swap an Elf64_Ehdr */
			SWAP_IDENT(t.e_ident);
			SWAP_HALF(t.e_type);
			SWAP_HALF(t.e_machine);
			SWAP_WORD(t.e_version);
			SWAP_ADDR64(t.e_entry);
			SWAP_OFF64(t.e_phoff);
			SWAP_OFF64(t.e_shoff);
			SWAP_WORD(t.e_flags);
			SWAP_HALF(t.e_ehsize);
			SWAP_HALF(t.e_phentsize);
			SWAP_HALF(t.e_phnum);
			SWAP_HALF(t.e_shentsize);
			SWAP_HALF(t.e_shnum);
			SWAP_HALF(t.e_shstrndx);
			/**/
		}
		*d-- = t; s0 -= fsz;
	}

	return (1);
}

static int
_libelf_cvt_HALF_tom(unsigned char *dst, size_t dsz, unsigned char *src,
    size_t count, int byteswap)
{
	Elf64_Half t, *d = (Elf64_Half *) (uintptr_t) dst;
	size_t c;

	if (dsz < count * sizeof(Elf64_Half))
		return (0);

	if (!byteswap) {
		(void) memcpy(dst, src, count * sizeof(*d));
		return (1);
	}

	for (c = 0; c < count; c++) {
		READ_HALF(src,t);
		SWAP_HALF(t);
		*d++ = t;
	}

	return (1);
}

static int
_libelf_cvt_LWORD_tom(unsigned char *dst, size_t dsz, unsigned char *src,
    size_t count, int byteswap)
{
	Elf64_Lword t, *d = (Elf64_Lword *) (uintptr_t) dst;
	size_t c;

	if (dsz < count * sizeof(Elf64_Lword))
		return (0);

	if (!byteswap) {
		(void) memcpy(dst, src, count * sizeof(*d));
		return (1);
	}

	for (c = 0; c < count; c++) {
		READ_LWORD(src,t);
		SWAP_LWORD(t);
		*d++ = t;
	}

	return (1);
}

static int
_libelf_cvt_MOVE64_tom(unsigned char *dst, size_t dsz, unsigned char *src,
    size_t count, int byteswap)
{
	Elf64_Move	t, *d;
	unsigned char	*s,*s0;
	size_t		fsz;

	fsz = elf64_fsize(ELF_T_MOVE, (size_t) 1, EV_CURRENT);
	d   = ((Elf64_Move *) (uintptr_t) dst) + (count - 1);
	s0  = src + (count - 1) * fsz;

	if (dsz < count * sizeof(Elf64_Move))
		return (0);

	while (count--) {
		s = s0;
		/* Read an Elf64_Move */
		READ_LWORD(s,t.m_value);
		READ_XWORD(s,t.m_info);
		READ_XWORD(s,t.m_poffset);
		READ_HALF(s,t.m_repeat);
		READ_HALF(s,t.m_stride);
		/**/
		if (byteswap) {
			/* Swap an Elf64_Move */
			SWAP_LWORD(t.m_value);
			SWAP_XWORD(t.m_info);
			SWAP_XWORD(t.m_poffset);
			SWAP_HALF(t.m_repeat);
			SWAP_HALF(t.m_stride);
			/**/
		}
		*d-- = t; s0 -= fsz;
	}

	return (1);
}

static int
_libelf_cvt_OFF64_tom(unsigned char *dst, size_t dsz, unsigned char *src,
    size_t count, int byteswap)
{
	Elf64_Off t, *d = (Elf64_Off *) (uintptr_t) dst;
	size_t c;

	if (dsz < count * sizeof(Elf64_Off))
		return (0);

	if (!byteswap) {
		(void) memcpy(dst, src, count * sizeof(*d));
		return (1);
	}

	for (c = 0; c < count; c++) {
		READ_OFF64(src,t);
		SWAP_OFF64(t);
		*d++ = t;
	}

	return (1);
}

static int
_libelf_cvt_PHDR64_tom(unsigned char *dst, size_t dsz, unsigned char *src,
    size_t count, int byteswap)
{
	Elf64_Phdr	t, *d;
	unsigned char	*s,*s0;
	size_t		fsz;

	fsz = elf64_fsize(ELF_T_PHDR, (size_t) 1, EV_CURRENT);
	d   = ((Elf64_Phdr *) (uintptr_t) dst) + (count - 1);
	s0  = src + (count - 1) * fsz;

	if (dsz < count * sizeof(Elf64_Phdr))
		return (0);

	while (count--) {
		s = s0;
		/* Read an Elf64_Phdr */
		READ_WORD(s,t.p_type);
		READ_WORD(s,t.p_flags);
		READ_OFF64(s,t.p_offset);
		READ_ADDR64(s,t.p_vaddr);
		READ_ADDR64(s,t.p_paddr);
		READ_XWORD(s,t.p_filesz);
		READ_XWORD(s,t.p_memsz);
		READ_XWORD(s,t.p_align);
		/**/
		if (byteswap) {
			/* Swap an Elf64_Phdr */
			SWAP_WORD(t.p_type);
			SWAP_WORD(t.p_flags);
			SWAP_OFF64(t.p_offset);
			SWAP_ADDR64(t.p_vaddr);
			SWAP_ADDR64(t.p_paddr);
			SWAP_XWORD(t.p_filesz);
			SWAP_XWORD(t.p_memsz);
			SWAP_XWORD(t.p_align);
			/**/
		}
		*d-- = t; s0 -= fsz;
	}

	return (1);
}

static int
_libelf_cvt_REL64_tom(unsigned char *dst, size_t dsz, unsigned char *src,
    size_t count, int byteswap)
{
	Elf64_Rel	t, *d;
	unsigned char	*s,*s0;
	size_t		fsz;

	fsz = elf64_fsize(ELF_T_REL, (size_t) 1, EV_CURRENT);
	d   = ((Elf64_Rel *) (uintptr_t) dst) + (count - 1);
	s0  = src + (count - 1) * fsz;

	if (dsz < count * sizeof(Elf64_Rel))
		return (0);

	while (count--) {
		s = s0;
		/* Read an Elf64_Rel */
		READ_ADDR64(s,t.r_offset);
		READ_XWORD(s,t.r_info);
		/**/
		if (byteswap) {
			/* Swap an Elf64_Rel */
			SWAP_ADDR64(t.r_offset);
			SWAP_XWORD(t.r_info);
			/**/
		}
		*d-- = t; s0 -= fsz;
	}

	return (1);
}

static int
_libelf_cvt_RELA64_tom(unsigned char *dst, size_t dsz, unsigned char *src,
    size_t count, int byteswap)
{
	Elf64_Rela	t, *d;
	unsigned char	*s,*s0;
	size_t		fsz;

	fsz = elf64_fsize(ELF_T_RELA, (size_t) 1, EV_CURRENT);
	d   = ((Elf64_Rela *) (uintptr_t) dst) + (count - 1);
	s0  = src + (count - 1) * fsz;

	if (dsz < count * sizeof(Elf64_Rela))
		return (0);

	while (count--) {
		s = s0;
		/* Read an Elf64_Rela */
		READ_ADDR64(s,t.r_offset);
		READ_XWORD(s,t.r_info);
		READ_SXWORD(s,t.r_addend);
		/**/
		if (byteswap) {
			/* Swap an Elf64_Rela */
			SWAP_ADDR64(t.r_offset);
			SWAP_XWORD(t.r_info);
			SWAP_SXWORD(t.r_addend);
			/**/
		}
		*d-- = t; s0 -= fsz;
	}

	return (1);
}

static int
_libelf_cvt_SHDR64_tom(unsigned char *dst, size_t dsz, unsigned char *src,
    size_t count, int byteswap)
{
	Elf64_Shdr	t, *d;
	unsigned char	*s,*s0;
	size_t		fsz;

	fsz = elf64_fsize(ELF_T_SHDR, (size_t) 1, EV_CURRENT);
	d   = ((Elf64_Shdr *) (uintptr_t) dst) + (count - 1);
	s0  = src + (count - 1) * fsz;

	if (dsz < count * sizeof(Elf64_Shdr))
		return (0);

	while (count--) {
		s = s0;
		/* Read an Elf64_Shdr */
		READ_WORD(s,t.sh_name);
		READ_WORD(s,t.sh_type);
		READ_XWORD(s,t.sh_flags);
		READ_ADDR64(s,t.sh_addr);
		READ_OFF64(s,t.sh_offset);
		READ_XWORD(s,t.sh_size);
		READ_WORD(s,t.sh_link);
		READ_WORD(s,t.sh_info);
		READ_XWORD(s,t.sh_addralign);
		READ_XWORD(s,t.sh_entsize);
		/**/
		if (byteswap) {
			/* Swap an Elf64_Shdr */
			SWAP_WORD(t.sh_name);
			SWAP_WORD(t.sh_type);
			SWAP_XWORD(t.sh_flags);
			SWAP_ADDR64(t.sh_addr);
			SWAP_OFF64(t.sh_offset);
			SWAP_XWORD(t.sh_size);
			SWAP_WORD(t.sh_link);
			SWAP_WORD(t.sh_info);
			SWAP_XWORD(t.sh_addralign);
			SWAP_XWORD(t.sh_entsize);
			/**/
		}
		*d-- = t; s0 -= fsz;
	}

	return (1);
}

static int
_libelf_cvt_SWORD_tom(unsigned char *dst, size_t dsz, unsigned char *src,
    size_t count, int byteswap)
{
	Elf64_Sword t, *d = (Elf64_Sword *) (uintptr_t) dst;
	size_t c;

	if (dsz < count * sizeof(Elf64_Sword))
		return (0);

	if (!byteswap) {
		(void) memcpy(dst, src, count * sizeof(*d));
		return (1);
	}

	for (c = 0; c < count; c++) {
		READ_SWORD(src,t);
		SWAP_SWORD(t);
		*d++ = t;
	}

	return (1);
}

static int
_libelf_cvt_SXWORD_tom(unsigned char *dst, size_t dsz, unsigned char *src,
    size_t count, int byteswap)
{
	Elf64_Sxword t, *d = (Elf64_Sxword *) (uintptr_t) dst;
	size_t c;

	if (dsz < count * sizeof(Elf64_Sxword))
		return (0);

	if (!byteswap) {
		(void) memcpy(dst, src, count * sizeof(*d));
		return (1);
	}

	for (c = 0; c < count; c++) {
		READ_SXWORD(src,t);
		SWAP_SXWORD(t);
		*d++ = t;
	}

	return (1);
}

static int
_libelf_cvt_SYMINFO64_tom(unsigned char *dst, size_t dsz, unsigned char *src,
    size_t count, int byteswap)
{
	Elf64_Syminfo	t, *d;
	unsigned char	*s,*s0;
	size_t		fsz;

	fsz = elf64_fsize(ELF_T_SYMINFO, (size_t) 1, EV_CURRENT);
	d   = ((Elf64_Syminfo *) (uintptr_t) dst) + (count - 1);
	s0  = src + (count - 1) * fsz;

	if (dsz < count * sizeof(Elf64_Syminfo))
		return (0);

	while (count--) {
		s = s0;
		/* Read an Elf64_Syminfo */
		READ_HALF(s,t.si_boundto);
		READ_HALF(s,t.si_flags);
		/**/
		if (byteswap) {
			/* Swap an Elf64_Syminfo */
			SWAP_HALF(t.si_boundto);
			SWAP_HALF(t.si_flags);
			/**/
		}
		*d-- = t; s0 -= fsz;
	}

	return (1);
}

static int
_libelf_cvt_SYM64_tom(unsigned char *dst, size_t dsz, unsigned char *src,
    size_t count, int byteswap)
{
	Elf64_Sym	t, *d;
	unsigned char	*s,*s0;
	size_t		fsz;

	fsz = elf64_fsize(ELF_T_SYM, (size_t) 1, EV_CURRENT);
	d   = ((Elf64_Sym *) (uintptr_t) dst) + (count - 1);
	s0  = src + (count - 1) * fsz;

	if (dsz < count * sizeof(Elf64_Sym))
		return (0);

	while (count--) {
		s = s0;
		/* Read an Elf64_Sym */
		READ_WORD(s,t.st_name);
		READ_BYTE(s,t.st_info);
		READ_BYTE(s,t.st_other);
		READ_HALF(s,t.st_shndx);
		READ_ADDR64(s,t.st_value);
		READ_XWORD(s,t.st_size);
		/**/
		if (byteswap) {
			/* Swap an Elf64_Sym */
			SWAP_WORD(t.st_name);
			SWAP_BYTE(t.st_info);
			SWAP_BYTE(t.st_other);
			SWAP_HALF(t.st_shndx);
			SWAP_ADDR64(t.st_value);
			SWAP_XWORD(t.st_size);
			/**/
		}
		*d-- = t; s0 -= fsz;
	}

	return (1);
}

static int
_libelf_cvt_WORD_tom(unsigned char *dst, size_t dsz, unsigned char *src,
    size_t count, int byteswap)
{
	Elf64_Word t, *d = (Elf64_Word *) (uintptr_t) dst;
	size_t c;

	if (dsz < count * sizeof(Elf64_Word))
		return (0);

	if (!byteswap) {
		(void) memcpy(dst, src, count * sizeof(*d));
		return (1);
	}

	for (c = 0; c < count; c++) {
		READ_WORD(src,t);
		SWAP_WORD(t);
		*d++ = t;
	}

	return (1);
}

static int
_libelf_cvt_XWORD_tom(unsigned char *dst, size_t dsz, unsigned char *src,
    size_t count, int byteswap)
{
	Elf64_Xword t, *d = (Elf64_Xword *) (uintptr_t) dst;
	size_t c;

	if (dsz < count * sizeof(Elf64_Xword))
		return (0);

	if (!byteswap) {
		(void) memcpy(dst, src, count * sizeof(*d));
		return (1);
	}

	for (c = 0; c < count; c++) {
		READ_XWORD(src,t);
		SWAP_XWORD(t);
		*d++ = t;
	}

	return (1);
}

static int
_libelf_cvt_VDEF64_tom(unsigned char *dst, size_t dsz, unsigned char *src,
    size_t count, int byteswap)
{
	Elf64_Verdef	t, *dp;
	Elf64_Verdaux	a, *ap;
	const size_t	verfsz = 20;
	const size_t	auxfsz = 8;
	const size_t	vermsz = sizeof(Elf64_Verdef);
	const size_t	auxmsz = sizeof(Elf64_Verdaux);
	unsigned char * const dstend = dst + dsz;
	unsigned char * const srcend = src + count;
	unsigned char	*dstaux, *s, *srcaux, *stmp;
	Elf64_Word	aux, anext, cnt, vnext;

	for (stmp = src, vnext = ~0U;
	     vnext != 0 && stmp + verfsz <= srcend && dst + vermsz <= dstend;
	     stmp += vnext, dst += vnext) {

		/* Read in a VDEF structure. */
		s = stmp;
		/* Read an Elf64_Verdef */
		READ_HALF(s,t.vd_version);
		READ_HALF(s,t.vd_flags);
		READ_HALF(s,t.vd_ndx);
		READ_HALF(s,t.vd_cnt);
		READ_WORD(s,t.vd_hash);
		READ_WORD(s,t.vd_aux);
		READ_WORD(s,t.vd_next);
		/**/
		if (byteswap) {
			/* Swap an Elf64_Verdef */
			SWAP_HALF(t.vd_version);
			SWAP_HALF(t.vd_flags);
			SWAP_HALF(t.vd_ndx);
			SWAP_HALF(t.vd_cnt);
			SWAP_WORD(t.vd_hash);
			SWAP_WORD(t.vd_aux);
			SWAP_WORD(t.vd_next);
			/**/
		}

		dp = (Elf64_Verdef *) (uintptr_t) dst;
		*dp = t;

		aux = t.vd_aux;
		cnt = t.vd_cnt;
		vnext = t.vd_next;

		if (aux < vermsz)
			return (0);

		/* Process AUX entries. */
		for (anext = ~0U, dstaux = dst + aux, srcaux = stmp + aux;
		     cnt != 0 && anext != 0 && dstaux + auxmsz <= dstend &&
			srcaux + auxfsz <= srcend;
		     dstaux += anext, srcaux += anext, cnt--) {

			s = srcaux;
			/* Read an Elf64_Verdaux */
		READ_WORD(s,a.vda_name);
		READ_WORD(s,a.vda_next);
		/**/

			if (byteswap) {
				/* Swap an Elf64_Verdaux */
			SWAP_WORD(a.vda_name);
			SWAP_WORD(a.vda_next);
			/**/
			}

			anext = a.vda_next;

			ap = ((Elf64_Verdaux *) (uintptr_t) dstaux);
			*ap = a;
		}

		if (anext || cnt)
			return (0);
	}

	if (vnext)
		return (0);

	return (1);
}

static int
_libelf_cvt_VNEED64_tom(unsigned char *dst, size_t dsz, unsigned char *src,
    size_t count, int byteswap)
{
	Elf64_Verneed	t, *dp;
	Elf64_Vernaux	a, *ap;
	const size_t	verfsz = 16;
	const size_t	auxfsz = 16;
	const size_t	vermsz = sizeof(Elf64_Verneed);
	const size_t	auxmsz = sizeof(Elf64_Vernaux);
	unsigned char * const dstend = dst + dsz;
	unsigned char * const srcend = src + count;
	unsigned char	*dstaux, *s, *srcaux, *stmp;
	Elf64_Word	aux, anext, cnt, vnext;

	for (stmp = src, vnext = ~0U;
	     vnext != 0 && stmp + verfsz <= srcend && dst + vermsz <= dstend;
	     stmp += vnext, dst += vnext) {

		/* Read in a VNEED structure. */
		s = stmp;
		/* Read an Elf64_Verneed */
		READ_HALF(s,t.vn_version);
		READ_HALF(s,t.vn_cnt);
		READ_WORD(s,t.vn_file);
		READ_WORD(s,t.vn_aux);
		READ_WORD(s,t.vn_next);
		/**/
		if (byteswap) {
			/* Swap an Elf64_Verneed */
			SWAP_HALF(t.vn_version);
			SWAP_HALF(t.vn_cnt);
			SWAP_WORD(t.vn_file);
			SWAP_WORD(t.vn_aux);
			SWAP_WORD(t.vn_next);
			/**/
		}

		dp = (Elf64_Verneed *) (uintptr_t) dst;
		*dp = t;

		aux = t.vn_aux;
		cnt = t.vn_cnt;
		vnext = t.vn_next;

		if (aux < vermsz)
			return (0);

		/* Process AUX entries. */
		for (anext = ~0U, dstaux = dst + aux, srcaux = stmp + aux;
		     cnt != 0 && anext != 0 && dstaux + auxmsz <= dstend &&
			srcaux + auxfsz <= srcend;
		     dstaux += anext, srcaux += anext, cnt--) {

			s = srcaux;
			/* Read an Elf64_Vernaux */
		READ_WORD(s,a.vna_hash);
		READ_HALF(s,a.vna_flags);
		READ_HALF(s,a.vna_other);
		READ_WORD(s,a.vna_name);
		READ_WORD(s,a.vna_next);
		/**/

			if (byteswap) {
				/* Swap an Elf64_Vernaux */
			SWAP_WORD(a.vna_hash);
			SWAP_HALF(a.vna_flags);
			SWAP_HALF(a.vna_other);
			SWAP_WORD(a.vna_name);
			SWAP_WORD(a.vna_next);
			/**/
			}

			anext = a.vna_next;

			ap = ((Elf64_Vernaux *) (uintptr_t) dstaux);
			*ap = a;
		}

		if (anext || cnt)
			return (0);
	}

	if (vnext)
		return (0);

	return (1);
}

static int
_libelf_cvt_BYTE_tox(unsigned char *dst, size_t dsz, unsigned char *src,
    size_t count, int byteswap)
{
	(void) byteswap;
	if (dsz < count)
		return (0);
	if (dst != src)
		(void) memcpy(dst, src, count);
	return (1);
}

static int
_libelf_cvt_GNUHASH64_tom(unsigned char *dst, size_t dsz, unsigned char *src,
    size_t srcsz, int byteswap)
{
	size_t sz;
	uint64_t t64, *bloom64;
	Elf_GNU_Hash_Header *gh;
	uint32_t n, nbuckets, nchains, maskwords, shift2, symndx, t32;
	uint32_t *buckets, *chains;

	sz = 4 * sizeof(uint32_t);	/* File header is 4 words long. */
	if (dsz < sizeof(Elf_GNU_Hash_Header) || srcsz < sz)
		return (0);

	/* Read in the section header and byteswap if needed. */
	READ_WORD(src, nbuckets);
	READ_WORD(src, symndx);
	READ_WORD(src, maskwords);
	READ_WORD(src, shift2);

	srcsz -= sz;

	if (byteswap) {
		SWAP_WORD(nbuckets);
		SWAP_WORD(symndx);
		SWAP_WORD(maskwords);
		SWAP_WORD(shift2);
	}

	/* Check source buffer and destination buffer sizes. */
	sz = nbuckets * sizeof(uint32_t) + maskwords * sizeof(uint64_t);
	if (srcsz < sz || dsz < sz + sizeof(Elf_GNU_Hash_Header))
		return (0);

	gh = (Elf_GNU_Hash_Header *) (uintptr_t) dst;
	gh->gh_nbuckets  = nbuckets;
	gh->gh_symndx    = symndx;
	gh->gh_maskwords = maskwords;
	gh->gh_shift2    = shift2;

	dsz -= sizeof(Elf_GNU_Hash_Header);
	dst += sizeof(Elf_GNU_Hash_Header);

	bloom64 = (uint64_t *) (uintptr_t) dst;

	/* Copy bloom filter data. */
	for (n = 0; n < maskwords; n++) {
		READ_XWORD(src, t64);
		if (byteswap)
			SWAP_XWORD(t64);
		bloom64[n] = t64;
	}

	/* The hash buckets follows the bloom filter. */
	dst += maskwords * sizeof(uint64_t);
	buckets = (uint32_t *) (uintptr_t) dst;

	for (n = 0; n < nbuckets; n++) {
		READ_WORD(src, t32);
		if (byteswap)
			SWAP_WORD(t32);
		buckets[n] = t32;
	}

	dst += nbuckets * sizeof(uint32_t);

	/* The hash chain follows the hash buckets. */
	dsz -= sz;
	srcsz -= sz;

	if (dsz < srcsz)	/* Destination lacks space. */
		return (0);

	nchains = srcsz / sizeof(uint32_t);
	chains = (uint32_t *) (uintptr_t) dst;

	for (n = 0; n < nchains; n++) {
		READ_WORD(src, t32);
		if (byteswap)
			SWAP_WORD(t32);
		*chains++ = t32;
	}

	return (1);
}

static int
_libelf_cvt_NOTE_tom(unsigned char *dst, size_t dsz, unsigned char *src,
    size_t count, int byteswap)
{
	uint32_t namesz, descsz, type;
	Elf_Note *en;
	size_t sz, hdrsz;

	if (dsz < count)	/* Destination buffer is too small. */
		return (0);

	hdrsz = 3 * sizeof(uint32_t);
	if (count < hdrsz)		/* Source too small. */
		return (0);

	if (!byteswap) {
		(void) memcpy(dst, src, count);
		return (1);
	}

	/* Process all notes in the section. */
	while (count > hdrsz) {
		/* Read the note header. */
		READ_WORD(src, namesz);
		READ_WORD(src, descsz);
		READ_WORD(src, type);

		/* Translate. */
		SWAP_WORD(namesz);
		SWAP_WORD(descsz);
		SWAP_WORD(type);

		/* Copy out the translated note header. */
		en = (Elf_Note *) (uintptr_t) dst;
		en->n_namesz = namesz;
		en->n_descsz = descsz;
		en->n_type = type;

		dsz -= sizeof(Elf_Note);
		dst += sizeof(Elf_Note);
		count -= hdrsz;

		ROUNDUP2(namesz, 4U);
		ROUNDUP2(descsz, 4U);

		sz = namesz + descsz;

		if (count < sz || dsz < sz)	/* Buffers are too small. */
			return (0);

		(void) memcpy(dst, src, sz);

		src += sz;
		dst += sz;

		count -= sz;
		dsz -= sz;
	}

	return (1);
}

struct converters {
	int	(*tof32)(unsigned char *dst, size_t dsz, unsigned char *src,
		    size_t cnt, int byteswap);
	int	(*tom32)(unsigned char *dst, size_t dsz, unsigned char *src,
		    size_t cnt, int byteswap);
	int	(*tof64)(unsigned char *dst, size_t dsz, unsigned char *src,
		    size_t cnt, int byteswap);
	int	(*tom64)(unsigned char *dst, size_t dsz, unsigned char *src,
		    size_t cnt, int byteswap);
};

static struct converters cvt[ELF_T_NUM] = {
	/*[*/
	[ELF_T_ADDR] =    { .tof32 = NULL, .tom32 = NULL, .tof64 = NULL,
		.tom64 = _libelf_cvt_ADDR64_tom },
	[ELF_T_CAP] =     { .tof32 = NULL, .tom32 = NULL, .tof64 = NULL,
		.tom64 = _libelf_cvt_CAP64_tom },
	[ELF_T_DYN] =     { .tof32 = NULL, .tom32 = NULL, .tof64 = NULL,
		.tom64 = _libelf_cvt_DYN64_tom },
	[ELF_T_EHDR] =    { .tof32 = NULL, .tom32 = NULL, .tof64 = NULL,
		.tom64 = _libelf_cvt_EHDR64_tom },
	[ELF_T_GNUHASH] = { .tof32 = NULL, .tom32 = NULL, .tof64 = NULL,
		.tom64 = _libelf_cvt_GNUHASH64_tom },
	[ELF_T_HALF] =    { .tof32 = NULL, .tom32 = NULL, .tof64 = NULL,
		.tom64 = _libelf_cvt_HALF_tom },
	[ELF_T_LWORD] =   { .tof32 = NULL, .tom32 = NULL, .tof64 = NULL,
		.tom64 = _libelf_cvt_LWORD_tom },
	[ELF_T_MOVE] =    { .tof32 = NULL, .tom32 = NULL, .tof64 = NULL,
		.tom64 = _libelf_cvt_MOVE64_tom },
	[ELF_T_OFF] =     { .tof32 = NULL, .tom32 = NULL, .tof64 = NULL,
		.tom64 = _libelf_cvt_OFF64_tom },
	[ELF_T_PHDR] =    { .tof32 = NULL, .tom32 = NULL, .tof64 = NULL,
		.tom64 = _libelf_cvt_PHDR64_tom },
	[ELF_T_REL] =     { .tof32 = NULL, .tom32 = NULL, .tof64 = NULL,
		.tom64 = _libelf_cvt_REL64_tom },
	[ELF_T_RELA] =    { .tof32 = NULL, .tom32 = NULL, .tof64 = NULL,
		.tom64 = _libelf_cvt_RELA64_tom },
	[ELF_T_SHDR] =    { .tof32 = NULL, .tom32 = NULL, .tof64 = NULL,
		.tom64 = _libelf_cvt_SHDR64_tom },
	[ELF_T_SWORD] =   { .tof32 = NULL, .tom32 = NULL, .tof64 = NULL,
		.tom64 = _libelf_cvt_SWORD_tom },
	[ELF_T_SXWORD] =  { .tof32 = NULL, .tom32 = NULL, .tof64 = NULL,
		.tom64 = _libelf_cvt_SXWORD_tom },
	[ELF_T_SYMINFO] = { .tof32 = NULL, .tom32 = NULL, .tof64 = NULL,
		.tom64 = _libelf_cvt_SYMINFO64_tom },
	[ELF_T_SYM] =     { .tof32 = NULL, .tom32 = NULL, .tof64 = NULL,
		.tom64 = _libelf_cvt_SYM64_tom },
	[ELF_T_VDEF] =    { .tof32 = NULL, .tom32 = NULL, .tof64 = NULL,
		.tom64 = _libelf_cvt_VDEF64_tom },
	[ELF_T_VNEED] =   { .tof32 = NULL, .tom32 = NULL, .tof64 = NULL,
		.tom64 = _libelf_cvt_VNEED64_tom },
	[ELF_T_WORD] =    { .tof32 = NULL, .tom32 = NULL, .tof64 = NULL,
		.tom64 = _libelf_cvt_WORD_tom },
	[ELF_T_XWORD] =   { .tof32 = NULL, .tom32 = NULL, .tof64 = NULL,
		.tom64 = _libelf_cvt_XWORD_tom },
	/*]*/
	/*
	 * Types that need hand-coded converters follow.
	 */
	[ELF_T_BYTE] = { .tof32 = NULL, .tom32 = NULL, .tof64 = NULL,
		.tom64 = _libelf_cvt_BYTE_tox },
	[ELF_T_NOTE] = { .tof32 = NULL, .tom32 = NULL, .tof64 = NULL,
		.tom64 = _libelf_cvt_NOTE_tom }
};

static int (*_libelf_get_translator(Elf_Type t, int direction, int elfclass))
 (unsigned char *_dst, size_t dsz, unsigned char *_src, size_t _cnt,
  int _byteswap)
{
	assert(/* elfclass == ELFCLASS32 || */ elfclass == ELFCLASS64);
	assert(/* direction == ELF_TOFILE || */ direction == ELF_TOMEMORY);

	if (t >= ELF_T_NUM ||
	    (/* elfclass != ELFCLASS32 && */ elfclass != ELFCLASS64) ||
	    (/* direction != ELF_TOFILE && */ direction != ELF_TOMEMORY))
		return (NULL);

#if 1
	return cvt[t].tom64;
#else
	return ((elfclass == ELFCLASS32) ?
	    (direction == ELF_TOFILE ? cvt[t].tof32 : cvt[t].tom32) :
	    (direction == ELF_TOFILE ? cvt[t].tof64 : cvt[t].tom64));
#endif
}

/* libelf_ehdr.h */
/*
 * Retrieve counts for sections, phdrs and the section string table index
 * from section header #0 of the ELF object.
 */
static int
_libelf_load_extended(Elf *e, int ec, uint64_t shoff, uint16_t phnum,
    uint16_t strndx)
{
	Elf_Scn *scn;
	size_t fsz;
	int (*xlator)(unsigned char *_d, size_t _dsz, unsigned char *_s,
	    size_t _c, int _swap);
	uint32_t shtype;

	assert(STAILQ_EMPTY(&e->e_u.e_elf.e_scn));

	fsz = _libelf_fsize(ELF_T_SHDR, ec, e->e_version, 1);
	assert(fsz > 0);

	if (e->e_rawsize < shoff + fsz) { /* raw file too small */
		LIBELF_SET_ERROR(HEADER, 0);
		return (0);
	}

	if ((scn = _libelf_allocate_scn(e, (size_t) 0)) == NULL)
		return (0);

	xlator = _libelf_get_translator(ELF_T_SHDR, ELF_TOMEMORY, ec);
	(*xlator)((unsigned char *) &scn->s_shdr, sizeof(scn->s_shdr),
	    (unsigned char *) e->e_rawfile + shoff, (size_t) 1,
	    e->e_byteorder != LIBELF_PRIVATE(byteorder));

#define	GET_SHDR_MEMBER(M) (/* (ec == ELFCLASS32) ? scn->s_shdr.s_shdr32.M :*/ \
		scn->s_shdr.s_shdr64.M)

	if ((shtype = GET_SHDR_MEMBER(sh_type)) != SHT_NULL) {
		LIBELF_SET_ERROR(SECTION, 0);
		return (0);
	}

	e->e_u.e_elf.e_nscn = (size_t) GET_SHDR_MEMBER(sh_size);
	e->e_u.e_elf.e_nphdr = (phnum != PN_XNUM) ? phnum :
	    GET_SHDR_MEMBER(sh_info);
	e->e_u.e_elf.e_strndx = (strndx != SHN_XINDEX) ? strndx :
	    GET_SHDR_MEMBER(sh_link);
#undef	GET_SHDR_MEMBER

	return (1);
}

#define	EHDR_INIT(E,SZ)	 do {						\
		Elf##SZ##_Ehdr *eh = (E);				\
		eh->e_ident[EI_MAG0] = ELFMAG0;				\
		eh->e_ident[EI_MAG1] = ELFMAG1;				\
		eh->e_ident[EI_MAG2] = ELFMAG2;				\
		eh->e_ident[EI_MAG3] = ELFMAG3;				\
		eh->e_ident[EI_CLASS] = ELFCLASS##SZ;			\
		eh->e_ident[EI_DATA]  = ELFDATANONE;			\
		eh->e_ident[EI_VERSION] = LIBELF_PRIVATE(version) & 0xFFU; \
		eh->e_machine = EM_NONE;				\
		eh->e_type    = ELF_K_NONE;				\
		eh->e_version = LIBELF_PRIVATE(version);		\
	} while (0)

static void *
_libelf_ehdr(Elf *e, int ec, int allocate)
{
	void *ehdr;
	size_t fsz, msz;
	uint16_t phnum, shnum, strndx;
	uint64_t shoff;
	int (*xlator)(unsigned char *_d, size_t _dsz, unsigned char *_s,
	    size_t _c, int _swap);

	assert(ec == ELFCLASS32 || ec == ELFCLASS64);

	if (e == NULL || e->e_kind != ELF_K_ELF) {
		LIBELF_SET_ERROR(ARGUMENT, 0);
		return (NULL);
	}

	if (e->e_class != ELFCLASSNONE && e->e_class != ec) {
		LIBELF_SET_ERROR(CLASS, 0);
		return (NULL);
	}

	if (e->e_version != EV_CURRENT) {
		LIBELF_SET_ERROR(VERSION, 0);
		return (NULL);
	}

	if (e->e_class == ELFCLASSNONE)
		e->e_class = ec;

#if 0
	if (ec == ELFCLASS32)
		ehdr = (void *) e->e_u.e_elf.e_ehdr.e_ehdr32;
	else
#endif
		ehdr = (void *) e->e_u.e_elf.e_ehdr.e_ehdr64;

	if (ehdr != NULL)	/* already have a translated ehdr */
		return (ehdr);

	fsz = _libelf_fsize(ELF_T_EHDR, ec, e->e_version, (size_t) 1);
	assert(fsz > 0);

	if (e->e_cmd != ELF_C_WRITE && e->e_rawsize < fsz) {
		LIBELF_SET_ERROR(HEADER, 0);
		return (NULL);
	}

	msz = _libelf_msize(ELF_T_EHDR, ec, EV_CURRENT);

	assert(msz > 0);

	if ((ehdr = calloc((size_t) 1, msz)) == NULL) {
		LIBELF_SET_ERROR(RESOURCE, 0);
		return (NULL);
	}

#if 0
	if (ec == ELFCLASS32) {
		e->e_u.e_elf.e_ehdr.e_ehdr32 = ehdr;
		EHDR_INIT(ehdr,32);
	} else
#endif
	{
		e->e_u.e_elf.e_ehdr.e_ehdr64 = ehdr;
		EHDR_INIT(ehdr,64);
	}

	if (allocate)
		e->e_flags |= ELF_F_DIRTY;

	if (e->e_cmd == ELF_C_WRITE)
		return (ehdr);

	xlator = _libelf_get_translator(ELF_T_EHDR, ELF_TOMEMORY, ec);
	(*xlator)((unsigned char*) ehdr, msz, e->e_rawfile, (size_t) 1,
	    e->e_byteorder != LIBELF_PRIVATE(byteorder));

	/*
	 * If extended numbering is being used, read the correct
	 * number of sections and program header entries.
	 */
#if 0
	if (ec == ELFCLASS32) {
		phnum = ((Elf32_Ehdr *) ehdr)->e_phnum;
		shnum = ((Elf32_Ehdr *) ehdr)->e_shnum;
		shoff = ((Elf32_Ehdr *) ehdr)->e_shoff;
		strndx = ((Elf32_Ehdr *) ehdr)->e_shstrndx;
	} else
#endif
	{
		phnum = ((Elf64_Ehdr *) ehdr)->e_phnum;
		shnum = ((Elf64_Ehdr *) ehdr)->e_shnum;
		shoff = ((Elf64_Ehdr *) ehdr)->e_shoff;
		strndx = ((Elf64_Ehdr *) ehdr)->e_shstrndx;
	}

	if (shnum >= SHN_LORESERVE ||
	    (shoff == 0LL && (shnum != 0 || phnum == PN_XNUM ||
		strndx == SHN_XINDEX))) {
		LIBELF_SET_ERROR(HEADER, 0);
		return (NULL);
	}

	if (shnum != 0 || shoff == 0LL) { /* not using extended numbering */
		e->e_u.e_elf.e_nphdr = phnum;
		e->e_u.e_elf.e_nscn = shnum;
		e->e_u.e_elf.e_strndx = strndx;
	} else if (_libelf_load_extended(e, ec, shoff, phnum, strndx) == 0)
		return (NULL);

	return (ehdr);
}

/* libelf_shdr.c */
static void *
_libelf_getshdr(Elf_Scn *s, int ec)
{
	Elf *e;

	if (s == NULL || (e = s->s_elf) == NULL ||
	    e->e_kind != ELF_K_ELF) {
		LIBELF_SET_ERROR(ARGUMENT, 0);
		return (NULL);
	}

	if (ec == ELFCLASSNONE)
		ec = e->e_class;

	if (ec != e->e_class) {
		LIBELF_SET_ERROR(CLASS, 0);
		return (NULL);
	}

	return ((void *) &s->s_shdr);
}

/* elf_scn.c */
static int
_libelf_load_section_headers(Elf *e, void *ehdr)
{
	Elf_Scn *scn;
	uint64_t shoff;
#if 0
	Elf32_Ehdr *eh32;
#endif
	Elf64_Ehdr *eh64;
	int ec, swapbytes;
	unsigned char *src;
	size_t fsz, i, shnum;
	int (*xlator)(unsigned char *_d, size_t _dsz, unsigned char *_s,
	    size_t _c, int _swap);

	assert(e != NULL);
	assert(ehdr != NULL);
	assert((e->e_flags & LIBELF_F_SHDRS_LOADED) == 0);

#define	CHECK_EHDR(E,EH)	do {				\
		if (shoff > e->e_rawsize ||			\
		    fsz != (EH)->e_shentsize ||			\
		    shnum > SIZE_MAX / fsz ||			\
		    fsz * shnum > e->e_rawsize - shoff) {	\
			LIBELF_SET_ERROR(HEADER, 0);		\
			return (0);				\
		}						\
	} while (0)

	ec = e->e_class;
	fsz = _libelf_fsize(ELF_T_SHDR, ec, e->e_version, (size_t) 1);
	assert(fsz > 0);

	shnum = e->e_u.e_elf.e_nscn;

#if 0
	if (ec == ELFCLASS32) {
		eh32 = (Elf32_Ehdr *) ehdr;
		shoff = (uint64_t) eh32->e_shoff;
		CHECK_EHDR(e, eh32);
	} else
#endif
	{
		eh64 = (Elf64_Ehdr *) ehdr;
		shoff = eh64->e_shoff;
		CHECK_EHDR(e, eh64);
	}

	xlator = _libelf_get_translator(ELF_T_SHDR, ELF_TOMEMORY, ec);

	swapbytes = e->e_byteorder != LIBELF_PRIVATE(byteorder);
	src = e->e_rawfile + shoff;

	/*
	 * If the file is using extended numbering then section #0
	 * would have already been read in.
	 */

	i = 0;
	if (!STAILQ_EMPTY(&e->e_u.e_elf.e_scn)) {
		assert(STAILQ_FIRST(&e->e_u.e_elf.e_scn) ==
		    STAILQ_LAST(&e->e_u.e_elf.e_scn, _Elf_Scn, s_next));

		i = 1;
		src += fsz;
	}

	for (; i < shnum; i++, src += fsz) {
		if ((scn = _libelf_allocate_scn(e, i)) == NULL)
			return (0);

		(*xlator)((unsigned char *) &scn->s_shdr, sizeof(scn->s_shdr),
		    src, (size_t) 1, swapbytes);

#if 0
		if (ec == ELFCLASS32) {
			scn->s_offset = scn->s_rawoff =
			    scn->s_shdr.s_shdr32.sh_offset;
			scn->s_size = scn->s_shdr.s_shdr32.sh_size;
		} else
#endif
		{
			scn->s_offset = scn->s_rawoff =
			    scn->s_shdr.s_shdr64.sh_offset;
			scn->s_size = scn->s_shdr.s_shdr64.sh_size;
		}
	}

	e->e_flags |= LIBELF_F_SHDRS_LOADED;

	return (1);
}

static Elf_Scn *
elf_getscn(Elf *e, size_t index)
{
	int ec;
	void *ehdr;
	Elf_Scn *s;

	if (e == NULL || e->e_kind != ELF_K_ELF ||
	    ((ec = e->e_class) != ELFCLASS32 && ec != ELFCLASS64)) {
		LIBELF_SET_ERROR(ARGUMENT, 0);
		return (NULL);
	}

	if ((ehdr = _libelf_ehdr(e, ec, 0)) == NULL)
		return (NULL);

	if (e->e_cmd != ELF_C_WRITE &&
	    (e->e_flags & LIBELF_F_SHDRS_LOADED) == 0 &&
	    _libelf_load_section_headers(e, ehdr) == 0)
		return (NULL);

	STAILQ_FOREACH(s, &e->e_u.e_elf.e_scn, s_next)
		if (s->s_ndx == index)
			return (s);

	LIBELF_SET_ERROR(ARGUMENT, 0);
	return (NULL);
}

/* libelf_memory.c */
static Elf *
_libelf_memory(unsigned char *image, size_t sz, int reporterror)
{
	Elf *e;
	int e_class;
	enum Elf_Error error;
	unsigned int e_byteorder, e_version;

	assert(image != NULL);
	assert(sz > 0);

	if ((e = _libelf_allocate_elf()) == NULL)
		return (NULL);

	e->e_cmd = ELF_C_READ;
	e->e_rawfile = image;
	e->e_rawsize = sz;

#undef	LIBELF_IS_ELF
#define	LIBELF_IS_ELF(P) ((P)[EI_MAG0] == ELFMAG0 &&		\
	(P)[EI_MAG1] == ELFMAG1 && (P)[EI_MAG2] == ELFMAG2 &&	\
	(P)[EI_MAG3] == ELFMAG3)

	if (sz > EI_NIDENT && LIBELF_IS_ELF(image)) {
		e_byteorder = image[EI_DATA];
		e_class     = image[EI_CLASS];
		e_version   = image[EI_VERSION];

		error = ELF_E_NONE;

		if (e_version > EV_CURRENT)
			error = ELF_E_VERSION;
		else if ((e_byteorder != ELFDATA2LSB && e_byteorder !=
		    ELFDATA2MSB) || (e_class != ELFCLASS32 && e_class !=
		    ELFCLASS64))
			error = ELF_E_HEADER;

		if (error != ELF_E_NONE) {
			if (reporterror) {
				LIBELF_PRIVATE(error) = LIBELF_ERROR(error, 0);
				(void) _libelf_release_elf(e);
				return (NULL);
			}
		} else {
			_libelf_init_elf(e, ELF_K_ELF);

			e->e_byteorder = e_byteorder;
			e->e_class = e_class;
			e->e_version = e_version;
		}
	}
#if 0
	else if (sz >= SARMAG &&
	    strncmp((const char *) image, ARMAG, (size_t) SARMAG) == 0)
		return (_libelf_ar_open(e, reporterror));
#endif

	return (e);
}

/* libelf_open.c */
#define	_LIBELF_INITSIZE	(64*1024)

static void *
_libelf_read_special_file(int fd, size_t *fsz)
{
	ssize_t readsz;
	size_t bufsz, datasz;
	unsigned char *buf, *t;

	datasz = 0;
	readsz = 0;
	bufsz = _LIBELF_INITSIZE;
	if ((buf = malloc(bufsz)) == NULL)
		goto resourceerror;

	/*
	 * Read data from the file descriptor till we reach EOF, or
	 * till an error is encountered.
	 */
	do {
		/* Check if we need to expand the data buffer. */
		if (datasz == bufsz) {
			bufsz *= 2;
			if ((t = realloc(buf, bufsz)) == NULL)
				goto resourceerror;
			buf = t;
		}

		do {
			assert(bufsz - datasz > 0);
			t = buf + datasz;
			if ((readsz = read(fd, t, bufsz - datasz)) <= 0)
				break;
			datasz += (size_t) readsz;
		} while (datasz < bufsz);

	} while (readsz > 0);

	if (readsz < 0) {
		LIBELF_SET_ERROR(IO, errno);
		goto error;
	}

	assert(readsz == 0);

	/*
	 * Free up extra buffer space.
	 */
	if (bufsz > datasz) {
		if (datasz > 0) {
			if ((t = realloc(buf, datasz)) == NULL)
				goto resourceerror;
			buf = t;
		} else {	/* Zero bytes read. */
			LIBELF_SET_ERROR(ARGUMENT, 0);
			free(buf);
			buf = NULL;
		}
	}

	*fsz = datasz;
	return (buf);

resourceerror:
	LIBELF_SET_ERROR(RESOURCE, 0);
error:
	if (buf != NULL)
		free(buf);
	return (NULL);
}

static Elf *
_libelf_open_object(int fd, Elf_Cmd c, int reporterror)
{
	Elf *e;
	void *m;
	mode_t mode;
	size_t fsize;
	struct stat sb;
	unsigned int flags;

	assert(c == ELF_C_READ || c == ELF_C_RDWR || c == ELF_C_WRITE);

	if (fstat(fd, &sb) < 0) {
		LIBELF_SET_ERROR(IO, errno);
		return (NULL);
	}

	mode = sb.st_mode;
	fsize = (size_t) sb.st_size;

	/*
	 * Reject unsupported file types.
	 */
	if (!S_ISREG(mode) && !S_ISCHR(mode) && !S_ISFIFO(mode) &&
	    !S_ISSOCK(mode)) {
		LIBELF_SET_ERROR(ARGUMENT, 0);
		return (NULL);
	}

	/*
	 * For ELF_C_WRITE mode, allocate and return a descriptor.
	 */
	if (c == ELF_C_WRITE) {
		if ((e = _libelf_allocate_elf()) != NULL) {
			_libelf_init_elf(e, ELF_K_ELF);
			e->e_byteorder = LIBELF_PRIVATE(byteorder);
			e->e_fd = fd;
			e->e_cmd = c;
			if (!S_ISREG(mode))
				e->e_flags |= LIBELF_F_SPECIAL_FILE;
		}

		return (e);
	}


	/*
	 * ELF_C_READ and ELF_C_RDWR mode.
	 */
	m = NULL;
	flags = 0;
	if (S_ISREG(mode)) {

		/*
		 * Reject zero length files.
		 */
		if (fsize == 0) {
			LIBELF_SET_ERROR(ARGUMENT, 0);
			return (NULL);
		}

#if	ELFTC_HAVE_MMAP
		/*
		 * Always map regular files in with 'PROT_READ'
		 * permissions.
		 *
		 * For objects opened in ELF_C_RDWR mode, when
		 * elf_update(3) is called, we remove this mapping,
		 * write file data out using write(2), and map the new
		 * contents back.
		 */
		m = mmap(NULL, fsize, PROT_READ, MAP_PRIVATE, fd, (off_t) 0);

		if (m == MAP_FAILED)
			m = NULL;
		else
			flags = LIBELF_F_RAWFILE_MMAP;
#endif

		/*
		 * Fallback to a read() if the call to mmap() failed,
		 * or if mmap() is not available.
		 */
		if (m == NULL) {
			if ((m = malloc(fsize)) == NULL) {
				LIBELF_SET_ERROR(RESOURCE, 0);
				return (NULL);
			}

			if (read(fd, m, fsize) != (ssize_t) fsize) {
				LIBELF_SET_ERROR(IO, errno);
				free(m);
				return (NULL);
			}

			flags = LIBELF_F_RAWFILE_MALLOC;
		}
	} else if ((m = _libelf_read_special_file(fd, &fsize)) != NULL)
		flags = LIBELF_F_RAWFILE_MALLOC | LIBELF_F_SPECIAL_FILE;
	else
		return (NULL);

	if ((e = _libelf_memory(m, fsize, reporterror)) == NULL) {
		assert((flags & LIBELF_F_RAWFILE_MALLOC) ||
		    (flags & LIBELF_F_RAWFILE_MMAP));
		if (flags & LIBELF_F_RAWFILE_MALLOC)
			free(m);
#if	ELFTC_HAVE_MMAP
		else
			(void) munmap(m, fsize);
#endif
		return (NULL);
	}

	/* ar(1) archives aren't supported in RDWR mode. */
#if 0
	if (c == ELF_C_RDWR && e->e_kind == ELF_K_AR) {
		(void) elf_end(e);
		LIBELF_SET_ERROR(ARGUMENT, 0);
		return (NULL);
	}
#endif

	e->e_flags |= flags;
	e->e_fd = fd;
	e->e_cmd = c;

	return (e);
}

static const char *_libelf_errors[] = {
#define	DEFINE_ERROR(N,S)	[ELF_E_##N] = S
	DEFINE_ERROR(NONE,	"No Error"),
	DEFINE_ERROR(ARCHIVE,	"Malformed ar(1) archive"),
	DEFINE_ERROR(ARGUMENT,	"Invalid argument"),
	DEFINE_ERROR(CLASS,	"ELF class mismatch"),
	DEFINE_ERROR(DATA,	"Invalid data buffer descriptor"),
	DEFINE_ERROR(HEADER,	"Missing or malformed ELF header"),
	DEFINE_ERROR(IO,	"I/O error"),
	DEFINE_ERROR(LAYOUT,	"Layout constraint violation"),
	DEFINE_ERROR(MODE,	"Incorrect ELF descriptor mode"),
	DEFINE_ERROR(RANGE,	"Value out of range of target"),
	DEFINE_ERROR(RESOURCE,	"Resource exhaustion"),
	DEFINE_ERROR(SECTION,	"Invalid section descriptor"),
	DEFINE_ERROR(SEQUENCE,	"API calls out of sequence"),
	DEFINE_ERROR(UNIMPL,	"Unimplemented feature"),
	DEFINE_ERROR(VERSION,	"Unknown ELF API version"),
	DEFINE_ERROR(NUM,	"Unknown error")
#undef	DEFINE_ERROR
};

/* PUBLIC */

/* elf_errmsg.c */
static const char *
elf_errmsg(int error)
{
	int oserr;

	if (error == ELF_E_NONE &&
	    (error = LIBELF_PRIVATE(error)) == 0)
	    return NULL;
	else if (error == -1)
	    error = LIBELF_PRIVATE(error);

	oserr = error >> LIBELF_OS_ERROR_SHIFT;
	error &= LIBELF_ELF_ERROR_MASK;

	if (error < ELF_E_NONE || error >= ELF_E_NUM)
		return _libelf_errors[ELF_E_NUM];
	if (oserr) {
		(void) snprintf((char *) LIBELF_PRIVATE(msg),
		    sizeof(LIBELF_PRIVATE(msg)), "%s: %s",
		    _libelf_errors[error], strerror(oserr));
		return (const char *)&LIBELF_PRIVATE(msg);
	}
	return _libelf_errors[error];
}

/* elf_errno.c */
static int
elf_errno(void)
{
	int old;

	old = LIBELF_PRIVATE(error);
	LIBELF_PRIVATE(error) = 0;
	return (old & LIBELF_ELF_ERROR_MASK);
}

/* elf_version.c */
static unsigned int
elf_version(unsigned int v)
{
	unsigned int old;

	if ((old = LIBELF_PRIVATE(version)) == EV_NONE)
		old = EV_CURRENT;

	if (v == EV_NONE)
		return old;
	if (v > EV_CURRENT) {
		LIBELF_SET_ERROR(VERSION, 0);
		return EV_NONE;
	}

	LIBELF_PRIVATE(version) = v;
	return (old);
}

/* elf_begin.c */
static Elf *
elf_begin(int fd, Elf_Cmd c, Elf *a)
{
	Elf *e;

	e = NULL;

	if (LIBELF_PRIVATE(version) == EV_NONE) {
		LIBELF_SET_ERROR(SEQUENCE, 0);
		return (NULL);
	}

	switch (c) {
	case ELF_C_NULL:
		return (NULL);

	case ELF_C_WRITE:
		/*
		 * The ELF_C_WRITE command is required to ignore the
		 * descriptor passed in.
		 */
		a = NULL;
		break;

	case ELF_C_RDWR:
		if (a != NULL) { /* not allowed for ar(1) archives. */
			LIBELF_SET_ERROR(ARGUMENT, 0);
			return (NULL);
		}
		/*FALLTHROUGH*/
	case ELF_C_READ:
		/*
		 * Descriptor `a' could be for a regular ELF file, or
		 * for an ar(1) archive.  If descriptor `a' was opened
		 * using a valid file descriptor, we need to check if
		 * the passed in `fd' value matches the original one.
		 */
		if (a &&
		    ((a->e_fd != -1 && a->e_fd != fd) || c != a->e_cmd)) {
			LIBELF_SET_ERROR(ARGUMENT, 0);
			return (NULL);
		}
		break;

	default:
		LIBELF_SET_ERROR(ARGUMENT, 0);
		return (NULL);

	}

	if (a == NULL)
		e = _libelf_open_object(fd, c, 1);
#if 0
	else if (a->e_kind == ELF_K_AR)
		e = _libelf_ar_open_member(a->e_fd, c, a);
#endif
	else
		(e = a)->e_activations++;

	return (e);
}

/* elf_end.c */
static int
elf_end(Elf *e)
{
	Elf *sv;
	Elf_Scn *scn, *tscn;

	if (e == NULL || e->e_activations == 0)
		return (0);

	if (--e->e_activations > 0)
		return (e->e_activations);

	assert(e->e_activations == 0);

	while (e && e->e_activations == 0) {
		switch (e->e_kind) {
		case ELF_K_AR:
			/*
			 * If we still have open child descriptors, we
			 * need to defer reclaiming resources till all
			 * the child descriptors for the archive are
			 * closed.
			 */
			if (e->e_u.e_ar.e_nchildren > 0)
				return (0);
			break;
		case ELF_K_ELF:
			/*
			 * Reclaim all section descriptors.
			 */
			STAILQ_FOREACH_SAFE(scn, &e->e_u.e_elf.e_scn, s_next,
			    tscn)
				scn = _libelf_release_scn(scn);
			break;
		case ELF_K_NUM:
			assert(0);
		default:
			break;
		}

		if (e->e_rawfile) {
			if (e->e_flags & LIBELF_F_RAWFILE_MALLOC)
				free(e->e_rawfile);
#if	ELFTC_HAVE_MMAP
			else if (e->e_flags & LIBELF_F_RAWFILE_MMAP)
				(void) munmap(e->e_rawfile, e->e_rawsize);
#endif
		}

		sv = e;
		if ((e = e->e_parent) != NULL)
			e->e_u.e_ar.e_nchildren--;
		sv = _libelf_release_elf(sv);
	}

	return (0);
}

/* gelf_shdr.c */
static GElf_Shdr *
gelf_getshdr(Elf_Scn *s, GElf_Shdr *d)
{
	int ec;
	void *sh;
#if 0
	Elf32_Shdr *sh32;
#endif
	Elf64_Shdr *sh64;

	if (d == NULL) {
		LIBELF_SET_ERROR(ARGUMENT, 0);
		return (NULL);
	}

	if ((sh = _libelf_getshdr(s, ELFCLASSNONE)) == NULL)
		return (NULL);

	ec = s->s_elf->e_class;
	assert(/* ec == ELFCLASS32 || */ ec == ELFCLASS64);

#if 0
	if (ec == ELFCLASS32) {
		sh32 = (Elf32_Shdr *) sh;

		d->sh_name      = sh32->sh_name;
		d->sh_type      = sh32->sh_type;
		d->sh_flags     = (Elf64_Xword) sh32->sh_flags;
		d->sh_addr      = (Elf64_Addr) sh32->sh_addr;
		d->sh_offset    = (Elf64_Off) sh32->sh_offset;
		d->sh_size      = (Elf64_Xword) sh32->sh_size;
		d->sh_link      = sh32->sh_link;
		d->sh_info      = sh32->sh_info;
		d->sh_addralign = (Elf64_Xword) sh32->sh_addralign;
		d->sh_entsize   = (Elf64_Xword) sh32->sh_entsize;
	} else
#endif
	{
		sh64 = (Elf64_Shdr *) sh;
		*d = *sh64;
	}

	return (d);
}

/* gelf_sym.c */
static GElf_Sym *
gelf_getsym(Elf_Data *ed, int ndx, GElf_Sym *dst)
{
	int ec;
	Elf *e;
	size_t msz;
	Elf_Scn *scn;
	uint32_t sh_type;
#if 0
	Elf32_Sym *sym32;
#endif
	Elf64_Sym *sym64;
	struct _Libelf_Data *d;

	d = (struct _Libelf_Data *) ed;

	if (d == NULL || ndx < 0 || dst == NULL ||
	    (scn = d->d_scn) == NULL ||
	    (e = scn->s_elf) == NULL) {
		LIBELF_SET_ERROR(ARGUMENT, 0);
		return (NULL);
	}

	ec = e->e_class;
	assert(/* ec == ELFCLASS32 || */ ec == ELFCLASS64);

#if 0
	if (ec == ELFCLASS32)
		sh_type = scn->s_shdr.s_shdr32.sh_type;
	else
#endif
		sh_type = scn->s_shdr.s_shdr64.sh_type;

	if (_libelf_xlate_shtype(sh_type) != ELF_T_SYM) {
		LIBELF_SET_ERROR(ARGUMENT, 0);
		return (NULL);
	}

	msz = _libelf_msize(ELF_T_SYM, ec, e->e_version);

	assert(msz > 0);
	assert(ndx >= 0);

	if (msz * (size_t) ndx >= d->d_data.d_size) {
		LIBELF_SET_ERROR(ARGUMENT, 0);
		return (NULL);
	}

#if 0
	if (ec == ELFCLASS32) {
		sym32 = (Elf32_Sym *) d->d_data.d_buf + ndx;

		dst->st_name  = sym32->st_name;
		dst->st_value = (Elf64_Addr) sym32->st_value;
		dst->st_size  = (Elf64_Xword) sym32->st_size;
		dst->st_info  = sym32->st_info;
		dst->st_other = sym32->st_other;
		dst->st_shndx = sym32->st_shndx;
	} else
#endif
	{
		sym64 = (Elf64_Sym *) d->d_data.d_buf + ndx;

		*dst = *sym64;
	}

	return (dst);
}

/* elf_scn.c */
static Elf_Scn *
elf_nextscn(Elf *e, Elf_Scn *s)
{
	if (e == NULL || (e->e_kind != ELF_K_ELF) ||
	    (s && s->s_elf != e)) {
		LIBELF_SET_ERROR(ARGUMENT, 0);
		return (NULL);
	}

	return (s == NULL ? elf_getscn(e, (size_t) 1) :
	    STAILQ_NEXT(s, s_next));
}

/* elf_strptr.c */
static char *
elf_strptr(Elf *e, size_t scndx, size_t offset)
{
	Elf_Scn *s;
	Elf_Data *d;
	GElf_Shdr shdr;
	uint64_t alignment, count;

	if (e == NULL || e->e_kind != ELF_K_ELF) {
		LIBELF_SET_ERROR(ARGUMENT, 0);
		return (NULL);
	}

	if ((s = elf_getscn(e, scndx)) == NULL ||
	    gelf_getshdr(s, &shdr) == NULL)
		return (NULL);

	if (shdr.sh_type != SHT_STRTAB ||
	    offset >= shdr.sh_size) {
		LIBELF_SET_ERROR(ARGUMENT, 0);
		return (NULL);
	}

	d = NULL;
	if (e->e_flags & ELF_F_LAYOUT) {

		/*
		 * The application is taking responsibility for the
		 * ELF object's layout, so we can directly translate
		 * an offset to a `char *' address using the `d_off'
		 * members of Elf_Data descriptors.
		 */
		while ((d = elf_getdata(s, d)) != NULL) {

			if (d->d_buf == 0 || d->d_size == 0)
				continue;

			if (d->d_type != ELF_T_BYTE) {
				LIBELF_SET_ERROR(DATA, 0);
				return (NULL);
			}

			if (offset >= d->d_off &&
			    offset < d->d_off + d->d_size)
				return ((char *) d->d_buf + offset - d->d_off);
		}
	} else {
		/*
		 * Otherwise, the `d_off' members are not useable and
		 * we need to compute offsets ourselves, taking into
		 * account 'holes' in coverage of the section introduced
		 * by alignment requirements.
		 */
		count = (uint64_t) 0;	/* cumulative count of bytes seen */
		while ((d = elf_getdata(s, d)) != NULL && count <= offset) {

			if (d->d_buf == NULL || d->d_size == 0)
				continue;

			if (d->d_type != ELF_T_BYTE) {
				LIBELF_SET_ERROR(DATA, 0);
				return (NULL);
			}

			if ((alignment = d->d_align) > 1) {
				if ((alignment & (alignment - 1)) != 0) {
					LIBELF_SET_ERROR(DATA, 0);
					return (NULL);
				}
				count = roundup2(count, alignment);
			}

			if (offset < count) {
				/* offset starts in the 'hole' */
				LIBELF_SET_ERROR(ARGUMENT, 0);
				return (NULL);
			}

			if (offset < count + d->d_size) {
				if (d->d_buf != NULL)
					return ((char *) d->d_buf +
					    offset - count);
				LIBELF_SET_ERROR(DATA, 0);
				return (NULL);
			}

			count += d->d_size;
		}
	}

	LIBELF_SET_ERROR(ARGUMENT, 0);
	return (NULL);
}

/* elf_data.c */
static Elf_Data *
elf_getdata(Elf_Scn *s, Elf_Data *ed)
{
	Elf *e;
	unsigned int sh_type;
	int elfclass, elftype;
	size_t count, fsz, msz;
	struct _Libelf_Data *d;
	uint64_t sh_align, sh_offset, sh_size;
	int (*xlate)(unsigned char *_d, size_t _dsz, unsigned char *_s,
	    size_t _c, int _swap);

	d = (struct _Libelf_Data *) ed;

	if (s == NULL || (e = s->s_elf) == NULL ||
	    (d != NULL && s != d->d_scn)) {
		LIBELF_SET_ERROR(ARGUMENT, 0);
		return (NULL);
	}

	assert(e->e_kind == ELF_K_ELF);

	if (d == NULL && (d = STAILQ_FIRST(&s->s_data)) != NULL)
		return (&d->d_data);

	if (d != NULL)
		return (&STAILQ_NEXT(d, d_next)->d_data);

	if (e->e_rawfile == NULL) {
		/*
		 * In the ELF_C_WRITE case, there is no source that
		 * can provide data for the section.
		 */
		LIBELF_SET_ERROR(ARGUMENT, 0);
		return (NULL);
	}

	elfclass = e->e_class;

	assert(/* elfclass == ELFCLASS32 || */ elfclass == ELFCLASS64);

#if 0
	if (elfclass == ELFCLASS32) {
		sh_type   = s->s_shdr.s_shdr32.sh_type;
		sh_offset = (uint64_t) s->s_shdr.s_shdr32.sh_offset;
		sh_size   = (uint64_t) s->s_shdr.s_shdr32.sh_size;
		sh_align  = (uint64_t) s->s_shdr.s_shdr32.sh_addralign;
	} else
#endif
	{
		sh_type   = s->s_shdr.s_shdr64.sh_type;
		sh_offset = s->s_shdr.s_shdr64.sh_offset;
		sh_size   = s->s_shdr.s_shdr64.sh_size;
		sh_align  = s->s_shdr.s_shdr64.sh_addralign;
	}

	if (sh_type == SHT_NULL) {
		LIBELF_SET_ERROR(SECTION, 0);
		return (NULL);
	}

	if ((elftype = _libelf_xlate_shtype(sh_type)) < ELF_T_FIRST ||
	    elftype > ELF_T_LAST || (sh_type != SHT_NOBITS &&
	    sh_offset + sh_size > (uint64_t) e->e_rawsize)) {
		LIBELF_SET_ERROR(SECTION, 0);
		return (NULL);
	}

	if ((fsz = (/* elfclass == ELFCLASS32 ? elf32_fsize :*/ elf64_fsize)
            (elftype, (size_t) 1, e->e_version)) == 0) {
		LIBELF_SET_ERROR(UNIMPL, 0);
		return (NULL);
	}

	if (sh_size % fsz) {
		LIBELF_SET_ERROR(SECTION, 0);
		return (NULL);
	}

	if (sh_size / fsz > SIZE_MAX) {
		LIBELF_SET_ERROR(RANGE, 0);
		return (NULL);
	}

	count = (size_t) (sh_size / fsz);

	msz = _libelf_msize(elftype, elfclass, e->e_version);

	if (count > 0 && msz > SIZE_MAX / count) {
		LIBELF_SET_ERROR(RANGE, 0);
		return (NULL);
	}

	assert(msz > 0);
	assert(count <= SIZE_MAX);
	assert(msz * count <= SIZE_MAX);

	if ((d = _libelf_allocate_data(s)) == NULL)
		return (NULL);

	d->d_data.d_buf     = NULL;
	d->d_data.d_off     = 0;
	d->d_data.d_align   = sh_align;
	d->d_data.d_size    = msz * count;
	d->d_data.d_type    = elftype;
	d->d_data.d_version = e->e_version;

	if (sh_type == SHT_NOBITS || sh_size == 0) {
	        STAILQ_INSERT_TAIL(&s->s_data, d, d_next);
		return (&d->d_data);
        }

	if ((d->d_data.d_buf = malloc(msz * count)) == NULL) {
		(void) _libelf_release_data(d);
		LIBELF_SET_ERROR(RESOURCE, 0);
		return (NULL);
	}

	d->d_flags  |= LIBELF_F_DATA_MALLOCED;

	xlate = _libelf_get_translator(elftype, ELF_TOMEMORY, elfclass);
	if (!(*xlate)(d->d_data.d_buf, (size_t) d->d_data.d_size,
	    e->e_rawfile + sh_offset, count,
	    e->e_byteorder != LIBELF_PRIVATE(byteorder))) {
		_libelf_release_data(d);
		LIBELF_SET_ERROR(DATA, 0);
		return (NULL);
	}

	STAILQ_INSERT_TAIL(&s->s_data, d, d_next);

	return (&d->d_data);
}

#endif /* !_PRIVATE_LIBELF_H_ */
