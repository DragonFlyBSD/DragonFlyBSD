/******************************************************************************

Copyright (c) 2006-2013, Myricom Inc.
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

 1. Redistributions of source code must retain the above copyright notice,
    this list of conditions and the following disclaimer.

 2. Neither the name of the Myricom Inc, nor the names of its
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

$FreeBSD: head/sys/dev/mxge/if_mxge.c 254263 2013-08-12 23:30:01Z scottl $

***************************************************************************/

#include "opt_ifpoll.h"
#include "opt_inet.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/linker.h>
#include <sys/firmware.h>
#include <sys/endian.h>
#include <sys/in_cksum.h>
#include <sys/sockio.h>
#include <sys/mbuf.h>
#include <sys/malloc.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/serialize.h>
#include <sys/socket.h>
#include <sys/sysctl.h>

#include <net/if.h>
#include <net/if_arp.h>
#include <net/ifq_var.h>
#include <net/if_ringmap.h>
#include <net/ethernet.h>
#include <net/if_dl.h>
#include <net/if_media.h>
#include <net/if_poll.h>

#include <net/bpf.h>

#include <net/if_types.h>
#include <net/vlan/if_vlan_var.h>
#include <net/zlib.h>
#include <net/toeplitz.h>

#include <netinet/in_systm.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/tcp.h>

#include <sys/bus.h>
#include <sys/rman.h>

#include <bus/pci/pcireg.h>
#include <bus/pci/pcivar.h>
#include <bus/pci/pci_private.h> /* XXX for pci_cfg_restore */

#include <vm/vm.h>		/* for pmap_mapdev() */
#include <vm/pmap.h>

#if defined(__x86_64__)
#include <machine/specialreg.h>
#endif

#include <dev/netif/mxge/mxge_mcp.h>
#include <dev/netif/mxge/mcp_gen_header.h>
#include <dev/netif/mxge/if_mxge_var.h>

#define MXGE_IFM	(IFM_ETHER | IFM_FDX | IFM_ETH_FORCEPAUSE)

#define MXGE_RX_SMALL_BUFLEN		(MHLEN - MXGEFW_PAD)
#define MXGE_HWRSS_KEYLEN		16

/* Tunable params */
static int mxge_nvidia_ecrc_enable = 1;
static int mxge_force_firmware = 0;
static int mxge_intr_coal_delay = MXGE_INTR_COAL_DELAY;
static int mxge_deassert_wait = 1;
static int mxge_ticks;
static int mxge_num_slices = 0;
static int mxge_always_promisc = 0;
static int mxge_throttle = 0;
static int mxge_msi_enable = 1;
static int mxge_msix_enable = 1;
static int mxge_multi_tx = 1;
/*
 * Don't use RSS by default, its just too slow
 */
static int mxge_use_rss = 0;

static char mxge_flowctrl[IFM_ETH_FC_STRLEN] = IFM_ETH_FC_FORCE_NONE;

static const char *mxge_fw_unaligned = "mxge_ethp_z8e";
static const char *mxge_fw_aligned = "mxge_eth_z8e";
static const char *mxge_fw_rss_aligned = "mxge_rss_eth_z8e";
static const char *mxge_fw_rss_unaligned = "mxge_rss_ethp_z8e";

TUNABLE_INT("hw.mxge.num_slices", &mxge_num_slices);
TUNABLE_INT("hw.mxge.intr_coal_delay", &mxge_intr_coal_delay);	
TUNABLE_INT("hw.mxge.nvidia_ecrc_enable", &mxge_nvidia_ecrc_enable);	
TUNABLE_INT("hw.mxge.force_firmware", &mxge_force_firmware);	
TUNABLE_INT("hw.mxge.deassert_wait", &mxge_deassert_wait);	
TUNABLE_INT("hw.mxge.ticks", &mxge_ticks);
TUNABLE_INT("hw.mxge.always_promisc", &mxge_always_promisc);
TUNABLE_INT("hw.mxge.throttle", &mxge_throttle);
TUNABLE_INT("hw.mxge.multi_tx", &mxge_multi_tx);
TUNABLE_INT("hw.mxge.use_rss", &mxge_use_rss);
TUNABLE_INT("hw.mxge.msi.enable", &mxge_msi_enable);
TUNABLE_INT("hw.mxge.msix.enable", &mxge_msix_enable);
TUNABLE_STR("hw.mxge.flow_ctrl", mxge_flowctrl, sizeof(mxge_flowctrl));

static int mxge_probe(device_t dev);
static int mxge_attach(device_t dev);
static int mxge_detach(device_t dev);
static int mxge_shutdown(device_t dev);

static int mxge_alloc_intr(struct mxge_softc *sc);
static void mxge_free_intr(struct mxge_softc *sc);
static int mxge_setup_intr(struct mxge_softc *sc);
static void mxge_teardown_intr(struct mxge_softc *sc, int cnt);

static device_method_t mxge_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe, mxge_probe),
	DEVMETHOD(device_attach, mxge_attach),
	DEVMETHOD(device_detach, mxge_detach),
	DEVMETHOD(device_shutdown, mxge_shutdown),
	DEVMETHOD_END
};

static driver_t mxge_driver = {
	"mxge",
	mxge_methods,
	sizeof(mxge_softc_t),
};

static devclass_t mxge_devclass;

/* Declare ourselves to be a child of the PCI bus.*/
DRIVER_MODULE(mxge, pci, mxge_driver, mxge_devclass, NULL, NULL);
MODULE_DEPEND(mxge, firmware, 1, 1, 1);
MODULE_DEPEND(mxge, zlib, 1, 1, 1);

static int mxge_load_firmware(mxge_softc_t *sc, int adopt);
static int mxge_send_cmd(mxge_softc_t *sc, uint32_t cmd, mxge_cmd_t *data);
static void mxge_close(mxge_softc_t *sc, int down);
static int mxge_open(mxge_softc_t *sc);
static void mxge_tick(void *arg);
static void mxge_watchdog_reset(mxge_softc_t *sc);
static void mxge_warn_stuck(mxge_softc_t *sc, mxge_tx_ring_t *tx, int slice);

static int
mxge_probe(device_t dev)
{
	if (pci_get_vendor(dev) == MXGE_PCI_VENDOR_MYRICOM &&
	    (pci_get_device(dev) == MXGE_PCI_DEVICE_Z8E ||
	     pci_get_device(dev) == MXGE_PCI_DEVICE_Z8E_9)) {
		int rev = pci_get_revid(dev);

		switch (rev) {
		case MXGE_PCI_REV_Z8E:
			device_set_desc(dev, "Myri10G-PCIE-8A");
			break;
		case MXGE_PCI_REV_Z8ES:
			device_set_desc(dev, "Myri10G-PCIE-8B");
			break;
		default:
			device_set_desc(dev, "Myri10G-PCIE-8??");
			device_printf(dev, "Unrecognized rev %d NIC\n", rev);
			break;	
		}
		return 0;
	}
	return ENXIO;
}

static void
mxge_enable_wc(mxge_softc_t *sc)
{
#if defined(__x86_64__)
	vm_offset_t len;

	sc->wc = 1;
	len = rman_get_size(sc->mem_res);
	pmap_change_attr((vm_offset_t) sc->sram, len / PAGE_SIZE,
	    PAT_WRITE_COMBINING);
#endif
}

static int
mxge_dma_alloc(mxge_softc_t *sc, bus_dmamem_t *dma, size_t bytes,
    bus_size_t alignment)
{
	bus_size_t boundary;
	int err;

	if (bytes > 4096 && alignment == 4096)
		boundary = 0;
	else
		boundary = 4096;

	err = bus_dmamem_coherent(sc->parent_dmat, alignment, boundary,
	    BUS_SPACE_MAXADDR, BUS_SPACE_MAXADDR, bytes,
	    BUS_DMA_WAITOK | BUS_DMA_ZERO, dma);
	if (err != 0) {
		device_printf(sc->dev, "bus_dmamem_coherent failed: %d\n", err);
		return err;
	}
	return 0;
}

static void
mxge_dma_free(bus_dmamem_t *dma)
{
	bus_dmamap_unload(dma->dmem_tag, dma->dmem_map);
	bus_dmamem_free(dma->dmem_tag, dma->dmem_addr, dma->dmem_map);
	bus_dma_tag_destroy(dma->dmem_tag);
}

/*
 * The eeprom strings on the lanaiX have the format
 * SN=x\0
 * MAC=x:x:x:x:x:x\0
 * PC=text\0
 */
static int
mxge_parse_strings(mxge_softc_t *sc)
{
	const char *ptr;
	int i, found_mac, found_sn2;
	char *endptr;

	ptr = sc->eeprom_strings;
	found_mac = 0;
	found_sn2 = 0;
	while (*ptr != '\0') {
		if (strncmp(ptr, "MAC=", 4) == 0) {
			ptr += 4;
			for (i = 0;;) {
				sc->mac_addr[i] = strtoul(ptr, &endptr, 16);
				if (endptr - ptr != 2)
					goto abort;
				ptr = endptr;
				if (++i == 6)
					break;
				if (*ptr++ != ':')
					goto abort;
			}
			found_mac = 1;
		} else if (strncmp(ptr, "PC=", 3) == 0) {
			ptr += 3;
			strlcpy(sc->product_code_string, ptr,
			    sizeof(sc->product_code_string));
		} else if (!found_sn2 && (strncmp(ptr, "SN=", 3) == 0)) {
			ptr += 3;
			strlcpy(sc->serial_number_string, ptr,
			    sizeof(sc->serial_number_string));
		} else if (strncmp(ptr, "SN2=", 4) == 0) {
			/* SN2 takes precedence over SN */
			ptr += 4;
			found_sn2 = 1;
			strlcpy(sc->serial_number_string, ptr,
			    sizeof(sc->serial_number_string));
		}
		while (*ptr++ != '\0') {}
	}

	if (found_mac)
		return 0;

abort:
	device_printf(sc->dev, "failed to parse eeprom_strings\n");
	return ENXIO;
}

#if defined(__x86_64__)

static void
mxge_enable_nvidia_ecrc(mxge_softc_t *sc)
{
	uint32_t val;
	unsigned long base, off;
	char *va, *cfgptr;
	device_t pdev, mcp55;
	uint16_t vendor_id, device_id, word;
	uintptr_t bus, slot, func, ivend, idev;
	uint32_t *ptr32;

	if (!mxge_nvidia_ecrc_enable)
		return;

	pdev = device_get_parent(device_get_parent(sc->dev));
	if (pdev == NULL) {
		device_printf(sc->dev, "could not find parent?\n");
		return;
	}
	vendor_id = pci_read_config(pdev, PCIR_VENDOR, 2);
	device_id = pci_read_config(pdev, PCIR_DEVICE, 2);

	if (vendor_id != 0x10de)
		return;

	base = 0;

	if (device_id == 0x005d) {
		/* ck804, base address is magic */
		base = 0xe0000000UL;
	} else if (device_id >= 0x0374 && device_id <= 0x378) {
		/* mcp55, base address stored in chipset */
		mcp55 = pci_find_bsf(0, 0, 0);
		if (mcp55 &&
		    0x10de == pci_read_config(mcp55, PCIR_VENDOR, 2) &&
		    0x0369 == pci_read_config(mcp55, PCIR_DEVICE, 2)) {
			word = pci_read_config(mcp55, 0x90, 2);
			base = ((unsigned long)word & 0x7ffeU) << 25;
		}
	}
	if (!base)
		return;

	/*
	 * XXXX
	 * Test below is commented because it is believed that doing
	 * config read/write beyond 0xff will access the config space
	 * for the next larger function.  Uncomment this and remove 
	 * the hacky pmap_mapdev() way of accessing config space when
	 * DragonFly grows support for extended pcie config space access.
	 */
#if 0
	/*
	 * See if we can, by some miracle, access the extended
	 * config space
	 */
	val = pci_read_config(pdev, 0x178, 4);
	if (val != 0xffffffff) {
		val |= 0x40;
		pci_write_config(pdev, 0x178, val, 4);
		return;
	}
#endif
	/*
	 * Rather than using normal pci config space writes, we must
	 * map the Nvidia config space ourselves.  This is because on
	 * opteron/nvidia class machine the 0xe000000 mapping is
	 * handled by the nvidia chipset, that means the internal PCI
	 * device (the on-chip northbridge), or the amd-8131 bridge
	 * and things behind them are not visible by this method.
	 */

	BUS_READ_IVAR(device_get_parent(pdev), pdev,
		      PCI_IVAR_BUS, &bus);
	BUS_READ_IVAR(device_get_parent(pdev), pdev,
		      PCI_IVAR_SLOT, &slot);
	BUS_READ_IVAR(device_get_parent(pdev), pdev,
		      PCI_IVAR_FUNCTION, &func);
	BUS_READ_IVAR(device_get_parent(pdev), pdev,
		      PCI_IVAR_VENDOR, &ivend);
	BUS_READ_IVAR(device_get_parent(pdev), pdev,
		      PCI_IVAR_DEVICE, &idev);

	off =  base + 0x00100000UL * (unsigned long)bus +
	    0x00001000UL * (unsigned long)(func + 8 * slot);

	/* map it into the kernel */
	va = pmap_mapdev(trunc_page((vm_paddr_t)off), PAGE_SIZE);
	if (va == NULL) {
		device_printf(sc->dev, "pmap_kenter_temporary didn't\n");
		return;
	}
	/* get a pointer to the config space mapped into the kernel */
	cfgptr = va + (off & PAGE_MASK);

	/* make sure that we can really access it */
	vendor_id = *(uint16_t *)(cfgptr + PCIR_VENDOR);
	device_id = *(uint16_t *)(cfgptr + PCIR_DEVICE);
	if (!(vendor_id == ivend && device_id == idev)) {
		device_printf(sc->dev, "mapping failed: 0x%x:0x%x\n",
		    vendor_id, device_id);
		pmap_unmapdev((vm_offset_t)va, PAGE_SIZE);
		return;
	}

	ptr32 = (uint32_t*)(cfgptr + 0x178);
	val = *ptr32;

	if (val == 0xffffffff) {
		device_printf(sc->dev, "extended mapping failed\n");
		pmap_unmapdev((vm_offset_t)va, PAGE_SIZE);
		return;
	}
	*ptr32 = val | 0x40;
	pmap_unmapdev((vm_offset_t)va, PAGE_SIZE);
	if (bootverbose) {
		device_printf(sc->dev, "Enabled ECRC on upstream "
		    "Nvidia bridge at %d:%d:%d\n",
		    (int)bus, (int)slot, (int)func);
	}
}

#else	/* __x86_64__ */

static void
mxge_enable_nvidia_ecrc(mxge_softc_t *sc)
{
	device_printf(sc->dev, "Nforce 4 chipset on non-x86/x86_64!?!?!\n");
}

#endif

static int
mxge_dma_test(mxge_softc_t *sc, int test_type)
{
	mxge_cmd_t cmd;
	bus_addr_t dmatest_bus = sc->dmabench_dma.dmem_busaddr;
	int status;
	uint32_t len;
	const char *test = " ";

	/*
	 * Run a small DMA test.
	 * The magic multipliers to the length tell the firmware
	 * to do DMA read, write, or read+write tests.  The
	 * results are returned in cmd.data0.  The upper 16
	 * bits of the return is the number of transfers completed.
	 * The lower 16 bits is the time in 0.5us ticks that the
	 * transfers took to complete.
	 */

	len = sc->tx_boundary;

	cmd.data0 = MXGE_LOWPART_TO_U32(dmatest_bus);
	cmd.data1 = MXGE_HIGHPART_TO_U32(dmatest_bus);
	cmd.data2 = len * 0x10000;
	status = mxge_send_cmd(sc, test_type, &cmd);
	if (status != 0) {
		test = "read";
		goto abort;
	}
	sc->read_dma = ((cmd.data0>>16) * len * 2) / (cmd.data0 & 0xffff);

	cmd.data0 = MXGE_LOWPART_TO_U32(dmatest_bus);
	cmd.data1 = MXGE_HIGHPART_TO_U32(dmatest_bus);
	cmd.data2 = len * 0x1;
	status = mxge_send_cmd(sc, test_type, &cmd);
	if (status != 0) {
		test = "write";
		goto abort;
	}
	sc->write_dma = ((cmd.data0>>16) * len * 2) / (cmd.data0 & 0xffff);

	cmd.data0 = MXGE_LOWPART_TO_U32(dmatest_bus);
	cmd.data1 = MXGE_HIGHPART_TO_U32(dmatest_bus);
	cmd.data2 = len * 0x10001;
	status = mxge_send_cmd(sc, test_type, &cmd);
	if (status != 0) {
		test = "read/write";
		goto abort;
	}
	sc->read_write_dma = ((cmd.data0>>16) * len * 2 * 2) /
	    (cmd.data0 & 0xffff);

abort:
	if (status != 0 && test_type != MXGEFW_CMD_UNALIGNED_TEST) {
		device_printf(sc->dev, "DMA %s benchmark failed: %d\n",
		    test, status);
	}
	return status;
}

/*
 * The Lanai Z8E PCI-E interface achieves higher Read-DMA throughput
 * when the PCI-E Completion packets are aligned on an 8-byte
 * boundary.  Some PCI-E chip sets always align Completion packets; on
 * the ones that do not, the alignment can be enforced by enabling
 * ECRC generation (if supported).
 *
 * When PCI-E Completion packets are not aligned, it is actually more
 * efficient to limit Read-DMA transactions to 2KB, rather than 4KB.
 *
 * If the driver can neither enable ECRC nor verify that it has
 * already been enabled, then it must use a firmware image which works
 * around unaligned completion packets (ethp_z8e.dat), and it should
 * also ensure that it never gives the device a Read-DMA which is
 * larger than 2KB by setting the tx_boundary to 2KB.  If ECRC is
 * enabled, then the driver should use the aligned (eth_z8e.dat)
 * firmware image, and set tx_boundary to 4KB.
 */
static int
mxge_firmware_probe(mxge_softc_t *sc)
{
	device_t dev = sc->dev;
	int reg, status;
	uint16_t pectl;

	sc->tx_boundary = 4096;

	/*
	 * Verify the max read request size was set to 4KB
	 * before trying the test with 4KB.
	 */
	if (pci_find_extcap(dev, PCIY_EXPRESS, &reg) == 0) {
		pectl = pci_read_config(dev, reg + 0x8, 2);
		if ((pectl & (5 << 12)) != (5 << 12)) {
			device_printf(dev, "Max Read Req. size != 4k (0x%x)\n",
			    pectl);
			sc->tx_boundary = 2048;
		}
	}

	/*
	 * Load the optimized firmware (which assumes aligned PCIe
	 * completions) in order to see if it works on this host.
	 */
	sc->fw_name = mxge_fw_aligned;
	status = mxge_load_firmware(sc, 1);
	if (status != 0)
		return status;

	/*
	 * Enable ECRC if possible
	 */
	mxge_enable_nvidia_ecrc(sc);

	/* 
	 * Run a DMA test which watches for unaligned completions and
	 * aborts on the first one seen.  Not required on Z8ES or newer.
	 */
	if (pci_get_revid(sc->dev) >= MXGE_PCI_REV_Z8ES)
		return 0;

	status = mxge_dma_test(sc, MXGEFW_CMD_UNALIGNED_TEST);
	if (status == 0)
		return 0; /* keep the aligned firmware */

	if (status != E2BIG)
		device_printf(dev, "DMA test failed: %d\n", status);
	if (status == ENOSYS) {
		device_printf(dev, "Falling back to ethp! "
		    "Please install up to date fw\n");
	}
	return status;
}

static int
mxge_select_firmware(mxge_softc_t *sc)
{
	int aligned = 0;
	int force_firmware = mxge_force_firmware;

	if (sc->throttle)
		force_firmware = sc->throttle;

	if (force_firmware != 0) {
		if (force_firmware == 1)
			aligned = 1;
		else
			aligned = 0;
		if (bootverbose) {
			device_printf(sc->dev,
			    "Assuming %s completions (forced)\n",
			    aligned ? "aligned" : "unaligned");
		}
		goto abort;
	}

	/*
	 * If the PCIe link width is 4 or less, we can use the aligned
	 * firmware and skip any checks
	 */
	if (sc->link_width != 0 && sc->link_width <= 4) {
		device_printf(sc->dev, "PCIe x%d Link, "
		    "expect reduced performance\n", sc->link_width);
		aligned = 1;
		goto abort;
	}

	if (mxge_firmware_probe(sc) == 0)
		return 0;

abort:
	if (aligned) {
		sc->fw_name = mxge_fw_aligned;
		sc->tx_boundary = 4096;
	} else {
		sc->fw_name = mxge_fw_unaligned;
		sc->tx_boundary = 2048;
	}
	return mxge_load_firmware(sc, 0);
}

static int
mxge_validate_firmware(mxge_softc_t *sc, const mcp_gen_header_t *hdr)
{
	if (be32toh(hdr->mcp_type) != MCP_TYPE_ETH) {
		if_printf(sc->ifp, "Bad firmware type: 0x%x\n",
		    be32toh(hdr->mcp_type));
		return EIO;
	}

	/* Save firmware version for sysctl */
	strlcpy(sc->fw_version, hdr->version, sizeof(sc->fw_version));
	if (bootverbose)
		if_printf(sc->ifp, "firmware id: %s\n", hdr->version);

	ksscanf(sc->fw_version, "%d.%d.%d", &sc->fw_ver_major,
	    &sc->fw_ver_minor, &sc->fw_ver_tiny);

	if (!(sc->fw_ver_major == MXGEFW_VERSION_MAJOR &&
	      sc->fw_ver_minor == MXGEFW_VERSION_MINOR)) {
		if_printf(sc->ifp, "Found firmware version %s\n",
		    sc->fw_version);
		if_printf(sc->ifp, "Driver needs %d.%d\n",
		    MXGEFW_VERSION_MAJOR, MXGEFW_VERSION_MINOR);
		return EINVAL;
	}
	return 0;
}

static void *
z_alloc(void *nil, u_int items, u_int size)
{
	return kmalloc(items * size, M_TEMP, M_WAITOK);
}

static void
z_free(void *nil, void *ptr)
{
	kfree(ptr, M_TEMP);
}

static int
mxge_load_firmware_helper(mxge_softc_t *sc, uint32_t *limit)
{
	z_stream zs;
	char *inflate_buffer;
	const struct firmware *fw;
	const mcp_gen_header_t *hdr;
	unsigned hdr_offset;
	int status;
	unsigned int i;
	char dummy;
	size_t fw_len;

	fw = firmware_get(sc->fw_name);
	if (fw == NULL) {
		if_printf(sc->ifp, "Could not find firmware image %s\n",
		    sc->fw_name);
		return ENOENT;
	}

	/* Setup zlib and decompress f/w */
	bzero(&zs, sizeof(zs));
	zs.zalloc = z_alloc;
	zs.zfree = z_free;
	status = inflateInit(&zs);
	if (status != Z_OK) {
		status = EIO;
		goto abort_with_fw;
	}

	/*
	 * The uncompressed size is stored as the firmware version,
	 * which would otherwise go unused
	 */
	fw_len = (size_t)fw->version;
	inflate_buffer = kmalloc(fw_len, M_TEMP, M_WAITOK);
	zs.avail_in = fw->datasize;
	zs.next_in = __DECONST(char *, fw->data);
	zs.avail_out = fw_len;
	zs.next_out = inflate_buffer;
	status = inflate(&zs, Z_FINISH);
	if (status != Z_STREAM_END) {
		if_printf(sc->ifp, "zlib %d\n", status);
		status = EIO;
		goto abort_with_buffer;
	}

	/* Check id */
	hdr_offset =
	htobe32(*(const uint32_t *)(inflate_buffer + MCP_HEADER_PTR_OFFSET));
	if ((hdr_offset & 3) || hdr_offset + sizeof(*hdr) > fw_len) {
		if_printf(sc->ifp, "Bad firmware file");
		status = EIO;
		goto abort_with_buffer;
	}
	hdr = (const void*)(inflate_buffer + hdr_offset);

	status = mxge_validate_firmware(sc, hdr);
	if (status != 0)
		goto abort_with_buffer;

	/* Copy the inflated firmware to NIC SRAM. */
	for (i = 0; i < fw_len; i += 256) {
		mxge_pio_copy(sc->sram + MXGE_FW_OFFSET + i, inflate_buffer + i,
		    min(256U, (unsigned)(fw_len - i)));
		wmb();
		dummy = *sc->sram;
		wmb();
	}

	*limit = fw_len;
	status = 0;
abort_with_buffer:
	kfree(inflate_buffer, M_TEMP);
	inflateEnd(&zs);
abort_with_fw:
	firmware_put(fw, FIRMWARE_UNLOAD);
	return status;
}

/*
 * Enable or disable periodic RDMAs from the host to make certain
 * chipsets resend dropped PCIe messages
 */
static void
mxge_dummy_rdma(mxge_softc_t *sc, int enable)
{
	char buf_bytes[72];
	volatile uint32_t *confirm;
	volatile char *submit;
	uint32_t *buf, dma_low, dma_high;
	int i;

	buf = (uint32_t *)((unsigned long)(buf_bytes + 7) & ~7UL);

	/* Clear confirmation addr */
	confirm = (volatile uint32_t *)sc->cmd;
	*confirm = 0;
	wmb();

	/*
	 * Send an rdma command to the PCIe engine, and wait for the
	 * response in the confirmation address.  The firmware should
	 * write a -1 there to indicate it is alive and well
	 */
	dma_low = MXGE_LOWPART_TO_U32(sc->cmd_dma.dmem_busaddr);
	dma_high = MXGE_HIGHPART_TO_U32(sc->cmd_dma.dmem_busaddr);
	buf[0] = htobe32(dma_high);		/* confirm addr MSW */
	buf[1] = htobe32(dma_low);		/* confirm addr LSW */
	buf[2] = htobe32(0xffffffff);		/* confirm data */
	dma_low = MXGE_LOWPART_TO_U32(sc->zeropad_dma.dmem_busaddr);
	dma_high = MXGE_HIGHPART_TO_U32(sc->zeropad_dma.dmem_busaddr);
	buf[3] = htobe32(dma_high); 		/* dummy addr MSW */
	buf[4] = htobe32(dma_low); 		/* dummy addr LSW */
	buf[5] = htobe32(enable);		/* enable? */

	submit = (volatile char *)(sc->sram + MXGEFW_BOOT_DUMMY_RDMA);

	mxge_pio_copy(submit, buf, 64);
	wmb();
	DELAY(1000);
	wmb();
	i = 0;
	while (*confirm != 0xffffffff && i < 20) {
		DELAY(1000);
		i++;
	}
	if (*confirm != 0xffffffff) {
		if_printf(sc->ifp, "dummy rdma %s failed (%p = 0x%x)",
		    (enable ? "enable" : "disable"), confirm, *confirm);
	}
}

static int 
mxge_send_cmd(mxge_softc_t *sc, uint32_t cmd, mxge_cmd_t *data)
{
	mcp_cmd_t *buf;
	char buf_bytes[sizeof(*buf) + 8];
	volatile mcp_cmd_response_t *response = sc->cmd;
	volatile char *cmd_addr = sc->sram + MXGEFW_ETH_CMD;
	uint32_t dma_low, dma_high;
	int err, sleep_total = 0;

	/* Ensure buf is aligned to 8 bytes */
	buf = (mcp_cmd_t *)((unsigned long)(buf_bytes + 7) & ~7UL);

	buf->data0 = htobe32(data->data0);
	buf->data1 = htobe32(data->data1);
	buf->data2 = htobe32(data->data2);
	buf->cmd = htobe32(cmd);
	dma_low = MXGE_LOWPART_TO_U32(sc->cmd_dma.dmem_busaddr);
	dma_high = MXGE_HIGHPART_TO_U32(sc->cmd_dma.dmem_busaddr);

	buf->response_addr.low = htobe32(dma_low);
	buf->response_addr.high = htobe32(dma_high);

	response->result = 0xffffffff;
	wmb();
	mxge_pio_copy((volatile void *)cmd_addr, buf, sizeof (*buf));

	/*
	 * Wait up to 20ms
	 */
	err = EAGAIN;
	for (sleep_total = 0; sleep_total < 20; sleep_total++) {
		wmb();
		switch (be32toh(response->result)) {
		case 0:
			data->data0 = be32toh(response->data);
			err = 0;
			break;
		case 0xffffffff:
			DELAY(1000);
			break;
		case MXGEFW_CMD_UNKNOWN:
			err = ENOSYS;
			break;
		case MXGEFW_CMD_ERROR_UNALIGNED:
			err = E2BIG;
			break;
		case MXGEFW_CMD_ERROR_BUSY:
			err = EBUSY;
			break;
		case MXGEFW_CMD_ERROR_I2C_ABSENT:
			err = ENXIO;
			break;
		default:
			if_printf(sc->ifp, "command %d failed, result = %d\n",
			    cmd, be32toh(response->result));
			err = ENXIO;
			break;
		}
		if (err != EAGAIN)
			break;
	}
	if (err == EAGAIN) {
		if_printf(sc->ifp, "command %d timed out result = %d\n",
		    cmd, be32toh(response->result));
	}
	return err;
}

static int
mxge_adopt_running_firmware(mxge_softc_t *sc)
{
	struct mcp_gen_header *hdr;
	const size_t bytes = sizeof(struct mcp_gen_header);
	size_t hdr_offset;
	int status;

	/*
	 * Find running firmware header
	 */
	hdr_offset =
	htobe32(*(volatile uint32_t *)(sc->sram + MCP_HEADER_PTR_OFFSET));

	if ((hdr_offset & 3) || hdr_offset + sizeof(*hdr) > sc->sram_size) {
		if_printf(sc->ifp, "Running firmware has bad header offset "
		    "(%zu)\n", hdr_offset);
		return EIO;
	}

	/*
	 * Copy header of running firmware from SRAM to host memory to
	 * validate firmware
	 */
	hdr = kmalloc(bytes, M_DEVBUF, M_WAITOK);
	bus_space_read_region_1(rman_get_bustag(sc->mem_res),
	    rman_get_bushandle(sc->mem_res), hdr_offset, (char *)hdr, bytes);
	status = mxge_validate_firmware(sc, hdr);
	kfree(hdr, M_DEVBUF);

	/* 
	 * Check to see if adopted firmware has bug where adopting
	 * it will cause broadcasts to be filtered unless the NIC
	 * is kept in ALLMULTI mode
	 */
	if (sc->fw_ver_major == 1 && sc->fw_ver_minor == 4 &&
	    sc->fw_ver_tiny >= 4 && sc->fw_ver_tiny <= 11) {
		sc->adopted_rx_filter_bug = 1;
		if_printf(sc->ifp, "Adopting fw %d.%d.%d: "
		    "working around rx filter bug\n",
		    sc->fw_ver_major, sc->fw_ver_minor, sc->fw_ver_tiny);
	}

	return status;
}

static int
mxge_load_firmware(mxge_softc_t *sc, int adopt)
{
	volatile uint32_t *confirm;
	volatile char *submit;
	char buf_bytes[72];
	uint32_t *buf, size, dma_low, dma_high;
	int status, i;

	buf = (uint32_t *)((unsigned long)(buf_bytes + 7) & ~7UL);

	size = sc->sram_size;
	status = mxge_load_firmware_helper(sc, &size);
	if (status) {
		if (!adopt)
			return status;

		/*
		 * Try to use the currently running firmware, if
		 * it is new enough
		 */
		status = mxge_adopt_running_firmware(sc);
		if (status) {
			if_printf(sc->ifp,
			    "failed to adopt running firmware\n");
			return status;
		}
		if_printf(sc->ifp, "Successfully adopted running firmware\n");

		if (sc->tx_boundary == 4096) {
			if_printf(sc->ifp,
			     "Using firmware currently running on NIC.  "
			     "For optimal\n");
			if_printf(sc->ifp, "performance consider loading "
			     "optimized firmware\n");
		}
		sc->fw_name = mxge_fw_unaligned;
		sc->tx_boundary = 2048;
		return 0;
	}

	/* Clear confirmation addr */
	confirm = (volatile uint32_t *)sc->cmd;
	*confirm = 0;
	wmb();

	/*
	 * Send a reload command to the bootstrap MCP, and wait for the
	 * response in the confirmation address.  The firmware should
	 * write a -1 there to indicate it is alive and well
	 */

	dma_low = MXGE_LOWPART_TO_U32(sc->cmd_dma.dmem_busaddr);
	dma_high = MXGE_HIGHPART_TO_U32(sc->cmd_dma.dmem_busaddr);

	buf[0] = htobe32(dma_high);	/* confirm addr MSW */
	buf[1] = htobe32(dma_low);	/* confirm addr LSW */
	buf[2] = htobe32(0xffffffff);	/* confirm data */

	/*
	 * FIX: All newest firmware should un-protect the bottom of
	 * the sram before handoff. However, the very first interfaces
	 * do not. Therefore the handoff copy must skip the first 8 bytes
	 */
					/* where the code starts*/
	buf[3] = htobe32(MXGE_FW_OFFSET + 8);
	buf[4] = htobe32(size - 8); 	/* length of code */
	buf[5] = htobe32(8);		/* where to copy to */
	buf[6] = htobe32(0);		/* where to jump to */

	submit = (volatile char *)(sc->sram + MXGEFW_BOOT_HANDOFF);
	mxge_pio_copy(submit, buf, 64);
	wmb();
	DELAY(1000);
	wmb();
	i = 0;
	while (*confirm != 0xffffffff && i < 20) {
		DELAY(1000*10);
		i++;
	}
	if (*confirm != 0xffffffff) {
		if_printf(sc->ifp,"handoff failed (%p = 0x%x)", 
		    confirm, *confirm);
		return ENXIO;
	}
	return 0;
}

static int
mxge_update_mac_address(mxge_softc_t *sc)
{
	mxge_cmd_t cmd;
	uint8_t *addr = sc->mac_addr;

	cmd.data0 = (addr[0] << 24) | (addr[1] << 16) |
	    (addr[2] << 8) | addr[3];
	cmd.data1 = (addr[4] << 8) | (addr[5]);
	return mxge_send_cmd(sc, MXGEFW_SET_MAC_ADDRESS, &cmd);
}

static int
mxge_change_pause(mxge_softc_t *sc, int pause)
{	
	mxge_cmd_t cmd;
	int status;

	bzero(&cmd, sizeof(cmd));	/* silence gcc warning */
	if (pause)
		status = mxge_send_cmd(sc, MXGEFW_ENABLE_FLOW_CONTROL, &cmd);
	else
		status = mxge_send_cmd(sc, MXGEFW_DISABLE_FLOW_CONTROL, &cmd);
	if (status) {
		if_printf(sc->ifp, "Failed to set flow control mode\n");
		return ENXIO;
	}
	sc->pause = pause;
	return 0;
}

static void
mxge_change_promisc(mxge_softc_t *sc, int promisc)
{	
	mxge_cmd_t cmd;
	int status;

	bzero(&cmd, sizeof(cmd));	/* avoid gcc warning */
	if (mxge_always_promisc)
		promisc = 1;

	if (promisc)
		status = mxge_send_cmd(sc, MXGEFW_ENABLE_PROMISC, &cmd);
	else
		status = mxge_send_cmd(sc, MXGEFW_DISABLE_PROMISC, &cmd);
	if (status)
		if_printf(sc->ifp, "Failed to set promisc mode\n");
}

static void
mxge_set_multicast_list(mxge_softc_t *sc)
{
	mxge_cmd_t cmd;
	struct ifmultiaddr *ifma;
	struct ifnet *ifp = sc->ifp;
	int err;

	/* This firmware is known to not support multicast */
	if (!sc->fw_multicast_support)
		return;

	/* Disable multicast filtering while we play with the lists*/
	bzero(&cmd, sizeof(cmd));	/* silence gcc warning */
	err = mxge_send_cmd(sc, MXGEFW_ENABLE_ALLMULTI, &cmd);
	if (err != 0) {
		if_printf(ifp, "Failed MXGEFW_ENABLE_ALLMULTI, "
		    "error status: %d\n", err);
		return;
	}

	if (sc->adopted_rx_filter_bug)
		return;
	
	if (ifp->if_flags & IFF_ALLMULTI) {
		/* Request to disable multicast filtering, so quit here */
		return;
	}

	/* Flush all the filters */
	err = mxge_send_cmd(sc, MXGEFW_LEAVE_ALL_MULTICAST_GROUPS, &cmd);
	if (err != 0) {
		if_printf(ifp, "Failed MXGEFW_LEAVE_ALL_MULTICAST_GROUPS, "
		    "error status: %d\n", err);
		return;
	}

	/*
	 * Walk the multicast list, and add each address
	 */
	TAILQ_FOREACH(ifma, &ifp->if_multiaddrs, ifma_link) {
		if (ifma->ifma_addr->sa_family != AF_LINK)
			continue;

		bcopy(LLADDR((struct sockaddr_dl *)ifma->ifma_addr),
		    &cmd.data0, 4);
		bcopy(LLADDR((struct sockaddr_dl *)ifma->ifma_addr) + 4,
		    &cmd.data1, 2);
		cmd.data0 = htonl(cmd.data0);
		cmd.data1 = htonl(cmd.data1);
		err = mxge_send_cmd(sc, MXGEFW_JOIN_MULTICAST_GROUP, &cmd);
		if (err != 0) {
			if_printf(ifp, "Failed MXGEFW_JOIN_MULTICAST_GROUP, "
			    "error status: %d\n", err);
			/* Abort, leaving multicast filtering off */
			return;
		}
	}

	/* Enable multicast filtering */
	err = mxge_send_cmd(sc, MXGEFW_DISABLE_ALLMULTI, &cmd);
	if (err != 0) {
		if_printf(ifp, "Failed MXGEFW_DISABLE_ALLMULTI, "
		    "error status: %d\n", err);
	}
}

#if 0
static int
mxge_max_mtu(mxge_softc_t *sc)
{
	mxge_cmd_t cmd;
	int status;

	if (MJUMPAGESIZE - MXGEFW_PAD >  MXGEFW_MAX_MTU)
		return  MXGEFW_MAX_MTU - MXGEFW_PAD;

	/* try to set nbufs to see if it we can
	   use virtually contiguous jumbos */
	cmd.data0 = 0;
	status = mxge_send_cmd(sc, MXGEFW_CMD_ALWAYS_USE_N_BIG_BUFFERS,
			       &cmd);
	if (status == 0)
		return  MXGEFW_MAX_MTU - MXGEFW_PAD;

	/* otherwise, we're limited to MJUMPAGESIZE */
	return MJUMPAGESIZE - MXGEFW_PAD;
}
#endif

static int
mxge_reset(mxge_softc_t *sc, int interrupts_setup)
{
	struct mxge_slice_state *ss;
	mxge_rx_done_t *rx_done;
	volatile uint32_t *irq_claim;
	mxge_cmd_t cmd;
	int slice, status, rx_intr_size;

	/*
	 * Try to send a reset command to the card to see if it
	 * is alive
	 */
	memset(&cmd, 0, sizeof (cmd));
	status = mxge_send_cmd(sc, MXGEFW_CMD_RESET, &cmd);
	if (status != 0) {
		if_printf(sc->ifp, "failed reset\n");
		return ENXIO;
	}

	mxge_dummy_rdma(sc, 1);

	/*
	 * Set the intrq size
	 * XXX assume 4byte mcp_slot
	 */
	rx_intr_size = sc->rx_intr_slots * sizeof(mcp_slot_t);
	cmd.data0 = rx_intr_size;
	status = mxge_send_cmd(sc, MXGEFW_CMD_SET_INTRQ_SIZE, &cmd);

	/*
	 * Even though we already know how many slices are supported
	 * via mxge_slice_probe(), MXGEFW_CMD_GET_MAX_RSS_QUEUES
	 * has magic side effects, and must be called after a reset.
	 * It must be called prior to calling any RSS related cmds,
	 * including assigning an interrupt queue for anything but
	 * slice 0.  It must also be called *after*
	 * MXGEFW_CMD_SET_INTRQ_SIZE, since the intrq size is used by
	 * the firmware to compute offsets.
	 */
	if (sc->num_slices > 1) {
		/* Ask the maximum number of slices it supports */
		status = mxge_send_cmd(sc, MXGEFW_CMD_GET_MAX_RSS_QUEUES, &cmd);
		if (status != 0) {
			if_printf(sc->ifp, "failed to get number of slices\n");
			return status;
		}

		/* 
		 * MXGEFW_CMD_ENABLE_RSS_QUEUES must be called prior
		 * to setting up the interrupt queue DMA
		 */
		cmd.data0 = sc->num_slices;
		cmd.data1 = MXGEFW_SLICE_INTR_MODE_ONE_PER_SLICE;
		if (sc->num_tx_rings > 1)
			cmd.data1 |= MXGEFW_SLICE_ENABLE_MULTIPLE_TX_QUEUES;
		status = mxge_send_cmd(sc, MXGEFW_CMD_ENABLE_RSS_QUEUES, &cmd);
		if (status != 0) {
			if_printf(sc->ifp, "failed to set number of slices\n");
			return status;
		}
	}

	if (interrupts_setup) {
		/* Now exchange information about interrupts  */
		for (slice = 0; slice < sc->num_slices; slice++) {
			ss = &sc->ss[slice];

			rx_done = &ss->rx_data.rx_done;
			memset(rx_done->entry, 0, rx_intr_size);

			cmd.data0 =
			    MXGE_LOWPART_TO_U32(ss->rx_done_dma.dmem_busaddr);
			cmd.data1 =
			    MXGE_HIGHPART_TO_U32(ss->rx_done_dma.dmem_busaddr);
			cmd.data2 = slice;
			status |= mxge_send_cmd(sc, MXGEFW_CMD_SET_INTRQ_DMA,
			    &cmd);
		}
	}

	status |= mxge_send_cmd(sc, MXGEFW_CMD_GET_INTR_COAL_DELAY_OFFSET,
	    &cmd);
	sc->intr_coal_delay_ptr = (volatile uint32_t *)(sc->sram + cmd.data0);

	status |= mxge_send_cmd(sc, MXGEFW_CMD_GET_IRQ_ACK_OFFSET, &cmd);
	irq_claim = (volatile uint32_t *)(sc->sram + cmd.data0);

	status |= mxge_send_cmd(sc,  MXGEFW_CMD_GET_IRQ_DEASSERT_OFFSET, &cmd);
	sc->irq_deassert = (volatile uint32_t *)(sc->sram + cmd.data0);

	if (status != 0) {
		if_printf(sc->ifp, "failed set interrupt parameters\n");
		return status;
	}

	*sc->intr_coal_delay_ptr = htobe32(sc->intr_coal_delay);

	/* Run a DMA benchmark */
	mxge_dma_test(sc, MXGEFW_DMA_TEST);

	for (slice = 0; slice < sc->num_slices; slice++) {
		ss = &sc->ss[slice];

		ss->irq_claim = irq_claim + (2 * slice);

		/* Reset mcp/driver shared state back to 0 */
		ss->rx_data.rx_done.idx = 0;
		ss->tx.req = 0;
		ss->tx.done = 0;
		ss->tx.pkt_done = 0;
		ss->tx.queue_active = 0;
		ss->tx.activate = 0;
		ss->tx.deactivate = 0;
		ss->rx_data.rx_big.cnt = 0;
		ss->rx_data.rx_small.cnt = 0;
		if (ss->fw_stats != NULL)
			bzero(ss->fw_stats, sizeof(*ss->fw_stats));
	}
	sc->rdma_tags_available = 15;

	status = mxge_update_mac_address(sc);
	mxge_change_promisc(sc, sc->ifp->if_flags & IFF_PROMISC);
	mxge_change_pause(sc, sc->pause);
	mxge_set_multicast_list(sc);

	if (sc->throttle) {
		cmd.data0 = sc->throttle;
		if (mxge_send_cmd(sc, MXGEFW_CMD_SET_THROTTLE_FACTOR, &cmd))
			if_printf(sc->ifp, "can't enable throttle\n");
	}
	return status;
}

static int
mxge_change_throttle(SYSCTL_HANDLER_ARGS)
{
	mxge_cmd_t cmd;
	mxge_softc_t *sc;
	int err;
	unsigned int throttle;

	sc = arg1;
	throttle = sc->throttle;
	err = sysctl_handle_int(oidp, &throttle, arg2, req);
	if (err != 0)
		return err;

	if (throttle == sc->throttle)
		return 0;

	if (throttle < MXGE_MIN_THROTTLE || throttle > MXGE_MAX_THROTTLE)
		return EINVAL;

	ifnet_serialize_all(sc->ifp);

	cmd.data0 = throttle;
	err = mxge_send_cmd(sc, MXGEFW_CMD_SET_THROTTLE_FACTOR, &cmd);
	if (err == 0)
		sc->throttle = throttle;

	ifnet_deserialize_all(sc->ifp);
	return err;
}

static int
mxge_change_use_rss(SYSCTL_HANDLER_ARGS)
{
	mxge_softc_t *sc;
	int err, use_rss;

	sc = arg1;
	use_rss = sc->use_rss;
	err = sysctl_handle_int(oidp, &use_rss, arg2, req);
	if (err != 0)
		return err;

	if (use_rss == sc->use_rss)
		return 0;

	ifnet_serialize_all(sc->ifp);

	sc->use_rss = use_rss;
	if (sc->ifp->if_flags & IFF_RUNNING) {
		mxge_close(sc, 0);
		mxge_open(sc);
	}

	ifnet_deserialize_all(sc->ifp);
	return err;
}

static int
mxge_change_intr_coal(SYSCTL_HANDLER_ARGS)
{
	mxge_softc_t *sc;
	unsigned int intr_coal_delay;
	int err;

	sc = arg1;
	intr_coal_delay = sc->intr_coal_delay;
	err = sysctl_handle_int(oidp, &intr_coal_delay, arg2, req);
	if (err != 0)
		return err;

	if (intr_coal_delay == sc->intr_coal_delay)
		return 0;

	if (intr_coal_delay == 0 || intr_coal_delay > 1000*1000)
		return EINVAL;

	ifnet_serialize_all(sc->ifp);

	*sc->intr_coal_delay_ptr = htobe32(intr_coal_delay);
	sc->intr_coal_delay = intr_coal_delay;

	ifnet_deserialize_all(sc->ifp);
	return err;
}

static int
mxge_handle_be32(SYSCTL_HANDLER_ARGS)
{
	int err;

	if (arg1 == NULL)
		return EFAULT;
	arg2 = be32toh(*(int *)arg1);
	arg1 = NULL;
	err = sysctl_handle_int(oidp, arg1, arg2, req);

	return err;
}

static void
mxge_rem_sysctls(mxge_softc_t *sc)
{
	if (sc->ss != NULL) {
		struct mxge_slice_state *ss;
		int slice;

		for (slice = 0; slice < sc->num_slices; slice++) {
			ss = &sc->ss[slice];
			if (ss->sysctl_tree != NULL) {
				sysctl_ctx_free(&ss->sysctl_ctx);
				ss->sysctl_tree = NULL;
			}
		}
	}

	if (sc->slice_sysctl_tree != NULL) {
		sysctl_ctx_free(&sc->slice_sysctl_ctx);
		sc->slice_sysctl_tree = NULL;
	}
}

static void
mxge_add_sysctls(mxge_softc_t *sc)
{
	struct sysctl_ctx_list *ctx;
	struct sysctl_oid_list *children;
	mcp_irq_data_t *fw;
	struct mxge_slice_state *ss;
	int slice;
	char slice_num[8];

	ctx = device_get_sysctl_ctx(sc->dev);
	children = SYSCTL_CHILDREN(device_get_sysctl_tree(sc->dev));
	fw = sc->ss[0].fw_stats;

	/*
	 * Random information
	 */
	SYSCTL_ADD_STRING(ctx, children, OID_AUTO, "firmware_version",
	    CTLFLAG_RD, &sc->fw_version, 0, "firmware version");

	SYSCTL_ADD_STRING(ctx, children, OID_AUTO, "serial_number",
	    CTLFLAG_RD, &sc->serial_number_string, 0, "serial number");

	SYSCTL_ADD_STRING(ctx, children, OID_AUTO, "product_code",
	    CTLFLAG_RD, &sc->product_code_string, 0, "product code");

	SYSCTL_ADD_INT(ctx, children, OID_AUTO, "pcie_link_width",
	    CTLFLAG_RD, &sc->link_width, 0, "link width");

	SYSCTL_ADD_INT(ctx, children, OID_AUTO, "tx_boundary",
	    CTLFLAG_RD, &sc->tx_boundary, 0, "tx boundary");

	SYSCTL_ADD_INT(ctx, children, OID_AUTO, "write_combine",
	    CTLFLAG_RD, &sc->wc, 0, "write combining PIO");

	SYSCTL_ADD_INT(ctx, children, OID_AUTO, "read_dma_MBs",
	    CTLFLAG_RD, &sc->read_dma, 0, "DMA Read speed in MB/s");

	SYSCTL_ADD_INT(ctx, children, OID_AUTO, "write_dma_MBs",
	    CTLFLAG_RD, &sc->write_dma, 0, "DMA Write speed in MB/s");

	SYSCTL_ADD_INT(ctx, children, OID_AUTO, "read_write_dma_MBs",
	    CTLFLAG_RD, &sc->read_write_dma, 0,
	    "DMA concurrent Read/Write speed in MB/s");

	SYSCTL_ADD_INT(ctx, children, OID_AUTO, "watchdog_resets",
	    CTLFLAG_RD, &sc->watchdog_resets, 0,
	    "Number of times NIC was reset");

	if (sc->num_slices > 1) {
		SYSCTL_ADD_PROC(ctx, children, OID_AUTO, "slice_cpumap",
		    CTLTYPE_OPAQUE | CTLFLAG_RD, sc->ring_map, 0,
		    if_ringmap_cpumap_sysctl, "I", "slice CPU map");
	}

	/*
	 * Performance related tunables
	 */
	SYSCTL_ADD_PROC(ctx, children, OID_AUTO, "intr_coal_delay",
	    CTLTYPE_INT|CTLFLAG_RW, sc, 0, mxge_change_intr_coal, "I",
	    "Interrupt coalescing delay in usecs");

	SYSCTL_ADD_PROC(ctx, children, OID_AUTO, "throttle",
	    CTLTYPE_INT|CTLFLAG_RW, sc, 0, mxge_change_throttle, "I",
	    "Transmit throttling");

	SYSCTL_ADD_PROC(ctx, children, OID_AUTO, "use_rss",
	    CTLTYPE_INT|CTLFLAG_RW, sc, 0, mxge_change_use_rss, "I",
	    "Use RSS");

	SYSCTL_ADD_INT(ctx, children, OID_AUTO, "deassert_wait",
	    CTLFLAG_RW, &mxge_deassert_wait, 0,
	    "Wait for IRQ line to go low in ihandler");

	/*
	 * Stats block from firmware is in network byte order.
	 * Need to swap it
	 */
	SYSCTL_ADD_PROC(ctx, children, OID_AUTO, "link_up",
	    CTLTYPE_INT|CTLFLAG_RD, &fw->link_up, 0,
	    mxge_handle_be32, "I", "link up");

	SYSCTL_ADD_PROC(ctx, children, OID_AUTO, "rdma_tags_available",
	    CTLTYPE_INT|CTLFLAG_RD, &fw->rdma_tags_available, 0,
	    mxge_handle_be32, "I", "rdma_tags_available");

	SYSCTL_ADD_PROC(ctx, children, OID_AUTO, "dropped_bad_crc32",
	    CTLTYPE_INT|CTLFLAG_RD, &fw->dropped_bad_crc32, 0,
	    mxge_handle_be32, "I", "dropped_bad_crc32");

	SYSCTL_ADD_PROC(ctx, children, OID_AUTO, "dropped_bad_phy",
	    CTLTYPE_INT|CTLFLAG_RD, &fw->dropped_bad_phy, 0,
	    mxge_handle_be32, "I", "dropped_bad_phy");

	SYSCTL_ADD_PROC(ctx, children, OID_AUTO, "dropped_link_error_or_filtered",
	    CTLTYPE_INT|CTLFLAG_RD, &fw->dropped_link_error_or_filtered, 0,
	    mxge_handle_be32, "I", "dropped_link_error_or_filtered");

	SYSCTL_ADD_PROC(ctx, children, OID_AUTO, "dropped_link_overflow",
	    CTLTYPE_INT|CTLFLAG_RD, &fw->dropped_link_overflow, 0,
	    mxge_handle_be32, "I", "dropped_link_overflow");

	SYSCTL_ADD_PROC(ctx, children, OID_AUTO, "dropped_multicast_filtered",
	    CTLTYPE_INT|CTLFLAG_RD, &fw->dropped_multicast_filtered, 0,
	    mxge_handle_be32, "I", "dropped_multicast_filtered");

	SYSCTL_ADD_PROC(ctx, children, OID_AUTO, "dropped_no_big_buffer",
	    CTLTYPE_INT|CTLFLAG_RD, &fw->dropped_no_big_buffer, 0,
	    mxge_handle_be32, "I", "dropped_no_big_buffer");

	SYSCTL_ADD_PROC(ctx, children, OID_AUTO, "dropped_no_small_buffer",
	    CTLTYPE_INT|CTLFLAG_RD, &fw->dropped_no_small_buffer, 0,
	    mxge_handle_be32, "I", "dropped_no_small_buffer");

	SYSCTL_ADD_PROC(ctx, children, OID_AUTO, "dropped_overrun",
	    CTLTYPE_INT|CTLFLAG_RD, &fw->dropped_overrun, 0,
	    mxge_handle_be32, "I", "dropped_overrun");

	SYSCTL_ADD_PROC(ctx, children, OID_AUTO, "dropped_pause",
	    CTLTYPE_INT|CTLFLAG_RD, &fw->dropped_pause, 0,
	    mxge_handle_be32, "I", "dropped_pause");

	SYSCTL_ADD_PROC(ctx, children, OID_AUTO, "dropped_runt",
	    CTLTYPE_INT|CTLFLAG_RD, &fw->dropped_runt, 0,
	    mxge_handle_be32, "I", "dropped_runt");

	SYSCTL_ADD_PROC(ctx, children, OID_AUTO, "dropped_unicast_filtered",
	    CTLTYPE_INT|CTLFLAG_RD, &fw->dropped_unicast_filtered, 0,
	    mxge_handle_be32, "I", "dropped_unicast_filtered");

	/* add counters exported for debugging from all slices */
	sysctl_ctx_init(&sc->slice_sysctl_ctx);
	sc->slice_sysctl_tree = SYSCTL_ADD_NODE(&sc->slice_sysctl_ctx,
	    children, OID_AUTO, "slice", CTLFLAG_RD, 0, "");
	if (sc->slice_sysctl_tree == NULL) {
		device_printf(sc->dev, "can't add slice sysctl node\n");
		return;
	}

	for (slice = 0; slice < sc->num_slices; slice++) {
		ss = &sc->ss[slice];
		sysctl_ctx_init(&ss->sysctl_ctx);
		ctx = &ss->sysctl_ctx;
		children = SYSCTL_CHILDREN(sc->slice_sysctl_tree);
		ksprintf(slice_num, "%d", slice);
		ss->sysctl_tree = SYSCTL_ADD_NODE(ctx, children, OID_AUTO,
		    slice_num, CTLFLAG_RD, 0, "");
		if (ss->sysctl_tree == NULL) {
			device_printf(sc->dev,
			    "can't add %d slice sysctl node\n", slice);
			return;	/* XXX continue? */
		}
		children = SYSCTL_CHILDREN(ss->sysctl_tree);

		/*
		 * XXX change to ULONG
		 */

		SYSCTL_ADD_INT(ctx, children, OID_AUTO, "rx_small_cnt",
		    CTLFLAG_RD, &ss->rx_data.rx_small.cnt, 0, "rx_small_cnt");

		SYSCTL_ADD_INT(ctx, children, OID_AUTO, "rx_big_cnt",
		    CTLFLAG_RD, &ss->rx_data.rx_big.cnt, 0, "rx_small_cnt");

		SYSCTL_ADD_INT(ctx, children, OID_AUTO, "tx_req",
		    CTLFLAG_RD, &ss->tx.req, 0, "tx_req");

		SYSCTL_ADD_INT(ctx, children, OID_AUTO, "tx_done",
		    CTLFLAG_RD, &ss->tx.done, 0, "tx_done");

		SYSCTL_ADD_INT(ctx, children, OID_AUTO, "tx_pkt_done",
		    CTLFLAG_RD, &ss->tx.pkt_done, 0, "tx_done");

		SYSCTL_ADD_INT(ctx, children, OID_AUTO, "tx_queue_active",
		    CTLFLAG_RD, &ss->tx.queue_active, 0, "tx_queue_active");

		SYSCTL_ADD_INT(ctx, children, OID_AUTO, "tx_activate",
		    CTLFLAG_RD, &ss->tx.activate, 0, "tx_activate");

		SYSCTL_ADD_INT(ctx, children, OID_AUTO, "tx_deactivate",
		    CTLFLAG_RD, &ss->tx.deactivate, 0, "tx_deactivate");
	}
}

/*
 * Copy an array of mcp_kreq_ether_send_t's to the mcp.  Copy 
 * backwards one at a time and handle ring wraps
 */
static __inline void 
mxge_submit_req_backwards(mxge_tx_ring_t *tx,
    mcp_kreq_ether_send_t *src, int cnt)
{
	int idx, starting_slot;

	starting_slot = tx->req;
	while (cnt > 1) {
		cnt--;
		idx = (starting_slot + cnt) & tx->mask;
		mxge_pio_copy(&tx->lanai[idx], &src[cnt], sizeof(*src));
		wmb();
	}
}

/*
 * Copy an array of mcp_kreq_ether_send_t's to the mcp.  Copy
 * at most 32 bytes at a time, so as to avoid involving the software
 * pio handler in the nic.  We re-write the first segment's flags
 * to mark them valid only after writing the entire chain 
 */
static __inline void 
mxge_submit_req(mxge_tx_ring_t *tx, mcp_kreq_ether_send_t *src, int cnt)
{
	int idx, i;
	uint32_t *src_ints;
	volatile uint32_t *dst_ints;
	mcp_kreq_ether_send_t *srcp;
	volatile mcp_kreq_ether_send_t *dstp, *dst;
	uint8_t last_flags;

	idx = tx->req & tx->mask;

	last_flags = src->flags;
	src->flags = 0;
	wmb();
	dst = dstp = &tx->lanai[idx];
	srcp = src;

	if ((idx + cnt) < tx->mask) {
		for (i = 0; i < cnt - 1; i += 2) {
			mxge_pio_copy(dstp, srcp, 2 * sizeof(*src));
			wmb(); /* force write every 32 bytes */
			srcp += 2;
			dstp += 2;
		}
	} else {
		/*
		 * Submit all but the first request, and ensure 
		 * that it is submitted below
		 */
		mxge_submit_req_backwards(tx, src, cnt);
		i = 0;
	}
	if (i < cnt) {
		/* Submit the first request */
		mxge_pio_copy(dstp, srcp, sizeof(*src));
		wmb(); /* barrier before setting valid flag */
	}

	/* Re-write the last 32-bits with the valid flags */
	src->flags = last_flags;
	src_ints = (uint32_t *)src;
	src_ints+=3;
	dst_ints = (volatile uint32_t *)dst;
	dst_ints+=3;
	*dst_ints = *src_ints;
	tx->req += cnt;
	wmb();
}

static int
mxge_pullup_tso(struct mbuf **mp)
{
	int hoff, iphlen, thoff;
	struct mbuf *m;

	m = *mp;
	KASSERT(M_WRITABLE(m), ("TSO mbuf not writable"));

	iphlen = m->m_pkthdr.csum_iphlen;
	thoff = m->m_pkthdr.csum_thlen;
	hoff = m->m_pkthdr.csum_lhlen;

	KASSERT(iphlen > 0, ("invalid ip hlen"));
	KASSERT(thoff > 0, ("invalid tcp hlen"));
	KASSERT(hoff > 0, ("invalid ether hlen"));

	if (__predict_false(m->m_len < hoff + iphlen + thoff)) {
		m = m_pullup(m, hoff + iphlen + thoff);
		if (m == NULL) {
			*mp = NULL;
			return ENOBUFS;
		}
		*mp = m;
	}
	return 0;
}

static int
mxge_encap_tso(mxge_tx_ring_t *tx, struct mxge_buffer_state *info_map,
    struct mbuf *m, int busdma_seg_cnt)
{
	mcp_kreq_ether_send_t *req;
	bus_dma_segment_t *seg;
	uint32_t low, high_swapped;
	int len, seglen, cum_len, cum_len_next;
	int next_is_first, chop, cnt, rdma_count, small;
	uint16_t pseudo_hdr_offset, cksum_offset, mss;
	uint8_t flags, flags_next;
	struct mxge_buffer_state *info_last;
	bus_dmamap_t map = info_map->map;

	mss = m->m_pkthdr.tso_segsz;

	/*
	 * Negative cum_len signifies to the send loop that we are
	 * still in the header portion of the TSO packet.
	 */
	cum_len = -(m->m_pkthdr.csum_lhlen + m->m_pkthdr.csum_iphlen +
	    m->m_pkthdr.csum_thlen);

	/*
	 * TSO implies checksum offload on this hardware
	 */
	cksum_offset = m->m_pkthdr.csum_lhlen + m->m_pkthdr.csum_iphlen;
	flags = MXGEFW_FLAGS_TSO_HDR | MXGEFW_FLAGS_FIRST;

	/*
	 * For TSO, pseudo_hdr_offset holds mss.  The firmware figures
	 * out where to put the checksum by parsing the header.
	 */
	pseudo_hdr_offset = htobe16(mss);

	req = tx->req_list;
	seg = tx->seg_list;
	cnt = 0;
	rdma_count = 0;

	/*
	 * "rdma_count" is the number of RDMAs belonging to the current
	 * packet BEFORE the current send request.  For non-TSO packets,
	 * this is equal to "count".
	 *
	 * For TSO packets, rdma_count needs to be reset to 0 after a
	 * segment cut.
	 *
	 * The rdma_count field of the send request is the number of
	 * RDMAs of the packet starting at that request.  For TSO send
	 * requests with one ore more cuts in the middle, this is the
	 * number of RDMAs starting after the last cut in the request.
	 * All previous segments before the last cut implicitly have 1
	 * RDMA.
	 *
	 * Since the number of RDMAs is not known beforehand, it must be
	 * filled-in retroactively - after each segmentation cut or at
	 * the end of the entire packet.
	 */

	while (busdma_seg_cnt) {
		/*
		 * Break the busdma segment up into pieces
		 */
		low = MXGE_LOWPART_TO_U32(seg->ds_addr);
		high_swapped = htobe32(MXGE_HIGHPART_TO_U32(seg->ds_addr));
		len = seg->ds_len;

		while (len) {
			flags_next = flags & ~MXGEFW_FLAGS_FIRST;
			seglen = len;
			cum_len_next = cum_len + seglen;
			(req - rdma_count)->rdma_count = rdma_count + 1;
			if (__predict_true(cum_len >= 0)) {
				/* Payload */
				chop = (cum_len_next > mss);
				cum_len_next = cum_len_next % mss;
				next_is_first = (cum_len_next == 0);
				flags |= chop * MXGEFW_FLAGS_TSO_CHOP;
				flags_next |=
				    next_is_first * MXGEFW_FLAGS_FIRST;
				rdma_count |= -(chop | next_is_first);
				rdma_count += chop & !next_is_first;
			} else if (cum_len_next >= 0) {
				/* Header ends */
				rdma_count = -1;
				cum_len_next = 0;
				seglen = -cum_len;
				small = (mss <= MXGEFW_SEND_SMALL_SIZE);
				flags_next = MXGEFW_FLAGS_TSO_PLD |
				    MXGEFW_FLAGS_FIRST |
				    (small * MXGEFW_FLAGS_SMALL);
			}

			req->addr_high = high_swapped;
			req->addr_low = htobe32(low);
			req->pseudo_hdr_offset = pseudo_hdr_offset;
			req->pad = 0;
			req->rdma_count = 1;
			req->length = htobe16(seglen);
			req->cksum_offset = cksum_offset;
			req->flags =
			    flags | ((cum_len & 1) * MXGEFW_FLAGS_ALIGN_ODD);
			low += seglen;
			len -= seglen;
			cum_len = cum_len_next;
			flags = flags_next;
			req++;
			cnt++;
			rdma_count++;
			if (__predict_false(cksum_offset > seglen))
				cksum_offset -= seglen;
			else
				cksum_offset = 0;
			if (__predict_false(cnt > tx->max_desc))
				goto drop;
		}
		busdma_seg_cnt--;
		seg++;
	}
	(req - rdma_count)->rdma_count = rdma_count;

	do {
		req--;
		req->flags |= MXGEFW_FLAGS_TSO_LAST;
	} while (!(req->flags & (MXGEFW_FLAGS_TSO_CHOP | MXGEFW_FLAGS_FIRST)));

	info_last = &tx->info[((cnt - 1) + tx->req) & tx->mask];

	info_map->map = info_last->map;
	info_last->map = map;
	info_last->m = m;

	mxge_submit_req(tx, tx->req_list, cnt);

	if (tx->send_go != NULL && tx->queue_active == 0) {
		/* Tell the NIC to start polling this slice */
		*tx->send_go = 1;
		tx->queue_active = 1;
		tx->activate++;
		wmb();
	}
	return 0;

drop:
	bus_dmamap_unload(tx->dmat, tx->info[tx->req & tx->mask].map);
	m_freem(m);
	return ENOBUFS;
}

static int
mxge_encap(mxge_tx_ring_t *tx, struct mbuf *m, bus_addr_t zeropad)
{
	mcp_kreq_ether_send_t *req;
	bus_dma_segment_t *seg;
	bus_dmamap_t map;
	int cnt, cum_len, err, i, idx, odd_flag;
	uint16_t pseudo_hdr_offset;
	uint8_t flags, cksum_offset;
	struct mxge_buffer_state *info_map, *info_last;

	if (m->m_pkthdr.csum_flags & CSUM_TSO) {
		err = mxge_pullup_tso(&m);
		if (__predict_false(err))
			return err;
	}

	/*
	 * Map the frame for DMA
	 */
	idx = tx->req & tx->mask;
	info_map = &tx->info[idx];
	map = info_map->map;

	err = bus_dmamap_load_mbuf_defrag(tx->dmat, map, &m,
	    tx->seg_list, tx->max_desc - 2, &cnt, BUS_DMA_NOWAIT);
	if (__predict_false(err != 0))
		goto drop;
	bus_dmamap_sync(tx->dmat, map, BUS_DMASYNC_PREWRITE);

	/*
	 * TSO is different enough, we handle it in another routine
	 */
	if (m->m_pkthdr.csum_flags & CSUM_TSO)
		return mxge_encap_tso(tx, info_map, m, cnt);

	req = tx->req_list;
	cksum_offset = 0;
	pseudo_hdr_offset = 0;
	flags = MXGEFW_FLAGS_NO_TSO;

	/*
	 * Checksum offloading
	 */
	if (m->m_pkthdr.csum_flags & CSUM_DELAY_DATA) {
		cksum_offset = m->m_pkthdr.csum_lhlen + m->m_pkthdr.csum_iphlen;
		pseudo_hdr_offset = cksum_offset +  m->m_pkthdr.csum_data;
		pseudo_hdr_offset = htobe16(pseudo_hdr_offset);
		req->cksum_offset = cksum_offset;
		flags |= MXGEFW_FLAGS_CKSUM;
		odd_flag = MXGEFW_FLAGS_ALIGN_ODD;
	} else {
		odd_flag = 0;
	}
	if (m->m_pkthdr.len < MXGEFW_SEND_SMALL_SIZE)
		flags |= MXGEFW_FLAGS_SMALL;

	/*
	 * Convert segments into a request list
	 */
	cum_len = 0;
	seg = tx->seg_list;
	req->flags = MXGEFW_FLAGS_FIRST;
	for (i = 0; i < cnt; i++) {
		req->addr_low = htobe32(MXGE_LOWPART_TO_U32(seg->ds_addr));
		req->addr_high = htobe32(MXGE_HIGHPART_TO_U32(seg->ds_addr));
		req->length = htobe16(seg->ds_len);
		req->cksum_offset = cksum_offset;
		if (cksum_offset > seg->ds_len)
			cksum_offset -= seg->ds_len;
		else
			cksum_offset = 0;
		req->pseudo_hdr_offset = pseudo_hdr_offset;
		req->pad = 0; /* complete solid 16-byte block */
		req->rdma_count = 1;
		req->flags |= flags | ((cum_len & 1) * odd_flag);
		cum_len += seg->ds_len;
		seg++;
		req++;
		req->flags = 0;
	}
	req--;

	/*
	 * Pad runt to 60 bytes
	 */
	if (cum_len < 60) {
		req++;
		req->addr_low = htobe32(MXGE_LOWPART_TO_U32(zeropad));
		req->addr_high = htobe32(MXGE_HIGHPART_TO_U32(zeropad));
		req->length = htobe16(60 - cum_len);
		req->cksum_offset = 0;
		req->pseudo_hdr_offset = pseudo_hdr_offset;
		req->pad = 0; /* complete solid 16-byte block */
		req->rdma_count = 1;
		req->flags |= flags | ((cum_len & 1) * odd_flag);
		cnt++;
	}

	tx->req_list[0].rdma_count = cnt;
#if 0
	/* print what the firmware will see */
	for (i = 0; i < cnt; i++) {
		kprintf("%d: addr: 0x%x 0x%x len:%d pso%d,"
		    "cso:%d, flags:0x%x, rdma:%d\n",
		    i, (int)ntohl(tx->req_list[i].addr_high),
		    (int)ntohl(tx->req_list[i].addr_low),
		    (int)ntohs(tx->req_list[i].length),
		    (int)ntohs(tx->req_list[i].pseudo_hdr_offset),
		    tx->req_list[i].cksum_offset, tx->req_list[i].flags,
		    tx->req_list[i].rdma_count);
	}
	kprintf("--------------\n");
#endif
	info_last = &tx->info[((cnt - 1) + tx->req) & tx->mask];

	info_map->map = info_last->map;
	info_last->map = map;
	info_last->m = m;

	mxge_submit_req(tx, tx->req_list, cnt);

	if (tx->send_go != NULL && tx->queue_active == 0) {
		/* Tell the NIC to start polling this slice */
		*tx->send_go = 1;
		tx->queue_active = 1;
		tx->activate++;
		wmb();
	}
	return 0;

drop:
	m_freem(m);
	return err;
}

static void
mxge_start(struct ifnet *ifp, struct ifaltq_subque *ifsq)
{
	mxge_softc_t *sc = ifp->if_softc;
	mxge_tx_ring_t *tx = ifsq_get_priv(ifsq);
	bus_addr_t zeropad;
	int encap = 0;

	KKASSERT(tx->ifsq == ifsq);
	ASSERT_SERIALIZED(&tx->tx_serialize);

	if ((ifp->if_flags & IFF_RUNNING) == 0 || ifsq_is_oactive(ifsq))
		return;

	zeropad = sc->zeropad_dma.dmem_busaddr;
	while (tx->mask - (tx->req - tx->done) > tx->max_desc) {
		struct mbuf *m;
		int error;

		m = ifsq_dequeue(ifsq);
		if (m == NULL)
			goto done;

		BPF_MTAP(ifp, m);
		error = mxge_encap(tx, m, zeropad);
		if (!error)
			encap = 1;
		else
			IFNET_STAT_INC(ifp, oerrors, 1);
	}

	/* Ran out of transmit slots */
	ifsq_set_oactive(ifsq);
done:
	if (encap)
		ifsq_watchdog_set_count(&tx->watchdog, 5);
}

static void
mxge_watchdog(struct ifaltq_subque *ifsq)
{
	struct ifnet *ifp = ifsq_get_ifp(ifsq);
	struct mxge_softc *sc = ifp->if_softc;
	uint32_t rx_pause = be32toh(sc->ss->fw_stats->dropped_pause);
	mxge_tx_ring_t *tx = ifsq_get_priv(ifsq);

	ASSERT_IFNET_SERIALIZED_ALL(ifp);

	/* Check for pause blocking before resetting */
	if (tx->watchdog_rx_pause == rx_pause) {
		mxge_warn_stuck(sc, tx, 0);
		mxge_watchdog_reset(sc);
		return;
	} else {
		if_printf(ifp, "Flow control blocking xmits, "
		    "check link partner\n");
	}
	tx->watchdog_rx_pause = rx_pause;
}

/*
 * Copy an array of mcp_kreq_ether_recv_t's to the mcp.  Copy
 * at most 32 bytes at a time, so as to avoid involving the software
 * pio handler in the nic.  We re-write the first segment's low
 * DMA address to mark it valid only after we write the entire chunk
 * in a burst
 */
static __inline void
mxge_submit_8rx(volatile mcp_kreq_ether_recv_t *dst,
    mcp_kreq_ether_recv_t *src)
{
	uint32_t low;

	low = src->addr_low;
	src->addr_low = 0xffffffff;
	mxge_pio_copy(dst, src, 4 * sizeof (*src));
	wmb();
	mxge_pio_copy(dst + 4, src + 4, 4 * sizeof (*src));
	wmb();
	src->addr_low = low;
	dst->addr_low = low;
	wmb();
}

static int
mxge_get_buf_small(mxge_rx_ring_t *rx, bus_dmamap_t map, int idx,
    boolean_t init)
{
	bus_dma_segment_t seg;
	struct mbuf *m;
	int cnt, err, mflag;

	mflag = M_NOWAIT;
	if (__predict_false(init))
		mflag = M_WAITOK;

	m = m_gethdr(mflag, MT_DATA);
	if (m == NULL) {
		err = ENOBUFS;
		if (__predict_false(init)) {
			/*
			 * During initialization, there
			 * is nothing to setup; bail out
			 */
			return err;
		}
		goto done;
	}
	m->m_len = m->m_pkthdr.len = MHLEN;

	err = bus_dmamap_load_mbuf_segment(rx->dmat, map, m,
	    &seg, 1, &cnt, BUS_DMA_NOWAIT);
	if (err != 0) {
		m_freem(m);
		if (__predict_false(init)) {
			/*
			 * During initialization, there
			 * is nothing to setup; bail out
			 */
			return err;
		}
		goto done;
	}

	rx->info[idx].m = m;
	rx->shadow[idx].addr_low = htobe32(MXGE_LOWPART_TO_U32(seg.ds_addr));
	rx->shadow[idx].addr_high = htobe32(MXGE_HIGHPART_TO_U32(seg.ds_addr));

done:
	if ((idx & 7) == 7)
		mxge_submit_8rx(&rx->lanai[idx - 7], &rx->shadow[idx - 7]);
	return err;
}

static int
mxge_get_buf_big(mxge_rx_ring_t *rx, bus_dmamap_t map, int idx,
    boolean_t init)
{
	bus_dma_segment_t seg;
	struct mbuf *m;
	int cnt, err, mflag;

	mflag = M_NOWAIT;
	if (__predict_false(init))
		mflag = M_WAITOK;

	if (rx->cl_size == MCLBYTES)
		m = m_getcl(mflag, MT_DATA, M_PKTHDR);
	else
		m = m_getjcl(mflag, MT_DATA, M_PKTHDR, MJUMPAGESIZE);
	if (m == NULL) {
		err = ENOBUFS;
		if (__predict_false(init)) {
			/*
			 * During initialization, there
			 * is nothing to setup; bail out
			 */
			return err;
		}
		goto done;
	}
	m->m_len = m->m_pkthdr.len = rx->cl_size;

	err = bus_dmamap_load_mbuf_segment(rx->dmat, map, m,
	    &seg, 1, &cnt, BUS_DMA_NOWAIT);
	if (err != 0) {
		m_freem(m);
		if (__predict_false(init)) {
			/*
			 * During initialization, there
			 * is nothing to setup; bail out
			 */
			return err;
		}
		goto done;
	}

	rx->info[idx].m = m;
	rx->shadow[idx].addr_low = htobe32(MXGE_LOWPART_TO_U32(seg.ds_addr));
	rx->shadow[idx].addr_high = htobe32(MXGE_HIGHPART_TO_U32(seg.ds_addr));

done:
	if ((idx & 7) == 7)
		mxge_submit_8rx(&rx->lanai[idx - 7], &rx->shadow[idx - 7]);
	return err;
}

/* 
 * Myri10GE hardware checksums are not valid if the sender
 * padded the frame with non-zero padding.  This is because
 * the firmware just does a simple 16-bit 1s complement
 * checksum across the entire frame, excluding the first 14
 * bytes.  It is best to simply to check the checksum and
 * tell the stack about it only if the checksum is good
 */
static __inline uint16_t
mxge_rx_csum(struct mbuf *m, int csum)
{
	const struct ether_header *eh;
	const struct ip *ip;
	uint16_t c;

	eh = mtod(m, const struct ether_header *);

	/* Only deal with IPv4 TCP & UDP for now */
	if (__predict_false(eh->ether_type != htons(ETHERTYPE_IP)))
		return 1;

	ip = (const struct ip *)(eh + 1);
	if (__predict_false(ip->ip_p != IPPROTO_TCP && ip->ip_p != IPPROTO_UDP))
		return 1;

#ifdef INET
	c = in_pseudo(ip->ip_src.s_addr, ip->ip_dst.s_addr,
	    htonl(ntohs(csum) + ntohs(ip->ip_len) +
	          - (ip->ip_hl << 2) + ip->ip_p));
#else
	c = 1;
#endif
	c ^= 0xffff;
	return c;
}

static void
mxge_vlan_tag_remove(struct mbuf *m, uint32_t *csum)
{
	struct ether_vlan_header *evl;
	uint32_t partial;

	evl = mtod(m, struct ether_vlan_header *);

	/*
	 * Fix checksum by subtracting EVL_ENCAPLEN bytes after
	 * what the firmware thought was the end of the ethernet
	 * header.
	 */

	/* Put checksum into host byte order */
	*csum = ntohs(*csum);

	partial = ntohl(*(uint32_t *)(mtod(m, char *) + ETHER_HDR_LEN));
	*csum += ~partial;
	*csum += ((*csum) < ~partial);
	*csum = ((*csum) >> 16) + ((*csum) & 0xFFFF);
	*csum = ((*csum) >> 16) + ((*csum) & 0xFFFF);

	/*
	 * Restore checksum to network byte order;
	 * later consumers expect this
	 */
	*csum = htons(*csum);

	/* save the tag */
	m->m_pkthdr.ether_vlantag = ntohs(evl->evl_tag);
	m->m_flags |= M_VLANTAG;

	/*
	 * Remove the 802.1q header by copying the Ethernet
	 * addresses over it and adjusting the beginning of
	 * the data in the mbuf.  The encapsulated Ethernet
	 * type field is already in place.
	 */
	bcopy((char *)evl, (char *)evl + EVL_ENCAPLEN,
	    ETHER_HDR_LEN - ETHER_TYPE_LEN);
	m_adj(m, EVL_ENCAPLEN);
}


static __inline void
mxge_rx_done_big(struct ifnet *ifp, mxge_rx_ring_t *rx,
    uint32_t len, uint32_t csum)
{
	struct mbuf *m;
	const struct ether_header *eh;
	bus_dmamap_t old_map;
	int idx;

	idx = rx->cnt & rx->mask;
	rx->cnt++;

	/* Save a pointer to the received mbuf */
	m = rx->info[idx].m;

	/* Try to replace the received mbuf */
	if (mxge_get_buf_big(rx, rx->extra_map, idx, FALSE)) {
		/* Drop the frame -- the old mbuf is re-cycled */
		IFNET_STAT_INC(ifp, ierrors, 1);
		return;
	}

	/* Unmap the received buffer */
	old_map = rx->info[idx].map;
	bus_dmamap_sync(rx->dmat, old_map, BUS_DMASYNC_POSTREAD);
	bus_dmamap_unload(rx->dmat, old_map);

	/* Swap the bus_dmamap_t's */
	rx->info[idx].map = rx->extra_map;
	rx->extra_map = old_map;

	/*
	 * mcp implicitly skips 1st 2 bytes so that packet is properly
	 * aligned
	 */
	m->m_data += MXGEFW_PAD;

	m->m_pkthdr.rcvif = ifp;
	m->m_len = m->m_pkthdr.len = len;

	IFNET_STAT_INC(ifp, ipackets, 1);

	eh = mtod(m, const struct ether_header *);
	if (eh->ether_type == htons(ETHERTYPE_VLAN))
		mxge_vlan_tag_remove(m, &csum);

	/* If the checksum is valid, mark it in the mbuf header */
	if ((ifp->if_capenable & IFCAP_RXCSUM) &&
	    mxge_rx_csum(m, csum) == 0) {
		/* Tell the stack that the checksum is good */
		m->m_pkthdr.csum_data = 0xffff;
		m->m_pkthdr.csum_flags = CSUM_PSEUDO_HDR |
		    CSUM_DATA_VALID;
	}
	ifp->if_input(ifp, m, NULL, -1);
}

static __inline void
mxge_rx_done_small(struct ifnet *ifp, mxge_rx_ring_t *rx,
    uint32_t len, uint32_t csum)
{
	const struct ether_header *eh;
	struct mbuf *m;
	bus_dmamap_t old_map;
	int idx;

	idx = rx->cnt & rx->mask;
	rx->cnt++;

	/* Save a pointer to the received mbuf */
	m = rx->info[idx].m;

	/* Try to replace the received mbuf */
	if (mxge_get_buf_small(rx, rx->extra_map, idx, FALSE)) {
		/* Drop the frame -- the old mbuf is re-cycled */
		IFNET_STAT_INC(ifp, ierrors, 1);
		return;
	}

	/* Unmap the received buffer */
	old_map = rx->info[idx].map;
	bus_dmamap_sync(rx->dmat, old_map, BUS_DMASYNC_POSTREAD);
	bus_dmamap_unload(rx->dmat, old_map);

	/* Swap the bus_dmamap_t's */
	rx->info[idx].map = rx->extra_map;
	rx->extra_map = old_map;

	/*
	 * mcp implicitly skips 1st 2 bytes so that packet is properly
	 * aligned
	 */
	m->m_data += MXGEFW_PAD;

	m->m_pkthdr.rcvif = ifp;
	m->m_len = m->m_pkthdr.len = len;

	IFNET_STAT_INC(ifp, ipackets, 1);

	eh = mtod(m, const struct ether_header *);
	if (eh->ether_type == htons(ETHERTYPE_VLAN))
		mxge_vlan_tag_remove(m, &csum);

	/* If the checksum is valid, mark it in the mbuf header */
	if ((ifp->if_capenable & IFCAP_RXCSUM) &&
	    mxge_rx_csum(m, csum) == 0) {
		/* Tell the stack that the checksum is good */
		m->m_pkthdr.csum_data = 0xffff;
		m->m_pkthdr.csum_flags = CSUM_PSEUDO_HDR |
		    CSUM_DATA_VALID;
	}
	ifp->if_input(ifp, m, NULL, -1);
}

static __inline void
mxge_clean_rx_done(struct ifnet *ifp, struct mxge_rx_data *rx_data, int cycle)
{
	mxge_rx_done_t *rx_done = &rx_data->rx_done;

	while (rx_done->entry[rx_done->idx].length != 0 && cycle != 0) {
		uint16_t length, checksum;

		length = ntohs(rx_done->entry[rx_done->idx].length);
		rx_done->entry[rx_done->idx].length = 0;

		checksum = rx_done->entry[rx_done->idx].checksum;

		if (length <= MXGE_RX_SMALL_BUFLEN) {
			mxge_rx_done_small(ifp, &rx_data->rx_small,
			    length, checksum);
		} else {
			mxge_rx_done_big(ifp, &rx_data->rx_big,
			    length, checksum);
		}

		rx_done->idx++;
		rx_done->idx &= rx_done->mask;
		--cycle;
	}
}

static __inline void
mxge_tx_done(struct ifnet *ifp, mxge_tx_ring_t *tx, uint32_t mcp_idx)
{
	ASSERT_SERIALIZED(&tx->tx_serialize);

	while (tx->pkt_done != mcp_idx) {
		struct mbuf *m;
		int idx;

		idx = tx->done & tx->mask;
		tx->done++;

		m = tx->info[idx].m;
		/*
		 * mbuf and DMA map only attached to the first
		 * segment per-mbuf.
		 */
		if (m != NULL) {
			tx->pkt_done++;
			IFNET_STAT_INC(ifp, opackets, 1);
			tx->info[idx].m = NULL;
			bus_dmamap_unload(tx->dmat, tx->info[idx].map);
			m_freem(m);
		}
	}

	/*
	 * If we have space, clear OACTIVE to tell the stack that
	 * its OK to send packets
	 */
	if (tx->req - tx->done < (tx->mask + 1) / 2) {
		ifsq_clr_oactive(tx->ifsq);
		if (tx->req == tx->done) {
			/* Reset watchdog */
			ifsq_watchdog_set_count(&tx->watchdog, 0);
		}
	}

	if (!ifsq_is_empty(tx->ifsq))
		ifsq_devstart(tx->ifsq);

	if (tx->send_stop != NULL && tx->req == tx->done) {
		/*
		 * Let the NIC stop polling this queue, since there
		 * are no more transmits pending
		 */
		*tx->send_stop = 1;
		tx->queue_active = 0;
		tx->deactivate++;
		wmb();
	}
}

static struct mxge_media_type mxge_xfp_media_types[] = {
	{IFM_10G_CX4,	0x7f, 		"10GBASE-CX4 (module)"},
	{IFM_10G_SR, 	(1 << 7),	"10GBASE-SR"},
	{IFM_10G_LR, 	(1 << 6),	"10GBASE-LR"},
	{IFM_NONE,	(1 << 5),	"10GBASE-ER"},
	{IFM_10G_LRM,	(1 << 4),	"10GBASE-LRM"},
	{IFM_NONE,	(1 << 3),	"10GBASE-SW"},
	{IFM_NONE,	(1 << 2),	"10GBASE-LW"},
	{IFM_NONE,	(1 << 1),	"10GBASE-EW"},
	{IFM_NONE,	(1 << 0),	"Reserved"}
};

static struct mxge_media_type mxge_sfp_media_types[] = {
	{IFM_10G_TWINAX,      0,	"10GBASE-Twinax"},
	{IFM_NONE,	(1 << 7),	"Reserved"},
	{IFM_10G_LRM,	(1 << 6),	"10GBASE-LRM"},
	{IFM_10G_LR, 	(1 << 5),	"10GBASE-LR"},
	{IFM_10G_SR,	(1 << 4),	"10GBASE-SR"},
	{IFM_10G_TWINAX,(1 << 0),	"10GBASE-Twinax"}
};

static void
mxge_media_set(mxge_softc_t *sc, int media_type)
{
	int fc_opt = 0;

	if (media_type == IFM_NONE)
		return;

	if (sc->pause)
		fc_opt = IFM_ETH_RXPAUSE | IFM_ETH_TXPAUSE;

	ifmedia_add(&sc->media, MXGE_IFM | media_type, 0, NULL);
	ifmedia_set(&sc->media, MXGE_IFM | media_type | fc_opt);

	sc->current_media = media_type;
}

static void
mxge_media_unset(mxge_softc_t *sc)
{
	ifmedia_removeall(&sc->media);
	sc->current_media = IFM_NONE;
}

static void
mxge_media_init(mxge_softc_t *sc)
{
	const char *ptr;
	int i;

	mxge_media_unset(sc);

	/* 
	 * Parse the product code to deterimine the interface type
	 * (CX4, XFP, Quad Ribbon Fiber) by looking at the character
	 * after the 3rd dash in the driver's cached copy of the
	 * EEPROM's product code string.
	 */
	ptr = sc->product_code_string;
	if (ptr == NULL) {
		if_printf(sc->ifp, "Missing product code\n");
		return;
	}

	for (i = 0; i < 3; i++, ptr++) {
		ptr = strchr(ptr, '-');
		if (ptr == NULL) {
			if_printf(sc->ifp, "only %d dashes in PC?!?\n", i);
			return;
		}
	}
	if (*ptr == 'C' || *(ptr +1) == 'C') {
		/* -C is CX4 */
		sc->connector = MXGE_CX4;
		mxge_media_set(sc, IFM_10G_CX4);
	} else if (*ptr == 'Q') {
		/* -Q is Quad Ribbon Fiber */
		sc->connector = MXGE_QRF;
		if_printf(sc->ifp, "Quad Ribbon Fiber Media\n");
		/* DragonFly has no media type for Quad ribbon fiber */
	} else if (*ptr == 'R') {
		/* -R is XFP */
		sc->connector = MXGE_XFP;
		/* NOTE: ifmedia will be installed later */
	} else if (*ptr == 'S' || *(ptr +1) == 'S') {
		/* -S or -2S is SFP+ */
		sc->connector = MXGE_SFP;
		/* NOTE: ifmedia will be installed later */
	} else {
		sc->connector = MXGE_UNK;
		if_printf(sc->ifp, "Unknown media type: %c\n", *ptr);
	}
}

/*
 * Determine the media type for a NIC.  Some XFPs will identify
 * themselves only when their link is up, so this is initiated via a
 * link up interrupt.  However, this can potentially take up to
 * several milliseconds, so it is run via the watchdog routine, rather
 * than in the interrupt handler itself. 
 */
static void
mxge_media_probe(mxge_softc_t *sc)
{
	mxge_cmd_t cmd;
	const char *cage_type;
	struct mxge_media_type *mxge_media_types = NULL;
	int i, err, ms, mxge_media_type_entries;
	uint32_t byte;

	sc->need_media_probe = 0;

	if (sc->connector == MXGE_XFP) {
		/* -R is XFP */
		mxge_media_types = mxge_xfp_media_types;
		mxge_media_type_entries = NELEM(mxge_xfp_media_types);
		byte = MXGE_XFP_COMPLIANCE_BYTE;
		cage_type = "XFP";
	} else 	if (sc->connector == MXGE_SFP) {
		/* -S or -2S is SFP+ */
		mxge_media_types = mxge_sfp_media_types;
		mxge_media_type_entries = NELEM(mxge_sfp_media_types);
		cage_type = "SFP+";
		byte = 3;
	} else {
		/* nothing to do; media type cannot change */
		return;
	}

	/*
	 * At this point we know the NIC has an XFP cage, so now we
	 * try to determine what is in the cage by using the
	 * firmware's XFP I2C commands to read the XFP 10GbE compilance
	 * register.  We read just one byte, which may take over
	 * a millisecond
	 */

	bzero(&cmd, sizeof(cmd));	/* silence gcc warning */
	cmd.data0 = 0;	 /* just fetch 1 byte, not all 256 */
	cmd.data1 = byte;
	err = mxge_send_cmd(sc, MXGEFW_CMD_I2C_READ, &cmd);
	if (err != MXGEFW_CMD_OK) {
		if (err == MXGEFW_CMD_ERROR_I2C_FAILURE)
			if_printf(sc->ifp, "failed to read XFP\n");
		else if (err == MXGEFW_CMD_ERROR_I2C_ABSENT)
			if_printf(sc->ifp, "Type R/S with no XFP!?!?\n");
		else
			if_printf(sc->ifp, "I2C read failed, err: %d", err);
		mxge_media_unset(sc);
		return;
	}

	/* Now we wait for the data to be cached */
	cmd.data0 = byte;
	err = mxge_send_cmd(sc, MXGEFW_CMD_I2C_BYTE, &cmd);
	for (ms = 0; err == EBUSY && ms < 50; ms++) {
		DELAY(1000);
		cmd.data0 = byte;
		err = mxge_send_cmd(sc, MXGEFW_CMD_I2C_BYTE, &cmd);
	}
	if (err != MXGEFW_CMD_OK) {
		if_printf(sc->ifp, "failed to read %s (%d, %dms)\n",
		    cage_type, err, ms);
		mxge_media_unset(sc);
		return;
	}

	if (cmd.data0 == mxge_media_types[0].bitmask) {
		if (bootverbose) {
			if_printf(sc->ifp, "%s:%s\n", cage_type,
			    mxge_media_types[0].name);
		}
		if (sc->current_media != mxge_media_types[0].flag) {
			mxge_media_unset(sc);
			mxge_media_set(sc, mxge_media_types[0].flag);
		}
		return;
	}
	for (i = 1; i < mxge_media_type_entries; i++) {
		if (cmd.data0 & mxge_media_types[i].bitmask) {
			if (bootverbose) {
				if_printf(sc->ifp, "%s:%s\n", cage_type,
				    mxge_media_types[i].name);
			}

			if (sc->current_media != mxge_media_types[i].flag) {
				mxge_media_unset(sc);
				mxge_media_set(sc, mxge_media_types[i].flag);
			}
			return;
		}
	}
	mxge_media_unset(sc);
	if (bootverbose) {
		if_printf(sc->ifp, "%s media 0x%x unknown\n", cage_type,
		    cmd.data0);
	}
}

static void
mxge_intr_status(struct mxge_softc *sc, const mcp_irq_data_t *stats)
{
	if (sc->link_state != stats->link_up) {
		sc->link_state = stats->link_up;
		if (sc->link_state) {
			sc->ifp->if_link_state = LINK_STATE_UP;
			if_link_state_change(sc->ifp);
			if (bootverbose)
				if_printf(sc->ifp, "link up\n");
		} else {
			sc->ifp->if_link_state = LINK_STATE_DOWN;
			if_link_state_change(sc->ifp);
			if (bootverbose)
				if_printf(sc->ifp, "link down\n");
		}
		sc->need_media_probe = 1;
	}

	if (sc->rdma_tags_available != be32toh(stats->rdma_tags_available)) {
		sc->rdma_tags_available = be32toh(stats->rdma_tags_available);
		if_printf(sc->ifp, "RDMA timed out! %d tags left\n",
		    sc->rdma_tags_available);
	}

	if (stats->link_down) {
		sc->down_cnt += stats->link_down;
		sc->link_state = 0;
		sc->ifp->if_link_state = LINK_STATE_DOWN;
		if_link_state_change(sc->ifp);
	}
}

static void
mxge_serialize_skipmain(struct mxge_softc *sc)
{
	lwkt_serialize_array_enter(sc->serializes, sc->nserialize, 1);
}

static void
mxge_deserialize_skipmain(struct mxge_softc *sc)
{
	lwkt_serialize_array_exit(sc->serializes, sc->nserialize, 1);
}

static void
mxge_legacy(void *arg)
{
	struct mxge_slice_state *ss = arg;
	mxge_softc_t *sc = ss->sc;
	mcp_irq_data_t *stats = ss->fw_stats;
	mxge_tx_ring_t *tx = &ss->tx;
	mxge_rx_done_t *rx_done = &ss->rx_data.rx_done;
	uint32_t send_done_count;
	uint8_t valid;

	ASSERT_SERIALIZED(&sc->main_serialize);

	/* Make sure the DMA has finished */
	if (!stats->valid)
		return;
	valid = stats->valid;

	/* Lower legacy IRQ */
	*sc->irq_deassert = 0;
	if (!mxge_deassert_wait) {
		/* Don't wait for conf. that irq is low */
		stats->valid = 0;
	}

	mxge_serialize_skipmain(sc);

	/*
	 * Loop while waiting for legacy irq deassertion
	 * XXX do we really want to loop?
	 */
	do {
		/* Check for transmit completes and receives */
		send_done_count = be32toh(stats->send_done_count);
		while ((send_done_count != tx->pkt_done) ||
		       (rx_done->entry[rx_done->idx].length != 0)) {
			if (send_done_count != tx->pkt_done) {
				mxge_tx_done(&sc->arpcom.ac_if, tx,
				    (int)send_done_count);
			}
			mxge_clean_rx_done(&sc->arpcom.ac_if, &ss->rx_data, -1);
			send_done_count = be32toh(stats->send_done_count);
		}
		if (mxge_deassert_wait)
			wmb();
	} while (*((volatile uint8_t *)&stats->valid));

	mxge_deserialize_skipmain(sc);

	/* Fw link & error stats meaningful only on the first slice */
	if (__predict_false(stats->stats_updated))
		mxge_intr_status(sc, stats);

	/* Check to see if we have rx token to pass back */
	if (valid & 0x1)
		*ss->irq_claim = be32toh(3);
	*(ss->irq_claim + 1) = be32toh(3);
}

static void
mxge_msi(void *arg)
{
	struct mxge_slice_state *ss = arg;
	mxge_softc_t *sc = ss->sc;
	mcp_irq_data_t *stats = ss->fw_stats;
	mxge_tx_ring_t *tx = &ss->tx;
	mxge_rx_done_t *rx_done = &ss->rx_data.rx_done;
	uint32_t send_done_count;
	uint8_t valid;
#ifndef IFPOLL_ENABLE
	const boolean_t polling = FALSE;
#else
	boolean_t polling = FALSE;
#endif

	ASSERT_SERIALIZED(&sc->main_serialize);

	/* Make sure the DMA has finished */
	if (__predict_false(!stats->valid))
		return;

	valid = stats->valid;
	stats->valid = 0;

#ifdef IFPOLL_ENABLE
	if (sc->arpcom.ac_if.if_flags & IFF_NPOLLING)
		polling = TRUE;
#endif

	if (!polling) {
		/* Check for receives */
		lwkt_serialize_enter(&ss->rx_data.rx_serialize);
		if (rx_done->entry[rx_done->idx].length != 0)
			mxge_clean_rx_done(&sc->arpcom.ac_if, &ss->rx_data, -1);
		lwkt_serialize_exit(&ss->rx_data.rx_serialize);
	}

	/*
	 * Check for transmit completes
	 *
	 * NOTE:
	 * Since pkt_done is only changed by mxge_tx_done(),
	 * which is called only in interrupt handler, the
	 * check w/o holding tx serializer is MPSAFE.
	 */
	send_done_count = be32toh(stats->send_done_count);
	if (send_done_count != tx->pkt_done) {
		lwkt_serialize_enter(&tx->tx_serialize);
		mxge_tx_done(&sc->arpcom.ac_if, tx, (int)send_done_count);
		lwkt_serialize_exit(&tx->tx_serialize);
	}

	if (__predict_false(stats->stats_updated))
		mxge_intr_status(sc, stats);

	/* Check to see if we have rx token to pass back */
	if (!polling && (valid & 0x1))
		*ss->irq_claim = be32toh(3);
	*(ss->irq_claim + 1) = be32toh(3);
}

static void
mxge_msix_rx(void *arg)
{
	struct mxge_slice_state *ss = arg;
	mxge_rx_done_t *rx_done = &ss->rx_data.rx_done;

#ifdef IFPOLL_ENABLE
	if (ss->sc->arpcom.ac_if.if_flags & IFF_NPOLLING)
		return;
#endif

	ASSERT_SERIALIZED(&ss->rx_data.rx_serialize);

	if (rx_done->entry[rx_done->idx].length != 0)
		mxge_clean_rx_done(&ss->sc->arpcom.ac_if, &ss->rx_data, -1);

	*ss->irq_claim = be32toh(3);
}

static void
mxge_msix_rxtx(void *arg)
{
	struct mxge_slice_state *ss = arg;
	mxge_softc_t *sc = ss->sc;
	mcp_irq_data_t *stats = ss->fw_stats;
	mxge_tx_ring_t *tx = &ss->tx;
	mxge_rx_done_t *rx_done = &ss->rx_data.rx_done;
	uint32_t send_done_count;
	uint8_t valid;
#ifndef IFPOLL_ENABLE
	const boolean_t polling = FALSE;
#else
	boolean_t polling = FALSE;
#endif

	ASSERT_SERIALIZED(&ss->rx_data.rx_serialize);

	/* Make sure the DMA has finished */
	if (__predict_false(!stats->valid))
		return;

	valid = stats->valid;
	stats->valid = 0;

#ifdef IFPOLL_ENABLE
	if (sc->arpcom.ac_if.if_flags & IFF_NPOLLING)
		polling = TRUE;
#endif

	/* Check for receives */
	if (!polling && rx_done->entry[rx_done->idx].length != 0)
		mxge_clean_rx_done(&sc->arpcom.ac_if, &ss->rx_data, -1);

	/*
	 * Check for transmit completes
	 *
	 * NOTE:
	 * Since pkt_done is only changed by mxge_tx_done(),
	 * which is called only in interrupt handler, the
	 * check w/o holding tx serializer is MPSAFE.
	 */
	send_done_count = be32toh(stats->send_done_count);
	if (send_done_count != tx->pkt_done) {
		lwkt_serialize_enter(&tx->tx_serialize);
		mxge_tx_done(&sc->arpcom.ac_if, tx, (int)send_done_count);
		lwkt_serialize_exit(&tx->tx_serialize);
	}

	/* Check to see if we have rx token to pass back */
	if (!polling && (valid & 0x1))
		*ss->irq_claim = be32toh(3);
	*(ss->irq_claim + 1) = be32toh(3);
}

static void
mxge_init(void *arg)
{
	struct mxge_softc *sc = arg;

	ASSERT_IFNET_SERIALIZED_ALL(sc->ifp);
	if ((sc->ifp->if_flags & IFF_RUNNING) == 0)
		mxge_open(sc);
}

static void
mxge_free_slice_mbufs(struct mxge_slice_state *ss)
{
	int i;

	for (i = 0; i <= ss->rx_data.rx_big.mask; i++) {
		if (ss->rx_data.rx_big.info[i].m == NULL)
			continue;
		bus_dmamap_unload(ss->rx_data.rx_big.dmat,
		    ss->rx_data.rx_big.info[i].map);
		m_freem(ss->rx_data.rx_big.info[i].m);
		ss->rx_data.rx_big.info[i].m = NULL;
	}

	for (i = 0; i <= ss->rx_data.rx_small.mask; i++) {
		if (ss->rx_data.rx_small.info[i].m == NULL)
			continue;
		bus_dmamap_unload(ss->rx_data.rx_small.dmat,
		    ss->rx_data.rx_small.info[i].map);
		m_freem(ss->rx_data.rx_small.info[i].m);
		ss->rx_data.rx_small.info[i].m = NULL;
	}

	/* Transmit ring used only on the first slice */
	if (ss->tx.info == NULL)
		return;

	for (i = 0; i <= ss->tx.mask; i++) {
		if (ss->tx.info[i].m == NULL)
			continue;
		bus_dmamap_unload(ss->tx.dmat, ss->tx.info[i].map);
		m_freem(ss->tx.info[i].m);
		ss->tx.info[i].m = NULL;
	}
}

static void
mxge_free_mbufs(mxge_softc_t *sc)
{
	int slice;

	for (slice = 0; slice < sc->num_slices; slice++)
		mxge_free_slice_mbufs(&sc->ss[slice]);
}

static void
mxge_free_slice_rings(struct mxge_slice_state *ss)
{
	int i;

	if (ss->rx_data.rx_done.entry != NULL) {
		mxge_dma_free(&ss->rx_done_dma);
		ss->rx_data.rx_done.entry = NULL;
	}

	if (ss->tx.req_list != NULL) {
		kfree(ss->tx.req_list, M_DEVBUF);
		ss->tx.req_list = NULL;
	}

	if (ss->tx.seg_list != NULL) {
		kfree(ss->tx.seg_list, M_DEVBUF);
		ss->tx.seg_list = NULL;
	}

	if (ss->rx_data.rx_small.shadow != NULL) {
		kfree(ss->rx_data.rx_small.shadow, M_DEVBUF);
		ss->rx_data.rx_small.shadow = NULL;
	}

	if (ss->rx_data.rx_big.shadow != NULL) {
		kfree(ss->rx_data.rx_big.shadow, M_DEVBUF);
		ss->rx_data.rx_big.shadow = NULL;
	}

	if (ss->tx.info != NULL) {
		if (ss->tx.dmat != NULL) {
			for (i = 0; i <= ss->tx.mask; i++) {
				bus_dmamap_destroy(ss->tx.dmat,
				    ss->tx.info[i].map);
			}
			bus_dma_tag_destroy(ss->tx.dmat);
		}
		kfree(ss->tx.info, M_DEVBUF);
		ss->tx.info = NULL;
	}

	if (ss->rx_data.rx_small.info != NULL) {
		if (ss->rx_data.rx_small.dmat != NULL) {
			for (i = 0; i <= ss->rx_data.rx_small.mask; i++) {
				bus_dmamap_destroy(ss->rx_data.rx_small.dmat,
				    ss->rx_data.rx_small.info[i].map);
			}
			bus_dmamap_destroy(ss->rx_data.rx_small.dmat,
			    ss->rx_data.rx_small.extra_map);
			bus_dma_tag_destroy(ss->rx_data.rx_small.dmat);
		}
		kfree(ss->rx_data.rx_small.info, M_DEVBUF);
		ss->rx_data.rx_small.info = NULL;
	}

	if (ss->rx_data.rx_big.info != NULL) {
		if (ss->rx_data.rx_big.dmat != NULL) {
			for (i = 0; i <= ss->rx_data.rx_big.mask; i++) {
				bus_dmamap_destroy(ss->rx_data.rx_big.dmat,
				    ss->rx_data.rx_big.info[i].map);
			}
			bus_dmamap_destroy(ss->rx_data.rx_big.dmat,
			    ss->rx_data.rx_big.extra_map);
			bus_dma_tag_destroy(ss->rx_data.rx_big.dmat);
		}
		kfree(ss->rx_data.rx_big.info, M_DEVBUF);
		ss->rx_data.rx_big.info = NULL;
	}
}

static void
mxge_free_rings(mxge_softc_t *sc)
{
	int slice;

	if (sc->ss == NULL)
		return;

	for (slice = 0; slice < sc->num_slices; slice++)
		mxge_free_slice_rings(&sc->ss[slice]);
}

static int
mxge_alloc_slice_rings(struct mxge_slice_state *ss, int rx_ring_entries,
    int tx_ring_entries)
{
	mxge_softc_t *sc = ss->sc;
	size_t bytes;
	int err, i;

	/*
	 * Allocate per-slice receive resources
	 */

	ss->rx_data.rx_small.mask = ss->rx_data.rx_big.mask =
	    rx_ring_entries - 1;
	ss->rx_data.rx_done.mask = (2 * rx_ring_entries) - 1;

	/* Allocate the rx shadow rings */
	bytes = rx_ring_entries * sizeof(*ss->rx_data.rx_small.shadow);
	ss->rx_data.rx_small.shadow = kmalloc(bytes, M_DEVBUF, M_ZERO|M_WAITOK);

	bytes = rx_ring_entries * sizeof(*ss->rx_data.rx_big.shadow);
	ss->rx_data.rx_big.shadow = kmalloc(bytes, M_DEVBUF, M_ZERO|M_WAITOK);

	/* Allocate the rx host info rings */
	bytes = rx_ring_entries * sizeof(*ss->rx_data.rx_small.info);
	ss->rx_data.rx_small.info = kmalloc(bytes, M_DEVBUF, M_ZERO|M_WAITOK);

	bytes = rx_ring_entries * sizeof(*ss->rx_data.rx_big.info);
	ss->rx_data.rx_big.info = kmalloc(bytes, M_DEVBUF, M_ZERO|M_WAITOK);

	/* Allocate the rx busdma resources */
	err = bus_dma_tag_create(sc->parent_dmat,	/* parent */
				 1,			/* alignment */
				 4096,			/* boundary */
				 BUS_SPACE_MAXADDR,	/* low */
				 BUS_SPACE_MAXADDR,	/* high */
				 MHLEN,			/* maxsize */
				 1,			/* num segs */
				 MHLEN,			/* maxsegsize */
				 BUS_DMA_WAITOK | BUS_DMA_ALLOCNOW,
				 			/* flags */
				 &ss->rx_data.rx_small.dmat); /* tag */
	if (err != 0) {
		device_printf(sc->dev, "Err %d allocating rx_small dmat\n",
		    err);
		return err;
	}

	err = bus_dmamap_create(ss->rx_data.rx_small.dmat, BUS_DMA_WAITOK,
	    &ss->rx_data.rx_small.extra_map);
	if (err != 0) {
		device_printf(sc->dev, "Err %d extra rx_small dmamap\n", err);
		bus_dma_tag_destroy(ss->rx_data.rx_small.dmat);
		ss->rx_data.rx_small.dmat = NULL;
		return err;
	}
	for (i = 0; i <= ss->rx_data.rx_small.mask; i++) {
		err = bus_dmamap_create(ss->rx_data.rx_small.dmat,
		    BUS_DMA_WAITOK, &ss->rx_data.rx_small.info[i].map);
		if (err != 0) {
			int j;

			device_printf(sc->dev, "Err %d rx_small dmamap\n", err);

			for (j = 0; j < i; ++j) {
				bus_dmamap_destroy(ss->rx_data.rx_small.dmat,
				    ss->rx_data.rx_small.info[j].map);
			}
			bus_dmamap_destroy(ss->rx_data.rx_small.dmat,
			    ss->rx_data.rx_small.extra_map);
			bus_dma_tag_destroy(ss->rx_data.rx_small.dmat);
			ss->rx_data.rx_small.dmat = NULL;
			return err;
		}
	}

	err = bus_dma_tag_create(sc->parent_dmat,	/* parent */
				 1,			/* alignment */
				 4096,			/* boundary */
				 BUS_SPACE_MAXADDR,	/* low */
				 BUS_SPACE_MAXADDR,	/* high */
				 4096,			/* maxsize */
				 1,			/* num segs */
				 4096,			/* maxsegsize*/
				 BUS_DMA_WAITOK | BUS_DMA_ALLOCNOW,
				 			/* flags */
				 &ss->rx_data.rx_big.dmat); /* tag */
	if (err != 0) {
		device_printf(sc->dev, "Err %d allocating rx_big dmat\n",
		    err);
		return err;
	}

	err = bus_dmamap_create(ss->rx_data.rx_big.dmat, BUS_DMA_WAITOK,
	    &ss->rx_data.rx_big.extra_map);
	if (err != 0) {
		device_printf(sc->dev, "Err %d extra rx_big dmamap\n", err);
		bus_dma_tag_destroy(ss->rx_data.rx_big.dmat);
		ss->rx_data.rx_big.dmat = NULL;
		return err;
	}
	for (i = 0; i <= ss->rx_data.rx_big.mask; i++) {
		err = bus_dmamap_create(ss->rx_data.rx_big.dmat, BUS_DMA_WAITOK,
		    &ss->rx_data.rx_big.info[i].map);
		if (err != 0) {
			int j;

			device_printf(sc->dev, "Err %d rx_big dmamap\n", err);
			for (j = 0; j < i; ++j) {
				bus_dmamap_destroy(ss->rx_data.rx_big.dmat,
				    ss->rx_data.rx_big.info[j].map);
			}
			bus_dmamap_destroy(ss->rx_data.rx_big.dmat,
			    ss->rx_data.rx_big.extra_map);
			bus_dma_tag_destroy(ss->rx_data.rx_big.dmat);
			ss->rx_data.rx_big.dmat = NULL;
			return err;
		}
	}

	/*
	 * Now allocate TX resources
	 */

	ss->tx.mask = tx_ring_entries - 1;
	ss->tx.max_desc = MIN(MXGE_MAX_SEND_DESC, tx_ring_entries / 4);

	/*
	 * Allocate the tx request copy block; MUST be at least 8 bytes
	 * aligned
	 */
	bytes = sizeof(*ss->tx.req_list) * (ss->tx.max_desc + 4);
	ss->tx.req_list = kmalloc(__VM_CACHELINE_ALIGN(bytes),
				  M_DEVBUF,
				  M_WAITOK | M_CACHEALIGN);

	/* Allocate the tx busdma segment list */
	bytes = sizeof(*ss->tx.seg_list) * ss->tx.max_desc;
	ss->tx.seg_list = kmalloc(bytes, M_DEVBUF, M_WAITOK);

	/* Allocate the tx host info ring */
	bytes = tx_ring_entries * sizeof(*ss->tx.info);
	ss->tx.info = kmalloc(bytes, M_DEVBUF, M_ZERO|M_WAITOK);

	/* Allocate the tx busdma resources */
	err = bus_dma_tag_create(sc->parent_dmat,	/* parent */
				 1,			/* alignment */
				 sc->tx_boundary,	/* boundary */
				 BUS_SPACE_MAXADDR,	/* low */
				 BUS_SPACE_MAXADDR,	/* high */
				 IP_MAXPACKET +
				 sizeof(struct ether_vlan_header),
				 			/* maxsize */
				 ss->tx.max_desc - 2,	/* num segs */
				 sc->tx_boundary,	/* maxsegsz */
				 BUS_DMA_WAITOK | BUS_DMA_ALLOCNOW |
				 BUS_DMA_ONEBPAGE,	/* flags */
				 &ss->tx.dmat);		/* tag */
	if (err != 0) {
		device_printf(sc->dev, "Err %d allocating tx dmat\n", err);
		return err;
	}

	/*
	 * Now use these tags to setup DMA maps for each slot in the ring
	 */
	for (i = 0; i <= ss->tx.mask; i++) {
		err = bus_dmamap_create(ss->tx.dmat,
		    BUS_DMA_WAITOK | BUS_DMA_ONEBPAGE, &ss->tx.info[i].map);
		if (err != 0) {
			int j;

			device_printf(sc->dev, "Err %d tx dmamap\n", err);
			for (j = 0; j < i; ++j) {
				bus_dmamap_destroy(ss->tx.dmat,
				    ss->tx.info[j].map);
			}
			bus_dma_tag_destroy(ss->tx.dmat);
			ss->tx.dmat = NULL;
			return err;
		}
	}
	return 0;
}

static int
mxge_alloc_rings(mxge_softc_t *sc)
{
	mxge_cmd_t cmd;
	int tx_ring_size;
	int tx_ring_entries, rx_ring_entries;
	int err, slice;

	/* Get ring sizes */
	err = mxge_send_cmd(sc, MXGEFW_CMD_GET_SEND_RING_SIZE, &cmd);
	if (err != 0) {
		device_printf(sc->dev, "Cannot determine tx ring sizes\n");
		return err;
	}
	tx_ring_size = cmd.data0;

	tx_ring_entries = tx_ring_size / sizeof(mcp_kreq_ether_send_t);
	rx_ring_entries = sc->rx_intr_slots / 2;

	if (bootverbose) {
		device_printf(sc->dev, "tx desc %d, rx desc %d\n",
		    tx_ring_entries, rx_ring_entries);
	}

	sc->ifp->if_nmbclusters = rx_ring_entries * sc->num_slices;
	sc->ifp->if_nmbjclusters = sc->ifp->if_nmbclusters;

	ifq_set_maxlen(&sc->ifp->if_snd, tx_ring_entries - 1);
	ifq_set_ready(&sc->ifp->if_snd);
	ifq_set_subq_cnt(&sc->ifp->if_snd, sc->num_tx_rings);

	if (sc->num_tx_rings > 1) {
		sc->ifp->if_mapsubq = ifq_mapsubq_modulo;
		ifq_set_subq_divisor(&sc->ifp->if_snd, sc->num_tx_rings);
	}

	for (slice = 0; slice < sc->num_slices; slice++) {
		err = mxge_alloc_slice_rings(&sc->ss[slice],
		    rx_ring_entries, tx_ring_entries);
		if (err != 0) {
			device_printf(sc->dev,
			    "alloc %d slice rings failed\n", slice);
			return err;
		}
	}
	return 0;
}

static void
mxge_choose_params(int mtu, int *cl_size)
{
	int bufsize = mtu + ETHER_HDR_LEN + EVL_ENCAPLEN + MXGEFW_PAD;

	if (bufsize < MCLBYTES) {
		*cl_size = MCLBYTES;
	} else {
		KASSERT(bufsize < MJUMPAGESIZE, ("invalid MTU %d", mtu));
		*cl_size = MJUMPAGESIZE;
	}
}

static int
mxge_slice_open(struct mxge_slice_state *ss, int cl_size)
{
	mxge_cmd_t cmd;
	int err, i, slice;

	slice = ss - ss->sc->ss;

	/*
	 * Get the lanai pointers to the send and receive rings
	 */
	err = 0;

	bzero(&cmd, sizeof(cmd));	/* silence gcc warning */
	if (ss->sc->num_tx_rings == 1) {
		if (slice == 0) {
			cmd.data0 = slice;
			err = mxge_send_cmd(ss->sc, MXGEFW_CMD_GET_SEND_OFFSET,
			    &cmd);
			ss->tx.lanai = (volatile mcp_kreq_ether_send_t *)
			    (ss->sc->sram + cmd.data0);
			/* Leave send_go and send_stop as NULL */
		}
	} else {
		cmd.data0 = slice;
		err = mxge_send_cmd(ss->sc, MXGEFW_CMD_GET_SEND_OFFSET, &cmd);
		ss->tx.lanai = (volatile mcp_kreq_ether_send_t *)
		    (ss->sc->sram + cmd.data0);
		ss->tx.send_go = (volatile uint32_t *)
		    (ss->sc->sram + MXGEFW_ETH_SEND_GO + 64 * slice);
		ss->tx.send_stop = (volatile uint32_t *)
		    (ss->sc->sram + MXGEFW_ETH_SEND_STOP + 64 * slice);
	}

	cmd.data0 = slice;
	err |= mxge_send_cmd(ss->sc, MXGEFW_CMD_GET_SMALL_RX_OFFSET, &cmd);
	ss->rx_data.rx_small.lanai =
	    (volatile mcp_kreq_ether_recv_t *)(ss->sc->sram + cmd.data0);

	cmd.data0 = slice;
	err |= mxge_send_cmd(ss->sc, MXGEFW_CMD_GET_BIG_RX_OFFSET, &cmd);
	ss->rx_data.rx_big.lanai =
	    (volatile mcp_kreq_ether_recv_t *)(ss->sc->sram + cmd.data0);

	if (err != 0) {
		if_printf(ss->sc->ifp,
		    "failed to get ring sizes or locations\n");
		return EIO;
	}

	/*
	 * Stock small receive ring
	 */
	for (i = 0; i <= ss->rx_data.rx_small.mask; i++) {
		err = mxge_get_buf_small(&ss->rx_data.rx_small,
		    ss->rx_data.rx_small.info[i].map, i, TRUE);
		if (err) {
			if_printf(ss->sc->ifp, "alloced %d/%d smalls\n", i,
			    ss->rx_data.rx_small.mask + 1);
			return ENOMEM;
		}
	}

	/*
	 * Stock big receive ring
	 */
	for (i = 0; i <= ss->rx_data.rx_big.mask; i++) {
		ss->rx_data.rx_big.shadow[i].addr_low = 0xffffffff;
		ss->rx_data.rx_big.shadow[i].addr_high = 0xffffffff;
	}

	ss->rx_data.rx_big.cl_size = cl_size;

	for (i = 0; i <= ss->rx_data.rx_big.mask; i++) {
		err = mxge_get_buf_big(&ss->rx_data.rx_big,
		    ss->rx_data.rx_big.info[i].map, i, TRUE);
		if (err) {
			if_printf(ss->sc->ifp, "alloced %d/%d bigs\n", i,
			    ss->rx_data.rx_big.mask + 1);
			return ENOMEM;
		}
	}
	return 0;
}

static int 
mxge_open(mxge_softc_t *sc)
{
	struct ifnet *ifp = sc->ifp;
	mxge_cmd_t cmd;
	int err, slice, cl_size, i;
	bus_addr_t bus;
	volatile uint8_t *itable;
	struct mxge_slice_state *ss;

	ASSERT_IFNET_SERIALIZED_ALL(ifp);

	/* Copy the MAC address in case it was overridden */
	bcopy(IF_LLADDR(ifp), sc->mac_addr, ETHER_ADDR_LEN);

	err = mxge_reset(sc, 1);
	if (err != 0) {
		if_printf(ifp, "failed to reset\n");
		return EIO;
	}

	if (sc->num_slices > 1) {
		/*
		 * Setup the indirect table.
		 */
		if_ringmap_rdrtable(sc->ring_map, sc->rdr_table, NETISR_CPUMAX);

		cmd.data0 = NETISR_CPUMAX;
		err = mxge_send_cmd(sc, MXGEFW_CMD_SET_RSS_TABLE_SIZE, &cmd);

		err |= mxge_send_cmd(sc, MXGEFW_CMD_GET_RSS_TABLE_OFFSET, &cmd);
		if (err != 0) {
			if_printf(ifp, "failed to setup rss tables\n");
			return err;
		}

		itable = sc->sram + cmd.data0;
		for (i = 0; i < NETISR_CPUMAX; i++)
			itable[i] = sc->rdr_table[i];

		if (sc->use_rss) {
			volatile uint8_t *hwkey;
			uint8_t swkey[MXGE_HWRSS_KEYLEN];

			/*
			 * Setup Toeplitz key.
			 */
			err = mxge_send_cmd(sc, MXGEFW_CMD_GET_RSS_KEY_OFFSET,
			    &cmd);
			if (err != 0) {
				if_printf(ifp, "failed to get rsskey\n");
				return err;
			}
			hwkey = sc->sram + cmd.data0;

			toeplitz_get_key(swkey, MXGE_HWRSS_KEYLEN);
			for (i = 0; i < MXGE_HWRSS_KEYLEN; ++i)
				hwkey[i] = swkey[i];
			wmb();

			err = mxge_send_cmd(sc, MXGEFW_CMD_RSS_KEY_UPDATED,
			    &cmd);
			if (err != 0) {
				if_printf(ifp, "failed to update rsskey\n");
				return err;
			}
			if (bootverbose)
				if_printf(ifp, "RSS key updated\n");
		}

		cmd.data0 = 1;
		if (sc->use_rss) {
			if (bootverbose)
				if_printf(ifp, "input hash: RSS\n");
			cmd.data1 = MXGEFW_RSS_HASH_TYPE_IPV4 |
			    MXGEFW_RSS_HASH_TYPE_TCP_IPV4;
		} else {
			if (bootverbose)
				if_printf(ifp, "input hash: SRC_DST_PORT\n");
			cmd.data1 = MXGEFW_RSS_HASH_TYPE_SRC_DST_PORT;
		}
		err = mxge_send_cmd(sc, MXGEFW_CMD_SET_RSS_ENABLE, &cmd);
		if (err != 0) {
			if_printf(ifp, "failed to enable slices\n");
			return err;
		}
	}

	cmd.data0 = MXGEFW_TSO_MODE_NDIS;
	err = mxge_send_cmd(sc, MXGEFW_CMD_SET_TSO_MODE, &cmd);
	if (err) {
		/*
		 * Can't change TSO mode to NDIS, never allow TSO then
		 */
		if_printf(ifp, "failed to set TSO mode\n");
		ifp->if_capenable &= ~IFCAP_TSO;
		ifp->if_capabilities &= ~IFCAP_TSO;
		ifp->if_hwassist &= ~CSUM_TSO;
	}

	mxge_choose_params(ifp->if_mtu, &cl_size);

	cmd.data0 = 1;
	err = mxge_send_cmd(sc, MXGEFW_CMD_ALWAYS_USE_N_BIG_BUFFERS, &cmd);
	/*
	 * Error is only meaningful if we're trying to set
	 * MXGEFW_CMD_ALWAYS_USE_N_BIG_BUFFERS > 1
	 */

	/*
	 * Give the firmware the mtu and the big and small buffer
	 * sizes.  The firmware wants the big buf size to be a power
	 * of two. Luckily, DragonFly's clusters are powers of two
	 */
	cmd.data0 = ifp->if_mtu + ETHER_HDR_LEN + EVL_ENCAPLEN;
	err = mxge_send_cmd(sc, MXGEFW_CMD_SET_MTU, &cmd);

	cmd.data0 = MXGE_RX_SMALL_BUFLEN;
	err |= mxge_send_cmd(sc, MXGEFW_CMD_SET_SMALL_BUFFER_SIZE, &cmd);

	cmd.data0 = cl_size;
	err |= mxge_send_cmd(sc, MXGEFW_CMD_SET_BIG_BUFFER_SIZE, &cmd);

	if (err != 0) {
		if_printf(ifp, "failed to setup params\n");
		goto abort;
	}

	/* Now give him the pointer to the stats block */
	for (slice = 0; slice < sc->num_slices; slice++) {
		ss = &sc->ss[slice];
		cmd.data0 = MXGE_LOWPART_TO_U32(ss->fw_stats_dma.dmem_busaddr);
		cmd.data1 = MXGE_HIGHPART_TO_U32(ss->fw_stats_dma.dmem_busaddr);
		cmd.data2 = sizeof(struct mcp_irq_data);
		cmd.data2 |= (slice << 16);
		err |= mxge_send_cmd(sc, MXGEFW_CMD_SET_STATS_DMA_V2, &cmd);
	}

	if (err != 0) {
		bus = sc->ss->fw_stats_dma.dmem_busaddr;
		bus += offsetof(struct mcp_irq_data, send_done_count);
		cmd.data0 = MXGE_LOWPART_TO_U32(bus);
		cmd.data1 = MXGE_HIGHPART_TO_U32(bus);
		err = mxge_send_cmd(sc, MXGEFW_CMD_SET_STATS_DMA_OBSOLETE,
		    &cmd);

		/* Firmware cannot support multicast without STATS_DMA_V2 */
		sc->fw_multicast_support = 0;
	} else {
		sc->fw_multicast_support = 1;
	}

	if (err != 0) {
		if_printf(ifp, "failed to setup params\n");
		goto abort;
	}

	for (slice = 0; slice < sc->num_slices; slice++) {
		err = mxge_slice_open(&sc->ss[slice], cl_size);
		if (err != 0) {
			if_printf(ifp, "couldn't open slice %d\n", slice);
			goto abort;
		}
	}

	/* Finally, start the firmware running */
	err = mxge_send_cmd(sc, MXGEFW_CMD_ETHERNET_UP, &cmd);
	if (err) {
		if_printf(ifp, "Couldn't bring up link\n");
		goto abort;
	}

	ifp->if_flags |= IFF_RUNNING;
	for (i = 0; i < sc->num_tx_rings; ++i) {
		mxge_tx_ring_t *tx = &sc->ss[i].tx;

		ifsq_clr_oactive(tx->ifsq);
		ifsq_watchdog_start(&tx->watchdog);
	}

	return 0;

abort:
	mxge_free_mbufs(sc);
	return err;
}

static void
mxge_close(mxge_softc_t *sc, int down)
{
	struct ifnet *ifp = sc->ifp;
	mxge_cmd_t cmd;
	int err, old_down_cnt, i;

	ASSERT_IFNET_SERIALIZED_ALL(ifp);

	if (!down) {
		old_down_cnt = sc->down_cnt;
		wmb();

		err = mxge_send_cmd(sc, MXGEFW_CMD_ETHERNET_DOWN, &cmd);
		if (err)
			if_printf(ifp, "Couldn't bring down link\n");

		if (old_down_cnt == sc->down_cnt) {
			/*
			 * Wait for down irq
			 * XXX racy
			 */
			ifnet_deserialize_all(ifp);
			DELAY(10 * sc->intr_coal_delay);
			ifnet_serialize_all(ifp);
		}

		wmb();
		if (old_down_cnt == sc->down_cnt)
			if_printf(ifp, "never got down irq\n");
	}
	mxge_free_mbufs(sc);

	ifp->if_flags &= ~IFF_RUNNING;
	for (i = 0; i < sc->num_tx_rings; ++i) {
		mxge_tx_ring_t *tx = &sc->ss[i].tx;

		ifsq_clr_oactive(tx->ifsq);
		ifsq_watchdog_stop(&tx->watchdog);
	}
}

static void
mxge_setup_cfg_space(mxge_softc_t *sc)
{
	device_t dev = sc->dev;
	int reg;
	uint16_t lnk, pectl;

	/* Find the PCIe link width and set max read request to 4KB */
	if (pci_find_extcap(dev, PCIY_EXPRESS, &reg) == 0) {
		lnk = pci_read_config(dev, reg + 0x12, 2);
		sc->link_width = (lnk >> 4) & 0x3f;

		if (sc->pectl == 0) {
			pectl = pci_read_config(dev, reg + 0x8, 2);
			pectl = (pectl & ~0x7000) | (5 << 12);
			pci_write_config(dev, reg + 0x8, pectl, 2);
			sc->pectl = pectl;
		} else {
			/* Restore saved pectl after watchdog reset */
			pci_write_config(dev, reg + 0x8, sc->pectl, 2);
		}
	}

	/* Enable DMA and memory space access */
	pci_enable_busmaster(dev);
}

static uint32_t
mxge_read_reboot(mxge_softc_t *sc)
{
	device_t dev = sc->dev;
	uint32_t vs;

	/* Find the vendor specific offset */
	if (pci_find_extcap(dev, PCIY_VENDOR, &vs) != 0) {
		if_printf(sc->ifp, "could not find vendor specific offset\n");
		return (uint32_t)-1;
	}
	/* Enable read32 mode */
	pci_write_config(dev, vs + 0x10, 0x3, 1);
	/* Tell NIC which register to read */
	pci_write_config(dev, vs + 0x18, 0xfffffff0, 4);
	return pci_read_config(dev, vs + 0x14, 4);
}

static void
mxge_watchdog_reset(mxge_softc_t *sc)
{
	struct pci_devinfo *dinfo;
	int err, running;
	uint32_t reboot;
	uint16_t cmd;

	err = ENXIO;

	if_printf(sc->ifp, "Watchdog reset!\n");

	/*
	 * Check to see if the NIC rebooted.  If it did, then all of
	 * PCI config space has been reset, and things like the
	 * busmaster bit will be zero.  If this is the case, then we
	 * must restore PCI config space before the NIC can be used
	 * again
	 */
	cmd = pci_read_config(sc->dev, PCIR_COMMAND, 2);
	if (cmd == 0xffff) {
		/* 
		 * Maybe the watchdog caught the NIC rebooting; wait
		 * up to 100ms for it to finish.  If it does not come
		 * back, then give up 
		 */
		DELAY(1000*100);
		cmd = pci_read_config(sc->dev, PCIR_COMMAND, 2);
		if (cmd == 0xffff)
			if_printf(sc->ifp, "NIC disappeared!\n");
	}
	if ((cmd & PCIM_CMD_BUSMASTEREN) == 0) {
		/* Print the reboot status */
		reboot = mxge_read_reboot(sc);
		if_printf(sc->ifp, "NIC rebooted, status = 0x%x\n", reboot);

		running = sc->ifp->if_flags & IFF_RUNNING;
		if (running) {
			/*
			 * Quiesce NIC so that TX routines will not try to
			 * xmit after restoration of BAR
			 */

			/* Mark the link as down */
			if (sc->link_state) {
				sc->ifp->if_link_state = LINK_STATE_DOWN;
				if_link_state_change(sc->ifp);
			}
			mxge_close(sc, 1);
		}
		/* Restore PCI configuration space */
		dinfo = device_get_ivars(sc->dev);
		pci_cfg_restore(sc->dev, dinfo);

		/* And redo any changes we made to our config space */
		mxge_setup_cfg_space(sc);

		/* Reload f/w */
		err = mxge_load_firmware(sc, 0);
		if (err)
			if_printf(sc->ifp, "Unable to re-load f/w\n");
		if (running && !err) {
			int i;

			err = mxge_open(sc);

			for (i = 0; i < sc->num_tx_rings; ++i)
				ifsq_devstart_sched(sc->ss[i].tx.ifsq);
		}
		sc->watchdog_resets++;
	} else {
		if_printf(sc->ifp, "NIC did not reboot, not resetting\n");
		err = 0;
	}
	if (err) {
		if_printf(sc->ifp, "watchdog reset failed\n");
	} else {
		if (sc->dying == 2)
			sc->dying = 0;
		callout_reset(&sc->co_hdl, mxge_ticks, mxge_tick, sc);
	}
}

static void
mxge_warn_stuck(mxge_softc_t *sc, mxge_tx_ring_t *tx, int slice)
{
	if_printf(sc->ifp, "slice %d struck? ring state:\n", slice);
	if_printf(sc->ifp, "tx.req=%d tx.done=%d, tx.queue_active=%d\n",
	    tx->req, tx->done, tx->queue_active);
	if_printf(sc->ifp, "tx.activate=%d tx.deactivate=%d\n",
	    tx->activate, tx->deactivate);
	if_printf(sc->ifp, "pkt_done=%d fw=%d\n",
	    tx->pkt_done, be32toh(sc->ss->fw_stats->send_done_count));
}

static u_long
mxge_update_stats(mxge_softc_t *sc)
{
	u_long ipackets, opackets, pkts;

	IFNET_STAT_GET(sc->ifp, ipackets, ipackets);
	IFNET_STAT_GET(sc->ifp, opackets, opackets);

	pkts = ipackets - sc->ipackets;
	pkts += opackets - sc->opackets;

	sc->ipackets = ipackets;
	sc->opackets = opackets;

	return pkts;
}

static void
mxge_tick(void *arg)
{
	mxge_softc_t *sc = arg;
	u_long pkts = 0;
	int err = 0;
	int ticks;

	lwkt_serialize_enter(&sc->main_serialize);

	ticks = mxge_ticks;
	if (sc->ifp->if_flags & IFF_RUNNING) {
		/* Aggregate stats from different slices */
		pkts = mxge_update_stats(sc);
		if (sc->need_media_probe)
			mxge_media_probe(sc);
	}
	if (pkts == 0) {
		uint16_t cmd;

		/* Ensure NIC did not suffer h/w fault while idle */
		cmd = pci_read_config(sc->dev, PCIR_COMMAND, 2);		
		if ((cmd & PCIM_CMD_BUSMASTEREN) == 0) {
			sc->dying = 2;
			mxge_serialize_skipmain(sc);
			mxge_watchdog_reset(sc);
			mxge_deserialize_skipmain(sc);
			err = ENXIO;
		}

		/* Look less often if NIC is idle */
		ticks *= 4;
	}

	if (err == 0)
		callout_reset(&sc->co_hdl, ticks, mxge_tick, sc);

	lwkt_serialize_exit(&sc->main_serialize);
}

static int
mxge_media_change(struct ifnet *ifp)
{
	mxge_softc_t *sc = ifp->if_softc;
	const struct ifmedia *ifm = &sc->media;
	int pause;

	if (IFM_OPTIONS(ifm->ifm_media) & (IFM_ETH_RXPAUSE | IFM_ETH_TXPAUSE)) {
		if (sc->pause)
			return 0;
		pause = 1;
	} else {
		if (!sc->pause)
			return 0;
		pause = 0;
	}
	return mxge_change_pause(sc, pause);
}

static int
mxge_change_mtu(mxge_softc_t *sc, int mtu)
{
	struct ifnet *ifp = sc->ifp;
	int real_mtu, old_mtu;
	int err = 0;

	real_mtu = mtu + ETHER_HDR_LEN + EVL_ENCAPLEN;
	if (mtu > sc->max_mtu || real_mtu < 60)
		return EINVAL;

	old_mtu = ifp->if_mtu;
	ifp->if_mtu = mtu;
	if (ifp->if_flags & IFF_RUNNING) {
		mxge_close(sc, 0);
		err = mxge_open(sc);
		if (err != 0) {
			ifp->if_mtu = old_mtu;
			mxge_close(sc, 0);
			mxge_open(sc);
		}
	}
	return err;
}	

static void
mxge_media_status(struct ifnet *ifp, struct ifmediareq *ifmr)
{
	mxge_softc_t *sc = ifp->if_softc;

	ifmr->ifm_status = IFM_AVALID;
	ifmr->ifm_active = IFM_ETHER;

	if (sc->link_state)
		ifmr->ifm_status |= IFM_ACTIVE;

	/*
	 * Autoselect is not supported, so the current media
	 * should be delivered.
	 */
	ifmr->ifm_active |= sc->current_media;
	if (sc->current_media != IFM_NONE) {
		ifmr->ifm_active |= MXGE_IFM;
		if (sc->pause)
			ifmr->ifm_active |= IFM_ETH_RXPAUSE | IFM_ETH_TXPAUSE;
	}
}

static int
mxge_ioctl(struct ifnet *ifp, u_long command, caddr_t data,
    struct ucred *cr __unused)
{
	mxge_softc_t *sc = ifp->if_softc;
	struct ifreq *ifr = (struct ifreq *)data;
	int err, mask;

	ASSERT_IFNET_SERIALIZED_ALL(ifp);
	err = 0;

	switch (command) {
	case SIOCSIFMTU:
		err = mxge_change_mtu(sc, ifr->ifr_mtu);
		break;

	case SIOCSIFFLAGS:
		if (sc->dying)
			return EINVAL;

		if (ifp->if_flags & IFF_UP) {
			if (!(ifp->if_flags & IFF_RUNNING)) {
				err = mxge_open(sc);
			} else {
				/*
				 * Take care of PROMISC and ALLMULTI
				 * flag changes
				 */
				mxge_change_promisc(sc,
				    ifp->if_flags & IFF_PROMISC);
				mxge_set_multicast_list(sc);
			}
		} else {
			if (ifp->if_flags & IFF_RUNNING)
				mxge_close(sc, 0);
		}
		break;

	case SIOCADDMULTI:
	case SIOCDELMULTI:
		mxge_set_multicast_list(sc);
		break;

	case SIOCSIFCAP:
		mask = ifr->ifr_reqcap ^ ifp->if_capenable;
		if (mask & IFCAP_TXCSUM) {
			ifp->if_capenable ^= IFCAP_TXCSUM;
			if (ifp->if_capenable & IFCAP_TXCSUM)
				ifp->if_hwassist |= CSUM_TCP | CSUM_UDP;
			else
				ifp->if_hwassist &= ~(CSUM_TCP | CSUM_UDP);
		}
		if (mask & IFCAP_TSO) {
			ifp->if_capenable ^= IFCAP_TSO;
			if (ifp->if_capenable & IFCAP_TSO)
				ifp->if_hwassist |= CSUM_TSO;
			else
				ifp->if_hwassist &= ~CSUM_TSO;
		}
		if (mask & IFCAP_RXCSUM)
			ifp->if_capenable ^= IFCAP_RXCSUM;
		if (mask & IFCAP_VLAN_HWTAGGING)
			ifp->if_capenable ^= IFCAP_VLAN_HWTAGGING;
		break;

	case SIOCGIFMEDIA:
	case SIOCSIFMEDIA:
		err = ifmedia_ioctl(ifp, (struct ifreq *)data,
		    &sc->media, command);
		break;

	default:
		err = ether_ioctl(ifp, command, data);
		break;
	}
	return err;
}

static void
mxge_fetch_tunables(mxge_softc_t *sc)
{
	int ifm;

	sc->intr_coal_delay = mxge_intr_coal_delay;
	if (sc->intr_coal_delay < 0 || sc->intr_coal_delay > (10 * 1000))
		sc->intr_coal_delay = MXGE_INTR_COAL_DELAY;

	/* XXX */
	if (mxge_ticks == 0)
		mxge_ticks = hz / 2;

	ifm = ifmedia_str2ethfc(mxge_flowctrl);
	if (ifm & (IFM_ETH_RXPAUSE | IFM_ETH_TXPAUSE))
		sc->pause = 1;

	sc->use_rss = mxge_use_rss;

	sc->throttle = mxge_throttle;
	if (sc->throttle && sc->throttle > MXGE_MAX_THROTTLE)
		sc->throttle = MXGE_MAX_THROTTLE;
	if (sc->throttle && sc->throttle < MXGE_MIN_THROTTLE)
		sc->throttle = MXGE_MIN_THROTTLE;
}

static void
mxge_free_slices(mxge_softc_t *sc)
{
	struct mxge_slice_state *ss;
	int i;

	if (sc->ss == NULL)
		return;

	for (i = 0; i < sc->num_slices; i++) {
		ss = &sc->ss[i];
		if (ss->fw_stats != NULL) {
			mxge_dma_free(&ss->fw_stats_dma);
			ss->fw_stats = NULL;
		}
		if (ss->rx_data.rx_done.entry != NULL) {
			mxge_dma_free(&ss->rx_done_dma);
			ss->rx_data.rx_done.entry = NULL;
		}
	}
	kfree(sc->ss, M_DEVBUF);
	sc->ss = NULL;
}

static int
mxge_alloc_slices(mxge_softc_t *sc)
{
	mxge_cmd_t cmd;
	struct mxge_slice_state *ss;
	size_t bytes;
	int err, i, rx_ring_size;

	err = mxge_send_cmd(sc, MXGEFW_CMD_GET_RX_RING_SIZE, &cmd);
	if (err != 0) {
		device_printf(sc->dev, "Cannot determine rx ring size\n");
		return err;
	}
	rx_ring_size = cmd.data0;
	sc->rx_intr_slots = 2 * (rx_ring_size / sizeof (mcp_dma_addr_t));

	bytes = sizeof(*sc->ss) * sc->num_slices;
	sc->ss = kmalloc(bytes, M_DEVBUF,
			 M_WAITOK | M_ZERO | M_CACHEALIGN);

	for (i = 0; i < sc->num_slices; i++) {
		ss = &sc->ss[i];

		ss->sc = sc;

		lwkt_serialize_init(&ss->rx_data.rx_serialize);
		lwkt_serialize_init(&ss->tx.tx_serialize);
		ss->intr_rid = -1;

		/*
		 * Allocate per-slice rx interrupt queue
		 * XXX assume 4bytes mcp_slot
		 */
		bytes = sc->rx_intr_slots * sizeof(mcp_slot_t);
		err = mxge_dma_alloc(sc, &ss->rx_done_dma, bytes, 4096);
		if (err != 0) {
			device_printf(sc->dev,
			    "alloc %d slice rx_done failed\n", i);
			return err;
		}
		ss->rx_data.rx_done.entry = ss->rx_done_dma.dmem_addr;

		/*
		 * Allocate the per-slice firmware stats
		 */
		bytes = sizeof(*ss->fw_stats);
		err = mxge_dma_alloc(sc, &ss->fw_stats_dma,
		    sizeof(*ss->fw_stats), 64);
		if (err != 0) {
			device_printf(sc->dev,
			    "alloc %d fw_stats failed\n", i);
			return err;
		}
		ss->fw_stats = ss->fw_stats_dma.dmem_addr;
	}
	return 0;
}

static void
mxge_slice_probe(mxge_softc_t *sc)
{
	int status, max_intr_slots, max_slices, num_slices;
	int msix_cnt, msix_enable, multi_tx;
	mxge_cmd_t cmd;
	const char *old_fw;

	sc->num_slices = 1;
	sc->num_tx_rings = 1;

	num_slices = device_getenv_int(sc->dev, "num_slices", mxge_num_slices);
	if (num_slices == 1)
		return;

	if (netisr_ncpus == 1)
		return;

	msix_enable = device_getenv_int(sc->dev, "msix.enable",
	    mxge_msix_enable);
	if (!msix_enable)
		return;

	msix_cnt = pci_msix_count(sc->dev);
	if (msix_cnt < 2)
		return;
	if (bootverbose)
		device_printf(sc->dev, "MSI-X count %d\n", msix_cnt);

	/*
	 * Now load the slice aware firmware see what it supports
	 */
	old_fw = sc->fw_name;
	if (old_fw == mxge_fw_aligned)
		sc->fw_name = mxge_fw_rss_aligned;
	else
		sc->fw_name = mxge_fw_rss_unaligned;
	status = mxge_load_firmware(sc, 0);
	if (status != 0) {
		device_printf(sc->dev, "Falling back to a single slice\n");
		return;
	}

	/*
	 * Try to send a reset command to the card to see if it is alive
	 */
	memset(&cmd, 0, sizeof(cmd));
	status = mxge_send_cmd(sc, MXGEFW_CMD_RESET, &cmd);
	if (status != 0) {
		device_printf(sc->dev, "failed reset\n");
		goto abort_with_fw;
	}

	/*
	 * Get rx ring size to calculate rx interrupt queue size
	 */
	status = mxge_send_cmd(sc, MXGEFW_CMD_GET_RX_RING_SIZE, &cmd);
	if (status != 0) {
		device_printf(sc->dev, "Cannot determine rx ring size\n");
		goto abort_with_fw;
	}
	max_intr_slots = 2 * (cmd.data0 / sizeof(mcp_dma_addr_t));

	/*
	 * Tell it the size of the rx interrupt queue
	 */
	cmd.data0 = max_intr_slots * sizeof(struct mcp_slot);
	status = mxge_send_cmd(sc, MXGEFW_CMD_SET_INTRQ_SIZE, &cmd);
	if (status != 0) {
		device_printf(sc->dev, "failed MXGEFW_CMD_SET_INTRQ_SIZE\n");
		goto abort_with_fw;
	}

	/*
	 * Ask the maximum number of slices it supports
	 */
	status = mxge_send_cmd(sc, MXGEFW_CMD_GET_MAX_RSS_QUEUES, &cmd);
	if (status != 0) {
		device_printf(sc->dev,
		    "failed MXGEFW_CMD_GET_MAX_RSS_QUEUES\n");
		goto abort_with_fw;
	}
	max_slices = cmd.data0;
	if (bootverbose)
		device_printf(sc->dev, "max slices %d\n", max_slices);

	if (max_slices > msix_cnt)
		max_slices = msix_cnt;

	sc->ring_map = if_ringmap_alloc(sc->dev, num_slices, max_slices);
	sc->num_slices = if_ringmap_count(sc->ring_map);

	multi_tx = device_getenv_int(sc->dev, "multi_tx", mxge_multi_tx);
	if (multi_tx)
		sc->num_tx_rings = sc->num_slices;

	if (bootverbose) {
		device_printf(sc->dev, "using %d slices, max %d\n",
		    sc->num_slices, max_slices);
	}

	if (sc->num_slices == 1)
		goto abort_with_fw;
	return;

abort_with_fw:
	sc->fw_name = old_fw;
	mxge_load_firmware(sc, 0);
}

static void
mxge_setup_serialize(struct mxge_softc *sc)
{
	int i = 0, slice;

	/* Main + rx + tx */
	sc->nserialize = (2 * sc->num_slices) + 1;
	sc->serializes =
	    kmalloc(sc->nserialize * sizeof(struct lwkt_serialize *),
	        M_DEVBUF, M_WAITOK | M_ZERO);

	/*
	 * Setup serializes
	 *
	 * NOTE: Order is critical
	 */

	KKASSERT(i < sc->nserialize);
	sc->serializes[i++] = &sc->main_serialize;

	for (slice = 0; slice < sc->num_slices; ++slice) {
		KKASSERT(i < sc->nserialize);
		sc->serializes[i++] = &sc->ss[slice].rx_data.rx_serialize;
	}

	for (slice = 0; slice < sc->num_slices; ++slice) {
		KKASSERT(i < sc->nserialize);
		sc->serializes[i++] = &sc->ss[slice].tx.tx_serialize;
	}

	KKASSERT(i == sc->nserialize);
}

static void
mxge_serialize(struct ifnet *ifp, enum ifnet_serialize slz)
{
	struct mxge_softc *sc = ifp->if_softc;

	ifnet_serialize_array_enter(sc->serializes, sc->nserialize, slz);
}

static void
mxge_deserialize(struct ifnet *ifp, enum ifnet_serialize slz)
{
	struct mxge_softc *sc = ifp->if_softc;

	ifnet_serialize_array_exit(sc->serializes, sc->nserialize, slz);
}

static int
mxge_tryserialize(struct ifnet *ifp, enum ifnet_serialize slz)
{
	struct mxge_softc *sc = ifp->if_softc;

	return ifnet_serialize_array_try(sc->serializes, sc->nserialize, slz);
}

#ifdef INVARIANTS

static void
mxge_serialize_assert(struct ifnet *ifp, enum ifnet_serialize slz,
    boolean_t serialized)
{
	struct mxge_softc *sc = ifp->if_softc;

	ifnet_serialize_array_assert(sc->serializes, sc->nserialize,
	    slz, serialized);
}

#endif	/* INVARIANTS */

#ifdef IFPOLL_ENABLE

static void
mxge_npoll_rx(struct ifnet *ifp, void *xss, int cycle)
{
	struct mxge_slice_state *ss = xss;
	mxge_rx_done_t *rx_done = &ss->rx_data.rx_done;

	ASSERT_SERIALIZED(&ss->rx_data.rx_serialize);

	if (rx_done->entry[rx_done->idx].length != 0) {
		mxge_clean_rx_done(&ss->sc->arpcom.ac_if, &ss->rx_data, cycle);
	} else {
		/*
		 * XXX
		 * This register writting obviously has cost,
		 * however, if we don't hand back the rx token,
		 * the upcoming packets may suffer rediculously
		 * large delay, as observed on 8AL-C using ping(8).
		 */
		*ss->irq_claim = be32toh(3);
	}
}

static void
mxge_npoll(struct ifnet *ifp, struct ifpoll_info *info)
{
	struct mxge_softc *sc = ifp->if_softc;
	int i;

	if (info == NULL)
		return;

	/*
	 * Only poll rx; polling tx and status don't seem to work
	 */
	for (i = 0; i < sc->num_slices; ++i) {
		struct mxge_slice_state *ss = &sc->ss[i];
		int cpu = ss->intr_cpuid;

		KKASSERT(cpu < netisr_ncpus);
		info->ifpi_rx[cpu].poll_func = mxge_npoll_rx;
		info->ifpi_rx[cpu].arg = ss;
		info->ifpi_rx[cpu].serializer = &ss->rx_data.rx_serialize;
	}
}

#endif	/* IFPOLL_ENABLE */

static int 
mxge_attach(device_t dev)
{
	mxge_softc_t *sc = device_get_softc(dev);
	struct ifnet *ifp = &sc->arpcom.ac_if;
	int err, rid, i;

	/*
	 * Avoid rewriting half the lines in this file to use
	 * &sc->arpcom.ac_if instead
	 */
	sc->ifp = ifp;
	sc->dev = dev;
	if_initname(ifp, device_get_name(dev), device_get_unit(dev));

	/* IFM_ETH_FORCEPAUSE can't be changed */
	ifmedia_init(&sc->media, IFM_ETH_RXPAUSE | IFM_ETH_TXPAUSE,
	    mxge_media_change, mxge_media_status);

	lwkt_serialize_init(&sc->main_serialize);

	mxge_fetch_tunables(sc);

	err = bus_dma_tag_create(NULL,			/* parent */
				 1,			/* alignment */
				 0,			/* boundary */
				 BUS_SPACE_MAXADDR,	/* low */
				 BUS_SPACE_MAXADDR,	/* high */
				 BUS_SPACE_MAXSIZE_32BIT,/* maxsize */
				 0, 			/* num segs */
				 BUS_SPACE_MAXSIZE_32BIT,/* maxsegsize */
				 0,			/* flags */
				 &sc->parent_dmat);	/* tag */
	if (err != 0) {
		device_printf(dev, "Err %d allocating parent dmat\n", err);
		goto failed;
	}

	callout_init_mp(&sc->co_hdl);

	mxge_setup_cfg_space(sc);

	/*
	 * Map the board into the kernel
	 */
	rid = PCIR_BARS;
	sc->mem_res = bus_alloc_resource_any(dev, SYS_RES_MEMORY,
	    &rid, RF_ACTIVE);
	if (sc->mem_res == NULL) {
		device_printf(dev, "could not map memory\n");
		err = ENXIO;
		goto failed;
	}

	sc->sram = rman_get_virtual(sc->mem_res);
	sc->sram_size = 2*1024*1024 - (2*(48*1024)+(32*1024)) - 0x100;
	if (sc->sram_size > rman_get_size(sc->mem_res)) {
		device_printf(dev, "impossible memory region size %ld\n",
		    rman_get_size(sc->mem_res));
		err = ENXIO;
		goto failed;
	}

	/*
	 * Make NULL terminated copy of the EEPROM strings section of
	 * lanai SRAM
	 */
	bzero(sc->eeprom_strings, MXGE_EEPROM_STRINGS_SIZE);
	bus_space_read_region_1(rman_get_bustag(sc->mem_res),
	    rman_get_bushandle(sc->mem_res),
	    sc->sram_size - MXGE_EEPROM_STRINGS_SIZE,
	    sc->eeprom_strings, MXGE_EEPROM_STRINGS_SIZE - 2);
	err = mxge_parse_strings(sc);
	if (err != 0) {
		device_printf(dev, "parse EEPROM string failed\n");
		goto failed;
	}

	/*
	 * Enable write combining for efficient use of PCIe bus
	 */
	mxge_enable_wc(sc);

	/*
	 * Allocate the out of band DMA memory
	 */
	err = mxge_dma_alloc(sc, &sc->cmd_dma, sizeof(mxge_cmd_t), 64);
	if (err != 0) {
		device_printf(dev, "alloc cmd DMA buf failed\n");
		goto failed;
	}
	sc->cmd = sc->cmd_dma.dmem_addr;

	err = mxge_dma_alloc(sc, &sc->zeropad_dma, 64, 64);
	if (err != 0) {
		device_printf(dev, "alloc zeropad DMA buf failed\n");
		goto failed;
	}

	err = mxge_dma_alloc(sc, &sc->dmabench_dma, 4096, 4096);
	if (err != 0) {
		device_printf(dev, "alloc dmabench DMA buf failed\n");
		goto failed;
	}

	/* Select & load the firmware */
	err = mxge_select_firmware(sc);
	if (err != 0) {
		device_printf(dev, "select firmware failed\n");
		goto failed;
	}

	mxge_slice_probe(sc);
	err = mxge_alloc_slices(sc);
	if (err != 0) {
		device_printf(dev, "alloc slices failed\n");
		goto failed;
	}

	err = mxge_alloc_intr(sc);
	if (err != 0) {
		device_printf(dev, "alloc intr failed\n");
		goto failed;
	}

	/* Setup serializes */
	mxge_setup_serialize(sc);

	err = mxge_reset(sc, 0);
	if (err != 0) {
		device_printf(dev, "reset failed\n");
		goto failed;
	}

	err = mxge_alloc_rings(sc);
	if (err != 0) {
		device_printf(dev, "failed to allocate rings\n");
		goto failed;
	}

	ifp->if_baudrate = IF_Gbps(10UL);
	ifp->if_capabilities = IFCAP_RXCSUM | IFCAP_TXCSUM | IFCAP_TSO;
	ifp->if_hwassist = CSUM_TCP | CSUM_UDP | CSUM_TSO;

	ifp->if_capabilities |= IFCAP_VLAN_MTU;
#if 0
	/* Well, its software, sigh */
	ifp->if_capabilities |= IFCAP_VLAN_HWTAGGING;
#endif
	ifp->if_capenable = ifp->if_capabilities;

	ifp->if_softc = sc;
	ifp->if_flags = IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST;
	ifp->if_init = mxge_init;
	ifp->if_ioctl = mxge_ioctl;
	ifp->if_start = mxge_start;
#ifdef IFPOLL_ENABLE
	if (sc->intr_type != PCI_INTR_TYPE_LEGACY)
		ifp->if_npoll = mxge_npoll;
#endif
	ifp->if_serialize = mxge_serialize;
	ifp->if_deserialize = mxge_deserialize;
	ifp->if_tryserialize = mxge_tryserialize;
#ifdef INVARIANTS
	ifp->if_serialize_assert = mxge_serialize_assert;
#endif

	/* Increase TSO burst length */
	ifp->if_tsolen = (32 * ETHERMTU);

	/* Initialise the ifmedia structure */
	mxge_media_init(sc);
	mxge_media_probe(sc);

	ether_ifattach(ifp, sc->mac_addr, NULL);

	/* Setup TX rings and subqueues */
	for (i = 0; i < sc->num_tx_rings; ++i) {
		struct ifaltq_subque *ifsq = ifq_get_subq(&ifp->if_snd, i);
		struct mxge_slice_state *ss = &sc->ss[i];

		ifsq_set_cpuid(ifsq, ss->intr_cpuid);
		ifsq_set_hw_serialize(ifsq, &ss->tx.tx_serialize);
		ifsq_set_priv(ifsq, &ss->tx);
		ss->tx.ifsq = ifsq;

		ifsq_watchdog_init(&ss->tx.watchdog, ifsq, mxge_watchdog, 0);
	}

	/*
	 * XXX
	 * We are not ready to do "gather" jumbo frame, so
	 * limit MTU to MJUMPAGESIZE
	 */
	sc->max_mtu = MJUMPAGESIZE -
	    ETHER_HDR_LEN - EVL_ENCAPLEN - MXGEFW_PAD - 1;
	sc->dying = 0;

	err = mxge_setup_intr(sc);
	if (err != 0) {
		device_printf(dev, "alloc and setup intr failed\n");
		ether_ifdetach(ifp);
		goto failed;
	}

	mxge_add_sysctls(sc);

	/* Increase non-cluster mbuf limit; used by small RX rings */
	mb_inclimit(ifp->if_nmbclusters);

	callout_reset_bycpu(&sc->co_hdl, mxge_ticks, mxge_tick, sc,
	    sc->ss[0].intr_cpuid);
	return 0;

failed:
	mxge_detach(dev);
	return err;
}

static int
mxge_detach(device_t dev)
{
	mxge_softc_t *sc = device_get_softc(dev);

	if (device_is_attached(dev)) {
		struct ifnet *ifp = sc->ifp;
		int mblimit = ifp->if_nmbclusters;

		ifnet_serialize_all(ifp);

		sc->dying = 1;
		if (ifp->if_flags & IFF_RUNNING)
			mxge_close(sc, 1);
		callout_stop(&sc->co_hdl);

		mxge_teardown_intr(sc, sc->num_slices);

		ifnet_deserialize_all(ifp);

		callout_terminate(&sc->co_hdl);

		ether_ifdetach(ifp);

		/* Decrease non-cluster mbuf limit increased by us */
		mb_inclimit(-mblimit);
	}
	ifmedia_removeall(&sc->media);

	if (sc->cmd != NULL && sc->zeropad_dma.dmem_addr != NULL &&
	    sc->sram != NULL)
		mxge_dummy_rdma(sc, 0);

	mxge_free_intr(sc);
	mxge_rem_sysctls(sc);
	mxge_free_rings(sc);

	/* MUST after sysctls, intr and rings are freed */
	mxge_free_slices(sc);

	if (sc->dmabench_dma.dmem_addr != NULL)
		mxge_dma_free(&sc->dmabench_dma);
	if (sc->zeropad_dma.dmem_addr != NULL)
		mxge_dma_free(&sc->zeropad_dma);
	if (sc->cmd_dma.dmem_addr != NULL)
		mxge_dma_free(&sc->cmd_dma);

	if (sc->msix_table_res != NULL) {
		bus_release_resource(dev, SYS_RES_MEMORY, PCIR_BAR(2),
		    sc->msix_table_res);
	}
	if (sc->mem_res != NULL) {
		bus_release_resource(dev, SYS_RES_MEMORY, PCIR_BARS,
		    sc->mem_res);
	}

	if (sc->parent_dmat != NULL)
		bus_dma_tag_destroy(sc->parent_dmat);

	if (sc->ring_map != NULL)
		if_ringmap_free(sc->ring_map);

	return 0;
}

static int
mxge_shutdown(device_t dev)
{
	return 0;
}

static void
mxge_free_msix(struct mxge_softc *sc, boolean_t setup)
{
	int i;

	KKASSERT(sc->num_slices > 1);

	for (i = 0; i < sc->num_slices; ++i) {
		struct mxge_slice_state *ss = &sc->ss[i];

		if (ss->intr_res != NULL) {
			bus_release_resource(sc->dev, SYS_RES_IRQ,
			    ss->intr_rid, ss->intr_res);
		}
		if (ss->intr_rid >= 0)
			pci_release_msix_vector(sc->dev, ss->intr_rid);
	}
	if (setup)
		pci_teardown_msix(sc->dev);
}

static int
mxge_alloc_msix(struct mxge_softc *sc)
{
	struct mxge_slice_state *ss;
	int rid, error, i;
	boolean_t setup = FALSE;

	KKASSERT(sc->num_slices > 1);

	ss = &sc->ss[0];

	ss->intr_serialize = &sc->main_serialize;
	ss->intr_func = mxge_msi;
	ksnprintf(ss->intr_desc0, sizeof(ss->intr_desc0),
	    "%s comb", device_get_nameunit(sc->dev));
	ss->intr_desc = ss->intr_desc0;
	ss->intr_cpuid = if_ringmap_cpumap(sc->ring_map, 0);

	for (i = 1; i < sc->num_slices; ++i) {
		ss = &sc->ss[i];

		ss->intr_serialize = &ss->rx_data.rx_serialize;
		if (sc->num_tx_rings == 1) {
			ss->intr_func = mxge_msix_rx;
			ksnprintf(ss->intr_desc0, sizeof(ss->intr_desc0),
			    "%s rx%d", device_get_nameunit(sc->dev), i);
		} else {
			ss->intr_func = mxge_msix_rxtx;
			ksnprintf(ss->intr_desc0, sizeof(ss->intr_desc0),
			    "%s rxtx%d", device_get_nameunit(sc->dev), i);
		}
		ss->intr_desc = ss->intr_desc0;
		ss->intr_cpuid = if_ringmap_cpumap(sc->ring_map, i);
	}

	rid = PCIR_BAR(2);
	sc->msix_table_res = bus_alloc_resource_any(sc->dev, SYS_RES_MEMORY,
	    &rid, RF_ACTIVE);
	if (sc->msix_table_res == NULL) {
		device_printf(sc->dev, "couldn't alloc MSI-X table res\n");
		return ENXIO;
	}

	error = pci_setup_msix(sc->dev);
	if (error) {
		device_printf(sc->dev, "could not setup MSI-X\n");
		goto back;
	}
	setup = TRUE;

	for (i = 0; i < sc->num_slices; ++i) {
		ss = &sc->ss[i];

		error = pci_alloc_msix_vector(sc->dev, i, &ss->intr_rid,
		    ss->intr_cpuid);
		if (error) {
			device_printf(sc->dev, "could not alloc "
			    "MSI-X %d on cpu%d\n", i, ss->intr_cpuid);
			goto back;
		}

		ss->intr_res = bus_alloc_resource_any(sc->dev, SYS_RES_IRQ,
		    &ss->intr_rid, RF_ACTIVE);
		if (ss->intr_res == NULL) {
			device_printf(sc->dev, "could not alloc "
			    "MSI-X %d resource\n", i);
			error = ENXIO;
			goto back;
		}
	}

	pci_enable_msix(sc->dev);
	sc->intr_type = PCI_INTR_TYPE_MSIX;
back:
	if (error)
		mxge_free_msix(sc, setup);
	return error;
}

static int
mxge_alloc_intr(struct mxge_softc *sc)
{
	struct mxge_slice_state *ss;
	u_int irq_flags;

	if (sc->num_slices > 1) {
		int error;

		error = mxge_alloc_msix(sc);
		if (error)
			return error;
		KKASSERT(sc->intr_type == PCI_INTR_TYPE_MSIX);
		return 0;
	}

	ss = &sc->ss[0];

	sc->intr_type = pci_alloc_1intr(sc->dev, mxge_msi_enable,
	    &ss->intr_rid, &irq_flags);

	ss->intr_res = bus_alloc_resource_any(sc->dev, SYS_RES_IRQ,
	    &ss->intr_rid, irq_flags);
	if (ss->intr_res == NULL) {
		device_printf(sc->dev, "could not alloc interrupt\n");
		return ENXIO;
	}

	if (sc->intr_type == PCI_INTR_TYPE_LEGACY)
		ss->intr_func = mxge_legacy;
	else
		ss->intr_func = mxge_msi;
	ss->intr_serialize = &sc->main_serialize;
	ss->intr_cpuid = rman_get_cpuid(ss->intr_res);

	return 0;
}

static int
mxge_setup_intr(struct mxge_softc *sc)
{
	int i;

	for (i = 0; i < sc->num_slices; ++i) {
		struct mxge_slice_state *ss = &sc->ss[i];
		int error;

		error = bus_setup_intr_descr(sc->dev, ss->intr_res,
		    INTR_MPSAFE, ss->intr_func, ss, &ss->intr_hand,
		    ss->intr_serialize, ss->intr_desc);
		if (error) {
			device_printf(sc->dev, "can't setup %dth intr\n", i);
			mxge_teardown_intr(sc, i);
			return error;
		}
	}
	return 0;
}

static void
mxge_teardown_intr(struct mxge_softc *sc, int cnt)
{
	int i;

	if (sc->ss == NULL)
		return;

	for (i = 0; i < cnt; ++i) {
		struct mxge_slice_state *ss = &sc->ss[i];

		bus_teardown_intr(sc->dev, ss->intr_res, ss->intr_hand);
	}
}

static void
mxge_free_intr(struct mxge_softc *sc)
{
	if (sc->ss == NULL)
		return;

	if (sc->intr_type != PCI_INTR_TYPE_MSIX) {
		struct mxge_slice_state *ss = &sc->ss[0];

		if (ss->intr_res != NULL) {
			bus_release_resource(sc->dev, SYS_RES_IRQ,
			    ss->intr_rid, ss->intr_res);
		}
		if (sc->intr_type == PCI_INTR_TYPE_MSI)
			pci_release_msi(sc->dev);
	} else {
		mxge_free_msix(sc, TRUE);
	}
}
