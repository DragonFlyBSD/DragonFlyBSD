/*
 * Copyright (c) 2008 The DragonFly Project.  All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 * 3. Neither the name of The DragonFly Project nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific, prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 * 
 * $DragonFly: src/sys/dev/netif/sln/if_slnreg.h,v 1.1 2008/02/28 18:39:20 swildner Exp $
 */

#ifndef  _IF_SLREG_H_
#define  _IF_SLREG_H_

/*
 * Silan netcard register offsets
 */

#define SL_CFG0          0x00	/* software reset */
#define SL_CFG1          0x04	/* select RX buffer size */
#define SL_RBW_PTR       0x08	/* RX buffer write pointer */
#define SL_INT_STATUS    0x0C	/* interrupt status register */
#define SL_INT_MASK      0x10	/* interrupt mask register */
#define SL_RBSA          0x14	/* RX buffer start address */
#define SL_RBR_PTR       0x18	/* RX buffer read pointer */
#define SL_TSALD0        0x1C	/* TX status of all descriptors */
#define SL_TSD0          0x20	/* TX status of descriptor 0 */
#define SL_TSD1          0x24	/* TX status of descriptor 1 */
#define SL_TSD2          0x28	/* TX status of descriptor 2 */
#define SL_TSD3          0x2C	/* TX status of descriptor 3 */
#define SL_TSAD0         0x30	/* TX start address of descriptor 0 */
#define SL_TSAD1         0x34	/* TX start address of descriptor 1 */
#define SL_TSAD2         0x38	/* TX start address of descriptor 2 */
#define SL_TSAD3         0x3C	/* TX start address of descriptor 3 */
#define SL_RX_CONFIG     0x40	/* RX configuration register */
#define SL_MAC_ADDR0     0x44	/* MAC address register 0 [47-16] */
#define SL_MAC_ADDR1     0x48	/* MAC address register 1 [15-0] */
#define SL_MULTI_GROUP0  0x4C	/* multicast address config regiser 0 [63-32] */
#define SL_MULTI_GROUP1  0x50	/* multicast address config regiser 1 [31-0] */
#define SL_RX_STATUS0    0x54	/* RX status register 0 */
/* 0x58 reserved */
#define SL_TX_CONFIG     0x5C	/* TX configuration register */
#define SL_PHY_CTRL      0x60	/* Physical control */
#define SL_FLOW_CTRL     0x64	/* flow control register */
#define SL_MII_CMD0      0x68	/* MII command register 0 */
#define SL_MII_CMD1      0x6C	/* MII command register 1 */
#define SL_MII_STATUS    0x70	/* MII status register */
#define SL_TIMER_CNT     0x74	/* Timer counter register */
#define SL_TIMER_INTR    0x78	/* TImer interrupt register */
#define SL_PM_CFG        0x7C	/* power managerment configuration register */

/* config register 0 */
#define SL_SOFT_RESET           0x80000000
#define SL_ANAOFF               0x40000000
#define SL_LDPS                 0x20000000

/* config register 1 */
#define SL_EARLY_RX             0x80000000
#define SL_EARLY_TX             0x40000000

#define SL_RXFIFO_16BYTES	0x00000000
#define SL_RXFIFO_32BYTES	0x00200000
#define SL_RXFIFO_64BYTES	0x00400000
#define SL_RXFIFO_128BYTES	0x00600000
#define SL_RXFIFO_256BYTES	0x00800000
#define SL_RXFIFO_512BYTES	0x00A00000
#define SL_RXFIFO_1024BYTES	0x00C00000
#define SL_RXFIFO_NOTHRESH	0x00E00000

#define SL_RXBUF_8		0x00000000
#define SL_RXBUF_16		0x00000001
#define SL_RXBUF_32		0x00000003
#define SL_RXBUF_64		0x00000007
#define SL_RXBUF_128            0x0000000F

/* interrupt status register bits */
#define SL_INT_LINKFAIL         0x80000000
#define SL_INT_LINKOK           0x40000000
#define SL_INT_TIMEOUT          0x20000000
#define SL_INT_DMARD_ST         0x00080000
#define SL_INT_DMARD_FIN        0x00040000
#define SL_INT_STB_Pl           0x00020000
#define SL_INT_TXFIN_P          0x00010000
#define SL_INT_RXFIN_P          0x00008000
#define SL_INT_DMAWR_ST         0x00004000
#define SL_INT_DMAWR_FIN        0x00002000
#define SL_INT_RBO              0x00000040
#define SL_INT_ROK              0x00000020
#define SL_INT_TOK              0x00000001

#define  SL_INRTS     (SL_INT_LINKFAIL | SL_INT_LINKOK | SL_INT_TIMEOUT | SL_INT_RBO | SL_INT_ROK | SL_INT_TOK)

/* TX status of silan descriptors */
#define SL_TXSAD_TOK3        0x00008000
#define SL_TXSAD_TOK2        0x00004000
#define SL_TXSAD_TOK1        0x00002000
#define SL_TXSAD_TOK0        0x00001000
#define SL_TXSAD_TUN3        0x00000800
#define SL_TXSAD_TUN2        0x00000400
#define SL_TXSAD_TUN1        0x00000200
#define SL_TXSAD_TUN0        0x00000100
#define SL_TXSAD_TABT3       0x00000080
#define SL_TXSAD_TABT2       0x00000040
#define SL_TXSAD_TABT1 	     0x00000020
#define SL_TXSAD_TABT0	     0x00000010
#define SL_TXSAD_OWN3        0x00000008
#define SL_TXSAD_OWN2        0x00000004
#define SL_TXSAD_OWN1        0x00000002
#define SL_TXSAD_OWN0	     0x00000001

/* Transmit descriptor status register bits */
#define SL_TXSD_CRS           0x20000000
#define SL_TXSD_TABT          0x10000000
#define SL_TXSD_OWC           0x08000000
#define SL_TXSD_NCC           0x03C00000
#define SL_TXSD_EARLY_THRESH  0x003F0000
#define SL_TXSD_TOK           0x00008000
#define SL_TXSD_TUN           0x00004000
#define SL_TXSD_OWN           0x00002000
#define SL_TXSD_LENMASK       0x00001FFF

/* bits in TX configuration register */
#define SL_TXCFG_FULLDX          0x80000000
#define SL_TXCFG_EN              0x40000000
#define SL_TXCFG_PAD             0x20000000
#define SL_TXCFG_HUGE            0x10000000
#define SL_TXCFG_FCS             0x08000000
#define SL_TXCFG_NOBACKOFF       0x04000000
#define SL_TXCFG_PREMBLE         0x02000000
#define SL_TXCFG_LOSTCRS         0x01000000
#define SL_TXCFG_EXDCOLLNUM      0x00F00000
#define SL_TXCFG_DATARATE        0x00080000

/* bits in RX configuration register */
#define SL_RXCFG_FULLDX          0x80000000
#define SL_RXCFG_EN              0x40000000
#define SL_RXCFG_RCV_SMALL       0x20000000
#define SL_RXCFG_RCV_HUGE        0x10000000
#define SL_RXCFG_RCV_ERR         0x08000000
#define SL_RXCFG_RCV_ALL         0x04000000
#define SL_RXCFG_RCV_MULTI       0x02000000
#define SL_RXCFG_RCV_BROAD       0x01000000
#define SL_RXCFG_LP_BCK          0x00C00000
#define SL_RXCFG_LOW_THRESHOLD   0x00040000
#define SL_RXCFG_HIGH_THRESHOLD  0x00000700

/* Bits in RX status header (in RX'ed packet) */
#define SL_RXSTAT_LENMASK	0xFFF00000
#define SL_RXSTAT_RXOK		0x00080000
#define SL_RXSTAT_ALIGNERR      0x00040000
#define SL_RXSTAT_HUGEFRM	0x00020000
#define SL_RXSTAT_SMALLFRM	0x00010000
#define SL_RXSTAT_CRCOK 	0x00008000
#define SL_RXSTAT_CRLFRM	0x00004000
#define SL_RXSTAT_BROAD 	0x00002000
#define SL_RXSTAT_MULTI 	0x00001000
#define SL_RXSTAT_MATCH		0x00000800
#define SL_RXSTAT_MIIERR	0x00000400

/* Physical Control configuration register */
#define SL_PHYCTL_ANE           0x80000000
#define SL_PHYCTL_SPD100        0x40000000
#define SL_PHYCTL_SPD10         0x20000000
#define SL_PHYCTL_BASEADD       0x1F000000
#define SL_PHYCTL_DUX           0x00800000
#define SL_PHYCTL_RESET         0x00400000

/* Flow Control configuration register */
#define SL_FLOWCTL_FULLDX       0x80000000
#define SL_FLOWCTL_EN           0x40000000
#define SL_FLOWCTL_PASSALL      0x20000000
#define SL_FLOWCTL_ENPAUSE      0x10000000
#define SL_FLOWCTL_PAUSEF       0x08000000
#define SL_FLOWCTL_PAUSE0       0x04000000

/* MII command register 0 */
#define SL_MII0_DIVEDER       0x20000000
#define SL_MII0_NOPRE         0x00800000
#define SL_MII0_WRITE         0x00400000
#define SL_MII0_READ          0x00200000
#define SL_MII0_SCAN          0x00100000
#define SL_MII0_TXMODE        0x00080000
#define SL_MII0_DRVMOD        0x00040000
#define SL_MII0_MDC           0x00020000
#define SL_MII0_MDOEN         0x00010000
#define SL_MII0_MDO           0x00008000
#define SL_MII0_MDI           0x00004000

/* MII status register */
#define SL_MIISTAT_BUSY       0x80000000

/* register in 80225 */
#define SL_MII_CTRL            0
#define SL_MII_STAT            1
#define SL_MII_ADV             4
#define SL_MII_JAB             16
#define SL_MII_STAT_OUTPUT     24

/* bit value for 80225 */
#define SL_MIICTRL_ANEGEN	0x1000
#define SL_MIICTRL_SPEEDSEL     0x2000
#define SL_MIICTRL_DUPSEL       0x0100
#define SL_MIICTRL_ANEGRSTR     0x0200
#define SL_MIISTAT_LINK		0x0004
#define SL_MIISTAT_ANEGACK	0x0020
#define SL_PHY_16_JAB_ENB       0x1000
#define SL_PHY_16_PORT_ENB      0x1

/*
 * PCI low memory base and low I/O base register, and other PCI registers.
 */
#define SL_PCI_VENDORID 	0x00
#define SL_PCI_DEVICEID	        0x02
#define SL_PCI_COMMAND		0x04
#define SL_PCI_STATUS		0x06
#define SL_PCI_REVISIONID	0x08
#define SL_PCI_MEMAD            0x10
#define SL_PCI_IOAD             0x14
#define SL_PCI_SUBVENDORID      0x2C
#define SL_PCI_SUBDEVICEID      0x2E
#define RL_PCI_INTLINE		0x3C

#define SL_CMD_IO               0x0001
#define SL_CMD_MEMORY           0x0002
#define SL_CMD_BUSMASTER        0x0004

#define  SL_TXD_CNT        4
#define  SL_RX_BUF_SZ      SL_RXBUF_64
#define  SL_RX_BUFLEN      (1 << (SL_RX_BUF_SZ + 9))
#define  TX_CFG_DEFAULT    0x48800000

/* register space access macros */
#define SLN_WRITE_4(adapter, reg, val)   bus_space_write_4(adapter->sln_bustag, adapter->sln_bushandle, reg, val)
#define SLN_WRITE_2(adapter, reg, val)   bus_space_write_2(adapter->sln_bustag, adapter->sln_bushandle, reg, val)
#define SLN_WRITE_1(adapter, reg, val)   bus_space_write_1(adapter->sln_bustag, adapter->sln_bushandle, reg, val)

#define SLN_READ_4(adapter, reg)     bus_space_read_4(adapter->sln_bustag, adapter->sln_bushandle, reg)
#define SLN_READ_2(adapter, reg)     bus_space_read_2(adapter->sln_bustag, adapter->sln_bushandle, reg)
#define SLN_READ_1(adapter, reg)     bus_space_read_1(adapter->sln_bustag, adapter->sln_bushandle, reg)

#define SL_DIRTY_TXBUF(x)    x->sln_bufdata.sln_tx_buf[x->sln_bufdata.dirty_tx]
#define SL_CUR_TXBUF(x)      x->sln_bufdata.sln_tx_buf[x->sln_bufdata.cur_tx]

#endif	/* !_IF_SLREG_H_ */
