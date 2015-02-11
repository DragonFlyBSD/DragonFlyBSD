#ifndef _COREMCTL_REG_H_
#define _COREMCTL_REG_H_

#ifndef _SYS_BITOPS_H_
#include <sys/bitops.h>
#endif

#define PCI_CORE_MEMCTL_VID		0x8086
#define PCI_E3V1_MEMCTL_DID		0x0108
#define PCI_E3V2_MEMCTL_DID		0x0158
#define PCI_E3V3_MEMCTL_DID		0x0c08
#define PCI_COREV3_MEMCTL_DID		0x0c00

#define PCI_CORE_MCHBAR_LO		0x48
#define PCI_CORE_MCHBAR_LO_EN		0x1
#define PCI_CORE_MCHBAR_HI		0x4c

#define PCI_E3_ERRSTS			0xc8
#define PCI_E3_ERRSTS_DMERR		__BIT(1)
#define PCI_E3_ERRSTS_DSERR		__BIT(0)

#define PCI_CORE_CAPID0_A		0xe4
#define PCI_CORE_CAPID0_A_DMFC	__BITS(0, 2)	/* v1 */
#define PCI_CORE_CAPID0_A_ECCDIS	__BIT(25)

#define PCI_CORE_CAPID0_B		0xe8
#define PCI_CORE_CAPID0_B_DMFC		__BITS(4, 6)	/* v2/v3 */

#define PCI_CORE_CAPID0_DMFC_V1_ALL	0x0	/* v1 */
#define PCI_CORE_CAPID0_DMFC_2933	0x0	/* v2/v3 */
#define PCI_CORE_CAPID0_DMFC_2667	0x1	/* v2/v3 */
#define PCI_CORE_CAPID0_DMFC_2400	0x2	/* v2/v3 */
#define PCI_CORE_CAPID0_DMFC_2133	0x3	/* v2/v3 */
#define PCI_CORE_CAPID0_DMFC_1867	0x4	/* v2/v3 */
#define PCI_CORE_CAPID0_DMFC_1600	0x5	/* v2/v3 */
#define PCI_CORE_CAPID0_DMFC_1333	0x6
#define PCI_CORE_CAPID0_DMFC_1067	0x7

#define PCI_CORE_MCHBAR_ADDRMASK	__BITS64(15, 38)

#define MCH_CORE_SIZE			(32 * 1024)

#define MCH_E3_ERRLOG0_C0		0x40c8
#define MCH_E3_ERRLOG1_C0		0x40cc

#define MCH_E3_ERRLOG0_C1		0x44c8
#define MCH_E3_ERRLOG1_C1		0x44cc

#define MCH_E3_ERRLOG0_CERRSTS		__BIT(0)
#define MCH_E3_ERRLOG0_MERRSTS		__BIT(1)
#define MCH_E3_ERRLOG0_ERRSYND		__BITS(16, 23)
#define MCH_E3_ERRLOG0_ERRCHUNK		__BITS(24, 26)
#define MCH_E3_ERRLOG0_ERRRANK		__BITS(27, 28)
#define MCH_E3_ERRLOG0_ERRBANK		__BITS(29, 31)

#define MCH_E3_ERRLOG1_ERRROW		__BITS(0, 15)
#define MCH_E3_ERRLOG1_ERRCOL		__BITS(16, 31)

#define MCH_CORE_DIMM_CH0		0x5004
#define MCH_CORE_DIMM_CH1		0x5008

#define MCH_CORE_DIMM_SIZE_UNIT	256		/* MB */
#define MCH_CORE_DIMM_A_SIZE		__BITS(0, 7)
#define MCH_CORE_DIMM_B_SIZE		__BITS(8, 15)
#define MCH_CORE_DIMM_A_SELECT		__BIT(16)
#define MCH_CORE_DIMM_A_DUAL_RANK	__BIT(17)
#define MCH_CORE_DIMM_B_DUAL_RANK	__BIT(18)
#define MCH_CORE_DIMM_A_X16		__BIT(19)
#define MCH_CORE_DIMM_B_X16		__BIT(20)
#define MCH_CORE_DIMM_RI		__BIT(21)	/* rank interleave */
/* enchanced interleave */
#define MCH_CORE_DIMM_ENHI		__BIT(22)
#define MCH_E3_DIMM_ECC			__BITS(24, 25)
#define MCH_E3_DIMM_ECC_NONE		0x0
#define MCH_E3_DIMM_ECC_IO		0x1
#define MCH_E3_DIMM_ECC_LOGIC		0x2
#define MCH_E3_DIMM_ECC_ALL		0x3
/* high order rank interleave */
#define MCH_CORE_DIMM_HORI		__BIT(26)	/* v3 */
/* high order rank interleave address (addr bits [20,27]) */
#define MCH_CORE_DIMM_HORIADDR		__BITS(27, 29)	/* v3 */

#endif	/* !_COREMCTL_REG_H_ */
