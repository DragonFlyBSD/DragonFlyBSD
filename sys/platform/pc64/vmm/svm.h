#ifndef _VMM_SVM_H_
#define _VMM_SVM_H_

struct vmcb {
	/* Control Area */
	uint16_t	vmcb_cr_read;
	uint16_t	vmcb_cr_write;
	uint16_t	vmcb_dr_read;
	uint16_t	vmcb_dr_write;
	uint32_t	vmcb_exception;
	uint32_t	vmcb_ctrl1;
	uint32_t	vmcb_ctrl2;
	uint8_t		vmcb_unused1[40];
	uint16_t	vmcb_pause_filter_threshold;
	uint16_t	vmcb_pause_filter_count;
	uint64_t	vmcb_iopm_base_pa;
	uint64_t	vmcb_msrpm_base_pa;
	uint64_t	vmcb_tsc_offset;
	uint32_t	vmcb_asid;
	uint8_t		vmcb_tlb_ctrl;
	uint8_t		vmcb_unused2[3];
	uint8_t		vmcb_v_tpr;
	uint8_t		vmcb_v_irq;
	/* ... */
	uint8_t		vmcb_v_intr_vector;
	uint8_t		vmcb_unused3[3];
	uint64_t	vmcb_intr_shadow;
	uint64_t	vmcb_exitcode;
	uint64_t	vmcb_exitinfo1;
	uint64_t	vmcb_exitinfo2;
	uint64_t	vmcb_exitintinfo;
	uint64_t	vmcb_np_enable;
	uint8_t		vmcb_unused4[16];
	uint64_t	vmcb_event_injection;
	uint64_t	vmcb_n_cr3;

	/* Saved Guest State */
};

/* SVM Intercept Codes (vmcb_exitcode) */
#define VMEXIT_CR_READ(_cr)	(0 + (_cr))
#define VMEXIT_CR_WRITE(_cr)	(16 + (_cr))
#define VMEXIT_DR_READ(_dr)	(32 + (_dr))
#define VMEXIT_DR_WRITE(_dr)	(48 + (_dr))
#define VMEXIT_EXCP(_excp)	(64 + (_excp))
#define VMEXIT_INTR		96
#define VMEXIT_NMI		97
#define VMEXIT_SMI		98
#define VMEXIT_INIT		99
#define VMEXIT_VINTR		100
#define VMEXIT_CR0_SEL_WRITE	101
#define VMEXIT_IDTR_READ	102
#define VMEXIT_GDTR_READ	103
#define VMEXIT_LDTR_READ	104
#define VMEXIT_TR_READ		105
#define VMEXIT_IDTR_WRITE	106
#define VMEXIT_GDTR_WRITE	107
#define VMEXIT_LDTR_WRITE	108
#define VMEXIT_TR_WRITE		109
#define VMEXIT_RDTSC		110
#define VMEXIT_RDPMC		111
#define VMEXIT_PUSHF		112
#define VMEXIT_POPF		113
#define VMEXIT_CPUID		114
#define VMEXIT_RSM		115
#define VMEXIT_IRET		116
#define VMEXIT_SWINT		117
#define VMEXIT_INVD		118
#define VMEXIT_PAUSE		119
#define VMEXIT_HLT		120
#define VMEXIT_INVLPG		121
#define VMEXIT_INVLPGA		122
#define VMEXIT_IOIO		123
#define VMEXIT_MSR		124
#define VMEXIT_TASK_SWITCH	125
#define VMEXIT_FERR_FREEZE	126
#define VMEXIT_SHUTDOWN	127
#define VMEXIT_VMRUN		128
#define VMEXIT_VMMCALL		129
#define VMEXIT_VMLOAD		130
#define VMEXIT_VMSAVE		131
#define VMEXIT_STGI		132
#define VMEXIT_CLGI		133
#define VMEXIT_SKINIT		134
#define VMEXIT_RDTSCP		135
#define VMEXIT_ICEBP		137
#define VMEXIT_NPF		1024
#define VMEXIT_INVALID		-1

#endif  /* ndef _VMM_SVM_H_ */
