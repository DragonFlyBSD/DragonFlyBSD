/*
 * Copyright (c) 1996, by Peter Wemm and Steve Passe, All rights reserved.
 * Copyright (c) 2003 by Matthew Dillon, All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. The name of the developer may NOT be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD: src/sys/i386/include/apic.h,v 1.14.2.2 2003/03/21 21:46:15 jhb Exp $
 * $DragonFly: src/sys/platform/pc32/include/Attic/apicreg.h,v 1.2 2004/06/28 02:57:11 drhodus Exp $
 */

#ifndef _MACHINE_APICREG_H_
#define _MACHINE_APICREG_H_

/*
 * Local && I/O APIC definitions for Pentium P54C+ Built-in APIC.
 *
 * A per-cpu APIC resides in memory location 0xFEE00000.
 *
 *		  31 ... 24   23 ... 16   15 ... 8     7 ... 0
 *		+-----------+-----------+-----------+-----------+
 * 0000 	|           |           |           |           |
 * 0010 	|           |           |           |           |
 *		+-----------+-----------+-----------+-----------+
 *
 *		+-----------+-----------+-----------+-----------+
 * 0020 ID	|     | ID  |           |           |           | RW
 *		+-----------+-----------+-----------+-----------+
 *
 *		    The physical APIC ID is used with physical interrupt
 *		    delivery modes.
 *
 *		+-----------+-----------+-----------+-----------+
 * 0030 VER	|           |           |           |           |
 *		+-----------+-----------+-----------+-----------+
 * 0040 	|           |           |           |           |
 * 0050 	|           |           |           |           |
 * 0060 	|           |           |           |           |
 * 0070 	|           |           |           |           |
 *		+-----------+-----------+-----------+-----------+
 * 0080 TPR	|           |           |           | PRIO SUBC |
 * 0090 APR	|           |           |           |           |
 * 00A0 PPR	|           |           |           |           |
 *		+-----------+-----------+-----------+-----------+
 *
 *		    The Task Priority Register provides a priority threshold
 *		    mechanism for interrupting the processor.  Only interrupts
 *		    with a higher priority then that specified in the TPR will
 *		    be served.   Other interrupts are recorded and serviced
 *		    as soon as the TPR value decreases enough to allow that
 *		    (unless EOId by another APIC).
 *
 *		    PRIO (7:4).  Main priority.  If 15 the APIC will not
 *		    		 accept any interrupts.
 *		    SUBC (3:0)	 Sub priority.  See APR/PPR.
 *
 *
 *		    The Processor Priority Register determines whether a
 *		    pending interrupt can be dispensed to the processor.  ISRV
 *		    Is the vector of the highest priority ISR bit set or
 *		    zero if no ISR bit is set.
 *
 *		    IF TPR[7:4] >= ISRV[7:4]
 *			PPR[7:0] = TPR[7:0]
 *		    ELSE
 *			PPR[7:0] = ISRV[7:4].000
 *			
 *		    The Arbitration Priority Register holds the current
 *		    lowest priority of the procsesor, a value used during
 *		    lowest-priority arbitration.
 *
 *		    IF (TPR[7:4] >= IRRV[7:4] AND TPR[7:4] > ISRV[7:4])
 *			APR[7:0] = TPR[7:0]
 *		    ELSE
 *			APR[7:4] = max((TPR[7:4]&ISRV[7:4]),IRRV[7:4]).000
 *		    
 *		+-----------+-----------+-----------+-----------+
 * 00B0 EOI	|           |           |           |           |
 *		+-----------+-----------+-----------+-----------+
 * 00C0 	|           |           |           |           |
 *		+-----------+-----------+-----------+-----------+
 * 00D0 LDR	|LOG APICID |           |           |           |
 *		+-----------+-----------+-----------+-----------+
 * 00E0 DFR	|MODEL|     |           |           |           |
 *		+-----------+-----------+-----------+-----------+
 *
 *		    The logical APIC ID is used with logical interrupt
 *		    delivery modes.  Interpretation of logical destination
 *		    information depends on the MODEL bits in the Destination
 *		    Format Regiuster.  
 *
 *		    MODEL=1111 FLAT MODEL - The MDA is interpreted as
 *					    a decoded address.  By setting
 *					    one bit in the LDR for each
 *					    local apic 8 APICs can coexist.
 *
 *		    MODEL=0000 CLUSTER MODEL - 
 *
 *		  31 ... 24   23 ... 16   15 ... 8     7 ... 0
 *		+-----------+-----------+-----------+-----------+
 * 00F0 SVR	|           |           |           |           |
 *		+-----------+-----------+-----------+-----------+
 * 0100-0170 ISR|           |           |           |           |
 * 0180-01F0 TMR|           |           |           |           |
 * 0200-0270 IRR|           |           |           |           |
 *		+-----------+-----------+-----------+-----------+
 *
 *		    These registers represent 256 bits, one bit for each
 *		    possible interrupt.  Interrupts 0-15 are reserved so
 *		    bits 0-15 are also reserved.
 *
 *		    TMR - Trigger mode register.  Upon acceptance of an int
 *			  the corresponding bit is cleared for edge-trig and
 *			  set for level-trig.  If the TMR bit is set (level),
 *			  the local APIC sends an EOI to all I/O APICs as
 *			  a result of software issuing an EOI command.
 *			  
 *		    IRR - Interrupt Request Register.  Contains active
 *			  interrupt requests that have been accepted but not
 *			  yet dispensed by the current local APIC.  The bit is
 *			  cleared and the corresponding ISR bit is set when
 *			  the INTA cycle is issued.
 *
 *		    ISR - Interrupt In-Service register.  Interrupt has been
 *			  delivered but not yet fully serviced.  Cleared when
 *			  an EOI is issued from the processor.  An EOI will
 *			  also send an EOI to all I/O APICs if TMR was set.
 *
 *		+-----------+-----------+-----------+-----------+
 * 0280 ESR	|           |           |           |           |
 * 0290-02F0    |           |           |           |           |
 *		+--FEDCBA98-+--76543210-+--FEDCBA98-+-----------+
 * 0300	ICR_LO	|           |      XX   |  TL SDMMM | vector    |
 * 0310	ICR_HI	| DEST FIELD|           |           |           |
 *		+-----------+-----------+-----------+-----------+
 *
 *		    The interrupt command register.  Generally speaking
 *		    writing to ICR_LO initiates a command.  All fields
 *		    are R/W except the 'S' (delivery status) field, which
 *		    is read-only.  When
 *	
 *
 *			XX:	Destination Shorthand field:
 *
 *				00	Use Destination field
 *				01	Self only.  Dest field ignored.
 *				10	All including self (uses a 
 *					destination field of 0x0F)
 *				11	All excluding self (uses a
 *					destination field of 0x0F)
 *
 *			T:	1 = Level 0 = Edge Trigger modde, used for
 *				the INIT level de-assert delivery mode only
 *				to de-assert a request.
 *
 *			L:	0 = De-Assert, 1 = Assert.  Always write as
 *				1 when initiating a new command.  Can only
 *				write as 0 for INIT mode de-assertion of
 *				command.
 *
 *			S:	1 = Send Pending.  Interrupt has been injected
 *				but APIC has not yet accepted it.
 *
 *			D:	0=physical 1=logical.  In physical mode
 *				only 24-27 of DEST FIELD is used from ICR_HI.
 *
 *			MMM:	000 Fixed. Deliver to all processors according
 *				    to the ICR.  Always treated as edge trig.
 *
 *				001 Lowest Priority.  Deliver to just the
 *				    processor running at the lowest priority.
 *
 *				010 SMI.  The vector must be 00B.  Only edge
 *				    triggered is allowed.  The vector field
 *				    must be programmed to zero (huh?).
 *
 *				011 <reserved>
 *
 *				100 NMI.  Deliver as an NMI to all processors
 *				    listed in the destination field.  The
 *				    vector is ignored.  Alawys treated as 
 *				    edge triggered.
 *
 *				101 INIT.  Deliver as an INIT signal to all
 *				    processors (like FIXED).  Vector is ignored
 *				    and it is always edge-triggered.
 *
 *				110 Start Up.  Sends a special message between
 *				    cpus.  the vector contains a start-up
 *				    address for MP boot protocol.
 *				    Always edge triggered.  Note: a startup
 *				    int is not automatically tried in case of
 *				    failure.
 *
 *				111 <reserved>
 *
 *		+-----------+--------10-+--FEDCBA98-+-----------+
 * 0320	LTIMER  |           |        TM |  ---S---- | vector    |
 * 0330		|           |           |           |           |
 *		+-----------+--------10-+--FEDCBA98-+-----------+
 * 0340	LVPCINT	|           |        -M |  ---S-MMM | vector    |
 * 0350	LVINT0	|           |        -M |  LRPS-MMM | vector    |
 * 0360 LVINT1	|           |        -M |  LRPS-MMM | vector    |
 * 0370	LVERROR	|           |        -M |  -------- | vector    |
 *		+-----------+-----------+-----------+-----------+
 *
 *			T:	1 = periodic, 0 = one-shot
 *
 *			M:	1 = masked
 *
 *			L:	1 = level, 0 = edge
 *
 *			R:	For level triggered only, set to 1 when a
 *				level int is accepted, cleared by EOI.
 *
 *			P:	Pin Polarity 0 = Active High, 1 = Active Low
 *
 *			S:	1 = Send Pending.  Interrupt has been injected
 *				but APIC has not yet accepted it.
 *
 *			MMM 	000 = Fixed	deliver to cpu according to LVT
 *
 *			MMM 	100 = NMI	deliver as an NMI.  Always edge
 *
 *			MMM 	111 = ExtInt	deliver from 8259, routes INTA
 *						bus cycle to external
 *						controller.  Controller is 
 *						expected to supply vector.
 *						Always level.
 *
 *		+-----------+-----------+-----------+-----------+
 * 0380	TMR_ICR	|           |           |           |           |
 * 0390	TMR_CCR	|           |           |           |           |
 * 03A0		|           |           |           |           |
 * 03B0		|           |           |           |           |
 * 03C0		|           |           |           |           |
 * 03D0		|           |           |           |           |
 * 03E0 TMR_DCR	|           |           |           |           |
 *		+-----------+-----------+-----------+-----------+
 *
 *		    Timer control and access registers.
 *
 *
 *	NOTE ON EOI: Upon receiving an EOI the APIC clears the highest priority
 *	interrupt in the ISR and selects the next highest priority interrupt
 *	for posting to the CPU.  If the interrupt being EOId was level
 *	triggered the APIC will send an EOI to all I/O APICs.  For the moment
 *	you can write garbage to the EOI register but for future compatibility
 *	0 should be written.
 */

#ifndef LOCORE
#include <sys/types.h>

#define PAD3	int : 32; int : 32; int : 32
#define PAD4	int : 32; int : 32; int : 32; int : 32

struct LAPIC {
	/* reserved */		PAD4;
	/* reserved */		PAD4;
	u_int32_t id;		PAD3;	/* 0020	R/W */
	u_int32_t version;	PAD3;	/* 0030	RO */
	/* reserved */		PAD4;
	/* reserved */		PAD4;
	/* reserved */		PAD4;
	/* reserved */		PAD4;
	u_int32_t tpr;		PAD3;
	u_int32_t apr;		PAD3;
	u_int32_t ppr;		PAD3;
	u_int32_t eoi;		PAD3;
	/* reserved */		PAD4;
	u_int32_t ldr;		PAD3;
	u_int32_t dfr;		PAD3;
	u_int32_t svr;		PAD3;
	u_int32_t isr0;		PAD3;
	u_int32_t isr1;		PAD3;
	u_int32_t isr2;		PAD3;
	u_int32_t isr3;		PAD3;
	u_int32_t isr4;		PAD3;
	u_int32_t isr5;		PAD3;
	u_int32_t isr6;		PAD3;
	u_int32_t isr7;		PAD3;
	u_int32_t tmr0;		PAD3;
	u_int32_t tmr1;		PAD3;
	u_int32_t tmr2;		PAD3;
	u_int32_t tmr3;		PAD3;
	u_int32_t tmr4;		PAD3;
	u_int32_t tmr5;		PAD3;
	u_int32_t tmr6;		PAD3;
	u_int32_t tmr7;		PAD3;
	u_int32_t irr0;		PAD3;
	u_int32_t irr1;		PAD3;
	u_int32_t irr2;		PAD3;
	u_int32_t irr3;		PAD3;
	u_int32_t irr4;		PAD3;
	u_int32_t irr5;		PAD3;
	u_int32_t irr6;		PAD3;
	u_int32_t irr7;		PAD3;
	u_int32_t esr;		PAD3;
	/* reserved */		PAD4;
	/* reserved */		PAD4;
	/* reserved */		PAD4;
	/* reserved */		PAD4;
	/* reserved */		PAD4;
	/* reserved */		PAD4;
	/* reserved */		PAD4;
	u_int32_t icr_lo;	PAD3;
	u_int32_t icr_hi;	PAD3;
	u_int32_t lvt_timer;	PAD3;
	/* reserved */		PAD4;
	u_int32_t lvt_pcint;	PAD3;
	u_int32_t lvt_lint0;	PAD3;
	u_int32_t lvt_lint1;	PAD3;
	u_int32_t lvt_error;	PAD3;
	u_int32_t icr_timer;	PAD3;
	u_int32_t ccr_timer;	PAD3;
	/* reserved */		PAD4;
	/* reserved */		PAD4;
	/* reserved */		PAD4;
	/* reserved */		PAD4;
	u_int32_t dcr_timer;	PAD3;
	/* reserved */		PAD4;
};

typedef struct LAPIC lapic_t;

/******************************************************************************
 * I/O APIC structure
 */

struct IOAPIC {
	u_int32_t ioregsel;	PAD3;
	u_int32_t iowin;	PAD3;
};

typedef struct IOAPIC ioapic_t;

#undef PAD4
#undef PAD3

#endif  /* !LOCORE */


/******************************************************************************
 * various code 'logical' values
 */

#ifdef GRAB_LOPRIO
#define LOPRIO_LEVEL		0x00000010	/* TPR of CPU accepting INTs */
#define ALLHWI_LEVEL		0x00000000	/* TPR of CPU grabbing INTs */
#endif /** GRAB_LOPRIO */

/*
 * XXX This code assummes that the reserved field of the
 *      local APIC TPR can be written with all 0s.
 *     This saves quite a few memory accesses.
 *     If the silicon ever changes then things will break!
 *     It affects mplock.s, swtch.s, and possibly other files.
 */
#define CHEAP_TPR


/******************************************************************************
 * LOCAL APIC defines
 */

/* default physical locations of LOCAL (CPU) APICs */
#define DEFAULT_APIC_BASE	0xfee00000

/* fields in VER */
#define APIC_VER_VERSION	0x000000ff
#define APIC_VER_MAXLVT		0x00ff0000
#define MAXLVTSHIFT		16

/* fields in SVR */
#define APIC_SVR_VECTOR		0x000000ff
#define APIC_SVR_VEC_PROG	0x000000f0
#define APIC_SVR_VEC_FIX	0x0000000f
#define APIC_SVR_ENABLE		0x00000100
# define APIC_SVR_SWDIS		0x00000000
# define APIC_SVR_SWEN		0x00000100
#define APIC_SVR_FOCUS		0x00000200
# define APIC_SVR_FEN		0x00000000
# define APIC_SVR_FDIS		0x00000200

/* fields in TPR */
#define APIC_TPR_PRIO		0x000000ff
# define APIC_TPR_INT		0x000000f0
# define APIC_TPR_SUB		0x0000000f


/* fields in ICR_LOW */
#define APIC_VECTOR_MASK	0x000000ff

#define APIC_DELMODE_MASK	0x00000700
# define APIC_DELMODE_FIXED	0x00000000
# define APIC_DELMODE_LOWPRIO	0x00000100
# define APIC_DELMODE_SMI	0x00000200
# define APIC_DELMODE_RR	0x00000300
# define APIC_DELMODE_NMI	0x00000400
# define APIC_DELMODE_INIT	0x00000500
# define APIC_DELMODE_STARTUP	0x00000600
# define APIC_DELMODE_RESV	0x00000700

#define APIC_DESTMODE_MASK	0x00000800
# define APIC_DESTMODE_PHY	0x00000000
# define APIC_DESTMODE_LOG	0x00000800

#define APIC_DELSTAT_MASK	0x00001000
# define APIC_DELSTAT_IDLE	0x00000000
# define APIC_DELSTAT_PEND	0x00001000

#define APIC_RESV1_MASK		0x00002000

#define APIC_LEVEL_MASK		0x00004000
# define APIC_LEVEL_DEASSERT	0x00000000
# define APIC_LEVEL_ASSERT	0x00004000

#define APIC_TRIGMOD_MASK	0x00008000
# define APIC_TRIGMOD_EDGE	0x00000000
# define APIC_TRIGMOD_LEVEL	0x00008000

#define APIC_RRSTAT_MASK	0x00030000
# define APIC_RRSTAT_INVALID	0x00000000
# define APIC_RRSTAT_INPROG	0x00010000
# define APIC_RRSTAT_VALID	0x00020000
# define APIC_RRSTAT_RESV	0x00030000

#define APIC_DEST_MASK		0x000c0000
# define APIC_DEST_DESTFLD	0x00000000
# define APIC_DEST_SELF		0x00040000
# define APIC_DEST_ALLISELF	0x00080000
# define APIC_DEST_ALLESELF	0x000c0000

#define APIC_RESV2_MASK		0xfff00000


/* fields in ICR_HIGH */
#define APIC_ID_MASK		0xff000000


/* fields in LVT1/2 */
#define APIC_LVT_VECTOR		0x000000ff
#define APIC_LVT_DM		0x00000700
# define APIC_LVT_DM_FIXED	0x00000000
# define APIC_LVT_DM_NMI	0x00000400
# define APIC_LVT_DM_EXTINT	0x00000700
#define APIC_LVT_DS		0x00001000
#define APIC_LVT_IIPP		0x00002000
#define APIC_LVT_IIPP_INTALO	0x00002000
#define APIC_LVT_IIPP_INTAHI	0x00000000
#define APIC_LVT_RIRR		0x00004000
#define APIC_LVT_TM		0x00008000
#define APIC_LVT_M		0x00010000


/* fields in LVT Timer */
#define APIC_LVTT_VECTOR	0x000000ff
#define APIC_LVTT_DS		0x00001000
#define APIC_LVTT_M		0x00010000
#define APIC_LVTT_TM		0x00020000


/* fields in TDCR */
#define APIC_TDCR_2		0x00
#define APIC_TDCR_4		0x01
#define APIC_TDCR_8		0x02
#define APIC_TDCR_16		0x03
#define APIC_TDCR_32		0x08
#define APIC_TDCR_64		0x09
#define APIC_TDCR_128		0x0a
#define APIC_TDCR_1		0x0b


/*
 * fields in IRR
 * ISA INTerrupts are in bits 16-31 of the 1st IRR register.
 * these masks DON'T EQUAL the isa IRQs of the same name.
 */
#define APIC_IRQ0		0x00000001
#define APIC_IRQ1		0x00000002
#define APIC_IRQ2		0x00000004
#define APIC_IRQ3		0x00000008
#define APIC_IRQ4		0x00000010
#define APIC_IRQ5		0x00000020
#define APIC_IRQ6		0x00000040
#define APIC_IRQ7		0x00000080
#define APIC_IRQ8		0x00000100
#define APIC_IRQ9		0x00000200
#define APIC_IRQ10		0x00000400
#define APIC_IRQ11		0x00000800
#define APIC_IRQ12		0x00001000
#define APIC_IRQ13		0x00002000
#define APIC_IRQ14		0x00004000
#define APIC_IRQ15		0x00008000
#define APIC_IRQ16		0x00010000
#define APIC_IRQ17		0x00020000
#define APIC_IRQ18		0x00040000
#define APIC_IRQ19		0x00080000
#define APIC_IRQ20		0x00100000
#define APIC_IRQ21		0x00200000
#define APIC_IRQ22		0x00400000
#define APIC_IRQ23		0x00800000


/******************************************************************************
 * I/O APIC defines
 */

/* default physical locations of an IO APIC */
#define DEFAULT_IO_APIC_BASE	0xfec00000

/* window register offset */
#define IOAPIC_WINDOW		0x10

/* indexes into IO APIC */
#define IOAPIC_ID		0x00
#define IOAPIC_VER		0x01
#define IOAPIC_ARB		0x02
#define IOAPIC_REDTBL		0x10
#define IOAPIC_REDTBL0		IOAPIC_REDTBL
#define IOAPIC_REDTBL1		(IOAPIC_REDTBL+0x02)
#define IOAPIC_REDTBL2		(IOAPIC_REDTBL+0x04)
#define IOAPIC_REDTBL3		(IOAPIC_REDTBL+0x06)
#define IOAPIC_REDTBL4		(IOAPIC_REDTBL+0x08)
#define IOAPIC_REDTBL5		(IOAPIC_REDTBL+0x0a)
#define IOAPIC_REDTBL6		(IOAPIC_REDTBL+0x0c)
#define IOAPIC_REDTBL7		(IOAPIC_REDTBL+0x0e)
#define IOAPIC_REDTBL8		(IOAPIC_REDTBL+0x10)
#define IOAPIC_REDTBL9		(IOAPIC_REDTBL+0x12)
#define IOAPIC_REDTBL10		(IOAPIC_REDTBL+0x14)
#define IOAPIC_REDTBL11		(IOAPIC_REDTBL+0x16)
#define IOAPIC_REDTBL12		(IOAPIC_REDTBL+0x18)
#define IOAPIC_REDTBL13		(IOAPIC_REDTBL+0x1a)
#define IOAPIC_REDTBL14		(IOAPIC_REDTBL+0x1c)
#define IOAPIC_REDTBL15		(IOAPIC_REDTBL+0x1e)
#define IOAPIC_REDTBL16		(IOAPIC_REDTBL+0x20)
#define IOAPIC_REDTBL17		(IOAPIC_REDTBL+0x22)
#define IOAPIC_REDTBL18		(IOAPIC_REDTBL+0x24)
#define IOAPIC_REDTBL19		(IOAPIC_REDTBL+0x26)
#define IOAPIC_REDTBL20		(IOAPIC_REDTBL+0x28)
#define IOAPIC_REDTBL21		(IOAPIC_REDTBL+0x2a)
#define IOAPIC_REDTBL22		(IOAPIC_REDTBL+0x2c)
#define IOAPIC_REDTBL23		(IOAPIC_REDTBL+0x2e)

/* fields in VER */
#define IOART_VER_VERSION	0x000000ff
#define IOART_VER_MAXREDIR	0x00ff0000
#define MAXREDIRSHIFT		16

/*
 * fields in the IO APIC's redirection table entries
 */
#define IOART_DEST	APIC_ID_MASK	/* broadcast addr: all APICs */

#define IOART_RESV	0x00fe0000	/* reserved */

#define IOART_INTMASK	0x00010000	/* R/W: INTerrupt mask */
# define IOART_INTMCLR	0x00000000	/*       clear, allow INTs */
# define IOART_INTMSET	0x00010000	/*       set, inhibit INTs */

#define IOART_TRGRMOD	0x00008000	/* R/W: trigger mode */
# define IOART_TRGREDG	0x00000000	/*       edge */
# define IOART_TRGRLVL	0x00008000	/*       level */

#define IOART_REM_IRR	0x00004000	/* RO: remote IRR */

#define IOART_INTPOL	0x00002000	/* R/W: INT input pin polarity */
# define IOART_INTAHI	0x00000000	/*      active high */
# define IOART_INTALO	0x00002000	/*      active low */

#define IOART_DELIVS	0x00001000	/* RO: delivery status */

#define IOART_DESTMOD	0x00000800	/* R/W: destination mode */
# define IOART_DESTPHY	0x00000000	/*      physical */
# define IOART_DESTLOG	0x00000800	/*      logical */

#define IOART_DELMOD	0x00000700	/* R/W: delivery mode */
# define IOART_DELFIXED	0x00000000	/*       fixed */
# define IOART_DELLOPRI	0x00000100	/*       lowest priority */
# define IOART_DELSMI	0x00000200	/*       System Management INT */
# define IOART_DELRSV1	0x00000300	/*       reserved */
# define IOART_DELNMI	0x00000400	/*       NMI signal */
# define IOART_DELINIT	0x00000500	/*       INIT signal */
# define IOART_DELRSV2	0x00000600	/*       reserved */
# define IOART_DELEXINT	0x00000700	/*       External INTerrupt */

#define IOART_INTVEC	0x000000ff	/* R/W: INTerrupt vector field */

#endif /* _MACHINE_APIC_H_ */
