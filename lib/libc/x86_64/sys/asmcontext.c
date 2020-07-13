#define _KERNEL_STRUCTURES
#include <sys/types.h>
#include <sys/ucontext.h>
#include <sys/assym.h>
#include <machine/frame.h>
#include <machine/tss.h>
#include <machine/segments.h>
#include <stddef.h>

ASSYM(UC_SIGMASK, offsetof(ucontext_t, uc_sigmask));
ASSYM(UC_LINK, offsetof(ucontext_t, uc_link));
ASSYM(UC_MCONTEXT, offsetof(ucontext_t, uc_mcontext));
ASSYM(SIG_BLOCK, SIG_BLOCK);
#ifdef __x86_64__
ASSYM(KUCSEL, GSEL(GUCODE_SEL, SEL_UPL));
ASSYM(KUDSEL, GSEL(GUDATA_SEL, SEL_UPL));
ASSYM(MC_ONSTACK, offsetof(mcontext_t, mc_onstack));
ASSYM(MC_RDI, offsetof(mcontext_t, mc_rdi));
ASSYM(MC_RSI, offsetof(mcontext_t, mc_rsi));
ASSYM(MC_RDX, offsetof(mcontext_t, mc_rdx));
ASSYM(MC_RCX, offsetof(mcontext_t, mc_rcx));
ASSYM(MC_R8, offsetof(mcontext_t, mc_r8));
ASSYM(MC_R9, offsetof(mcontext_t, mc_r9));
ASSYM(MC_RAX, offsetof(mcontext_t, mc_rax));
ASSYM(MC_RBX, offsetof(mcontext_t, mc_rbx));
ASSYM(MC_RBP, offsetof(mcontext_t, mc_rbp));
ASSYM(MC_R10, offsetof(mcontext_t, mc_r10));
ASSYM(MC_R11, offsetof(mcontext_t, mc_r11));
ASSYM(MC_R12, offsetof(mcontext_t, mc_r12));
ASSYM(MC_R13, offsetof(mcontext_t, mc_r13));
ASSYM(MC_R14, offsetof(mcontext_t, mc_r14));
ASSYM(MC_R15, offsetof(mcontext_t, mc_r15));
ASSYM(MC_RIP, offsetof(mcontext_t, mc_rip));
ASSYM(MC_CS, offsetof(mcontext_t, mc_cs));
ASSYM(MC_RFLAGS, offsetof(mcontext_t, mc_rflags));
ASSYM(MC_RSP, offsetof(mcontext_t, mc_rsp));
ASSYM(MC_SS, offsetof(mcontext_t, mc_ss));
ASSYM(_MC_FPOWNED_NONE, _MC_FPOWNED_NONE);
ASSYM(_MC_FPFMT_NODEV, _MC_FPFMT_NODEV);
#endif
ASSYM(MC_LEN, offsetof(mcontext_t, mc_len));
ASSYM(MC_FPFORMAT, offsetof(mcontext_t, mc_fpformat));
ASSYM(MC_OWNEDFP, offsetof(mcontext_t, mc_ownedfp));
ASSYM(SIZEOF_MCONTEXT_T, sizeof(mcontext_t));
