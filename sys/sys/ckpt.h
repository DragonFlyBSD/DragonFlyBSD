/*
 * $DragonFly: src/sys/sys/ckpt.h,v 1.1 2003/10/19 19:24:20 dillon Exp $
 */
#ifndef _SYS_CKPT_H_
#define _SYS_CKPT_H_

struct ckpt_filehdr {
	int cfh_nfiles;
};


struct ckpt_fileinfo {
	int   cfi_index;
	u_int cfi_flags;	/* saved f_flag	*/
	off_t cfi_offset;	/* saved f_offset */
	fhandle_t cfi_fh;
};

struct ckpt_siginfo {
  int               csi_ckptpisz;
  struct procsig    csi_procsig;
  struct sigacts    csi_sigacts;
  struct itimerval  csi_itimerval;
  int               csi_sigparent;
};

struct vn_hdr {
	fhandle_t vnh_fh;
	Elf_Phdr vnh_phdr;
};

#ifdef _KERNEL
#ifdef DEBUG
#define TRACE_ENTER \
printf("entering %s at %s:%d\n", __FUNCTION__, __FILE__, __LINE__)
#define TRACE_EXIT \
printf("exiting %s at %s:%d\n", __FUNCTION__, __FILE__, __LINE__)
#define TRACE_ERR \
printf("failure encountered in %s at %s:%d\n", __FUNCTION__, __FILE__, __LINE__)
#define PRINTF printf
#else
#define TRACE_ENTER
#define TRACE_EXIT
#define TRACE_ERR
#define PRINTF()
#endif
#endif

#endif
