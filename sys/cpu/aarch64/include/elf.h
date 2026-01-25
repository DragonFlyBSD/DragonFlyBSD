/*
 * Placeholder ELF definitions for arm64.
 */

#ifndef _CPU_ELF_H_
#define _CPU_ELF_H_

#ifndef EM_AARCH64
#define EM_AARCH64 183
#endif

/*
 * aarch64 load base for PIE binaries (placeholder).
 */
#define ET_DYN_LOAD_ADDR	0x01021000

#endif /* !_CPU_ELF_H_ */
