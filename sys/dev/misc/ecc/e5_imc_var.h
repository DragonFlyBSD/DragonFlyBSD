#ifndef _E5_IMC_VAR_H_
#define _E5_IMC_VAR_H_

#define E5_IMC_CHAN_VER2	2	/* E5 v2 */
#define E5_IMC_CHAN_VER3	3	/* E5 v3 */

struct e5_imc_chan {
	uint16_t	did;
	int		slot;
	int		func;
	const char	*desc;

	int		chan_ext;	/* external channel */
	int		chan;
	int		ver;

	int		ubox_slot;
	int		ubox_func;
	uint16_t	ubox_did;

	int		cpgc_slot;
	int		cpgc_func;
	uint16_t	cpgc_did;
	uint32_t	cpgc_chandis;

	int		ctad_slot;
	int		ctad_func;
	uint16_t	ctad_did;
};

#define E5_IMC_CHAN_END	\
	{ 0, 0, 0, NULL, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 }

#define E5_IMC_CHAN_FIELDS(v, imc, c, c_ext)			\
	.chan_ext	= c_ext,				\
	.chan		= c,					\
	.ver		= E5_IMC_CHAN_VER##v,			\
								\
	.ubox_slot	= PCISLOT_E5V##v##_UBOX0,		\
	.ubox_func	= PCIFUNC_E5V##v##_UBOX0,		\
	.ubox_did	= PCI_E5V##v##_UBOX0_DID_ID,		\
								\
	.cpgc_slot	= PCISLOT_E5V##v##_IMC##imc##_CPGC,	\
	.cpgc_func	= PCIFUNC_E5V##v##_IMC##imc##_CPGC,	\
	.cpgc_did	= PCI_E5V##v##_IMC##imc##_CPGC_DID_ID,	\
	.cpgc_chandis	= PCI_E5V##v##_IMC_CPGC_MCMTR_CHN_DISABLE(c), \
								\
	.ctad_slot	= PCISLOT_E5V##v##_IMC##imc##_CTAD,	\
	.ctad_func	= PCIFUNC_E5V##v##_IMC##imc##_CTAD(c),	\
	.ctad_did	= PCI_E5V##v##_IMC##imc##_CTAD_DID_ID(c) \

#define UBOX_READ(dev, c, ofs, w)			\
	pcib_read_config((dev), pci_get_bus((dev)),	\
	    (c)->ubox_slot, (c)->ubox_func, (ofs), (w))
#define UBOX_READ_2(dev, c, ofs)	UBOX_READ((dev), (c), (ofs), 2)
#define UBOX_READ_4(dev, c, ofs)	UBOX_READ((dev), (c), (ofs), 4)

#define IMC_CPGC_READ(dev, c, ofs, w)			\
	pcib_read_config((dev), pci_get_bus((dev)),	\
	    (c)->cpgc_slot, (c)->cpgc_func, (ofs), (w))
#define IMC_CPGC_READ_2(dev, c, ofs)	IMC_CPGC_READ((dev), (c), (ofs), 2)
#define IMC_CPGC_READ_4(dev, c, ofs)	IMC_CPGC_READ((dev), (c), (ofs), 4)

#define IMC_CTAD_READ(dev, c, ofs, w)			\
	pcib_read_config((dev), pci_get_bus((dev)),	\
	    (c)->ctad_slot, (c)->ctad_func, (ofs), (w))
#define IMC_CTAD_READ_2(dev, c, ofs)	IMC_CTAD_READ((dev), (c), (ofs), 2)
#define IMC_CTAD_READ_4(dev, c, ofs)	IMC_CTAD_READ((dev), (c), (ofs), 4)

static __inline int
e5_imc_node_probe(device_t dev, const struct e5_imc_chan *c)
{
	int node, dimm;
	uint32_t val;

	/* Check CPGC vid/did */
	if (IMC_CPGC_READ_2(dev, c, PCIR_VENDOR) != PCI_E5_IMC_VID_ID ||
	    IMC_CPGC_READ_2(dev, c, PCIR_DEVICE) != c->cpgc_did)
		return -1;

	/* Is this channel disabled */
	val = IMC_CPGC_READ_4(dev, c, PCI_E5_IMC_CPGC_MCMTR);
	if (val & c->cpgc_chandis)
		return -1;

	/* Check CTAD vid/did */
	if (IMC_CTAD_READ_2(dev, c, PCIR_VENDOR) != PCI_E5_IMC_VID_ID ||
	    IMC_CTAD_READ_2(dev, c, PCIR_DEVICE) != c->ctad_did)
		return -1;

	/* Are there any DIMMs populated? */
	for (dimm = 0; dimm < PCI_E5_IMC_CHN_DIMM_MAX; ++dimm) {
		val = IMC_CTAD_READ_4(dev, c, PCI_E5_IMC_CTAD_DIMMMTR(dimm));
		if (val & PCI_E5_IMC_CTAD_DIMMMTR_DIMM_POP)
			break;
	}
	if (dimm == PCI_E5_IMC_CHN_DIMM_MAX)
		return -1;

	/* Check UBOX vid/did */
	if (UBOX_READ_2(dev, c, PCIR_VENDOR) != PCI_E5_IMC_VID_ID ||
	    UBOX_READ_2(dev, c, PCIR_DEVICE) != c->ubox_did)
		return -1;

	val = UBOX_READ_4(dev, c, PCI_E5_UBOX0_CPUNODEID);
	node = __SHIFTOUT(val, PCI_E5_UBOX0_CPUNODEID_LCLNODEID);

	return node;
}

#endif	/* !_E5_IMC_VAR_H_ */
