/**************************************************************************
**
** $FreeBSD: src/sys/pci/pcisupport.c,v 1.154.2.15 2003/04/29 15:55:06 simokawa Exp $
** $DragonFly: src/sys/bus/pci/pcisupport.c,v 1.14 2004/03/24 20:34:08 dillon Exp $
**
**  Device driver for DEC/INTEL PCI chipsets.
**
**  FreeBSD
**
**-------------------------------------------------------------------------
**
**  Written for FreeBSD by
**	wolf@cologne.de 	Wolfgang Stanglmeier
**	se@mi.Uni-Koeln.de	Stefan Esser
**
**-------------------------------------------------------------------------
**
** Copyright (c) 1994,1995 Stefan Esser.  All rights reserved.
**
** Redistribution and use in source and binary forms, with or without
** modification, are permitted provided that the following conditions
** are met:
** 1. Redistributions of source code must retain the above copyright
**    notice, this list of conditions and the following disclaimer.
** 2. Redistributions in binary form must reproduce the above copyright
**    notice, this list of conditions and the following disclaimer in the
**    documentation and/or other materials provided with the distribution.
** 3. The name of the author may not be used to endorse or promote products
**    derived from this software without specific prior written permission.
**
** THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
** IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
** OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
** IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
** INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
** NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
** DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
** THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
** (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
** THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
**
***************************************************************************
*/

#include "opt_bus.h"
#include "opt_pci.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/kernel.h>
#include <sys/bus.h>
#include <sys/rman.h>

#include <machine/resource.h>

#include "pcivar.h"
#include "pcireg.h"

#include <vm/vm.h>
#include <vm/vm_object.h>
#include <vm/pmap.h>

#include "pcib_if.h"
#include "pcib_private.h"

/*---------------------------------------------------------
**
**	Intel chipsets for 486 / Pentium processor
**
**---------------------------------------------------------
*/

static void
fixbushigh_i1225(device_t dev)
{
	int sublementarybus;

	sublementarybus = pci_read_config(dev, 0x41, 1);
	if (sublementarybus != 0xff) {
		pci_set_secondarybus(dev, sublementarybus + 1);
		pci_set_subordinatebus(dev, sublementarybus + 1);
	}
}

static void
fixwsc_natoma(device_t dev)
{
	int pmccfg;

	pmccfg = pci_read_config(dev, 0x50, 2);
#if defined(SMP)
	if (pmccfg & 0x8000) {
		printf("Correcting Natoma config for SMP\n");
		pmccfg &= ~0x8000;
		pci_write_config(dev, 0x50, pmccfg, 2);
	}
#else
	if ((pmccfg & 0x8000) == 0) {
		printf("Correcting Natoma config for non-SMP\n");
		pmccfg |= 0x8000;
		pci_write_config(dev, 0x50, pmccfg, 2);
	}
#endif
}

const char *
pci_bridge_type(device_t dev)
{
    char *descr, tmpbuf[120];

    if (pci_get_class(dev) != PCIC_BRIDGE)
	    return NULL;

    switch (pci_get_subclass(dev)) {
    case PCIS_BRIDGE_HOST:	strcpy(tmpbuf, "Host to PCI"); break;
    case PCIS_BRIDGE_ISA:	strcpy(tmpbuf, "PCI to ISA"); break;
    case PCIS_BRIDGE_EISA:	strcpy(tmpbuf, "PCI to EISA"); break;
    case PCIS_BRIDGE_MCA:	strcpy(tmpbuf, "PCI to MCA"); break;
    case PCIS_BRIDGE_PCI:	strcpy(tmpbuf, "PCI to PCI"); break;
    case PCIS_BRIDGE_PCMCIA:	strcpy(tmpbuf, "PCI to PCMCIA"); break;
    case PCIS_BRIDGE_NUBUS:	strcpy(tmpbuf, "PCI to NUBUS"); break;
    case PCIS_BRIDGE_CARDBUS:	strcpy(tmpbuf, "PCI to CardBus"); break;
    case PCIS_BRIDGE_OTHER:	strcpy(tmpbuf, "PCI to Other"); break;
    default: 
	    snprintf(tmpbuf, sizeof(tmpbuf),
		     "PCI to 0x%x", pci_get_subclass(dev)); 
	    break;
    }
    snprintf(tmpbuf+strlen(tmpbuf), sizeof(tmpbuf)-strlen(tmpbuf),
	     " bridge (vendor=%04x device=%04x)",
	     pci_get_vendor(dev), pci_get_device(dev));
    descr = malloc (strlen(tmpbuf) +1, M_DEVBUF, M_WAITOK);
    strcpy(descr, tmpbuf);
    return descr;
}

const char *
pci_usb_match(device_t dev)
{
	switch (pci_get_devid(dev)) {

	/* Intel -- vendor 0x8086 */
	case 0x70208086:
		return ("Intel 82371SB (PIIX3) USB controller");
	case 0x71128086:
		return ("Intel 82371AB/EB (PIIX4) USB controller");
	case 0x24128086:
		return ("Intel 82801AA (ICH) USB controller");
	case 0x24228086:
		return ("Intel 82801AB (ICH0) USB controller");
	case 0x24428086:
		return ("Intel 82801BA/BAM (ICH2) USB controller USB-A");
	case 0x24448086:
		return ("Intel 82801BA/BAM (ICH2) USB controller USB-B");

	/* VIA Technologies -- vendor 0x1106 (0x1107 on the Apollo Master) */
	case 0x30381106:
		return ("VIA 83C572 USB controller");

	/* AcerLabs -- vendor 0x10b9 */
	case 0x523710b9:
		return ("AcerLabs M5237 (Aladdin-V) USB controller");

	/* OPTi -- vendor 0x1045 */
	case 0xc8611045:
		return ("OPTi 82C861 (FireLink) USB controller");

	/* NEC -- vendor 0x1033 */
	case 0x00351033:
		return ("NEC uPD 9210 USB controller");

	/* CMD Tech -- vendor 0x1095 */
	case 0x06701095:
		return ("CMD Tech 670 (USB0670) USB controller");
	case 0x06731095:
		return ("CMD Tech 673 (USB0673) USB controller");
	}

	if (pci_get_class(dev) == PCIC_SERIALBUS
	    && pci_get_subclass(dev) == PCIS_SERIALBUS_USB) {
		if (pci_get_progif(dev) == 0x00 /* UHCI */ ) {
			return ("UHCI USB controller");
		} else if (pci_get_progif(dev) == 0x10 /* OHCI */ ) {
			return ("OHCI USB controller");
		} else {
			return ("USB controller");
		}
	}
	return NULL;
}

const char *
pci_ata_match(device_t dev)
{

	switch (pci_get_devid(dev)) {

	/* Intel -- vendor 0x8086 */
	case 0x12308086:
		return ("Intel PIIX ATA controller");
	case 0x70108086:
		return ("Intel PIIX3 ATA controller");
	case 0x71118086:
		return ("Intel PIIX4 ATA controller");
	case 0x12348086:
		return ("Intel 82371MX mobile PCI ATA accelerator (MPIIX)");

	/* Promise -- vendor 0x105a */
	case 0x4d33105a:
		return ("Promise Ultra/33 ATA controller");
	case 0x4d38105a:
		return ("Promise Ultra/66 ATA controller");

	/* AcerLabs -- vendor 0x10b9 */
	case 0x522910b9:
		return ("AcerLabs Aladdin ATA controller");

	/* VIA Technologies -- vendor 0x1106 (0x1107 on the Apollo Master) */
	case 0x05711106:
		switch (pci_read_config(dev, 0x08, 1)) {
		case 1:
			return ("VIA 85C586 ATA controller");
		case 6:
			return ("VIA 85C586 ATA controller");
		}
		/* FALL THROUGH */
	case 0x15711106:
		return ("VIA Apollo ATA controller");

	/* CMD Tech -- vendor 0x1095 */
	case 0x06401095:
		return ("CMD 640 ATA controller");
	case 0x06461095:
		return ("CMD 646 ATA controller");

	/* Cypress -- vendor 0x1080 */
	case 0xc6931080:
		return ("Cypress 82C693 ATA controller");

	/* Cyrix -- vendor 0x1078 */
	case 0x01021078:
		return ("Cyrix 5530 ATA controller");

	/* SiS -- vendor 0x1039 */
	case 0x55131039:
		return ("SiS 5591 ATA controller");

	/* Highpoint tech -- vendor 0x1103 */
	case 0x00041103:
		return ("HighPoint HPT366 ATA controller");
	}

	if (pci_get_class(dev) == PCIC_STORAGE &&
	    pci_get_subclass(dev) == PCIS_STORAGE_IDE)
		return ("Unknown PCI ATA controller");

	return NULL;
}


const char*
pci_chip_match(device_t dev)
{
	unsigned	rev;

	switch (pci_get_devid(dev)) {
	/* Intel -- vendor 0x8086 */
	case 0x00088086:
		/* Silently ignore this one! What is it, anyway ??? */
		return ("");
	case 0x71108086:
		/*
		 * On my laptop (Tecra 8000DVD), this device has a
		 * bogus subclass 0x80 so make sure that it doesn't
		 * match the generic 'chip' driver by accident.
		 */
		return NULL;
	case 0x12258086:
		fixbushigh_i1225(dev);
		return ("Intel 824?? host to PCI bridge");
	case 0x71808086:
		return ("Intel 82443LX (440 LX) host to PCI bridge");
	case 0x71908086:
		return ("Intel 82443BX (440 BX) host to PCI bridge");
	case 0x71928086:
		return ("Intel 82443BX host to PCI bridge (AGP disabled)");
 	case 0x71a08086:
 		return ("Intel 82443GX host to PCI bridge");
 	case 0x71a18086:
 		return ("Intel 82443GX host to AGP bridge");
 	case 0x71a28086:
 		return ("Intel 82443GX host to PCI bridge (AGP disabled)");
	case 0x84c48086:
		return ("Intel 82454KX/GX (Orion) host to PCI bridge");
	case 0x84ca8086:
		return ("Intel 82451NX Memory and I/O controller");
	case 0x04868086:
		return ("Intel 82425EX PCI system controller");
	case 0x04838086:
		return ("Intel 82424ZX (Saturn) cache DRAM controller");
	case 0x04a38086:
		rev = pci_get_revid(dev);
		if (rev == 16 || rev == 17)
		    return ("Intel 82434NX (Neptune) PCI cache memory controller");
		return ("Intel 82434LX (Mercury) PCI cache memory controller");
	case 0x122d8086:
		return ("Intel 82437FX PCI cache memory controller");
	case 0x12358086:
		return ("Intel 82437MX mobile PCI cache memory controller");
	case 0x12508086:
		return ("Intel 82439HX PCI cache memory controller");
	case 0x70308086:
		return ("Intel 82437VX PCI cache memory controller");
	case 0x71008086:
		return ("Intel 82439TX System controller (MTXC)");
	case 0x71138086:
		return ("Intel 82371AB Power management controller");
	case 0x719b8086:
		return ("Intel 82443MX Power management controller");
	case 0x12378086:
		fixwsc_natoma(dev);
		return ("Intel 82440FX (Natoma) PCI and memory controller");
	case 0x84c58086:
		return ("Intel 82453KX/GX (Orion) PCI memory controller");
	case 0x71208086:
		return ("Intel 82810 (i810 GMCH) Host To Hub bridge");
	case 0x71228086:
	return ("Intel 82810-DC100 (i810-DC100 GMCH) Host To Hub bridge");
	case 0x71248086:
	return ("Intel 82810E (i810E GMCH) Host To Hub bridge");
	case 0x24158086:
		return ("Intel 82801AA (ICH) AC'97 Audio Controller");
	case 0x24258086:
		return ("Intel 82801AB (ICH0) AC'97 Audio Controller");

	/* Sony -- vendor 0x104d */
	case 0x8009104d:
		return ("Sony CXD1947A FireWire Host Controller");

	/* SiS -- vendor 0x1039 */
	case 0x04961039:
		return ("SiS 85c496 PCI/VL Bridge");
	case 0x04061039:
		return ("SiS 85c501");
	case 0x06011039:
		return ("SiS 85c601");
	case 0x55911039:
		return ("SiS 5591 host to PCI bridge");
	case 0x00011039:
		return ("SiS 5591 host to AGP bridge");
	
	/* VLSI -- vendor 0x1004 */
	case 0x00051004:
		return ("VLSI 82C592 Host to PCI bridge");
	case 0x01011004:
		return ("VLSI 82C532 Eagle II Peripheral controller");
	case 0x01041004:
		return ("VLSI 82C535 Eagle II System controller");
	case 0x01051004:
		return ("VLSI 82C147 IrDA controller");

	/* VIA Technologies -- vendor 0x1106 (0x1107 on the Apollo Master) */
	case 0x15761107:
		return ("VIA 82C570 (Apollo Master) system controller");
	case 0x05851106:
		return ("VIA 82C585 (Apollo VP1/VPX) system controller");
	case 0x05951106:
	case 0x15951106:
		return ("VIA 82C595 (Apollo VP2) system controller");
	case 0x05971106:
		return ("VIA 82C597 (Apollo VP3) system controller");
	/* XXX Here is MVP3, I got the datasheet but NO M/B to test it  */
	/* totally. Please let me know if anything wrong.            -F */
	/* XXX need info on the MVP3 -- any takers? */
	case 0x05981106:
		return ("VIA 82C598MVP (Apollo MVP3) host bridge");
	case 0x30401106:
	case 0x30501106:
	case 0x30571106:
		return NULL;
	case 0x30581106:
		return ("VIA 82C686 AC97 Audio");
	case 0x30681106:
		return ("VIA 82C686 AC97 Modem");

	/* AMD -- vendor 0x1022 */
	case 0x70061022:
		return ("AMD-751 host to PCI bridge");
	case 0x700e1022:
		return ("AMD-761 host to PCI bridge");

	/* NEC -- vendor 0x1033 */
	case 0x00021033:
		return ("NEC 0002 PCI to PC-98 local bus bridge");
	case 0x00161033:
		return ("NEC 0016 PCI to PC-98 local bus bridge");

	/* AcerLabs -- vendor 0x10b9 */
	/* Funny : The datasheet told me vendor id is "10b8",sub-vendor */
	/* id is '10b9" but the register always shows "10b9". -Foxfair  */
	case 0x154110b9:
		return ("AcerLabs M1541 (Aladdin-V) PCI host bridge");
	case 0x710110b9:
		return ("AcerLabs M15x3 Power Management Unit");

	/* OPTi -- vendor 0x1045 */
	case 0xc5571045:
		return ("Opti 82C557 (Viper-M) host to PCI bridge");
	case 0xc5581045:
		return ("Opti 82C558 (Viper-M) ISA+IDE");
	case 0xc8221045:
		return ("OPTi 82C822 host to PCI Bridge");

	/* Texas Instruments -- vendor 0x104c */
	case 0xac1c104c:
		return ("Texas Instruments PCI1225 CardBus controller");
	case 0xac50104c:
		return ("Texas Instruments PCI1410 CardBus controller");
	case 0xac51104c:
		return ("Texas Instruments PCI1420 CardBus controller");
	case 0xac1b104c:
		return ("Texas Instruments PCI1450 CardBus controller");
	case 0xac52104c:
		return ("Texas Instruments PCI1451 CardBus controller");

	/* NeoMagic -- vendor 0x10c8 */
	case 0x800510c8:
		return ("NeoMagic MagicMedia 256AX Audio controller");
	case 0x800610c8:
		return ("NeoMagic MagicMedia 256ZX Audio controller");

	/* ESS Technology Inc -- vendor 0x125d */
	case 0x1978125d:
		return ("ESS Technology Maestro 2E Audio controller");

	/* Toshiba -- vendor 0x1179 */
	case 0x07011179:
		return ("Toshiba Fast Infra Red controller");

	/* NEC -- vendor 0x1033 */

	/* PCI to C-bus bridge */
	/* The following chipsets are PCI to PC98 C-bus bridge.
	 * The C-bus is the 16-bits bus on PC98 and it should be probed as
	 * PCI to ISA bridge.  Because class of the C-bus is not defined,
	 * C-bus bridges are recognized as "other bridge."  To make C-bus
	 * bridge be recognized as ISA bridge, this function returns NULL.
	 */
	case 0x00011033:
	case 0x002c1033:
	case 0x003b1033:
		return NULL;
	};

	if (pci_get_class(dev) == PCIC_BRIDGE
	    && pci_get_subclass(dev) != PCIS_BRIDGE_PCI
	    && pci_get_subclass(dev) != PCIS_BRIDGE_ISA
	    && pci_get_subclass(dev) != PCIS_BRIDGE_EISA)
		return pci_bridge_type(dev);

	return NULL;
}

/*---------------------------------------------------------
**
**	Catchall driver for VGA devices
**
**	By Garrett Wollman
**	<wollman@halloran-eldar.lcs.mit.edu>
**
**---------------------------------------------------------
*/

const char* pci_vga_match(device_t dev)
{
	u_int id = pci_get_devid(dev);
	const char *vendor, *chip, *type;

	vendor = chip = type = 0;
	switch (id & 0xffff) {
	case 0x003d:
		vendor = "Real 3D";
		switch (id >> 16) {
		case 0x00d1:
			chip = "i740"; break;
		}
		break;
	case 0x10c8:
		vendor = "NeoMagic";
		switch (id >> 16) {
		case 0x0003:
			chip = "MagicGraph 128ZV"; break;
		case 0x0004:
			chip = "MagicGraph 128XD"; break;
		case 0x0005:
			chip = "MagicMedia 256AV"; break;
		case 0x0006:
			chip = "MagicMedia 256ZX"; break;
		}
		break;
	case 0x121a:
		vendor = "3Dfx";
		type = "graphics accelerator";
		switch (id >> 16) {
		case 0x0001:
			chip = "Voodoo"; break;
		case 0x0002:
			chip = "Voodoo 2"; break;
  		case 0x0003:
			chip = "Voodoo Banshee"; break;
		case 0x0005:
			chip = "Voodoo 3"; break;
		}
		break;
	case 0x102b:
		vendor = "Matrox";
		type = "graphics accelerator";
		switch (id >> 16) {
		case 0x0518:
			chip = "MGA 2085PX"; break;
		case 0x0519:
			chip = "MGA Millennium 2064W"; break;
		case 0x051a:
			chip = "MGA 1024SG/1064SG/1164SG"; break;
		case 0x051b:
			chip = "MGA Millennium II 2164W"; break;
		case 0x051f:
			chip = "MGA Millennium II 2164WA-B AG"; break;
		case 0x0520:
			chip = "MGA G200"; break;
		case 0x0521:
			chip = "MGA G200 AGP"; break;
		case 0x0525:
			chip = "MGA G400 AGP"; break;
		case 0x0d10:
			chip = "MGA Impression"; break;
		case 0x1000:
			chip = "MGA G100"; break;
		case 0x1001:
			chip = "MGA G100 AGP"; break;
		case 0x2527:
			chip = "MGA G550 AGP"; break;

		}
		break;
	case 0x1002:
		vendor = "ATI";
		type = "graphics accelerator";
		switch (id >> 16) {
		case 0x4158:
			chip = "Mach32"; break;
		case 0x4354:
			chip = "Mach64-CT"; break;
		case 0x4358:
			chip = "Mach64-CX"; break;
		case 0x4554:
			chip = "Mach64-ET"; break;
		case 0x4654:
		case 0x5654:
			chip = "Mach64-VT"; break;
		case 0x4742:
			chip = "Mach64-GB"; break;
		case 0x4744:
			chip = "Mach64-GD"; break;
		case 0x4749:
			chip = "Mach64-GI"; break;
		case 0x474d:
			chip = "Mach64-GM"; break;
		case 0x474e:
			chip = "Mach64-GN"; break;
		case 0x474f:
			chip = "Mach64-GO"; break;
		case 0x4750:
			chip = "Mach64-GP"; break;
		case 0x4751:
			chip = "Mach64-GQ"; break;
		case 0x4752:
			chip = "Mach64-GR"; break;
		case 0x4753:
			chip = "Mach64-GS"; break;
		case 0x4754:
			chip = "Mach64-GT"; break;
		case 0x4755:
			chip = "Mach64-GU"; break;
		case 0x4756:
			chip = "Mach64-GV"; break;
		case 0x4757:
			chip = "Mach64-GW"; break;
		case 0x4758:
			chip = "Mach64-GX"; break;
		case 0x4c4d:
			chip = "Mobility-1"; break;
		case 0x4c52:
			chip = "RageMobility-P/M"; break;
		case 0x475a:
			chip = "Mach64-GZ"; break;
		case 0x5245:
			chip = "Rage128-RE"; break;
		case 0x5246:
			chip = "Rage128-RF"; break;
		case 0x524b:
			chip = "Rage128-RK"; break;
		case 0x524c:
			chip = "Rage128-RL"; break;
		}
		break;
	case 0x1005:
		vendor = "Avance Logic";
		switch (id >> 16) {
		case 0x2301:
			chip = "ALG2301"; break;
		case 0x2302:
			chip = "ALG2302"; break;
		}
		break;
	case 0x100c:
		vendor = "Tseng Labs";
		type = "graphics accelerator";
		switch (id >> 16) {
		case 0x3202:
		case 0x3205:
		case 0x3206:
		case 0x3207:
			chip = "ET4000 W32P"; break;
		case 0x3208:
			chip = "ET6000/ET6100"; break;
		case 0x4702:
			chip = "ET6300"; break;
		}
		break;
	case 0x100e:
		vendor = "Weitek";
		type = "graphics accelerator";
		switch (id >> 16) {
		case 0x9001:
			chip = "P9000"; break;
		case 0x9100:
			chip = "P9100"; break;
		}
		break;
	case 0x1013:
		vendor = "Cirrus Logic";
		switch (id >> 16) {
		case 0x0038:
			chip = "GD7548"; break;
		case 0x0040:
			chip = "GD7555"; break;
		case 0x004c:
			chip = "GD7556"; break;
		case 0x00a0:
			chip = "GD5430"; break;
		case 0x00a4:
		case 0x00a8:
			chip = "GD5434"; break;
		case 0x00ac:
			chip = "GD5436"; break;
		case 0x00b8:
			chip = "GD5446"; break;
		case 0x00bc:
			chip = "GD5480"; break;
		case 0x00d0:
			chip = "GD5462"; break;
		case 0x00d4:
		case 0x00d5:
			chip = "GD5464"; break;
		case 0x00d6:
			chip = "GD5465"; break;
		case 0x1200:
			chip = "GD7542"; break;
		case 0x1202:
			chip = "GD7543"; break;
		case 0x1204:
			chip = "GD7541"; break;
		}
		break;
	case 0x1023:
		vendor = "Trident";
		break;		/* let default deal with it */
	case 0x102c:
		vendor = "Chips & Technologies";
		switch (id >> 16) {
		case 0x00b8:
			chip = "64310"; break;
		case 0x00d8:
			chip = "65545"; break;
		case 0x00dc:
			chip = "65548"; break;
		case 0x00c0:
			chip = "69000"; break;
		case 0x00e0:
			chip = "65550"; break;
		case 0x00e4:
			chip = "65554"; break;
		case 0x00e5:
			chip = "65555"; break;
		case 0x00f4:
			chip = "68554"; break;
                }
		break;
	case 0x1033:
		vendor = "NEC";
		switch (id >> 16) {
		case 0x0009:
			type = "PCI to PC-98 Core Graph bridge";
			break;
		}
		break;
	case 0x1039:
		vendor = "SiS";
		switch (id >> 16) {
		case 0x0001:
			chip = "86c201"; break;
		case 0x0002:
			chip = "86c202"; break;
		case 0x0205:
			chip = "86c205"; break;
		case 0x0215:
			chip = "86c215"; break;
		case 0x0225:
			chip = "86c225"; break;
		case 0x0200:
			chip = "5597/98"; break;
		case 0x6326:
			chip = "6326"; break;
		case 0x6306:
			chip = "530/620"; break;
		}
		break;
	case 0x105d:
		vendor = "Number Nine";
		type = "graphics accelerator";
		switch (id >> 16) {
		case 0x2309:
			chip = "Imagine 128"; break;
		case 0x2339:
			chip = "Imagine 128 II"; break;
		}
		break;
	case 0x1142:
		vendor = "Alliance";
		switch (id >> 16) {
		case 0x3210:
			chip = "PM6410"; break;
		case 0x6422:
			chip = "PM6422"; break;
		case 0x6424:
			chip = "PMAT24"; break;
		}
		break;
	case 0x1163:
		vendor = "Rendition Verite";
		switch (id >> 16) {
		case 0x0001:
			chip = "V1000"; break;
		case 0x2000:
			chip = "V2000"; break;
		}
		break;
	case 0x1236:
		vendor = "Sigma Designs";
		if ((id >> 16) == 0x6401)
			chip = "REALmagic64/GX";
		break;
	case 0x5333:
		vendor = "S3";
		type = "graphics accelerator";
		switch (id >> 16) {
		case 0x8811:
			chip = "Trio"; break;
		case 0x8812:
			chip = "Aurora 64"; break;
		case 0x8814:
			chip = "Trio 64UV+"; break;
		case 0x8901:
			chip = "Trio 64V2/DX/GX"; break;
		case 0x8902:
			chip = "Plato"; break;
		case 0x8904:
			chip = "Trio3D"; break;
		case 0x8880:
			chip = "868"; break;
		case 0x88b0:
			chip = "928"; break;
		case 0x88c0:
		case 0x88c1:
			chip = "864"; break;
		case 0x88d0:
		case 0x88d1:
			chip = "964"; break;
		case 0x88f0:
			chip = "968"; break;
		case 0x5631:
			chip = "ViRGE"; break;
		case 0x883d:
			chip = "ViRGE VX"; break;
		case 0x8a01:
			chip = "ViRGE DX/GX"; break;
		case 0x8a10:
			chip = "ViRGE GX2"; break;
		case 0x8a13:
			chip = "Trio3D/2X"; break;
		case 0x8a20:
		case 0x8a21:
			chip = "Savage3D"; break;
		case 0x8a22:
			chip = "Savage 4"; break;
		case 0x8c01:
			chip = "ViRGE MX"; break;
		case 0x8c03:
			chip = "ViRGE MX+"; break;
		}
		break;
	case 0xedd8:
		vendor = "ARK Logic";
		switch (id >> 16) {
		case 0xa091:
			chip = "1000PV"; break;
		case 0xa099:
			chip = "2000PV"; break;
		case 0xa0a1:
			chip = "2000MT"; break;
		case 0xa0a9:
			chip = "2000MI"; break;
		}
		break;
	case 0x3d3d:
		vendor = "3D Labs";
		type = "graphics accelerator";
		switch (id >> 16) {
		case 0x0001:
			chip = "300SX"; break;
		case 0x0002:
			chip = "500TX"; break;
		case 0x0003:
			chip = "Delta"; break;
		case 0x0004:
			chip = "PerMedia"; break;
		}
		break;
	case 0x10de:
		vendor = "NVidia";
		type = "graphics accelerator";
		switch (id >> 16) {
		case 0x0008:
			chip = "NV1"; break;
		case 0x0020:
			chip = "Riva TNT"; break;	
		case 0x0028:
			chip = "Riva TNT2"; break;
		case 0x0029:
			chip = "Riva Ultra TNT2"; break;
		case 0x002c:
			chip = "Riva Vanta TNT2"; break;
		case 0x002d:
			chip = "Riva Ultra Vanta TNT2"; break;
		case 0x00a0:
			chip = "Riva Integrated TNT2"; break;
		case 0x0100:
			chip = "GeForce 256"; break;
		case 0x0101:
			chip = "GeForce DDR"; break;
		case 0x0103:
			chip = "Quadro"; break;
		case 0x0150:
		case 0x0151:
		case 0x0152:
			chip = "GeForce2 GTS"; break;
		case 0x0153:
			chip = "Quadro2"; break;
		}
		break;
	case 0x12d2:
		vendor = "NVidia/SGS-Thomson";
		type = "graphics accelerator";
		switch (id >> 16) {
		case 0x0018:
			chip = "Riva128"; break;	
		}
		break;
	case 0x104a:
		vendor = "SGS-Thomson";
		switch (id >> 16) {
		case 0x0008:
			chip = "STG2000"; break;
		}
		break;
	case 0x8086:
		vendor = "Intel";
		switch (id >> 16) {
		case 0x7121:
			chip = "82810 (i810 GMCH)"; break;
		case 0x7123:
			chip = "82810-DC100 (i810-DC100 GMCH)"; break;
		case 0x7125:
			chip = "82810E (i810E GMCH)"; break;
		case 0x7800:
			chip = "i740 AGP"; break;
		}
		break;
	case 0x10ea:
		vendor = "Intergraphics";
		switch (id >> 16) {
		case 0x1680:
			chip = "IGA-1680"; break;
		case 0x1682:
			chip = "IGA-1682"; break;
		}
		break;
	}

	if (vendor && chip) {
		char *buf;
		int len;

		if (type == 0)
			type = "SVGA controller";

		len = strlen(vendor) + strlen(chip) + strlen(type) + 4;
		MALLOC(buf, char *, len, M_TEMP, M_WAITOK);
		sprintf(buf, "%s %s %s", vendor, chip, type);
		return buf;
	}

	switch (pci_get_class(dev)) {

	case PCIC_OLD:
		if (pci_get_subclass(dev) != PCIS_OLD_VGA)
			return 0;
		if (type == 0)
			type = "VGA-compatible display device";
		break;

	case PCIC_DISPLAY:
		if (type == 0) {
			if (pci_get_subclass(dev) == PCIS_DISPLAY_VGA)
				type = "VGA-compatible display device";
			else {
				/*
				 * If it isn't a vga display device,
				 * don't pretend we found one.
				 */
				return 0;
			}
		}
		break;

	default:
		return 0;
	};
	/*
	 * If we got here, we know for sure it's some sort of display
	 * device, but we weren't able to identify it specifically.
	 * At a minimum we can return the type, but we'd like to
	 * identify the vendor and chip ID if at all possible.
	 * (Some of the checks above intentionally don't bother for
	 * vendors where we know the chip ID is the same as the
	 * model number.)
	 */
	if (vendor) {
		char *buf;
		int len;

		len = strlen(vendor) + 7 + 4 + 1 + strlen(type) + 1;
		MALLOC(buf, char *, len, M_TEMP, M_WAITOK);
		sprintf(buf, "%s model %04x %s", vendor, id >> 16, type);
		return buf;
	}
	return type;
}

/*---------------------------------------------------------
**
**	Devices to ignore
**
**---------------------------------------------------------
*/

static const char*
ign_match(device_t dev)
{
	switch (pci_get_devid(dev)) {

	case 0x10001042ul:	/* wd */
		return ("SMC FDC 37c665");
	};

	return NULL;
}

static int
ign_probe(device_t dev)
{
	const char *s;

	s = ign_match(dev);
	if (s) {
		device_set_desc(dev, s);
		device_quiet(dev);
		return -1000;
	}
	return ENXIO;
}

static int
ign_attach(device_t dev)
{
	return 0;
}

static device_method_t ign_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		ign_probe),
	DEVMETHOD(device_attach,	ign_attach),

	{ 0, 0 }
};

static driver_t ign_driver = {
	"ign",
	ign_methods,
	1,
};

static devclass_t ign_devclass;

DRIVER_MODULE(ign, pci, ign_driver, ign_devclass, 0, 0);
