/*
 * Copyright (c) 2006 David Gwynne <dlg@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 *
 * $OpenBSD: sili.c,v 1.147 2009/02/16 21:19:07 miod Exp $
 */

#if defined(__DragonFly__)
#include "sili_dragonfly.h"
#else
#error "build for OS unknown"
#endif
#include "pmreg.h"
#include "atascsi.h"

/* change to SILI_DEBUG for dmesg spam */
#define NO_SILI_DEBUG

#ifdef SILI_DEBUG
#define DPRINTF(m, f...) do { if ((silidebug & (m)) == (m)) kprintf(f); } \
    while (0)
#define SILI_D_TIMEOUT		0x00
#define SILI_D_VERBOSE		0x01
#define SILI_D_INTR		0x02
#define SILI_D_XFER		0x08
int silidebug = SILI_D_VERBOSE;
#else
#define DPRINTF(m, f...)
#endif

/*
 * BAR0 - Global Registers		128-byte aligned, 128-byte region
 * BAR1 - Port Registers and LRAM	32KB-aligned
 * BAR2 - Indirect I/O registers	(we don't use this)
 */

/*
 * Port-N Slot Status.
 *
 * NOTE: Mirrors SILI_PREG_SLOTST
 */
#define SILI_REG_SLOTST(n)	(0x0000 + ((n) * 4))
#define SILI_REG_SLOTST_ATTN	0x80000000	/* attention required */

#define SILI_REG_GCTL		0x0040		/* Global Control	*/
#define SILI_REG_GCTL_PORTEN(n)	(1 << (n))	/* Port interrupt ena	*/
#define SILI_REG_GCTL_300CAP	0x01000000	/* 3Gb/s capable (R)	*/
#define SILI_REG_GCTL_I2C_IEN	0x20000000	/* I2C Int enable	*/
#define SILI_REG_GCTL_MSIACK	0x40000000	/* MSI Ack W1C		*/
#define SILI_REG_GCTL_GRESET	0x80000000	/* global reset */

#define SILI_REG_GINT		0x0044		/* Global Interrupt Status */
#define SILI_REG_GINT_I2C	0x20000000	/* I2C Int Status	*/
#define SILI_REG_GINT_PORTST(n)	(1 << (n))	/* Port interrupt stat  */
#define SILI_REG_GINT_PORTMASK	0x0000FFFF

/*
 * Most bits in phyconf should not be modified.  Setting the low four bits
 * to 1's, all channel Tx outputs spread spectrum clocking.
 */
#define SILI_REG_PHYCONF	0x0048		/* PHY Configuration */
#define SILI_REG_PHYCONF_ALLSS	0x000F		/* spread spectrum tx */

/*
 * BIST_CTL_TEN -	Enable data paths for running data loopback BIST
 * BIST_CTL_TPAT -	Select repeating pattern (1) or pseudo-random
 *			pattern (0)
 * BIST_CTL_RXSEL-	Select the rx port for pattern comparison
 * BIST_CTL_TXSEL-	Select the tx ports that transmit loopback data
 */
#define SILI_REG_BIST_CTL	0x0050
#define SILI_REG_BIST_CTL_TEN	0x80000000
#define SILI_REG_BIST_CTL_TPAT	0x40000000
#define SILI_REG_BIST_CTL_RXSEL(n) ((n) << 16)
#define SILI_REG_BIST_CTL_TXSEL(n) (1 << (n))

#define SILI_REG_BIST_PATTERN	0x0054	/* 32 bit pattern */

/*
 * GOOD is set to 1 on BIST initiation, and reset to 0 on the first
 * comparison failure.
 */
#define SILI_REG_BIST_STATUS	0x0058
#define SILI_REG_BIST_STATUS_CNT  0x000007FF	/* pattern counter */
#define SILI_REG_BIST_STATUS_GOOD 0x80000000	/* set to 0 on compare fail */

#define SILI_REG_I2C_CTL	0x0060
#define SILI_REG_I2C_CTL_START	0x00000001
#define SILI_REG_I2C_CTL_STOP	0x00000002
#define SILI_REG_I2C_CTL_NACK	0x00000004	/* send nack on data byte rx */
#define SILI_REG_I2C_CTL_TFDATA	0x00000008	/* set to initiate txfer     */
						/* to/from data buffer	     */
#define SILI_REG_I2C_CTL_MABORT	0x00000010	/* set w/STOP to send stop   */
						/* without first xfering a   */
						/* byte			     */
#define SILI_REG_I2C_CTL_SCLEN	0x00000020	/* clock-en for master mode  */
#define SILI_REG_I2C_CTL_UNITEN	0x00000040	/* enable controller 	     */
#define SILI_REG_I2C_CTL_GCALLD	0x00000080	/* Disable detect of a       */
						/* general call address      */
#define SILI_REG_I2C_CTL_TXBEI	0x00000100	/* xmit buffer empty int en  */
#define SILI_REG_I2C_CTL_RXBFI	0x00000200	/* rx buffer full int en     */
#define SILI_REG_I2C_CTL_BERRI	0x00000400	/* bus error int en	     */
#define SILI_REG_I2C_CTL_STOPI	0x00000800	/* stop detect int en	     */
#define SILI_REG_I2C_CTL_ARBLI	0x00001000	/* arb loss int en	     */
#define SILI_REG_I2C_CTL_ARBDI	0x00002000	/* arb detect int en	     */
#define SILI_REG_I2C_CTL_UNITRS	0x00004000	/* reset I2C controller	     */
#define SILI_REG_I2C_CTL_FASTM	0x00008000	/* 400kbit/s (else 100kbit/s)*/

#define SILI_REG_I2C_STS	0x0064
#define SILI_REG_I2C_STS_RW	0x00000001
#define SILI_REG_I2C_STS_ACKSTS	0x00000002	/* ack/nack status(R) last   */
						/* ack or nack sent or rcvd  */
#define SILI_REG_I2C_STS_UNTBSY	0x00000004	/* unit busy (R)	     */
#define SILI_REG_I2C_STS_BUSBSY	0x00000008	/* bus busy with activity(R) */
						/* other then from controller*/
#define SILI_REG_I2C_STS_STOPDT	0x00000010	/* stop detect (R/W1C)	     */
#define SILI_REG_I2C_STS_ARBLD	0x00000020	/* arb loss detect   (R/W1C) */
#define SILI_REG_I2C_STS_TXBED	0x00000040	/* tx buffer empty   (R)     */
#define SILI_REG_I2C_STS_RXBFD	0x00000080	/* rx buffer full    (R/W1C) */
#define SILI_REG_I2C_STS_GCALLD	0x00000100	/* Gen Call detect   (R/W1C) */
#define SILI_REG_I2C_STS_SADDRD	0x00000200	/* Slave addr detect (R/W1C) */
#define SILI_REG_I2C_STS_BERRD	0x00000400	/* Bus error detect  (R/W1C) */

#define SILI_REG_I2C_SADDR	0x0068		/* our slave address         */
#define SILI_REG_I2C_SADDR_MASK	0x0000003F	/* 6 bits                    */

#define SILI_REG_I2C_DATA	0x006C		/* data buffer (8 bits)      */

#define SILI_REG_FLASH_ADDR	0x0070		/* Flash control & addr reg  */
#define SILI_REG_FLASH_ADDR_MEMPRS 0x04000000	/* Flash memory present	     */
#define SILI_REG_FLASH_ADDR_GPIOEN 0x80000000	/* use flash data pins for   */
						/* GPIO */
#define SILI_REG_FLASH_ADDR_MEMST  0x02000000	/* Mem Access Start (R/W)    */
						/* (clears on op complete)   */
#define SILI_REG_FLASH_ADDR_MEMRD  0x01000000	/* 0=write, 1=read           */
#define SILI_REG_FLASH_ADDR_MASK   0x0003FFFF	/* 18 bit memory address     */

/*
 * NOTE: In order to set a GPIO pin to read the DATA bit must be written to
 *       1and the DCTL (drain control) bit must be written to 1 as well
 *       to make it open-drain only (drive on low only).
 */
#define SILI_REG_GPIO		0x0074
#define SILI_REG_GPIO_DATA_SHIFT	0	/* 8 bits Flash or GPIO data */
#define SILI_REG_GPIO_TDET_SHIFT	8	/* 8 bits transition detect  */
#define SILI_REG_GPIO_DCTL_SHIFT	16	/* 8 bits drain control      */

/*
 * Per-port registers
 *
 */

#define SILI_PORT_REGION(port)	(8192 * (port))
#define SILI_PORT_SIZE		8192
#define SILI_PREG_LRAM		0x0000		/* 0x0000-0x0F7F	     */
#define SILI_PREG_LRAM_SLOT(n)	(0x0000 + (128 * (n)))

#define SILI_PREG_LRAM_SLOT_FIS	0x0000		/* Current FIS and Control   */
#define SILI_PREG_LRAM_DMA0	0x0020		/* DMA Entry 0 or ATAPI cmd  */
#define SILI_PREG_LRAM_DMA1	0x0030		/* DMA Entry 0 or ATAPI cmd  */
#define SILI_PREG_LRAM_CMDACT	0x0040		/* Cmd Act Reg (actual) 64b  */
#define SILI_PREG_LRAM_DMATAB	0x0040		/* Scatter Gather Table      */

/*
 * Each port has a port status and qactive register for each target behind
 * the port multiplier, if there is a port multiplier.
 *
 * SERVICE - Service received from device, service command from controller
 *	     not yet acknowledged.
 *
 * LEGACY  - One or more legacy queued commands is outstanding.
 *
 * NATIVE  - One or more NCQ queued commands is outstanding.
 *
 * VBSY    - A virtual device busy indicating that a command has been issued
 *	     to the device and the device has not yet sent the final D2H
 *	     register FIS, or that a data transfer is in progress.
 *
 * The PM_QACTIVE register contains a demultiplexed bitmap of slots queued
 * to each target behind the port multiplier.
 *
 */
#define SILI_PREG_PM_STATUS(n)	(0x0F80 + (8 * (n)))
#define SILI_PREG_PM_QACTIVE(n)	(0x0F84 + (8 * (n)))

#define SILI_PREG_PM_STATUS_SERVICE	0x00010000	/* Service pending */
#define SILI_PREG_PM_STATUS_LEGACY	0x00008000	/* Legacy outstanding*/
#define SILI_PREG_PM_STATUS_NATIVE	0x00004000	/* NCQ outstanding */
#define SILI_PREG_PM_STATUS_VBSY	0x00002000	/* virtual dev busy */
#define SILI_PREG_PM_STATUS_EXEC_SHIFT	8		/* last active slot */
#define SILI_PREG_PM_STATUS_EXEC_MASK	0x1F
#define SILI_PREG_PM_STATUS_PIO_MASK	0xFF		/* last PIO setup   */

/*
 * NOTE: SILI_PREG_STATUS uses the same address as SILI_PREG_CTL_SET,
 * but for read.
 */
#define SILI_PREG_STATUS	0x1000		/* Port Control Status (R)   */
#define SILI_PREG_STATUS_READY	0x80000000	/* Port Ready          (R)   */
#define SILI_PREG_STATUS_SLOT	0x001F0000	/* Active Slot         (R)   */
#define SILI_PREG_STATUS_SLOT_SHIFT	16	/* Shift value		     */
#define SILI_PREG_STATUS_MASK	0x0200FFFF	/* see PREG_CTL_xxx          */

/*
 * NOTE: Reset sequence.  Set CTL_RESET, Clear CTL_RESET, then Wait for
 *       the port to become ready.
 *
 * NOTE: NOAUTOCC.  If set to 1 a 1 must be written to the command completion
 *	 bit in the port interrupt status register to clear it.  When set to
 *	 0 then reading the port slot status register will automatically clear
 *       the command completion interrupt.
 *
 * NOTE: ATA16 controls whether a PACKET mode command is 12 or 16 bytes.
 *
 * NOTE: RESUME if set to 1 processing is enabled for outstanding commands
 *       to additional targets connected to a port multiplier after a command
 *	 error has occured.  When set the internal BUSY status will be set
 *	 for the target that errored, preventing additional commands from
 *       being sent until a Port Initialize operation is performed.
 *
 * NOTE: 32BITDMA if 1 causes a write to the low 32 bits of a Command
 *	 Activation register to  copy PREG_32BIT_ACTUA to the upper 32
 *       bits and start command execution.  If 0 you must write to the
 *       low 32 bits and then the high 32 bits and your write to the
 *       high 32 bits will start command execution.
 *
 * NOTE: OOB_BYP is set on global reset and not changed by a port reset.
 */
#define SILI_PREG_CTL_SET	0x1000		/* Port Control Set    (W1S) */
#define SILI_PREG_CTL_CLR	0x1004		/* Port Control Clear  (W1C) */
#define SILI_PREG_CTL_RESET	0x00000001	/* Hold port in reset        */
#define SILI_PREG_CTL_DEVRESET	0x00000002	/* Device reset              */
						/* (Self clearing)           */
#define SILI_PREG_CTL_INIT	0x00000004	/* Port initialize           */
						/* (Self clearing)           */
#define SILI_PREG_CTL_NOAUTOCC	0x00000008
#define SILI_PREG_CTL_NOLED	0x00000010	/* Disable the LED port      */
						/* activity indicator.       */
#define SILI_PREG_CTL_ATA16	0x00000020	/* 0=12 byte 1=16 byte       */
#define SILI_PREG_CTL_RESUME	0x00000040	/* PM special error handling */
#define SILI_PREG_CTL_TXBIST	0x00000080	/* transmit a BIST FIS       */
#define SILI_PREG_CTL_CONT_DIS	0x00000100	/* no CONT on repeat primitves*/
#define SILI_PREG_CTL_NOSCRAM	0x00000200	/* Disable link scrambler    */
#define SILI_PREG_CTL_32BITDMA	0x00000400	/* see above 		     */
#define SILI_PREG_CTL_ACC_ILCK	0x00000800	/* accept interlocked FIS rx */
						/* (Self clearing)           */
#define SILI_PREG_CTL_REJ_ILCK	0x00001000	/* reject interlocked FIS rx */
						/* (Self clearing)           */
#define SILI_PREG_CTL_PMA	0x00002000	/* Enable PM support         */
#define SILI_PREG_CTL_AUTO_ILCK	0x00004000	/* Auto interlock accept     */
#define SILI_PREG_CTL_LEDON	0x00008000	/* LED on		     */
#define SILI_PREG_CTL_OOB_BYP	0x02000000	/* Bypass OOB initialization */

/*
 * Status bits in the upper half of the register report the actual condition
 * while the status bits in the lower half of the register are masked by
 * the interrupt enable bits or threshold registers.  Writing a 1 to either
 * version will clear it.
 *
 * NOTE: The steering bits written to INT_ENABLE will not show up in the
 *       status register.  The INT_ENABLE/INT_DISABLE registers are R+W1S
 *	 or R+W1C and thus can be read.
 *
 * NOTE: PHYRDYCHG, COMWAKE, UNRECFIS, DEVEXCHG: Can be cleared by writing
 *	 W1C either here or via the DIAG.xxx bit bit in SError.
 */
#define SILI_PREG_INT_STATUS	0x1008		/* Control clear	     */
#define SILI_PREG_INT_ENABLE	0x1010		/* Interrupt Enable Set      */
#define SILI_PREG_INT_DISABLE	0x1014		/* Interrupt Enable Clear    */

#define SILI_PREG_INT_STEER(n)	((n) << 30)	/* Port Int -> INTA...INTD   */
#define SILI_PREG_INT_CCOMPLETE	0x00000001	/* one or more cmds completed*/
#define SILI_PREG_INT_CERROR	0x00000002	/* read port error register  */
						/* to get error              */
#define SILI_PREG_INT_READY	0x00000004	/* Port is ready for cmds    */
#define SILI_PREG_INT_PMCHANGE	0x00000008	/* Change in power mng state */
#define SILI_PREG_INT_PHYRDYCHG	0x00000010	/* Mirrors DIAG.N in SError  */
#define SILI_PREG_INT_COMWAKE	0x00000020	/* Mirrors DIAG.W in SError  */
#define SILI_PREG_INT_UNRECFIS	0x00000040	/* Mirrors DIAG.F in SError  */
#define SILI_PREG_INT_DEVEXCHG	0x00000080	/* Mirrors DIAG.X in SError  */
#define SILI_PREG_INT_DECODE	0x00000100	/* 8b/10b dec err  cnt > thr */
#define SILI_PREG_INT_CRC	0x00000200	/* CRC err       count > thr */
#define SILI_PREG_INT_HANDSHK	0x00000400	/* Handshake err count > thr */
#define SILI_PREG_INT_SDB	0x00000800	/* Set Device Bits (SNotify) */
#define SILI_PREG_INT_SHIFT	16		/* shift upper bits of status*/

#define SILI_PREG_IST_CCOMPLETE	0x00010000	/* one or more cmds completed*/
#define SILI_PREG_IST_CERROR	0x00020000	/* read port error register  */
						/* to get error              */
#define SILI_PREG_IST_READY	0x00040000	/* Port is ready for cmds    */
#define SILI_PREG_IST_PMCHANGE	0x00080000	/* Change in power mng state */
#define SILI_PREG_IST_PHYRDYCHG	0x00100000	/* Mirrors DIAG.N in SError  */
#define SILI_PREG_IST_COMWAKE	0x00200000	/* Mirrors DIAG.W in SError  */
#define SILI_PREG_IST_UNRECFIS	0x00400000	/* Mirrors DIAG.F in SError  */
#define SILI_PREG_IST_DEVEXCHG	0x00800000	/* Mirrors DIAG.X in SError  */
#define SILI_PREG_IST_DECODE	0x01000000	/* 8b/10b dec err  cnt > thr */
#define SILI_PREG_IST_CRC	0x02000000	/* CRC err       count > thr */
#define SILI_PREG_IST_HANDSHK	0x04000000	/* Handshake err count > thr */
#define SILI_PREG_IST_SDB	0x08000000	/* Set Device Bits (SNotify) */

#define SILI_PREG_INT_MASK	(SILI_PREG_INT_CCOMPLETE |		\
				 SILI_PREG_INT_CERROR |			\
				 SILI_PREG_INT_READY |			\
				 SILI_PREG_INT_PMCHANGE |		\
				 SILI_PREG_INT_PHYRDYCHG |		\
				 SILI_PREG_INT_COMWAKE |		\
				 SILI_PREG_INT_UNRECFIS |		\
				 SILI_PREG_INT_DEVEXCHG |		\
				 SILI_PREG_INT_DECODE |			\
				 SILI_PREG_INT_CRC |			\
				 SILI_PREG_INT_HANDSHK |		\
				 SILI_PREG_INT_SDB)
#define SILI_PREG_IST_MASK	(SILI_PREG_INT_MASK << 16)

#define SILI_PFMT_INT_STATUS	"\020" 			\
				"\034SDB"		\
				"\033HANDSHK"		\
				"\032CRC"		\
				"\031DECODE"		\
				"\030DEVEXCHG"		\
				"\027UNRECFIS"		\
				"\026COMWAKE"		\
				"\025PHYRDYCHG"		\
				"\024PMCHANGE"		\
				"\023READY"		\
				"\022ERROR"		\
				"\021COMPLETE"		\
							\
				"\014SDBm"		\
				"\013HANDSHKm"		\
				"\012CRCm"		\
				"\011DECODEm"		\
				"\010DEVEXCHGm"		\
				"\007UNRECFISm"		\
				"\006COMWAKEm"		\
				"\005PHYRDYCHGm"	\
				"\004PMCHANGEm"		\
				"\003READYm"		\
				"\002ERRORm"		\
				"\001COMPLETEm"

/*
 * 32BIT_ACTUA is only used when DMA is 32 bits.  It is typically set to 0.
 */
#define SILI_PREG_32BIT_ACTUA	0x101C		/* 32b activation upper addr */

/*
 * Writing a slot number 0-30 to CMD_FIFO starts the command from LRAM.
 */
#define SILI_PREG_CMD_FIFO	0x1020		/* Command execution FIFO    */

/*
 * If the port is interrupted via INT_CERROR this register contains
 * the error code.
 *
 * Most errors write the task file register back to the PRB slot for host
 * scrutiny.
 */
#define SILI_PREG_CERROR	0x1024		/* Command error             */
#define SILI_PREG_CERROR_DEVICE		1	/* ERR set in D2H FIS        */
#define SILI_PREG_CERROR_SDBERROR	2	/* ERR set in SDB from device*/
#define SILI_PREG_CERROR_DATAFISERR	3	/* Sil3132 error on send     */
#define SILI_PREG_CERROR_SENDFISERR	4	/* Sil3132 error on send     */
#define SILI_PREG_CERROR_BADSTATE	5	/* Sil3132 inconsistency     */
#define SILI_PREG_CERROR_DIRECTION	6	/* DMA direction mismatch    */
#define SILI_PREG_CERROR_UNDERRUN	7	/* DMA SG H2D list too small */
#define SILI_PREG_CERROR_OVERRUN	8	/* DMA SG D2H list too small */
#define SILI_PREG_CERROR_LLOVERUN	9	/* Too much data from device */
#define SILI_PREG_CERROR_PKTPROTO	11	/* Packet protocol error     */
#define SILI_PREG_CERROR_BADALIGN	16	/* Bad SG list, not 8-byte   */
						/* aligned                   */
#define SILI_PREG_CERROR_PCITGTABRT	17	/* PCI target abort received */
#define SILI_PREG_CERROR_PCIMASABRT	18	/* PCI master abort received */
#define SILI_PREG_CERROR_PCIPARABRT	19	/* PCI parity abort received */
#define SILI_PREG_CERROR_PRBALIGN	24	/* PRB addr not 8-byte algned*/
#define SILI_PREG_CERROR_PCITGTABRT2	25	/* During fetch of PRB       */
#define SILI_PREG_CERROR_PCIMASABRT2	26	/* During fetch of PRB       */
#define SILI_PREG_CERROR_PCIPARABRT3	33	/* During data transfer      */
#define SILI_PREG_CERROR_PCITGTABRT3	34	/* During data transfer      */
#define SILI_PREG_CERROR_PCIMASABRT3	35	/* During data transfer      */
#define SILI_PREG_CERROR_SERVICE	36	/* FIS received during tx    */
						/* phase                     */

/*
 * Port FIS Configuration.  Fir each possible FIS type, a 2-bit code
 * defines the desired reception behavior as follows.  Bits [1:0] define
 * the code for all other FIS types not defined by [29:2].
 *
 *	00 Accept FIS without interlock
 *	01 Reject FIS without interlock
 *	10 Interlock FIS
 *	11 (reserved)
 *
 * FIS Code	Name			Start	Default
 * --------	------			------	-------
 *    ----	(reserved)		30
 *    0x27	Register (H2D)		28	01
 *    0x34	Register (D2H)		26	00
 *    0x39	DMA Activate		24	00
 *    0x41	DMA Setup		22	00
 *    0x46	Data			20	00
 *    0x58	BIST Activate		18	00
 *    0x5F	PIO Setup		16	00
 *    0xA1	Set Device Bits		14	00
 *    0xA6	(reserved)		12	01
 *    0xB8	(reserved)		10	01
 *    0xBF	(reserved)		8	01
 *    0xC7	(reserved)		6	01
 *    0xD4	(reserved)		4	01
 *    0xD9	(reserved)		2	01
 * ALL OTHERS	(reserved)		0	01
 */
#define SILI_PREG_FIS_CONFIG	0x1028		/* FIS configuration         */

/*
 * The data FIFO is 2KBytes in each direction.
 *
 * When DMAing from the device the Write Request Threshold is used.
 * When DMAing to the device the Read Request Threshold is used.
 *
 * The threshold can be set from 1-2040 bytes (write 0-2039), in multiples
 * of 8 bits.  The low 3 bits are hardwired to 0.  A value of 0 causes a
 * request whenever possible.
 */
#define SILI_PREG_FIFO_CTL	0x102C		/* PCIex request FIFO thresh */
#define SILI_PREG_FIFO_CTL_READ_SHIFT	0
#define SILI_PREG_FIFO_CTL_WRITE_SHIFT	16
#define SILI_PREG_FIFO_CTL_MASK	0xFF
#define SILI_PREG_FIFO_CTL_ENCODE(rlevel, wlevel)  (rlevel | (wlevel << 16))

/*
 * Error counters and thresholds.  The counter occupies the low 16 bits
 * and the threshold occupies the high 16 bits.  The appropriate interrupt
 * occurs when the counter exceeds the threshold.  Clearing the interrupt
 * clears the counter as well.  A threshold of 0 disables the interrupt
 * assertion and masks the interrupt status bit in the port interrupt status
 * register.
 */
#define SILI_PREG_CTR_DECODE	0x1040		/* 8B/10B Decode Error Ctr   */
#define SILI_PREG_CTR_CRC	0x1044		/* CRC Error Counter         */
#define SILI_PREG_CTR_HANDSHK	0x1048		/* Handshake Error Counter   */

/*
 * NOTE: This register is reset only by the global reset and will not be
 * 	 reset by a port reset.
 *
 * NOTE: Bits 15:5 configure the PHY and should not be changed unless you
 *	 want to blow up the part.
 *
 *	 Bits 4:0 define the nominal output swing for the transmitter
 *	 and are set to 0x0C by default.  Generally speaking, don't mess
 *	 with them.
 */
#define SILI_PREG_PHY_CONFIG	0x1050		/* Handshake Error Counter   */
#define SILI_PREG_PHY_CONFIG_AMP_MASK	0x1F

#define SILI_PREG_SLOTST	0x1800		/* Slot Status		     */
#define SILI_PREG_SLOTST_ATTN	0x80000000	/* 0-30 bit for each slot */

/*
 * Shadow command activation register, shadows low or high 32 bits
 * of actual command activation register.
 */
#define SILI_PREG_CMDACT(n)	(0x1C00 + (8 * (n)))

/*
 * Port Context Register.  Contains the port multipler target (0-15) and
 * the command slot (0-31) for the PM port state machine.
 *
 * Upon a processing halt due to a device specific error, the port multipler
 * target is the one that returned the error status.
 *
 * The command slot is NOT deterministic in this case and should not be
 * assumed valid.  Use READ LOG EXTENDED to determine the tag number.
 * However, the documentation does appear to indicate that for non-NCQ
 * errors the command slot does contain the tag that errored (since there
 * will be only one truely active).
 */
#define SILI_PREG_CONTEXT		0x1E04
#define SILI_PREG_CONTEXT_SLOT_MASK	0x1F
#define SILI_PREG_CONTEXT_PMPORT_MASK	0x0F
#define SILI_PREG_CONTEXT_SLOT_SHIFT	0
#define SILI_PREG_CONTEXT_PMPORT_SHIFT	5

/*
 * SControl register - power management, speed negotiation, and COMRESET
 *		       operation.
 */
#define SILI_PREG_SCTL			0x1F00

/*
 * PMP: Identify the PM port for accessing the SActive register and some
 *	bit fields of the Diagnostic registers.
 */
#define SILI_PREG_SCTL_PMP		0x000F0000
#define SILI_PREG_SCTL_PMP_SHIFT	16

/*
 * SPM: It is unclear from mode 4 is.  "Transition from a power management
 *	state initiate (ComWake asserted)".  When setting a state, the field
 *	will self-reset to 0 as soon as the action is initiated.
 */
#define SILI_PREG_SCTL_SPM		0x0000F000
#define  SILI_PREG_SCTL_SPM_NONE	0x00000000
#define  SILI_PREG_SCTL_SPM_PARTIAL	0x00010000
#define  SILI_PREG_SCTL_SPM_SLUMBER	0x00020000
#define  SILI_PREG_SCTL_SPM_FROM	0x00040000

/*
 * IPM: Identify interface power management states not supported (bits).
 */
#define SILI_PREG_SCTL_IPM		0x00000F00
#define  SILI_PREG_SCTL_IPM_NONE	0x00000000
#define  SILI_PREG_SCTL_IPM_NPARTIAL	0x00000100
#define  SILI_PREG_SCTL_IPM_NSLUMBER	0x00000200

/*
 * SPD: Limit speed negotiation (0000 for no restrictions)
 */
#define	SILI_PREG_SCTL_SPD		0x000000F0
#define	 SILI_PREG_SCTL_SPD_NONE	0x00000000
#define	 SILI_PREG_SCTL_SPD_GEN1	0x00000010
#define	 SILI_PREG_SCTL_SPD_GEN2	0x00000020

/*
 * DET: Control host adapter device detection and interface initialization
 */
#define	SILI_PREG_SCTL_DET		0x0000000F
#define	 SILI_PREG_SCTL_DET_NONE	0x00000000	/* nop/complete     */
#define	 SILI_PREG_SCTL_DET_INIT	0x00000001	/* hold in COMRESET */

/*
 * SStatus register - Probe status
 */
#define SILI_PREG_SSTS			0x1F04
#define  SILI_PREG_SSTS_IPM		0x00000F00
#define  SILI_PREG_SSTS_IPM_NOCOMM	0x00000000
#define  SILI_PREG_SSTS_IPM_ACTIVE	0x00000100
#define  SILI_PREG_SSTS_IPM_PARTIAL	0x00000200
#define  SILI_PREG_SSTS_IPM_SLUMBER	0x00000600

#define SILI_PREG_SSTS_SPD		0x000000F0
#define  SILI_PREG_SSTS_SPD_NONE	0x00000000
#define  SILI_PREG_SSTS_SPD_GEN1	0x00000010
#define  SILI_PREG_SSTS_SPD_GEN2	0x00000020

#define SILI_PREG_SSTS_DET		0x0000000F
#define  SILI_PREG_SSTS_DET_NOPHY	0x00000000	/* no dev, no phy */
#define  SILI_PREG_SSTS_DET_DEV_NE	0x00000001	/* dev, no phy	*/
#define  SILI_PREG_SSTS_DET_DEV		0x00000003	/* dev and phy	*/
#define  SILI_PREG_SSTS_DET_OFFLINE	0x00000004	/* BIST/LOOPBACK */

/*
 * These are mostly R/W1C bits.  "B", "C", and "H" operate independantly
 * and depend on the corresponding error counter register.
 */
#define SILI_PREG_SERR			0x1F08
#define  SILI_PREG_SERR_ERR_I		(1<<0) /* Recovered Data Integrity */
#define  SILI_PREG_SERR_ERR_M		(1<<1) /* Recovered Communications */
#define  SILI_PREG_SERR_ERR_T		(1<<8) /* Transient Data Integrity */
#define  SILI_PREG_SERR_ERR_C		(1<<9) /* Persistent Comm/Data */
#define  SILI_PREG_SERR_ERR_P		(1<<10) /* Protocol */
#define  SILI_PREG_SERR_ERR_E		(1<<11) /* Internal */
#define  SILI_PREG_SERR_DIAG_N		(1<<16) /* PhyRdy Change */
#define  SILI_PREG_SERR_DIAG_I		(1<<17) /* Phy Internal Error */
#define  SILI_PREG_SERR_DIAG_W		(1<<18) /* Comm Wake */
#define  SILI_PREG_SERR_DIAG_B		(1<<19) /* 10B to 8B Decode Error */
#define  SILI_PREG_SERR_DIAG_D		(1<<20) /* Disparity Error */
#define  SILI_PREG_SERR_DIAG_C		(1<<21) /* CRC Error */
#define  SILI_PREG_SERR_DIAG_H		(1<<22) /* Handshake Error */
#define  SILI_PREG_SERR_DIAG_S		(1<<23) /* Link Sequence Error */
#define  SILI_PREG_SERR_DIAG_T		(1<<24) /* Transport State Trans Err */
#define  SILI_PREG_SERR_DIAG_F		(1<<25) /* Unknown FIS Type */
#define  SILI_PREG_SERR_DIAG_X		(1<<26) /* Exchanged */

#define  SILI_PFMT_SERR	"\020" 	\
			"\033DIAG.X" "\032DIAG.F" "\031DIAG.T" "\030DIAG.S" \
			"\027DIAG.H" "\026DIAG.C" "\025DIAG.D" "\024DIAG.B" \
			"\023DIAG.W" "\022DIAG.I" "\021DIAG.N"		    \
			"\014ERR.E" "\013ERR.P" "\012ERR.C" "\011ERR.T"	    \
			"\002ERR.M" "\001ERR.I"

/*
 * SACT provides indirect access to the Port Device QActive registers.
 * We have direct access and do not have to use this.
 */
#define SILI_PREG_SACT			0x1F0C

/*
 * Indicate which devices have sent a Set Device Bits FIS with Notifcation
 * set.  R/W1C
 */
#define SILI_PREG_SNTF			0x1F10

/*
 * Internal register space indirect register access via the PCI I/O space.
 * (This is for BIOSes running in 16-bit mode, we use the direct map).
 *
 * All offsets must be 4-byte aligned
 */
#define SILI_BAR2_GRO			0x0000	/* Global Register Offset */
#define SILI_BAR2_GRD			0x0004	/* Global Register Data */
#define SILI_BAR2_PRO			0x0008	/* Port Register Offset */
#define SILI_BAR2_PRD			0x000C	/* Port Register Data */

/*
 * SILI mapped structures
 */
struct sili_sge {
	u_int64_t		sge_paddr;
	u_int32_t		sge_count;
	u_int32_t		sge_flags;
} __packed;

#define SILI_SGE_FLAGS_TRM	0x80000000	/* last SGE associated w/cmd */
#define SILI_SGE_FLAGS_LNK	0x40000000	/* link to SGE array	*/
#define SILI_SGE_FLAGS_DRD	0x20000000	/* discard (ign sge_paddr) */
#define SILI_SGE_FLAGS_XCF	0x10000000	/* external cmd fetch	*/

/*
 * Each sge is 16 bytes.
 *
 * We want our prb structure to be power-of-2 aligned (it is required to be
 * at least 8-byte aligned).  the prb base header is 4 SGE's but includes 2
 * SGE's within it.
 * The prb structure also can't cross a 64KB boundary, and thus can only
 * have a maximum size of 65536 / 16 / 32  == ~128 entries (128 - 4)
 */
#define SILI_MAX_SGET		(128 - 4)
#define SILI_MAX_PMPORTS	16
#define SILI_MAXPHYS		(256 * 1024)	/* 256 KB */

#if SILI_MAXPHYS / PAGE_SIZE + 1 > (SILI_MAX_SGET * 3 / 4)
#error "SILI_MAX_SGET is not big enough"
#endif


/*
 * The PRB
 *
 * NOTE: ATAPI PACKETS.  The packet is stored in prb_sge[0] and sge[1]
 *			 is the first SGE.
 *
 * NOTE: LRAM PRB.  The PRB layout in the LRAM includes a single struct
 *	 sili_sge[4].  We could use the LRAM for the PRB and host memory
 *	 for an external SGE array, but LRAM in general has some serious
 *	 hardware bugs.
 *
 *	 From linux: Reading the LRAM for a port while a command is
 *	 outstanding can corrupt DMA.  So we use a completely external PRB.
 */
struct sili_prb {
	u_int16_t		prb_control;
	u_int16_t		prb_override;
	u_int32_t		prb_xfer_count;
	union {
		struct ata_fis_h2d	h2d;
		struct ata_fis_d2h	d2h;
	} prb_fis;
	u_int32_t		prb_reserved1c;
	struct sili_sge		prb_sge_base[2];
	struct sili_sge		prb_sge[SILI_MAX_SGET];
} __packed;

#define prb_h2d		prb_fis.h2d
#define prb_d2h		prb_fis.d2h
#define prb_activation	prb_ext[0].sge_paddr
#define prb_packet(prb)	((u_int8_t *)&(prb)->prb_sge[0])
#define prb_sge_normal	prb_sge_base[0]
#define prb_sge_packet	prb_sge_base[1]

/*
 * NOTE: override may be left 0 and the SIL3132 will decode the
 *	 8-bit ATA command and use the correct protocol.
 */
#define SILI_PRB_CTRL_OVERRIDE	0x0001	/* use protocol field override	*/
#define SILI_PRB_CTRL_REXMIT	0x0002	/* ok to rexmit ext command	*/
#define SILI_PRB_CTRL_EXTCMD	0x0004	/* FIS fetched from host memory */
					/* (else from LRAM)		*/
#define SILI_PRB_CTRL_RECEIVE	0x0008	/* Reserve cmd slot to receive	*/
					/* an interlocked FIS		*/
#define SILI_PRB_CTRL_READ	0x0010	/* device to host data		*/
#define SILI_PRB_CTRL_WRITE	0x0020	/* host to device data		*/
#define SILI_PRB_CTRL_NOINT	0x0040	/* do not post int on completion*/
#define SILI_PRB_CTRL_SOFTRESET	0x0080	/* issue soft reset (special)	*/

#define SILI_PRB_OVER_ATAPI	0x0001
#define SILI_PRB_OVER_ATA	0x0002
#define SILI_PRB_OVER_NCQ	0x0004
#define SILI_PRB_OVER_READ	0x0008	/* device to host data */
#define SILI_PRB_OVER_WRITE	0x0010	/* host to device data */
#define SILI_PRB_OVER_RAW	0x0020	/* no protocol special case */

#define SILI_MAX_PORTS		16
#define SILI_MAX_CMDS		31	/* not 32 */

struct sili_dmamem {
	bus_dma_tag_t		adm_tag;
	bus_dmamap_t		adm_map;
	bus_dma_segment_t	adm_seg;
	bus_addr_t		adm_busaddr;
	caddr_t			adm_kva;
};
#define SILI_DMA_MAP(_adm)	((_adm)->adm_map)
#define SILI_DMA_DVA(_adm)	((_adm)->adm_busaddr)
#define SILI_DMA_KVA(_adm)	((void *)(_adm)->adm_kva)

struct sili_softc;
struct sili_port;
struct sili_device;

struct sili_ccb {
	/* ATA xfer associated with this CCB.  Must be 1st struct member. */
	struct ata_xfer		ccb_xa;
	struct callout          ccb_timeout;

	int			ccb_slot;
	struct sili_port	*ccb_port;

	bus_dmamap_t		ccb_dmamap;
	struct sili_prb		*ccb_prb;
	struct sili_prb		*ccb_prb_lram;
	u_int64_t		ccb_prb_paddr;	/* phys addr of prb */

	void			(*ccb_done)(struct sili_ccb *);

	TAILQ_ENTRY(sili_ccb)	ccb_entry;
};

struct sili_port {
	struct sili_softc	*ap_sc;
	bus_space_handle_t	ap_ioh;

	int			ap_num;
	int			ap_pmcount;
	int			ap_flags;
#define AP_F_BUS_REGISTERED	0x0001
#define AP_F_CAM_ATTACHED	0x0002
#define AP_F_IN_RESET		0x0004
#define AP_F_SCAN_RUNNING	0x0008
#define AP_F_SCAN_REQUESTED	0x0010
#define AP_F_SCAN_COMPLETED	0x0020
#define AP_F_IGNORE_IFS		0x0040
#define AP_F_UNUSED0200		0x0200
#define AP_F_ERR_CCB_RESERVED	0x0400
#define AP_F_REINIT_ACTIVE	0x0800
	int			ap_signal;	/* os per-port thread sig */
	thread_t		ap_thread;	/* os per-port thread */
	struct lock		ap_lock;	/* os per-port lock */
	struct lock		ap_sim_lock;	/* cam sim lock */
	struct lock		ap_sig_lock;	/* signal thread */
#define AP_SIGF_INIT		0x0001
#define AP_SIGF_TIMEOUT		0x0002
#define AP_SIGF_PORTINT		0x0004
#define AP_SIGF_STOP		0x8000
	struct cam_sim		*ap_sim;

	struct sili_prb		*ap_prbs;

	struct sili_dmamem	*ap_dmamem_prbs;/* separate sge tables	*/

	u_int32_t		ap_active;	/* active bmask		*/
	u_int32_t		ap_active_cnt;	/* active count		*/
	u_int32_t		ap_expired;	/* deferred expired bmask */
	struct sili_ccb		*ap_ccbs;
	struct sili_ccb		*ap_err_ccb;	/* used to read LOG page  */
	int			ap_run_flags;	/* used to check excl mode */

	TAILQ_HEAD(, sili_ccb)	ap_ccb_free;
	TAILQ_HEAD(, sili_ccb)	ap_ccb_pending;
	struct lock		ap_ccb_lock;

	int			ap_type;	/* ATA_PORT_T_xxx */
	int			ap_probe;	/* ATA_PROBE_xxx */
	struct ata_port		*ap_ata;

	u_int32_t		ap_state;
#define AP_S_NORMAL			0
#define AP_S_FATAL_ERROR		1

	/* For error recovery. */
	u_int8_t		*ap_err_scratch;

	char			ap_name[16];
};

#define PORTNAME(_ap)		((_ap)->ap_name)
#define ATANAME(_ap, _at)	((_at) ? (_at)->at_name : (_ap)->ap_name)

struct sili_softc {
	device_t		sc_dev;
	const struct sili_device *sc_ad;	/* special casing */

	struct resource		*sc_irq;	/* bus resources */
	struct resource		*sc_regs;	/* bus resources */
	struct resource		*sc_pregs;	/* bus resources */
	bus_space_tag_t		sc_iot;		/* split from sc_regs */
	bus_space_handle_t	sc_ioh;		/* split from sc_regs */
	bus_space_tag_t		sc_piot;	/* split from sc_pregs */
	bus_space_handle_t	sc_pioh;	/* split from sc_pregs */

	int			sc_irq_type;
	int			sc_rid_irq;	/* saved bus RIDs */
	int			sc_rid_regs;
	int			sc_rid_pregs;

	void			*sc_irq_handle;	/* installed irq vector */

	bus_dma_tag_t		sc_tag_prbs;
	bus_dma_tag_t		sc_tag_data;

	int			sc_flags;
#define SILI_F_NO_NCQ			0x0001
#define SILI_F_IGN_FR			0x0002
#define SILI_F_INT_GOOD			0x0004
#define SILI_F_64BIT			0x0008
#define SILI_F_300			0x0010
#define SILI_F_NCQ			0x0020
#define SILI_F_SSNTF			0x0040
#define SILI_F_SPM			0x0080

	u_int			sc_ncmds;	/* max 31, NOT 32 */

	struct sili_port	*sc_ports[SILI_MAX_PORTS];
};
#define DEVNAME(_s)		((_s)->sc_dev.dv_xname)

struct sili_device {
	pci_vendor_id_t		ad_vendor;
	pci_product_id_t	ad_product;
	int			ad_nports;
	int			(*ad_attach)(device_t dev);
	int			(*ad_detach)(device_t dev);
	char			*name;
};

/* Wait for all bits in _b to be cleared */
#define sili_pwait_clr(_ap, _r, _b) \
	sili_pwait_eq((_ap), SILI_PWAIT_TIMEOUT, (_r), (_b), 0)
#define sili_pwait_clr_to(_ap, _to,  _r, _b) \
	sili_pwait_eq((_ap), _to, (_r), (_b), 0)

/* Wait for all bits in _b to be set */
#define sili_pwait_set(_ap, _r, _b) \
	sili_pwait_eq((_ap), SILI_PWAIT_TIMEOUT, (_r), (_b), (_b))
#define sili_pwait_set_to(_ap, _to, _r, _b) \
	sili_pwait_eq((_ap), _to, (_r), (_b), (_b))

/*
 * Misc defines
 */
#define SILI_PWAIT_TIMEOUT      1000

/*
 * Prototypes
 */
const struct sili_device *sili_lookup_device(device_t dev);
int	sili_init(struct sili_softc *);
int	sili_port_init(struct sili_port *ap);
int	sili_port_alloc(struct sili_softc *, u_int);
void	sili_port_state_machine(struct sili_port *ap, int initial);
void	sili_port_free(struct sili_softc *, u_int);
int	sili_port_reset(struct sili_port *, struct ata_port *at, int);
void	sili_exclusive_access(struct sili_port *ap);

u_int32_t sili_read(struct sili_softc *, bus_size_t);
void	sili_write(struct sili_softc *, bus_size_t, u_int32_t);
int	sili_wait_ne(struct sili_softc *, bus_size_t, u_int32_t, u_int32_t);
u_int32_t sili_pread(struct sili_port *, bus_size_t);
void	sili_pwrite(struct sili_port *, bus_size_t, u_int32_t);
int	sili_pwait_eq(struct sili_port *, int, bus_size_t,
			u_int32_t, u_int32_t);
void	sili_intr(void *);
void	sili_port_intr(struct sili_port *ap, int blockable);

int	sili_cam_attach(struct sili_port *ap);
void	sili_cam_changed(struct sili_port *ap, struct ata_port *at, int found);
void	sili_cam_detach(struct sili_port *ap);
int	sili_cam_probe(struct sili_port *ap, struct ata_port *at);

struct ata_xfer *sili_ata_get_xfer(struct sili_port *ap, struct ata_port *at);
void	sili_ata_put_xfer(struct ata_xfer *xa);
int	sili_ata_cmd(struct ata_xfer *xa);

int	sili_pm_port_probe(struct sili_port *ap, int);
int	sili_pm_port_init(struct sili_port *ap, struct ata_port *at);
int	sili_pm_identify(struct sili_port *ap);
int	sili_pm_set_feature(struct sili_port *ap, int feature, int enable);
int	sili_pm_hardreset(struct sili_port *ap, int target, int hard);
int	sili_pm_softreset(struct sili_port *ap, int target);
int	sili_pm_phy_status(struct sili_port *ap, int target, u_int32_t *datap);
int	sili_pm_read(struct sili_port *ap, int target,
			int which, u_int32_t *res);
int	sili_pm_write(struct sili_port *ap, int target,
			int which, u_int32_t data);
void	sili_pm_check_good(struct sili_port *ap, int target);
void	sili_ata_cmd_timeout(struct sili_ccb *ccb);
void	sili_quick_timeout(struct sili_ccb *ccb);
struct sili_ccb *sili_get_ccb(struct sili_port *ap);
void	sili_put_ccb(struct sili_ccb *ccb);
struct sili_ccb *sili_get_err_ccb(struct sili_port *);
void	sili_put_err_ccb(struct sili_ccb *);
int	sili_poll(struct sili_ccb *ccb, int timeout,
			void (*timeout_fn)(struct sili_ccb *));

int     sili_port_signature(struct sili_port *ap, struct ata_port *at,
			u_int32_t sig);
void	sili_port_thread_core(struct sili_port *ap, int mask);

void	sili_os_sleep(int ms);
void	sili_os_hardsleep(int us);
int	sili_os_softsleep(void);
void	sili_os_start_port(struct sili_port *ap);
void	sili_os_stop_port(struct sili_port *ap);
void	sili_os_signal_port_thread(struct sili_port *ap, int mask);
void	sili_os_lock_port(struct sili_port *ap);
int	sili_os_lock_port_nb(struct sili_port *ap);
void	sili_os_unlock_port(struct sili_port *ap);

extern u_int32_t SiliForceGen1;
extern u_int32_t SiliNoFeatures;
