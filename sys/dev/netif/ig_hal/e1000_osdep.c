/******************************************************************************

  Copyright (c) 2001-2014, Intel Corporation 
  All rights reserved.
  
  Redistribution and use in source and binary forms, with or without 
  modification, are permitted provided that the following conditions are met:
  
   1. Redistributions of source code must retain the above copyright notice, 
      this list of conditions and the following disclaimer.
  
   2. Redistributions in binary form must reproduce the above copyright 
      notice, this list of conditions and the following disclaimer in the 
      documentation and/or other materials provided with the distribution.
  
   3. Neither the name of the Intel Corporation nor the names of its 
      contributors may be used to endorse or promote products derived from 
      this software without specific prior written permission.
  
  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
  AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE 
  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE 
  ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE 
  LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR 
  CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF 
  SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS 
  INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN 
  CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) 
  ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
  POSSIBILITY OF SUCH DAMAGE.

******************************************************************************/

#include <sys/param.h>
#include <sys/sysctl.h>
#include <net/if_var.h>
#include <net/if_media.h>

#include "e1000_api.h"
#include "e1000_dragonfly.h"

/*
 * NOTE: the following routines using the e1000 
 * 	naming style are provided to the shared
 *	code but are OS specific
 */

void
e1000_write_pci_cfg(struct e1000_hw *hw, uint32_t reg, uint16_t *value)
{
	pci_write_config(((struct e1000_osdep *)hw->back)->dev, reg, *value, 2);
}

void
e1000_read_pci_cfg(struct e1000_hw *hw, uint32_t reg, uint16_t *value)
{
	*value = pci_read_config(((struct e1000_osdep *)hw->back)->dev, reg, 2);
}

void
e1000_pci_set_mwi(struct e1000_hw *hw)
{
	pci_write_config(((struct e1000_osdep *)hw->back)->dev, PCIR_COMMAND,
	    (hw->bus.pci_cmd_word | CMD_MEM_WRT_INVALIDATE), 2);
}

void
e1000_pci_clear_mwi(struct e1000_hw *hw)
{
	pci_write_config(((struct e1000_osdep *)hw->back)->dev, PCIR_COMMAND,
	    (hw->bus.pci_cmd_word & ~CMD_MEM_WRT_INVALIDATE), 2);
}

/*
 * Read the PCI Express capabilities
 */
int32_t
e1000_read_pcie_cap_reg(struct e1000_hw *hw, uint32_t reg, uint16_t *value)
{
	device_t dev = ((struct e1000_osdep *)hw->back)->dev;
	uint8_t pcie_ptr;

	pcie_ptr = pci_get_pciecap_ptr(dev);
	if (pcie_ptr == 0)
		return E1000_NOT_IMPLEMENTED;

	*value = pci_read_config(dev, pcie_ptr + reg, 2);
	return E1000_SUCCESS;
}

int32_t
e1000_write_pcie_cap_reg(struct e1000_hw *hw, uint32_t reg, uint16_t *value)
{
	device_t dev = ((struct e1000_osdep *)hw->back)->dev;
	uint8_t pcie_ptr;

	pcie_ptr = pci_get_pciecap_ptr(dev);
	if (pcie_ptr == 0)
		return E1000_NOT_IMPLEMENTED;

	pci_write_config(dev, pcie_ptr + reg, *value, 2);
	return E1000_SUCCESS;
}

enum e1000_fc_mode
e1000_str2fc(const char *str)
{
	if (strcmp(str, E1000_FC_STR_NONE) == 0)
		return e1000_fc_none;
	else if (strcmp(str, E1000_FC_STR_RX_PAUSE) == 0)
		return e1000_fc_rx_pause;
	else if (strcmp(str, E1000_FC_STR_TX_PAUSE) == 0)
		return e1000_fc_tx_pause;
	else
		return e1000_fc_full;
}

void
e1000_fc2str(enum e1000_fc_mode fc, char *str, int len)
{
	const char *fc_str = E1000_FC_STR_FULL;

	switch (fc) {
	case e1000_fc_none:
		fc_str = E1000_FC_STR_NONE;
		break;

	case e1000_fc_rx_pause:
		fc_str = E1000_FC_STR_RX_PAUSE;
		break;

	case e1000_fc_tx_pause:
		fc_str = E1000_FC_STR_TX_PAUSE;
		break;

	default:
		break;
	}
	strlcpy(str, fc_str, len);
}

int
e1000_sysctl_flowctrl(struct ifnet *ifp, enum e1000_fc_mode *fc0,
    struct e1000_hw *hw, struct sysctl_oid *oidp, struct sysctl_req *req)
{
	char flowctrl[E1000_FC_STRLEN];
	enum e1000_fc_mode fc;
	int error;

	e1000_fc2str(*fc0, flowctrl, sizeof(flowctrl));
	error = sysctl_handle_string(oidp, flowctrl, sizeof(flowctrl), req);
	if (error != 0 || req->newptr == NULL)
		return error;

	fc = e1000_str2fc(flowctrl);

	ifnet_serialize_all(ifp);
	if (fc == *fc0)
		goto done;

	*fc0 = fc;
	hw->fc.requested_mode = fc;
	hw->fc.current_mode = fc;
	e1000_force_mac_fc(hw);
done:
	ifnet_deserialize_all(ifp);

	return 0;
}

enum e1000_fc_mode
e1000_ifmedia2fc(int ifm)
{
	int fc_opt = ifm & (IFM_ETH_RXPAUSE | IFM_ETH_TXPAUSE);

	switch (fc_opt) {
	case (IFM_ETH_RXPAUSE | IFM_ETH_TXPAUSE):
		return e1000_fc_full;

	case IFM_ETH_RXPAUSE:
		return e1000_fc_rx_pause;

	case IFM_ETH_TXPAUSE:
		return e1000_fc_tx_pause;

	default:
		return e1000_fc_none;
	}
}

int
e1000_fc2ifmedia(enum e1000_fc_mode fc)
{
	switch (fc) {
	case e1000_fc_full:
		return (IFM_ETH_RXPAUSE | IFM_ETH_TXPAUSE);

	case e1000_fc_rx_pause:
		return IFM_ETH_RXPAUSE;

	case e1000_fc_tx_pause:
		return IFM_ETH_TXPAUSE;

	default:
		return 0;
	}
}

/* Module glue */
static moduledata_t ig_hal_mod = { "ig_hal" };
DECLARE_MODULE(ig_hal, ig_hal_mod, SI_SUB_DRIVERS, SI_ORDER_MIDDLE);
MODULE_VERSION(ig_hal, 1);
