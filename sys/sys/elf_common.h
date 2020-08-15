/*-
 * Copyright (c) 1998 John D. Polstra.
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

#ifndef _SYS_ELF_COMMON_H_
#define _SYS_ELF_COMMON_H_

#ifndef _SYS_TYPES_H_
#include <sys/types.h>
#endif

/*
 * ELF definitions that are independent of architecture or word size.
 */

/*
 * Note header.  The ".note" section contains an array of notes.  Each
 * begins with this header, aligned to a word boundary.  Immediately
 * following the note header is n_namesz bytes of name, padded to the
 * next word boundary.  Then comes n_descsz bytes of descriptor, again
 * padded to a word boundary.  The values of n_namesz and n_descsz do
 * not include the padding.
 */

typedef struct {
	u_int32_t	n_namesz;	/* Length of name. */
	u_int32_t	n_descsz;	/* Length of descriptor. */
	u_int32_t	n_type;		/* Type of this note. */
} Elf_Note;
typedef Elf_Note Elf_Nhdr;

/* Indexes into the e_ident array.  Keep synced with
   http://www.sco.com/developers/gabi/latest/ch4.eheader.html */
#define EI_MAG0		0	/* Magic number, byte 0. */
#define EI_MAG1		1	/* Magic number, byte 1. */
#define EI_MAG2		2	/* Magic number, byte 2. */
#define EI_MAG3		3	/* Magic number, byte 3. */
#define EI_CLASS	4	/* Class of machine. */
#define EI_DATA		5	/* Data format. */
#define EI_VERSION	6	/* ELF format version. */
#define EI_OSABI	7	/* Operating system / ABI identification */
#define EI_ABIVERSION	8	/* ABI version */
#define EI_PAD		9	/* Start of padding (per SVR4 ABI). */
#define EI_NIDENT	16	/* Size of e_ident array. */

/* Values for the magic number bytes. */
#define ELFMAG0		0x7f
#define ELFMAG1		'E'
#define ELFMAG2		'L'
#define ELFMAG3		'F'
#define ELFMAG		"\177ELF"	/* magic string */
#define SELFMAG		4		/* magic string size */

/* Values for e_ident[EI_VERSION] and e_version. */
#define EV_NONE		0
#define EV_CURRENT	1

/* Values for e_ident[EI_CLASS]. */
#define ELFCLASSNONE	0	/* Unknown class. */
#define ELFCLASS32	1	/* 32-bit architecture. */
#define ELFCLASS64	2	/* 64-bit architecture. */

/* Values for e_ident[EI_DATA]. */
#define ELFDATANONE	0	/* Unknown data format. */
#define ELFDATA2LSB	1	/* 2's complement little-endian. */
#define ELFDATA2MSB	2	/* 2's complement big-endian. */

/* Values for e_ident[EI_OSABI]. */
#define ELFOSABI_SYSV		0	/* symbol used in old spec */
#define ELFOSABI_NONE		0	/* UNIX System V ABI */
#define ELFOSABI_HPUX		1	/* HP-UX operating system */
#define ELFOSABI_NETBSD 	2	/* NetBSD */
#define ELFOSABI_GNU		3	/* GNU */
#define ELFOSABI_LINUX		3	/* Alias for ELFOSABI_GNU */
#define ELFOSABI_SOLARIS	6	/* Solaris */
#define ELFOSABI_AIX		7	/* AIX */
#define ELFOSABI_IRIX		8	/* IRIX */
#define ELFOSABI_FREEBSD	9	/* FreeBSD */
#define ELFOSABI_TRU64		10	/* TRU64 UNIX */
#define ELFOSABI_MODESTO	11	/* Novell Modesto */
#define ELFOSABI_OPENBSD	12	/* OpenBSD */
#define ELFOSABI_OPENVMS	13	/* OpenVMS */
#define ELFOSABI_NSK		14	/* Hewlett-Packard Non-Stop Kernel */
#define ELFOSABI_AROS		15	/* AROS */
#define ELFOSABI_FENIXOS	16	/* FenixOS */
#define ELFOSABI_C6000_ELFABI	64	/* Bare-metal TMS320C6000 */
#define ELFOSABI_C6000_LINUX	65	/* Linux TMS320C6000 */
#define ELFOSABI_ARM		97	/* ARM */
#define ELFOSABI_STANDALONE	255	/* Standalone (embedded) application */

/* e_ident */
#define IS_ELF(ehdr)	((ehdr).e_ident[EI_MAG0] == ELFMAG0 && \
			 (ehdr).e_ident[EI_MAG1] == ELFMAG1 && \
			 (ehdr).e_ident[EI_MAG2] == ELFMAG2 && \
			 (ehdr).e_ident[EI_MAG3] == ELFMAG3)

/* Values for e_type, which identifies the object file type.  */

#define ET_NONE 	0	/* No file type */
#define ET_REL		1	/* Relocatable file */
#define ET_EXEC 	2	/* Executable file */
#define ET_DYN		3	/* Shared object file */
#define ET_CORE 	4	/* Core file */
#define ET_LOOS 	0xFE00	/* Operating system-specific */
#define ET_HIOS 	0xFEFF	/* Operating system-specific */
#define ET_LOPROC	0xFF00	/* Processor-specific */
#define ET_HIPROC	0xFFFF	/* Processor-specific */

/* Values for e_machine, which identifies the architecture.  These numbers
   are officially assigned by registry@sco.com.  See below for a list of
   ad-hoc numbers used during initial development.  */

#define EM_NONE 	  0	/* No machine */
#define EM_M32		  1	/* AT&T WE 32100 */
#define EM_SPARC	  2	/* SUN SPARC */
#define EM_386		  3	/* Intel 80386 */
#define EM_68K		  4	/* Motorola m68k family */
#define EM_88K		  5	/* Motorola m88k family */
#define EM_486		  6	/* Intel 80486 *//* Reserved for future use */
#define EM_860		  7	/* Intel 80860 */
#define EM_MIPS 	  8	/* MIPS R3000 (officially, big-endian only) */
#define EM_S370 	  9	/* IBM System/370 */
#define EM_MIPS_RS3_LE	 10	/* MIPS R3000 little-endian (Oct 4 1999 Draft) Deprecated */
#define EM_res011	 11	/* Reserved */
#define EM_res012	 12	/* Reserved */
#define EM_res013	 13	/* Reserved */
#define EM_res014	 14	/* Reserved */
#define EM_PARISC	 15	/* HPPA */
#define EM_res016	 16	/* Reserved */
#define EM_VPP550	 17	/* Fujitsu VPP500 */
#define EM_SPARC32PLUS	 18	/* Sun's "v8plus" */
#define EM_960		 19	/* Intel 80960 */
#define EM_PPC		 20	/* PowerPC */
#define EM_PPC64	 21	/* 64-bit PowerPC */
#define EM_S390 	 22	/* IBM S/390 */
#define EM_SPU		 23	/* Sony/Toshiba/IBM SPU */
#define EM_res024	 24	/* Reserved */
#define EM_res025	 25	/* Reserved */
#define EM_res026	 26	/* Reserved */
#define EM_res027	 27	/* Reserved */
#define EM_res028	 28	/* Reserved */
#define EM_res029	 29	/* Reserved */
#define EM_res030	 30	/* Reserved */
#define EM_res031	 31	/* Reserved */
#define EM_res032	 32	/* Reserved */
#define EM_res033	 33	/* Reserved */
#define EM_res034	 34	/* Reserved */
#define EM_res035	 35	/* Reserved */
#define EM_V800 	 36	/* NEC V800 series */
#define EM_FR20 	 37	/* Fujitsu FR20 */
#define EM_RH32 	 38	/* TRW RH32 */
#define EM_MCORE	 39	/* Motorola M*Core */ /* May also be taken by Fujitsu MMA */
#define EM_RCE		 39	/* Old name for MCore */
#define EM_ARM		 40	/* ARM */
#define EM_OLD_ALPHA	 41	/* Digital Alpha */
#define EM_SH		 42	/* Renesas (formerly Hitachi) / SuperH SH */
#define EM_SPARCV9	 43	/* SPARC v9 64-bit */
#define EM_TRICORE	 44	/* Siemens Tricore embedded processor */
#define EM_ARC		 45	/* ARC Cores */
#define EM_H8_300	 46	/* Renesas (formerly Hitachi) H8/300 */
#define EM_H8_300H	 47	/* Renesas (formerly Hitachi) H8/300H */
#define EM_H8S		 48	/* Renesas (formerly Hitachi) H8S */
#define EM_H8_500	 49	/* Renesas (formerly Hitachi) H8/500 */
#define EM_IA_64	 50	/* Intel IA-64 Processor */
#define EM_MIPS_X	 51	/* Stanford MIPS-X */
#define EM_COLDFIRE	 52	/* Motorola Coldfire */
#define EM_68HC12	 53	/* Motorola M68HC12 */
#define EM_MMA		 54	/* Fujitsu Multimedia Accelerator */
#define EM_PCP		 55	/* Siemens PCP */
#define EM_NCPU 	 56	/* Sony nCPU embedded RISC processor */
#define EM_NDR1 	 57	/* Denso NDR1 microprocessor */
#define EM_STARCORE	 58	/* Motorola Star*Core processor */
#define EM_ME16 	 59	/* Toyota ME16 processor */
#define EM_ST100	 60	/* STMicroelectronics ST100 processor */
#define EM_TINYJ	 61	/* Advanced Logic Corp. TinyJ embedded processor */
#define EM_X86_64	 62	/* Advanced Micro Devices X86-64 processor */
#define EM_AMD64	 62	/* Advanced Micro Devices X86-64 (compat) */
#define EM_PDSP 	 63	/* Sony DSP Processor */
#define EM_PDP10	 64	/* Digital Equipment Corp. PDP-10 */
#define EM_PDP11	 65	/* Digital Equipment Corp. PDP-11 */
#define EM_FX66 	 66	/* Siemens FX66 microcontroller */
#define EM_ST9PLUS	 67	/* STMicroelectronics ST9+ 8/16 bit microcontroller */
#define EM_ST7		 68	/* STMicroelectronics ST7 8-bit microcontroller */
#define EM_68HC16	 69	/* Motorola MC68HC16 Microcontroller */
#define EM_68HC11	 70	/* Motorola MC68HC11 Microcontroller */
#define EM_68HC08	 71	/* Motorola MC68HC08 Microcontroller */
#define EM_68HC05	 72	/* Motorola MC68HC05 Microcontroller */
#define EM_SVX		 73	/* Silicon Graphics SVx */
#define EM_ST19 	 74	/* STMicroelectronics ST19 8-bit cpu */
#define EM_VAX		 75	/* Digital VAX */
#define EM_CRIS 	 76	/* Axis Communications 32-bit embedded processor */
#define EM_JAVELIN	 77	/* Infineon Technologies 32-bit embedded cpu */
#define EM_FIREPATH	 78	/* Element 14 64-bit DSP processor */
#define EM_ZSP		 79	/* LSI Logic's 16-bit DSP processor */
#define EM_MMIX 	 80	/* Donald Knuth's educational 64-bit processor */
#define EM_HUANY	 81	/* Harvard's machine-independent format */
#define EM_PRISM	 82	/* SiTera Prism */
#define EM_AVR		 83	/* Atmel AVR 8-bit microcontroller */
#define EM_FR30 	 84	/* Fujitsu FR30 */
#define EM_D10V 	 85	/* Mitsubishi D10V */
#define EM_D30V 	 86	/* Mitsubishi D30V */
#define EM_V850 	 87	/* Renesas V850 (formerly NEC V850) */
#define EM_M32R 	 88	/* Renesas M32R (formerly Mitsubishi M32R) */
#define EM_MN10300	 89	/* Matsushita MN10300 */
#define EM_MN10200	 90	/* Matsushita MN10200 */
#define EM_PJ		 91	/* picoJava */
#define EM_OPENRISC	 92	/* OpenRISC 32-bit embedded processor */
#define EM_ARC_A5	 93	/* ARC Cores Tangent-A5 */
#define EM_XTENSA	 94	/* Tensilica Xtensa Architecture */
#define EM_VIDEOCORE	 95	/* Alphamosaic VideoCore processor */
#define EM_TMM_GPP	 96	/* Thompson Multimedia General Purpose Processor */
#define EM_NS32K	 97	/* National Semiconductor 32000 series */
#define EM_TPC		 98	/* Tenor Network TPC processor */
#define EM_SNP1K	 99	/* Trebia SNP 1000 processor */
#define EM_ST200	100	/* STMicroelectronics ST200 microcontroller */
#define EM_IP2K 	101	/* Ubicom IP2022 micro controller */
#define EM_MAX		102	/* MAX Processor */
#define EM_CR		103	/* National Semiconductor CompactRISC */
#define EM_F2MC16	104	/* Fujitsu F2MC16 */
#define EM_MSP430	105	/* TI msp430 micro controller */
#define EM_BLACKFIN	106	/* ADI Blackfin */
#define EM_SE_C33	107	/* S1C33 Family of Seiko Epson processors */
#define EM_SEP		108	/* Sharp embedded microprocessor */
#define EM_ARCA 	109	/* Arca RISC Microprocessor */
#define EM_UNICORE	110	/* Microprocessor series from PKU-Unity Ltd. and MPRC of Peking University */
#define EM_EXCESS	111	/* eXcess: 16/32/64-bit configurable embedded CPU */
#define EM_DXP		112	/* Icera Semiconductor Inc. Deep Execution Processor */
#define EM_ALTERA_NIOS2	113	/* Altera Nios II soft-core processor */
#define EM_CRX		114	/* National Semiconductor CRX */
#define EM_XGATE	115	/* Motorola XGATE embedded processor */
#define EM_C166 	116	/* Infineon C16x/XC16x processor */
#define EM_M16C 	117	/* Renesas M16C series microprocessors */
#define EM_DSPIC30F	118	/* Microchip Technology dsPIC30F Digital Signal Controller */
#define EM_CE		119	/* Freescale Communication Engine RISC core */
#define EM_M32C 	120	/* Renesas M32C series microprocessors */
#define EM_res121	121	/* Reserved */
#define EM_res122	122	/* Reserved */
#define EM_res123	123	/* Reserved */
#define EM_res124	124	/* Reserved */
#define EM_res125	125	/* Reserved */
#define EM_res126	126	/* Reserved */
#define EM_res127	127	/* Reserved */
#define EM_res128	128	/* Reserved */
#define EM_res129	129	/* Reserved */
#define EM_res130	130	/* Reserved */
#define EM_TSK3000	131	/* Altium TSK3000 core */
#define EM_RS08 	132	/* Freescale RS08 embedded processor */
#define EM_res133	133	/* Reserved */
#define EM_ECOG2	134	/* Cyan Technology eCOG2 microprocessor */
#define EM_SCORE	135	/* Sunplus Score */
#define EM_SCORE7	135	/* Sunplus S+core7 RISC processor */
#define EM_DSP24	136	/* New Japan Radio (NJR) 24-bit DSP Processor */
#define EM_VIDEOCORE3	137	/* Broadcom VideoCore III processor */
#define EM_LATTICEMICO32 138	/* RISC processor for Lattice FPGA architecture */
#define EM_SE_C17	139	/* Seiko Epson C17 family */
#define EM_TI_C6000	140	/* Texas Instruments TMS320C6000 DSP family */
#define EM_TI_C2000	141	/* Texas Instruments TMS320C2000 DSP family */
#define EM_TI_C5500	142	/* Texas Instruments TMS320C55x DSP family */
#define EM_res143	143	/* Reserved */
#define EM_res144	144	/* Reserved */
#define EM_res145	145	/* Reserved */
#define EM_res146	146	/* Reserved */
#define EM_res147	147	/* Reserved */
#define EM_res148	148	/* Reserved */
#define EM_res149	149	/* Reserved */
#define EM_res150	150	/* Reserved */
#define EM_res151	151	/* Reserved */
#define EM_res152	152	/* Reserved */
#define EM_res153	153	/* Reserved */
#define EM_res154	154	/* Reserved */
#define EM_res155	155	/* Reserved */
#define EM_res156	156	/* Reserved */
#define EM_res157	157	/* Reserved */
#define EM_res158	158	/* Reserved */
#define EM_res159	159	/* Reserved */
#define EM_MMDSP_PLUS	160	/* STMicroelectronics 64bit VLIW Data Signal Processor */
#define EM_CYPRESS_M8C	161	/* Cypress M8C microprocessor */
#define EM_R32C 	162	/* Renesas R32C series microprocessors */
#define EM_TRIMEDIA	163	/* NXP Semiconductors TriMedia architecture family */
#define EM_QDSP6	164	/* QUALCOMM DSP6 Processor */
#define EM_8051 	165	/* Intel 8051 and variants */
#define EM_STXP7X	166	/* STMicroelectronics STxP7x family */
#define EM_NDS32	167	/* Andes Technology compact code size embedded RISC processor family */
#define EM_ECOG1	168	/* Cyan Technology eCOG1X family */
#define EM_ECOG1X	168	/* Cyan Technology eCOG1X family */
#define EM_MAXQ30	169	/* Dallas Semiconductor MAXQ30 Core Micro-controllers */
#define EM_XIMO16	170	/* New Japan Radio (NJR) 16-bit DSP Processor */
#define EM_MANIK	171	/* M2000 Reconfigurable RISC Microprocessor */
#define EM_CRAYNV2	172	/* Cray Inc. NV2 vector architecture */
#define EM_RX		173	/* Renesas RX family */
#define EM_METAG	174	/* Imagination Technologies META processor architecture */
#define EM_MCST_ELBRUS	175	/* MCST Elbrus general purpose hardware architecture */
#define EM_ECOG16	176	/* Cyan Technology eCOG16 family */
#define EM_CR16 	177	/* National Semiconductor CompactRISC 16-bit processor */
#define EM_ETPU 	178	/* Freescale Extended Time Processing Unit */
#define EM_SLE9X	179	/* Infineon Technologies SLE9X core */
#define EM_L1OM 	180	/* Intel L1OM */
#define EM_K1OM 	181	/* Intel K1OM */
#define EM_INTEL182	182	/* Reserved by Intel */
#define EM_res183	183	/* Reserved by ARM */
#define EM_res184	184	/* Reserved by ARM */
#define EM_AVR32	185	/* Atmel Corporation 32-bit microprocessor family */
#define EM_STM8 	186	/* STMicroeletronics STM8 8-bit microcontroller */
#define EM_TILE64	187	/* Tilera TILE64 multicore architecture family */
#define EM_TILEPRO	188	/* Tilera TILEPro multicore architecture family */
#define EM_MICROBLAZE	189	/* Xilinx MicroBlaze 32-bit RISC soft processor core */
#define EM_CUDA 	190	/* NVIDIA CUDA architecture */
#define EM_TILEGX	191	/* Tilera TILE-Gx multicore architecture family */

/* Alpha backend magic number.  Written in the absence of an ABI.  */
#define EM_ALPHA		0x9026

/* Special section indexes. */

#define SHN_UNDEF	     0		/* Undefined, missing, irrelevant. */
#define SHN_LORESERVE	0xff00		/* First of reserved range. */
#define SHN_LOPROC	0xff00		/* First processor-specific. */
#define SHN_HIPROC	0xff1f		/* Last processor-specific. */
#define SHN_LOOS	0xff20		/* First operating system-specific. */
#define SHN_HIOS	0xff3f		/* Last operating system-specific. */
#define SHN_ABS 	0xfff1		/* Absolute values. */
#define SHN_COMMON	0xfff2		/* Common data. */
#define SHN_XINDEX	0xffff		/* Escape -- index stored elsewhere. */
#define SHN_HIRESERVE	0xffff		/* Last of reserved range. */

/* Values for program header, p_type field. */

#define PT_NULL 	0		/* Program header table entry unused */
#define PT_LOAD 	1		/* Loadable program segment */
#define PT_DYNAMIC	2		/* Dynamic linking information */
#define PT_INTERP	3		/* Program interpreter */
#define PT_NOTE 	4		/* Auxiliary information */
#define PT_SHLIB	5		/* Reserved, unspecified semantics */
#define PT_PHDR 	6		/* Entry for header table itself */
#define PT_TLS		7		/* Thread local storage segment */
#define PT_LOOS 	0x60000000	/* OS-specific */
#define PT_HIOS 	0x6fffffff	/* OS-specific */
#define PT_LOPROC	0x70000000	/* Processor-specific */
#define PT_HIPROC	0x7FFFFFFF	/* Processor-specific */

#define PT_GNU_EH_FRAME (PT_LOOS + 0x474e550) /* Frame unwind information */
#define PT_SUNW_EH_FRAME PT_GNU_EH_FRAME      /* Solaris uses the same value */
#define PT_GNU_STACK    (PT_LOOS + 0x474e551) /* Stack flags */
#define PT_GNU_RELRO    (PT_LOOS + 0x474e552) /* Read-only after relocation */

/* Program segment permissions, in program header p_flags field.  */

#define PF_X		0x1		/* Segment is executable */
#define PF_W		0x2		/* Segment is writable */
#define PF_R		0x4		/* Segment is readable */
#define PF_MASKOS	0x0FF00000	/* New value, Oct 4, 1999 Draft */
#define PF_MASKPROC	0xF0000000	/* Processor-specific reserved bits */

/* Values for section header, sh_type field.  */

#define SHT_NULL		 0	/* Section header table entry unused */
#define SHT_PROGBITS		 1	/* Program specific (private) data */
#define SHT_SYMTAB		 2	/* Link editing symbol table */
#define SHT_STRTAB		 3	/* A string table */
#define SHT_RELA		 4	/* Relocation entries with addends */
#define SHT_HASH		 5	/* A symbol hash table */
#define SHT_DYNAMIC		 6	/* Information for dynamic linking */
#define SHT_NOTE		 7	/* Information that marks file */
#define SHT_NOBITS		 8	/* Section occupies no space in file */
#define SHT_REL 		 9	/* Relocation entries, no addends */
#define SHT_SHLIB		10	/* Reserved, unspecified semantics */
#define SHT_DYNSYM		11	/* Dynamic linking symbol table */

#define SHT_INIT_ARRAY		14	/* Array of ptrs to init functions */
#define SHT_FINI_ARRAY		15	/* Array of ptrs to finish functions */
#define SHT_PREINIT_ARRAY	16	/* Array of ptrs to pre-init funcs */
#define SHT_GROUP		17	/* Section contains a section group */
#define SHT_SYMTAB_SHNDX	18	/* Indicies for SHN_XINDEX entries */

#define SHT_LOOS		0x60000000	/* First of OS specific semantics */
#define SHT_HIOS		0x6fffffff	/* Last of OS specific semantics */

#define SHT_GNU_INCREMENTAL_INPUTS 0x6fff4700	/* incremental build data */
#define SHT_GNU_ATTRIBUTES	0x6ffffff5	/* Object attributes */
#define SHT_GNU_HASH		0x6ffffff6	/* GNU style symbol hash table */
#define SHT_GNU_LIBLIST 	0x6ffffff7	/* List of prelink dependencies */

/* The next three section types are defined by Solaris, and are named
   SHT_SUNW*.  We use them in GNU code, so we also define SHT_GNU*
   versions.  */
#define SHT_SUNW_verdef 	0x6ffffffd	/* Versions defined by file */
#define SHT_SUNW_verneed	0x6ffffffe	/* Versions needed by file */
#define SHT_SUNW_versym 	0x6fffffff	/* Symbol versions */

#define SHT_GNU_verdef		SHT_SUNW_verdef
#define SHT_GNU_verneed 	SHT_SUNW_verneed
#define SHT_GNU_versym		SHT_SUNW_versym

#define SHT_LOPROC	0x70000000	/* Processor-specific semantics, lo */
#define SHT_HIPROC	0x7FFFFFFF	/* Processor-specific semantics, hi */
#define SHT_LOUSER	0x80000000	/* Application-specific semantics */
#define SHT_HIUSER	0xFFFFFFFF	/* New value, defined in Oct 4, 1999 Draft */

/* Values for section header, sh_flags field.  */

#define SHF_WRITE		0x1	/* Writable data during execution */
#define SHF_ALLOC		0x2	/* Occupies memory during execution */
#define SHF_EXECINSTR		0x4	/* Executable machine instructions */
#define SHF_MERGE		0x10	/* Data in this section can be merged */
#define SHF_STRINGS		0x20	/* Contains null terminated character strings */
#define SHF_INFO_LINK		0x40	/* sh_info holds section header table index */
#define SHF_LINK_ORDER		0x80	/* Preserve section ordering when linking */
#define SHF_OS_NONCONFORMING	0x100	/* OS specific processing required */
#define SHF_GROUP		0x200	/* Member of a section group */
#define SHF_TLS			0x400	/* Thread local storage section */

#define SHF_MASKOS	0x0FF00000	/* New value, Oct 4, 1999 Draft */
#define SHF_MASKPROC	0xF0000000	/* Processor-specific semantics */

/* Values of note segment descriptor types for core files. */

#define NT_PRSTATUS	1		/* Contains copy of prstatus struct */
#define NT_FPREGSET	2		/* Contains copy of fpregset struct */
#define NT_PRPSINFO	3		/* Contains copy of prpsinfo struct */
#define NT_TASKSTRUCT	4		/* Contains copy of task struct */
#define NT_AUXV 	6		/* Contains copy of Elfxx_auxv_t */

/* GNU note types. */
#define NT_GNU_ABI_TAG		1
#define NT_GNU_HWCAP		2
#define NT_GNU_BUILD_ID		3
#define NT_GNU_GOLD_VERSION	4
#define NT_GNU_PROPERTY_TYPE_0	5

#define GNU_PROPERTY_LOPROC			0xc0000000
#define GNU_PROPERTY_HIPROC			0xdfffffff

#define GNU_PROPERTY_X86_FEATURE_1_AND		0xc0000002

#define GNU_PROPERTY_X86_FEATURE_1_IBT		0x00000001
#define GNU_PROPERTY_X86_FEATURE_1_SHSTK	0x00000002

#define STN_UNDEF	0		/* Undefined symbol index */

#define STB_LOCAL	0		/* Symbol not visible outside obj */
#define STB_GLOBAL	1		/* Symbol visible outside obj */
#define STB_WEAK	2		/* Like globals, lower precedence */
#define STB_LOOS	10		/* OS-specific semantics */
#define STB_GNU_UNIQUE	10		/* Symbol is unique in namespace */
#define STB_HIOS	12		/* OS-specific semantics */
#define STB_LOPROC	13		/* Processor-specific semantics */
#define STB_HIPROC	15		/* Processor-specific semantics */

#define STT_NOTYPE	0		/* Symbol type is unspecified */
#define STT_OBJECT	1		/* Symbol is a data object */
#define STT_FUNC	2		/* Symbol is a code object */
#define STT_SECTION	3		/* Symbol associated with a section */
#define STT_FILE	4		/* Symbol gives a file name */
#define STT_COMMON	5		/* An uninitialised common block */
#define STT_TLS 	6		/* Thread local data object */
#define STT_RELC	8		/* Complex relocation expression */
#define STT_SRELC	9		/* Signed Complex relocation expression */
#define STT_LOOS	10		/* OS-specific semantics */
#define STT_GNU_IFUNC	10		/* Symbol is an indirect code object */
#define STT_HIOS	12		/* OS-specific semantics */
#define STT_LOPROC	13		/* Processor-specific semantics */
#define STT_HIPROC	15		/* Processor-specific semantics */

/* The following constants control how a symbol may be accessed once it has
   become part of an executable or shared library.  */

#define STV_DEFAULT	0		/* Visibility is specified by binding type */
#define STV_INTERNAL	1		/* OS specific version of STV_HIDDEN */
#define STV_HIDDEN	2		/* Can only be seen inside currect component */
#define STV_PROTECTED	3		/* Treat as STB_LOCAL inside current component */

/* Dynamic section tags.  */

#define DT_NULL 	0	/* Terminating entry. */
#define DT_NEEDED	1	/* String table offset of a needed shared
				   library. */
#define DT_PLTRELSZ	2	/* Total size in bytes of PLT relocations. */
#define DT_PLTGOT	3	/* Processor-dependent address. */
#define DT_HASH 	4	/* Address of symbol hash table. */
#define DT_STRTAB	5	/* Address of string table. */
#define DT_SYMTAB	6	/* Address of symbol table. */
#define DT_RELA 	7	/* Address of ElfNN_Rela relocations. */
#define DT_RELASZ	8	/* Total size of ElfNN_Rela relocations. */
#define DT_RELAENT	9	/* Size of each ElfNN_Rela relocation entry. */
#define DT_STRSZ	10	/* Size of string table. */
#define DT_SYMENT	11	/* Size of each symbol table entry. */
#define DT_INIT 	12	/* Address of initialization function. */
#define DT_FINI 	13	/* Address of finalization function. */
#define DT_SONAME	14	/* String table offset of shared object
				   name. */
#define DT_RPATH	15	/* String table offset of library path. [sup] */
#define DT_SYMBOLIC	16	/* Indicates "symbolic" linking. [sup] */
#define DT_REL		17	/* Address of ElfNN_Rel relocations. */
#define DT_RELSZ	18	/* Total size of ElfNN_Rel relocations. */
#define DT_RELENT	19	/* Size of each ElfNN_Rel relocation. */
#define DT_PLTREL	20	/* Type of relocation used for PLT. */
#define DT_DEBUG	21	/* Reserved (not used). */
#define DT_TEXTREL	22	/* Indicates there may be relocations in
				   non-writable segments. [sup] */
#define DT_JMPREL	23	/* Address of PLT relocations. */
#define DT_BIND_NOW	24	/* [sup] */
#define DT_INIT_ARRAY	25	/* Address of the array of pointers to
				   initialization functions */
#define DT_FINI_ARRAY	26	/* Address of the array of pointers to
				   termination functions */
#define DT_INIT_ARRAYSZ	27	/* Size in bytes of the array of
				   initialization functions. */
#define DT_FINI_ARRAYSZ	28	/* Size in bytes of the array of
				   terminationfunctions. */
#define DT_RUNPATH	29	/* String table offset of a null-terminated
				   library search path string. */
#define DT_FLAGS	30	/* Object specific flag values. */
#define DT_ENCODING	32	/* Values greater than or equal to DT_ENCODING
				   and less than DT_LOOS follow the rules for
				   the interpretation of the d_un union
				   as follows: even == 'd_ptr', odd == 'd_val'
				   or none */
#define DT_PREINIT_ARRAY 32	/* Address of the array of pointers to
				   pre-initialization functions. */
#define DT_PREINIT_ARRAYSZ 33	/* Size in bytes of the array of
				   pre-initialization functions. */

#define DT_LOOS		0x6000000d	/* First OS-specific */
#define DT_HIOS		0x6fff0000	/* Last OS-specific */

/* The next 2 dynamic tag ranges, integer value range (DT_VALRNGLO to
   DT_VALRNGHI) and virtual address range (DT_ADDRRNGLO to DT_ADDRRNGHI),
   are used on Solaris.  We support them everywhere.  Note these values
   lie outside of the (new) range for OS specific values.  This is a
   deliberate special case and we maintain it for backwards compatibility.
 */
#define DT_VALRNGLO		0x6ffffd00
#define DT_GNU_PRELINKED	0x6ffffdf5
#define DT_GNU_CONFLICTSZ	0x6ffffdf6
#define DT_GNU_LIBLISTSZ	0x6ffffdf7
#define DT_CHECKSUM	0x6ffffdf8	/* elf checksum */
#define DT_PLTPADSZ	0x6ffffdf9	/* pltpadding size */
#define DT_MOVEENT	0x6ffffdfa	/* move table entry size */
#define DT_MOVESZ	0x6ffffdfb	/* move table size */
#define DT_FEATURE	0x6ffffdfc	/* feature holder */
#define DT_POSFLAG_1	0x6ffffdfd	/* flags for DT_* entries, effecting */
					/*   the following DT_* entry. */
					/*   See DF_P1_* definitions */
#define DT_SYMINSZ	0x6ffffdfe	/* syminfo table size (in bytes) */
#define DT_SYMINENT	0x6ffffdff	/* syminfo entry size (in bytes) */
#define DT_VALRNGHI	0x6ffffdff

#define DT_ADDRRNGLO	0x6ffffe00
#define DT_GNU_HASH	0x6ffffef5	/* GNU-style hash table */
#define DT_TLSDESC_PLT	0x6ffffef6
#define DT_TLSDESC_GOT	0x6ffffef7
#define DT_GNU_CONFLICT 0x6ffffef8
#define DT_GNU_LIBLIST	0x6ffffef9
#define DT_CONFIG	0x6ffffefa	/* configuration information */
#define DT_DEPAUDIT	0x6ffffefb	/* dependency auditing */
#define DT_AUDIT	0x6ffffefc	/* object auditing */
#define DT_PLTPAD	0x6ffffefd	/* pltpadding (sparcv9) */
#define DT_MOVETAB	0x6ffffefe	/* move table */
#define DT_SYMINFO	0x6ffffeff	/* syminfo table */
#define DT_ADDRRNGHI	0x6ffffeff

#define DT_RELACOUNT	0x6ffffff9	/* number of RELATIVE relocations */
#define DT_RELCOUNT	0x6ffffffa	/* number of RELATIVE relocations */
#define DT_FLAGS_1	0x6ffffffb	/* state flags - see DF_1_* defs */
#define DT_VERDEF	0x6ffffffc	/* Address of verdef section. */
#define DT_VERDEFNUM	0x6ffffffd	/* Number of elems in verdef section */
#define DT_VERNEED	0x6ffffffe	/* Address of verneed section. */
#define DT_VERNEEDNUM	0x6fffffff	/* Number of elems in verneed section */

/* This tag is a GNU extension to the Solaris version scheme.  */
#define DT_VERSYM	0x6ffffff0

#define DT_LOPROC	0x70000000	/* First processor-specific type. */
#define DT_HIPROC	0x7fffffff	/* Last processor-specific type. */

/* These section tags are used on Solaris.  We support them
   everywhere, and hope they do not conflict.  */

#define DT_AUXILIARY	0x7ffffffd	/* shared library auxiliary name */
#define DT_USED 	0x7ffffffe	/* ignored - same as needed */
#define DT_FILTER	0x7fffffff	/* shared library filter name */


/* Values used in DT_FEATURE .dynamic entry.  */
#define DTF_1_PARINIT	0x00000001
/* From

   http://docs.sun.com:80/ab2/coll.45.13/LLM/@Ab2PageView/21165?Ab2Lang=C&Ab2Enc=iso-8859-1

   DTF_1_CONFEXP is the same as DTF_1_PARINIT. It is a typo. The value
   defined here is the same as the one in <sys/link.h> on Solaris 8.  */
#define DTF_1_CONFEXP	0x00000002

/* Flag values used in the DT_POSFLAG_1 .dynamic entry.	 */
#define DF_P1_LAZYLOAD	0x00000001
#define DF_P1_GROUPPERM 0x00000002

/* Flag value in in the DT_FLAGS_1 .dynamic entry.  */
#define DF_1_NOW	0x00000001
#define DF_1_BIND_NOW	0x00000001	/* Same as DF_BIND_NOW */
#define DF_1_GLOBAL	0x00000002	/* Set the RTLD_GLOBAL for object */
#define DF_1_GROUP	0x00000004
#define DF_1_NODELETE	0x00000008	/* Set the RTLD_NODELETE for object */
#define DF_1_LOADFLTR	0x00000010	/* Immediate loading of filtees */
#define DF_1_INITFIRST	0x00000020
#define DF_1_NOOPEN	0x00000040	/* Do not allow loading on dlopen() */
#define DF_1_ORIGIN	0x00000080	/* Process $ORIGIN */
#define DF_1_DIRECT	0x00000100
#define DF_1_TRANS	0x00000200
#define DF_1_INTERPOSE	0x00000400
#define DF_1_NODEFLIB	0x00000800
#define DF_1_NODUMP	0x00001000
#define DF_1_CONLFAT	0x00002000

/* Flag values for the DT_FLAGS entry. */
#define DF_ORIGIN	0x1	/* Indicates that the object being loaded may
				   make reference to the $ORIGIN substitution
				   string */
#define DF_SYMBOLIC	0x2	/* Indicates "symbolic" linking. */
#define DF_TEXTREL	0x4	/* Indicates there may be relocations in
				   non-writable segments. */
#define DF_BIND_NOW	0x8	/* Indicates that the dynamic linker should
				   process all relocations for the object
				   containing this entry before transferring
				   control to the program. */
#define DF_STATIC_TLS	0x10	/* Indicates that the shared object or
				   executable contains code using a static
				   thread-local storage scheme. */

/* These constants are used for the version number of a Elf32_Verdef
   structure.  */

#define VER_DEF_NONE		0
#define VER_DEF_CURRENT 	1
#define VER_DEF_IDX(x)		VER_NDX(x)

/* These constants appear in the vd_flags field of a Elf32_Verdef
   structure.

   Cf. the Solaris Linker and Libraries Guide, Ch. 7, Object File Format,
   Versioning Sections, for a description:

   http://docs.sun.com/app/docs/doc/819-0690/chapter6-93046?l=en&a=view  */

#define VER_FLG_BASE		0x1
#define VER_FLG_WEAK		0x2
#define VER_FLG_INFO		0x4

/* These special constants can be found in an Elf32_Versym field.  */

#define VER_NDX_LOCAL		0
#define VER_NDX_GLOBAL		1
#define VER_NDX_GIVEN		2
#define VER_NDX_HIDDEN		(1u << 15)
#define VER_NDX(x)		((x) & ~(1u << 15))
/* These constants are used for the version number of a Elf32_Verneed
   structure.  */

#define VER_NEED_NONE		0
#define VER_NEED_CURRENT	1
#define VER_NEED_WEAK		(1u << 15)
#define VER_NEED_HIDDEN 	VER_NDX_HIDDEN
#define VER_NEED_IDX(x) 	VER_NDX(x)

/* This flag appears in a Versym structure.  It means that the symbol
   is hidden, and is only visible with an explicit version number.
   This is a GNU extension.  */

#define VERSYM_HIDDEN		0x8000

/* This is the mask for the rest of the Versym information.  */

#define VERSYM_VERSION		0x7fff

/* This is a special token which appears as part of a symbol name.  It
   indictes that the rest of the name is actually the name of a
   version node, and is not part of the actual name.  This is a GNU
   extension.  For example, the symbol name `stat@ver2' is taken to
   mean the symbol `stat' in version `ver2'.  */

#define ELF_VER_CHR		'@'

/* Possible values for si_boundto.  */

#define SYMINFO_BT_SELF 	0xffff	/* Symbol bound to self */
#define SYMINFO_BT_PARENT	0xfffe	/* Symbol bound to parent */
#define SYMINFO_BT_LOWRESERVE	0xff00	/* Beginning of reserved entries */

/* Possible bitmasks for si_flags.  */

#define SYMINFO_FLG_DIRECT	0x0001	/* Direct bound symbol */
#define SYMINFO_FLG_PASSTHRU	0x0002	/* Pass-thru symbol for translator */
#define SYMINFO_FLG_COPY	0x0004	/* Symbol is a copy-reloc */
#define SYMINFO_FLG_LAZYLOAD	0x0008	/* Symbol bound to object to be lazy loaded */

/* Syminfo version values.  */

#define SYMINFO_NONE		0
#define SYMINFO_CURRENT 	1
#define SYMINFO_NUM		2

/* Section Group Flags.	 */

#define GRP_COMDAT		0x1	/* A COMDAT group */

#endif /* !_SYS_ELF_COMMON_H_ */
