/*
 * Copyright (c) 2003,2004,2008 The DragonFly Project.  All rights reserved.
 * 
 * This code is derived from software contributed to The DragonFly Project
 * by Matthew Dillon <dillon@backplane.com>
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
 * Copyright (c) 1996, by Peter Wemm and Steve Passe, All rights reserved.
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
 * $DragonFly: src/sys/platform/pc64/apic/apicreg.h,v 1.1 2008/08/29 17:07:12 dillon Exp $
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
 * 00F0 SVR	|           |           |       FE  |  vvvvvvvv |
 *		+-----------+-----------+-----------+-----------+
 *
 *		    Spurious interrupt vector register.  The 4 low
 *		    vector bits must be programmed to 1111, e.g.
 *		    vvvv1111.
 *
 *		    E - LAPIC enable (0 = disable, 1 = enable)
 *
 *		    F - Focus processor disable (1 = disable, 0 = enable)
 *
 *		    NOTE: The handler for the spurious interrupt vector
 *		    should *NOT* issue an EOI because the spurious 
 *		    interrupt does not effect the ISR.
 *
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
 *		+-----------+-----------+-----------+-----------+
 *
 *		The timer initial count register and current count
 *		register (32 bits)
 *
 *		+-----------+-----------+-----------+-----------+
 * 03A0		|           |           |           |           |
 * 03B0		|           |           |           |           |
 * 03C0		|           |           |           |           |
 * 03D0		|           |           |           |           |
 *		+-----------+-----------+-----------+-----------+
 * 03E0 TMR_DCR	|           |           |           |      d-dd |
 *		+-----------+-----------+-----------+-----------+
 *
 *		The timer divide configuration register.  d-dd is:
 *
 *		0000 - divide by 2
 *		0001 - divide by 4
 *		0010 - divide by 8
 *		0011 - divide by 16
 *		1000 - divide by 32
 *		1001 - divide by 64
 *		1010 - divide by 128
 *		1011 - divide by 1
 *
 *	NOTE ON EOI: Upon receiving an EOI the APIC clears the highest priority
 *	interrupt in the ISR and selects the next highest priority interrupt
 *	for posting to the CPU.  If the interrupt being EOId was level
 *	triggered the APIC will send an EOI to all I/O APICs.  For the moment
 *	you can write garbage to the EOI register but for future compatibility
 *	0 should be written.
 *
 * 03F0 SELF_IPI
 * 0400 EXT_FEAT
 * 0410 EXT_CTRL
 * 0420 EXT_SEOI
 * 0430
 * 0440
 * 0450
 * 0460
 * 0470
 * 0480 EXT_IER0
 * 0490 EXT_IER1
 * 04A0 EXT_IER2
 * 04B0 EXT_IER3
 * 04C0 EXT_IER4
 * 04D0 EXT_IER5
 * 04E0 EXT_IER6
 * 04F0 EXT_IER7
 * 0500 EXT_LVT0
 * 0510 EXT_LVT1
 * 0520 EXT_LVT2
 * 0530 EXT_LVT3
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
	u_int32_t ccr_timer;	PAD3;	/* e9 */
	/* reserved */		PAD4;
	/* reserved */		PAD4;
	/* reserved */		PAD4;
	/* reserved */		PAD4;
	u_int32_t dcr_timer;	PAD3;	/* 3e */
	u_int32_t self_ipi;	PAD3;	/* 3f - Only in x2APIC */
	u_int32_t ext_feat;	PAD3;
	u_int32_t ext_ctrl;	PAD3;
	u_int32_t ext_seoi;	PAD3;
	/* reserved */		PAD4;
	/* reserved */		PAD4;
	/* reserved */		PAD4;
	/* reserved */		PAD4;
	/* reserved */		PAD4;
	u_int32_t ext_ier0;	PAD3;
	u_int32_t ext_ier1;	PAD3;
	u_int32_t ext_ier2;	PAD3;
	u_int32_t ext_ier3;	PAD3;
	u_int32_t ext_ier4;	PAD3;
	u_int32_t ext_ier5;	PAD3;
	u_int32_t ext_ier6;	PAD3;
	u_int32_t ext_ier7;	PAD3;
	struct {			/* 50 */
		u_int32_t lvt;	PAD3;
	} ext_lvt[16];
} __packed;

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

/*
 * TPR loads to prioritize which cpu grabs an interrupt
 *
 * (note: some fields of the TPR are reserved)
 */
#define LOPRIO_LEVEL		0x00000010	/* TPR of CPU accepting INTs */
#define ALLHWI_LEVEL		0x00000000	/* TPR of CPU grabbing INTs */

/******************************************************************************
 * LOCAL APIC defines
 */

/*
 * default physical location for the LOCAL (CPU) APIC
 */
#define DEFAULT_APIC_BASE	0xfee00000

/*
 * lapic.id (rw)
 */
#define APIC_ID_MASK		0xff000000
#define APIC_ID_SHIFT		24
#define APIC_ID_CLUSTER		0xf0
#define APIC_ID_CLUSTER_ID	0x0f
#define APIC_MAX_CLUSTER	0xe
#define APIC_MAX_INTRACLUSTER_ID 3
#define APIC_ID_CLUSTER_SHIFT   4

#define APIC_ID(id)		(((id) & APIC_ID_MASK) >> APIC_ID_SHIFT)

/*
 * lapic.ver (ro)
 */
#define APIC_VER_VERSION	0x000000ff
#define APIC_VER_MAXLVT		0x00ff0000
#define MAXLVTSHIFT		16
#define APIC_VER_EOI_SUPP	0x01000000
#define APIC_VER_AMD_EXT_SPACE	0x80000000

/*
 * lapic.ldr (rw)
 */
#define APIC_LDR_RESERVED       0x00ffffff

/*
 * lapic.dfr (rw)
 *
 * The logical APIC ID is used with logical interrupt
 * delivery modes.  Interpretation of logical destination
 * information depends on the MODEL bits in the Destination
 * Format Regiuster.
 *
 * MODEL=1111 FLAT MODEL - The MDA is interpreted as
 * 			   a decoded address.  By setting
 * 			   one bit in the LDR for each
 *			   local apic 8 APICs can coexist.
 *  
 * MODEL=0000 CLUSTER MODEL -
 */
#define APIC_DFR_RESERVED	0x0fffffff
#define APIC_DFR_MODEL_MASK	0xf0000000
#define APIC_DFR_MODEL_FLAT	0xf0000000
#define APIC_DFR_MODEL_CLUSTER	0x00000000

/*
 * lapic.svr
 *
 * Contains the spurious interrupt vector and bits to enable/disable
 * the local apic and focus processor.
 */
#define APIC_SVR_VECTOR		0x000000ff
#define APIC_SVR_ENABLE		0x00000100
#define APIC_SVR_FOCUS_DISABLE	0x00000200
#define APIC_SVR_EOI_SUPP	0x00001000

/*
 * lapic.tpr
 *
 *    PRIO (7:4).  Main priority.  If 15 the APIC will not
 *    		 accept any interrupts.
 *    SUBC (3:0)	 Sub priority.  See APR/PPR.
 */
#define APIC_TPR_PRIO		0x000000ff
#define APIC_TPR_INT		0x000000f0
#define APIC_TPR_SUB		0x0000000f

/*
 * lapic.icr_lo	  -------- ----XXRR TL-SDMMM vvvvvvvv
 * 
 *	The interrupt command register.  Generally speaking
 * 	writing to ICR_LO initiates a command.  All fields
 * 	are R/W except the 'S' (delivery status) field, which
 * 	is read-only.  When
 *              
 *      XX:     Destination Shorthand field:
 *
 *		00 -	Use Destination field
 *		01 -	Self only.  Dest field ignored.
 *		10 -	All including self (uses a
 *			destination field of 0x0F)
 *		11 -	All excluding self (uses a
 *			destination field of 0x0F)
 *
 *	RR:	RR mode (? needs documentation)
 *                  
 *      T:      1 = Level 0 = Edge Trigger modde, used for
 *      	the INIT level de-assert delivery mode only
 *      	to de-assert a request.
 * 
 *	L:      0 = De-Assert, 1 = Assert.  Always write as
 *      	1 when initiating a new command.  Can only
 *		write as 0 for INIT mode de-assertion of
 *		command.
 *
 *	S:	1 = Send Pending.  Interrupt has been injected but the APIC
 *		has not yet accepted it.
 * 
 *	D:	0 = physical 1 = logical.  In physical mode only bits 24-27
 *		of the DEST field is used from ICR_HI.
 *
 *	MMM:	Delivery mode
 *
 *		000 - Fixed.  Deliver to all processors according to the
 *		      ICR.  Always treated as edge triggered.
 *
 *		001 - Lowest Priority.  Deliver to just the processor
 *		      running at the lowest priority.
 *
 *		010 - SMI.  The vector must be 00B.  Only edge triggered
 *		      is allowed.  The vector field must be programmed to
 *		      0 (huh?)
 *
 *		011 - RR Delivery mode (?? needs documentation).
 *
 *		100 - NMI.  Deliver as an NMI to all processors listed in
 *		      the destination field.  The vector is ignored.  Always
 *		      treated as edge triggered.
 *
 *		101 - INIT.  Deliver as an INIT signal to all processors
 *		      (like FIXED) according to the ICR.  The vector is
 *		      ignored and delivery is always edge-triggered.
 *
 *		110 - Startup.  Send a special message between cpus.  The
 *		      vector contains a startup address for the MP boot
 *		      protocol.  Always edge triggered.  Note: a startup
 *		      interrupt is not automatically tried in case of failure.
 *
 *		111 - <reserved>
 */
#define APIC_VECTOR_MASK	0x000000ff

#define APIC_DELMODE_MASK	0x00000700
#define APIC_DELMODE_FIXED	0x00000000
#define APIC_DELMODE_LOWPRIO	0x00000100
#define APIC_DELMODE_SMI	0x00000200
#define APIC_DELMODE_RR		0x00000300
#define APIC_DELMODE_NMI	0x00000400
#define APIC_DELMODE_INIT	0x00000500
#define APIC_DELMODE_STARTUP	0x00000600
#define APIC_DELMODE_RESV7	0x00000700

#define APIC_DESTMODE_MASK	0x00000800
#define APIC_DESTMODE_PHY	0x00000000
#define APIC_DESTMODE_LOG	0x00000800

#define APIC_DELSTAT_MASK	0x00001000
#define APIC_DELSTAT_IDLE	0x00000000
#define APIC_DELSTAT_PEND	0x00001000

#define APIC_LEVEL_MASK		0x00004000
#define APIC_LEVEL_DEASSERT	0x00000000
#define APIC_LEVEL_ASSERT	0x00004000

#define APIC_TRIGMOD_MASK	0x00008000
#define APIC_TRIGMOD_EDGE	0x00000000
#define APIC_TRIGMOD_LEVEL	0x00008000

#define APIC_RRSTAT_MASK	0x00030000
#define APIC_RRSTAT_INVALID	0x00000000
#define APIC_RRSTAT_INPROG	0x00010000
#define APIC_RRSTAT_VALID	0x00020000
#define APIC_RRSTAT_RESV	0x00030000

#define APIC_DEST_MASK		0x000c0000
#define APIC_DEST_DESTFLD	0x00000000
#define APIC_DEST_SELF		0x00040000
#define APIC_DEST_ALLISELF	0x00080000
#define APIC_DEST_ALLESELF	0x000c0000

#define APIC_ICRLO_RESV_MASK	0xfff02000

/*
 * lapic.icr_hi
 */
#define APIC_ICRH_ID_MASK	APIC_ID_MASK

/*
 * lapic.lvt_timer
 * lapic.lvt_pcint
 * lapic.lvt_lint0
 * lapic.lvt_lint1
 * lapic.lvt_error
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
 *				(LTIMER only)
 *
 *			M:	1 = masked
 *
 *			L:	1 = level, 0 = edge
 *				(LVINT0/1 only)
 *
 *			R:	For level triggered only, set to 1 when a
 *				level int is accepted, cleared by EOI.
 *				(LVINT0/1 only)
 *
 *			P:	Pin Polarity 0 = Active High, 1 = Active Low
 *				(LVINT0/1 only)
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
 */
#define APIC_LVT_VECTOR		0x000000ff

#define APIC_LVT_DM_MASK	0x00000700
#define APIC_LVT_DM_FIXED	0x00000000
#define APIC_LVT_DM_NMI		0x00000400
#define APIC_LVT_DM_EXTINT	0x00000700

#define APIC_LVT_DS		0x00001000	/* (S) Send Pending */
#define APIC_LVT_POLARITY_MASK	0x00002000
#define APIC_LVT_POLARITY_LO	0x00002000	/* (P) Pin Polarity */
#define APIC_LVT_POLARITY_HI	0x00000000
#define APIC_LVT_LEVELSTATUS	0x00004000	/* (R) level trig status */
#define APIC_LVT_TRIG_MASK	0x00008000
#define APIC_LVT_LEVELTRIG	0x00008000	/* (L) 1 = level, 0 = edge */
#define APIC_LVT_MASKED		0x00010000	/* (M) 1 = masked */

/*
 * lapic.lvt_timer
 */
#define APIC_LVTT_VECTOR	APIC_LVT_VECTOR
#define APIC_LVTT_DS		APIC_LVT_DS
#define APIC_LVTT_MASKED	APIC_LVT_MASKED
#define APIC_LVTT_PERIODIC	0x00020000
#define APIC_LVTT_TSCDLT	0x00040000

#define APIC_TIMER_MAX_COUNT    0xffffffff

/*
 * lapic.icr_timer - initial count register (32 bits)
 * lapic.ccr_timer - current count register (32 bits)
 */

/*
 * lapic.dcr_timer - timer divider register
 *
 * d0dd
 *
 *	0000 - divide by 2
 *	0001 - divide by 4
 *	0010 - divide by 8
 *	0011 - divide by 16
 *	1000 - divide by 32
 *	1001 - divide by 64
 *	1010 - divide by 128
 *	1011 - divide by 1
 */
#define APIC_TDCR_2		0x00
#define APIC_TDCR_4		0x01
#define APIC_TDCR_8		0x02
#define APIC_TDCR_16		0x03
#define APIC_TDCR_32		0x08
#define APIC_TDCR_64		0x09
#define APIC_TDCR_128		0x0a
#define APIC_TDCR_1		0x0b

/*
 * lapic.self_ipi (x2APIC only)
 */
/*
 * lapic.ext_feat (AMD only)
 */
#define APIC_EXTFEAT_MASK	0x00ff0000
#define APIC_EXTFEAT_SHIFT    	16
#define APIC_EXTFEAT_EXTID_CAP	0x00000004
#define APIC_EXTFEAT_SEIO_CAP	0x00000002
#define APIC_EXTFEAT_IER_CAP	0x00000001

/*
 * lapic.ext_ctrl
 * lapic.ext_seoi
 * lapic.ext_ier{0-7}
 */
/*
 * lapic.ext_lvt[N].lvt
 */
#define APIC_EXTLVT_IBS		0	/* Instruction based sampling */
#define APIC_EXTLVT_MCA		1	/* MCE thresholding */
#define APIC_EXTLVT_DEI		2	/* Deferred error interrupt */
#define APIC_EXTLVT_SBI		3	/* Sideband interface */

/******************************************************************************
 * I/O APIC defines
 */

/* default physical locations of an IO APIC */
#define DEFAULT_IO_APIC_BASE	0xfec00000

/* window register offset */
#define IOAPIC_WINDOW		0x10

/* 
 * indexes into IO APIC (index into array of 32 bit entities)
 */
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

/*
 * High 32 bit word.  The high 8 bits contain the destination field.
 *
 * If this entry is set up for Physical Mode, bits 59:56 (the low 4 bits
 * of the 8 bit destination field) contain an APIC ID.
 *
 * If this entry is set up for Logical Mode, the destination field potentially
 * defines a set of processors.  Bits 63:56 (all 8 bits) specify the logical
 * destination address.
 *
 * Current we use IOART_HI_DEST_BROADCAST to broadcast to all LAPICs
 */
#define IOART_HI_DEST_MASK	APIC_ID_MASK
#define IOART_HI_DEST_RESV	~APIC_ID_MASK
#define IOART_HI_DEST_BROADCAST	IOART_HI_DEST_MASK	
#define IOART_HI_DEST_SHIFT	24

/*
 * Low 32 bit word
 */
#define IOART_RESV	0x00fe0000	/* reserved */

/*
 * Interrupt mask bit.  If 1 the interrupt is masked.  An edge sensitive
 * interrupt which is masked will be lost.
 */
#define IOART_INTMASK	0x00010000	/* R/W: INTerrupt mask */
#define IOART_INTMCLR	0x00000000	/*       clear, allow INTs */
#define IOART_INTMSET	0x00010000	/*       set, inhibit INTs */

/*
 * Select trigger mode.
 */
#define IOART_TRGRMOD	0x00008000	/* R/W: trigger mode */
#define IOART_TRGREDG	0x00000000	/*       edge */
#define IOART_TRGRLVL	0x00008000	/*       level */

/*
 * Remote IRR.  Only applies to level triggered interrupts, this bit 
 * is set to 1 when the IOAPIC has delivered a level triggered interrupt
 * to a local APIC.  It is cleared when the LAPIC EOI's the interrupt.
 * This field is read-only.
 */
#define IOART_REM_IRR	0x00004000	/* RO: remote IRR */

/*
 * Select interrupt pin polarity
 */
#define IOART_INTPOL	0x00002000	/* R/W: INT input pin polarity */
#define IOART_INTAHI	0x00000000	/*      active high */
#define IOART_INTALO	0x00002000	/*      active low */

/*
 * Delivery Status (read only).  0 = no interrupt pending, 1 = interrupt
 * pending for tranmission to an LAPIC.  Note that this bit does not 
 * indicate whether the interrupt has been processed or is undergoing 
 * processing by a cpu.
 */
#define IOART_DELIVS	0x00001000	/* RO: delivery status */

/*
 * Destination mode.
 *
 * In physical mode the destination APIC is identified by its ID.
 * Bits 56-63 specify the 8 bit APIC ID.
 *
 * In logical mode destinations are identified by matching on the logical
 * destination under the control of the destination format register and 
 * logical destination register in each local APIC.
 *
 */
#define IOART_DESTMOD	0x00000800	/* R/W: destination mode */
#define IOART_DESTPHY	0x00000000	/*      physical */
#define IOART_DESTLOG	0x00000800	/*      logical */

/*
 * Delivery mode.
 *
 *	000	Fixed		Deliver the signal on the INTR signal for
 *				all processor core's LAPICs listed in the 
 *				destination.  The trigger mode may be
 *				edge or level.
 *
 *	001	Lowest Pri	Deliver to the processor core whos LAPIC
 *				is operating at the lowest priority (TPR).
 *				The trigger mode may be edge or level.
 *
 *	010	SMI		System management interrupt.  the vector
 *				information is ignored but must be programmed
 *				to all zero's for future compatibility.
 *				Must be edge triggered.
 *
 *	011	Reserved
 *
 *	100	NMI		Deliver on the NMI signal for all cpu cores
 *				listed in the destination.  Vector information
 *				is ignored.  NMIs are treated as edge triggered
 *				interrupts even if programmed as level 
 *				triggered.  For proper operation the pin must
 *				be programmed as an edge trigger.
 *
 *	101	INIT		Deliver to all processor cores listed in
 *				the destination by asserting their INIT signal.
 *				All addressed LAPICs will assume their INIT
 *				state.  Always treated as edge-triggered even
 *				if programmed as level.  For proper operation
 *				the pin must be programed as an edge trigger.
 *
 *	110	Reserved
 *
 *	111	ExINT		Deliver as an INTR signal to all processor
 *				cores listed in the destination as an 
 *				interrupt originating in an externally
 *				connected interrupt controller.
 *				The INTA cycle corresponding to this ExINT
 *				will be routed to the external controller
 *				that is expected to supply the vector. 
 *				Must be edge triggered.
 *			
 */
#define IOART_DELMOD	0x00000700	/* R/W: delivery mode */
#define IOART_DELFIXED	0x00000000	/*       fixed */
#define IOART_DELLOPRI	0x00000100	/*       lowest priority */
#define IOART_DELSMI	0x00000200	/*       System Management INT */
#define IOART_DELRSV1	0x00000300	/*       reserved */
#define IOART_DELNMI	0x00000400	/*       NMI signal */
#define IOART_DELINIT	0x00000500	/*       INIT signal */
#define IOART_DELRSV2	0x00000600	/*       reserved */
#define IOART_DELEXINT	0x00000700	/*       External INTerrupt */

/*
 * The interrupt vector.  Valid values range from 0x10 to 0xFE.
 */
#define IOART_INTVEC	0x000000ff	/* R/W: INTerrupt vector field */

#endif /* _MACHINE_APIC_H_ */
