/*-
 * Copyright (c) 1990 The Regents of the University of California.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD: src/sys/i386/i386/exception.s,v 1.65.2.3 2001/08/15 01:23:49 peter Exp $
 */

#include "use_npx.h"

#include <machine/asmacros.h>
#include <machine/segments.h>
#include <machine/lock.h>
#include <machine/psl.h>
#include <machine/trap.h>

#include "assym.s"

	.text

	.globl	lwkt_switch_return

#ifdef DEBUG_INTERRUPTS
	.globl	Xrsvdary

Xrsvdary:
 .long	Xrsvd0
 .long	Xrsvd1  , Xrsvd2  , Xrsvd3  , Xrsvd4  , Xrsvd5  , Xrsvd6  , Xrsvd7  , Xrsvd8
 .long	Xrsvd9  , Xrsvd10 , Xrsvd11 , Xrsvd12 , Xrsvd13 , Xrsvd14 , Xrsvd15 , Xrsvd16
 .long	Xrsvd17 , Xrsvd18 , Xrsvd19 , Xrsvd20 , Xrsvd21 , Xrsvd22 , Xrsvd23 , Xrsvd24
 .long	Xrsvd25 , Xrsvd26 , Xrsvd27 , Xrsvd28 , Xrsvd29 , Xrsvd30 , Xrsvd31 , Xrsvd32
 .long	Xrsvd33 , Xrsvd34 , Xrsvd35 , Xrsvd36 , Xrsvd37 , Xrsvd38 , Xrsvd39 , Xrsvd40
 .long	Xrsvd41 , Xrsvd42 , Xrsvd43 , Xrsvd44 , Xrsvd45 , Xrsvd46 , Xrsvd47 , Xrsvd48
 .long	Xrsvd49 , Xrsvd50 , Xrsvd51 , Xrsvd52 , Xrsvd53 , Xrsvd54 , Xrsvd55 , Xrsvd56
 .long	Xrsvd57 , Xrsvd58 , Xrsvd59 , Xrsvd60 , Xrsvd61 , Xrsvd62 , Xrsvd63 , Xrsvd64
 .long	Xrsvd65 , Xrsvd66 , Xrsvd67 , Xrsvd68 , Xrsvd69 , Xrsvd70 , Xrsvd71 , Xrsvd72
 .long	Xrsvd73 , Xrsvd74 , Xrsvd75 , Xrsvd76 , Xrsvd77 , Xrsvd78 , Xrsvd79 , Xrsvd80
 .long	Xrsvd81 , Xrsvd82 , Xrsvd83 , Xrsvd84 , Xrsvd85 , Xrsvd86 , Xrsvd87 , Xrsvd88
 .long	Xrsvd89 , Xrsvd90 , Xrsvd91 , Xrsvd92 , Xrsvd93 , Xrsvd94 , Xrsvd95 , Xrsvd96
 .long	Xrsvd97 , Xrsvd98 , Xrsvd99 , Xrsvd100, Xrsvd101, Xrsvd102, Xrsvd103, Xrsvd104
 .long	Xrsvd105, Xrsvd106, Xrsvd107, Xrsvd108, Xrsvd109, Xrsvd110, Xrsvd111, Xrsvd112
 .long	Xrsvd113, Xrsvd114, Xrsvd115, Xrsvd116, Xrsvd117, Xrsvd118, Xrsvd119, Xrsvd120
 .long	Xrsvd121, Xrsvd122, Xrsvd123, Xrsvd124, Xrsvd125, Xrsvd126, Xrsvd127, Xrsvd128
 .long	Xrsvd129, Xrsvd130, Xrsvd131, Xrsvd132, Xrsvd133, Xrsvd134, Xrsvd135, Xrsvd136
 .long	Xrsvd137, Xrsvd138, Xrsvd139, Xrsvd140, Xrsvd141, Xrsvd142, Xrsvd143, Xrsvd144
 .long	Xrsvd145, Xrsvd146, Xrsvd147, Xrsvd148, Xrsvd149, Xrsvd150, Xrsvd151, Xrsvd152
 .long	Xrsvd153, Xrsvd154, Xrsvd155, Xrsvd156, Xrsvd157, Xrsvd158, Xrsvd159, Xrsvd160
 .long	Xrsvd161, Xrsvd162, Xrsvd163, Xrsvd164, Xrsvd165, Xrsvd166, Xrsvd167, Xrsvd168
 .long	Xrsvd169, Xrsvd170, Xrsvd171, Xrsvd172, Xrsvd173, Xrsvd174, Xrsvd175, Xrsvd176
 .long	Xrsvd177, Xrsvd178, Xrsvd179, Xrsvd180, Xrsvd181, Xrsvd182, Xrsvd183, Xrsvd184
 .long	Xrsvd185, Xrsvd186, Xrsvd187, Xrsvd188, Xrsvd189, Xrsvd190, Xrsvd191, Xrsvd192
 .long	Xrsvd193, Xrsvd194, Xrsvd195, Xrsvd196, Xrsvd197, Xrsvd198, Xrsvd199, Xrsvd200
 .long	Xrsvd201, Xrsvd202, Xrsvd203, Xrsvd204, Xrsvd205, Xrsvd206, Xrsvd207, Xrsvd208
 .long	Xrsvd209, Xrsvd210, Xrsvd211, Xrsvd212, Xrsvd213, Xrsvd214, Xrsvd215, Xrsvd216
 .long	Xrsvd217, Xrsvd218, Xrsvd219, Xrsvd220, Xrsvd221, Xrsvd222, Xrsvd223, Xrsvd224
 .long	Xrsvd225, Xrsvd226, Xrsvd227, Xrsvd228, Xrsvd229, Xrsvd230, Xrsvd231, Xrsvd232
 .long	Xrsvd233, Xrsvd234, Xrsvd235, Xrsvd236, Xrsvd237, Xrsvd238, Xrsvd239, Xrsvd240
 .long	Xrsvd241, Xrsvd242, Xrsvd243, Xrsvd244, Xrsvd245, Xrsvd246, Xrsvd247, Xrsvd248
 .long	Xrsvd249, Xrsvd250, Xrsvd251, Xrsvd252, Xrsvd253, Xrsvd254, Xrsvd255

#endif

/*****************************************************************************/
/* Trap handling                                                             */
/*****************************************************************************/
/*
 * Trap and fault vector routines.
 *
 * Most traps are 'trap gates', SDT_SYS386TGT.  A trap gate pushes state on
 * the stack that mostly looks like an interrupt, but does not disable 
 * interrupts.  A few of the traps we are use are interrupt gates, 
 * SDT_SYS386IGT, which are nearly the same thing except interrupts are
 * disabled on entry.
 *
 * The cpu will push a certain amount of state onto the kernel stack for
 * the current process.  The amount of state depends on the type of trap 
 * and whether the trap crossed rings or not.  See i386/include/frame.h.  
 * At the very least the current EFLAGS (status register, which includes 
 * the interrupt disable state prior to the trap), the code segment register,
 * and the return instruction pointer are pushed by the cpu.  The cpu 
 * will also push an 'error' code for certain traps.  We push a dummy 
 * error code for those traps where the cpu doesn't in order to maintain 
 * a consistent frame.  We also push a contrived 'trap number'.
 *
 * The cpu does not push the general registers, we must do that, and we 
 * must restore them prior to calling 'iret'.  The cpu adjusts the %cs and
 * %ss segment registers, but does not mess with %ds, %es, %fs, or %gs.
 * Thus we must load the ones we use (which is most of them) with appropriate
 * values for supervisor mode operation.
 *
 * On entry to a trap or interrupt WE DO NOT OWN THE MP LOCK.  This means
 * that we must be careful in regards to accessing global variables.  We
 * save (push) the current cpl (our software interrupt disable mask), call
 * the trap function, then jump to doreti to restore the cpl and deal with
 * ASTs (software interrupts).  doreti will determine if the restoration
 * of the cpl unmasked any pending interrupts and will issue those interrupts
 * synchronously prior to doing the iret.
 */

#define	TRAP(a)		pushl $(a) ; jmp alltraps
#define BPTTRAP(a)	testl $PSL_I,4+8(%esp) ; je 1f ; sti ; 1: ; TRAP(a)

MCOUNT_LABEL(user)
MCOUNT_LABEL(btrap)

IDTVEC(div)
	pushl $0; TRAP(T_DIVIDE)
IDTVEC(dbg)
	pushl $0; BPTTRAP(T_TRCTRAP)
IDTVEC(nmi)
	pushl $0; TRAP(T_NMI)
IDTVEC(bpt)
	pushl $0; BPTTRAP(T_BPTFLT)
IDTVEC(ofl)
	pushl $0; TRAP(T_OFLOW)
IDTVEC(bnd)
	pushl $0; TRAP(T_BOUND)
IDTVEC(ill)
	pushl $0; TRAP(T_PRIVINFLT)
IDTVEC(dna)
	pushl $0; TRAP(T_DNA)
IDTVEC(fpusegm)
	pushl $0; TRAP(T_FPOPFLT)
IDTVEC(tss)
	TRAP(T_TSSFLT)
IDTVEC(missing)
	TRAP(T_SEGNPFLT)
IDTVEC(stk)
	TRAP(T_STKFLT)
IDTVEC(prot)
	TRAP(T_PROTFLT)
IDTVEC(page)
	TRAP(T_PAGEFLT)
IDTVEC(mchk)
	pushl $0; TRAP(T_MCHK)

IDTVEC(rsvd0)
	pushl $0; TRAP(T_RESERVED)

#ifdef DEBUG_INTERRUPTS

IDTVEC(rsvd1)
	pushl $1; TRAP(T_RESERVED)
IDTVEC(rsvd2)
	pushl $2; TRAP(T_RESERVED)
IDTVEC(rsvd3)
	pushl $3; TRAP(T_RESERVED)
IDTVEC(rsvd4)
	pushl $4; TRAP(T_RESERVED)
IDTVEC(rsvd5)
	pushl $5; TRAP(T_RESERVED)
IDTVEC(rsvd6)
	pushl $6; TRAP(T_RESERVED)
IDTVEC(rsvd7)
	pushl $7; TRAP(T_RESERVED)
IDTVEC(rsvd8)
	pushl $8; TRAP(T_RESERVED)
IDTVEC(rsvd9)
	pushl $9; TRAP(T_RESERVED)
IDTVEC(rsvd10)
	pushl $10; TRAP(T_RESERVED)
IDTVEC(rsvd11)
	pushl $11; TRAP(T_RESERVED)
IDTVEC(rsvd12)
	pushl $12; TRAP(T_RESERVED)
IDTVEC(rsvd13)
	pushl $13; TRAP(T_RESERVED)
IDTVEC(rsvd14)
	pushl $14; TRAP(T_RESERVED)
IDTVEC(rsvd15)
	pushl $15; TRAP(T_RESERVED)
IDTVEC(rsvd16)
	pushl $16; TRAP(T_RESERVED)
IDTVEC(rsvd17)
	pushl $17; TRAP(T_RESERVED)
IDTVEC(rsvd18)
	pushl $18; TRAP(T_RESERVED)
IDTVEC(rsvd19)
	pushl $19; TRAP(T_RESERVED)
IDTVEC(rsvd20)
	pushl $20; TRAP(T_RESERVED)
IDTVEC(rsvd21)
	pushl $21; TRAP(T_RESERVED)
IDTVEC(rsvd22)
	pushl $22; TRAP(T_RESERVED)
IDTVEC(rsvd23)
	pushl $23; TRAP(T_RESERVED)
IDTVEC(rsvd24)
	pushl $24; TRAP(T_RESERVED)
IDTVEC(rsvd25)
	pushl $25; TRAP(T_RESERVED)
IDTVEC(rsvd26)
	pushl $26; TRAP(T_RESERVED)
IDTVEC(rsvd27)
	pushl $27; TRAP(T_RESERVED)
IDTVEC(rsvd28)
	pushl $28; TRAP(T_RESERVED)
IDTVEC(rsvd29)
	pushl $29; TRAP(T_RESERVED)
IDTVEC(rsvd30)
	pushl $30; TRAP(T_RESERVED)
IDTVEC(rsvd31)
	pushl $31; TRAP(T_RESERVED)
IDTVEC(rsvd32)
	pushl $32; TRAP(T_RESERVED)
IDTVEC(rsvd33)
	pushl $33; TRAP(T_RESERVED)
IDTVEC(rsvd34)
	pushl $34; TRAP(T_RESERVED)
IDTVEC(rsvd35)
	pushl $35; TRAP(T_RESERVED)
IDTVEC(rsvd36)
	pushl $36; TRAP(T_RESERVED)
IDTVEC(rsvd37)
	pushl $37; TRAP(T_RESERVED)
IDTVEC(rsvd38)
	pushl $38; TRAP(T_RESERVED)
IDTVEC(rsvd39)
	pushl $39; TRAP(T_RESERVED)
IDTVEC(rsvd40)
	pushl $40; TRAP(T_RESERVED)
IDTVEC(rsvd41)
	pushl $41; TRAP(T_RESERVED)
IDTVEC(rsvd42)
	pushl $42; TRAP(T_RESERVED)
IDTVEC(rsvd43)
	pushl $43; TRAP(T_RESERVED)
IDTVEC(rsvd44)
	pushl $44; TRAP(T_RESERVED)
IDTVEC(rsvd45)
	pushl $45; TRAP(T_RESERVED)
IDTVEC(rsvd46)
	pushl $46; TRAP(T_RESERVED)
IDTVEC(rsvd47)
	pushl $47; TRAP(T_RESERVED)
IDTVEC(rsvd48)
	pushl $48; TRAP(T_RESERVED)
IDTVEC(rsvd49)
	pushl $49; TRAP(T_RESERVED)
IDTVEC(rsvd50)
	pushl $50; TRAP(T_RESERVED)
IDTVEC(rsvd51)
	pushl $51; TRAP(T_RESERVED)
IDTVEC(rsvd52)
	pushl $52; TRAP(T_RESERVED)
IDTVEC(rsvd53)
	pushl $53; TRAP(T_RESERVED)
IDTVEC(rsvd54)
	pushl $54; TRAP(T_RESERVED)
IDTVEC(rsvd55)
	pushl $55; TRAP(T_RESERVED)
IDTVEC(rsvd56)
	pushl $56; TRAP(T_RESERVED)
IDTVEC(rsvd57)
	pushl $57; TRAP(T_RESERVED)
IDTVEC(rsvd58)
	pushl $58; TRAP(T_RESERVED)
IDTVEC(rsvd59)
	pushl $59; TRAP(T_RESERVED)
IDTVEC(rsvd60)
	pushl $60; TRAP(T_RESERVED)
IDTVEC(rsvd61)
	pushl $61; TRAP(T_RESERVED)
IDTVEC(rsvd62)
	pushl $62; TRAP(T_RESERVED)
IDTVEC(rsvd63)
	pushl $63; TRAP(T_RESERVED)
IDTVEC(rsvd64)
	pushl $64; TRAP(T_RESERVED)
IDTVEC(rsvd65)
	pushl $65; TRAP(T_RESERVED)
IDTVEC(rsvd66)
	pushl $66; TRAP(T_RESERVED)
IDTVEC(rsvd67)
	pushl $67; TRAP(T_RESERVED)
IDTVEC(rsvd68)
	pushl $68; TRAP(T_RESERVED)
IDTVEC(rsvd69)
	pushl $69; TRAP(T_RESERVED)
IDTVEC(rsvd70)
	pushl $70; TRAP(T_RESERVED)
IDTVEC(rsvd71)
	pushl $71; TRAP(T_RESERVED)
IDTVEC(rsvd72)
	pushl $72; TRAP(T_RESERVED)
IDTVEC(rsvd73)
	pushl $73; TRAP(T_RESERVED)
IDTVEC(rsvd74)
	pushl $74; TRAP(T_RESERVED)
IDTVEC(rsvd75)
	pushl $75; TRAP(T_RESERVED)
IDTVEC(rsvd76)
	pushl $76; TRAP(T_RESERVED)
IDTVEC(rsvd77)
	pushl $77; TRAP(T_RESERVED)
IDTVEC(rsvd78)
	pushl $78; TRAP(T_RESERVED)
IDTVEC(rsvd79)
	pushl $79; TRAP(T_RESERVED)
IDTVEC(rsvd80)
	pushl $80; TRAP(T_RESERVED)
IDTVEC(rsvd81)
	pushl $81; TRAP(T_RESERVED)
IDTVEC(rsvd82)
	pushl $82; TRAP(T_RESERVED)
IDTVEC(rsvd83)
	pushl $83; TRAP(T_RESERVED)
IDTVEC(rsvd84)
	pushl $84; TRAP(T_RESERVED)
IDTVEC(rsvd85)
	pushl $85; TRAP(T_RESERVED)
IDTVEC(rsvd86)
	pushl $86; TRAP(T_RESERVED)
IDTVEC(rsvd87)
	pushl $87; TRAP(T_RESERVED)
IDTVEC(rsvd88)
	pushl $88; TRAP(T_RESERVED)
IDTVEC(rsvd89)
	pushl $89; TRAP(T_RESERVED)
IDTVEC(rsvd90)
	pushl $90; TRAP(T_RESERVED)
IDTVEC(rsvd91)
	pushl $91; TRAP(T_RESERVED)
IDTVEC(rsvd92)
	pushl $92; TRAP(T_RESERVED)
IDTVEC(rsvd93)
	pushl $93; TRAP(T_RESERVED)
IDTVEC(rsvd94)
	pushl $94; TRAP(T_RESERVED)
IDTVEC(rsvd95)
	pushl $95; TRAP(T_RESERVED)
IDTVEC(rsvd96)
	pushl $96; TRAP(T_RESERVED)
IDTVEC(rsvd97)
	pushl $97; TRAP(T_RESERVED)
IDTVEC(rsvd98)
	pushl $98; TRAP(T_RESERVED)
IDTVEC(rsvd99)
	pushl $99; TRAP(T_RESERVED)
IDTVEC(rsvd100)
	pushl $100; TRAP(T_RESERVED)
IDTVEC(rsvd101)
	pushl $101; TRAP(T_RESERVED)
IDTVEC(rsvd102)
	pushl $102; TRAP(T_RESERVED)
IDTVEC(rsvd103)
	pushl $103; TRAP(T_RESERVED)
IDTVEC(rsvd104)
	pushl $104; TRAP(T_RESERVED)
IDTVEC(rsvd105)
	pushl $105; TRAP(T_RESERVED)
IDTVEC(rsvd106)
	pushl $106; TRAP(T_RESERVED)
IDTVEC(rsvd107)
	pushl $107; TRAP(T_RESERVED)
IDTVEC(rsvd108)
	pushl $108; TRAP(T_RESERVED)
IDTVEC(rsvd109)
	pushl $109; TRAP(T_RESERVED)
IDTVEC(rsvd110)
	pushl $110; TRAP(T_RESERVED)
IDTVEC(rsvd111)
	pushl $111; TRAP(T_RESERVED)
IDTVEC(rsvd112)
	pushl $112; TRAP(T_RESERVED)
IDTVEC(rsvd113)
	pushl $113; TRAP(T_RESERVED)
IDTVEC(rsvd114)
	pushl $114; TRAP(T_RESERVED)
IDTVEC(rsvd115)
	pushl $115; TRAP(T_RESERVED)
IDTVEC(rsvd116)
	pushl $116; TRAP(T_RESERVED)
IDTVEC(rsvd117)
	pushl $117; TRAP(T_RESERVED)
IDTVEC(rsvd118)
	pushl $118; TRAP(T_RESERVED)
IDTVEC(rsvd119)
	pushl $119; TRAP(T_RESERVED)
IDTVEC(rsvd120)
	pushl $120; TRAP(T_RESERVED)
IDTVEC(rsvd121)
	pushl $121; TRAP(T_RESERVED)
IDTVEC(rsvd122)
	pushl $122; TRAP(T_RESERVED)
IDTVEC(rsvd123)
	pushl $123; TRAP(T_RESERVED)
IDTVEC(rsvd124)
	pushl $124; TRAP(T_RESERVED)
IDTVEC(rsvd125)
	pushl $125; TRAP(T_RESERVED)
IDTVEC(rsvd126)
	pushl $126; TRAP(T_RESERVED)
IDTVEC(rsvd127)
	pushl $127; TRAP(T_RESERVED)
IDTVEC(rsvd128)
	pushl $128; TRAP(T_RESERVED)
IDTVEC(rsvd129)
	pushl $129; TRAP(T_RESERVED)
IDTVEC(rsvd130)
	pushl $130; TRAP(T_RESERVED)
IDTVEC(rsvd131)
	pushl $131; TRAP(T_RESERVED)
IDTVEC(rsvd132)
	pushl $132; TRAP(T_RESERVED)
IDTVEC(rsvd133)
	pushl $133; TRAP(T_RESERVED)
IDTVEC(rsvd134)
	pushl $134; TRAP(T_RESERVED)
IDTVEC(rsvd135)
	pushl $135; TRAP(T_RESERVED)
IDTVEC(rsvd136)
	pushl $136; TRAP(T_RESERVED)
IDTVEC(rsvd137)
	pushl $137; TRAP(T_RESERVED)
IDTVEC(rsvd138)
	pushl $138; TRAP(T_RESERVED)
IDTVEC(rsvd139)
	pushl $139; TRAP(T_RESERVED)
IDTVEC(rsvd140)
	pushl $140; TRAP(T_RESERVED)
IDTVEC(rsvd141)
	pushl $141; TRAP(T_RESERVED)
IDTVEC(rsvd142)
	pushl $142; TRAP(T_RESERVED)
IDTVEC(rsvd143)
	pushl $143; TRAP(T_RESERVED)
IDTVEC(rsvd144)
	pushl $144; TRAP(T_RESERVED)
IDTVEC(rsvd145)
	pushl $145; TRAP(T_RESERVED)
IDTVEC(rsvd146)
	pushl $146; TRAP(T_RESERVED)
IDTVEC(rsvd147)
	pushl $147; TRAP(T_RESERVED)
IDTVEC(rsvd148)
	pushl $148; TRAP(T_RESERVED)
IDTVEC(rsvd149)
	pushl $149; TRAP(T_RESERVED)
IDTVEC(rsvd150)
	pushl $150; TRAP(T_RESERVED)
IDTVEC(rsvd151)
	pushl $151; TRAP(T_RESERVED)
IDTVEC(rsvd152)
	pushl $152; TRAP(T_RESERVED)
IDTVEC(rsvd153)
	pushl $153; TRAP(T_RESERVED)
IDTVEC(rsvd154)
	pushl $154; TRAP(T_RESERVED)
IDTVEC(rsvd155)
	pushl $155; TRAP(T_RESERVED)
IDTVEC(rsvd156)
	pushl $156; TRAP(T_RESERVED)
IDTVEC(rsvd157)
	pushl $157; TRAP(T_RESERVED)
IDTVEC(rsvd158)
	pushl $158; TRAP(T_RESERVED)
IDTVEC(rsvd159)
	pushl $159; TRAP(T_RESERVED)
IDTVEC(rsvd160)
	pushl $160; TRAP(T_RESERVED)
IDTVEC(rsvd161)
	pushl $161; TRAP(T_RESERVED)
IDTVEC(rsvd162)
	pushl $162; TRAP(T_RESERVED)
IDTVEC(rsvd163)
	pushl $163; TRAP(T_RESERVED)
IDTVEC(rsvd164)
	pushl $164; TRAP(T_RESERVED)
IDTVEC(rsvd165)
	pushl $165; TRAP(T_RESERVED)
IDTVEC(rsvd166)
	pushl $166; TRAP(T_RESERVED)
IDTVEC(rsvd167)
	pushl $167; TRAP(T_RESERVED)
IDTVEC(rsvd168)
	pushl $168; TRAP(T_RESERVED)
IDTVEC(rsvd169)
	pushl $169; TRAP(T_RESERVED)
IDTVEC(rsvd170)
	pushl $170; TRAP(T_RESERVED)
IDTVEC(rsvd171)
	pushl $171; TRAP(T_RESERVED)
IDTVEC(rsvd172)
	pushl $172; TRAP(T_RESERVED)
IDTVEC(rsvd173)
	pushl $173; TRAP(T_RESERVED)
IDTVEC(rsvd174)
	pushl $174; TRAP(T_RESERVED)
IDTVEC(rsvd175)
	pushl $175; TRAP(T_RESERVED)
IDTVEC(rsvd176)
	pushl $176; TRAP(T_RESERVED)
IDTVEC(rsvd177)
	pushl $177; TRAP(T_RESERVED)
IDTVEC(rsvd178)
	pushl $178; TRAP(T_RESERVED)
IDTVEC(rsvd179)
	pushl $179; TRAP(T_RESERVED)
IDTVEC(rsvd180)
	pushl $180; TRAP(T_RESERVED)
IDTVEC(rsvd181)
	pushl $181; TRAP(T_RESERVED)
IDTVEC(rsvd182)
	pushl $182; TRAP(T_RESERVED)
IDTVEC(rsvd183)
	pushl $183; TRAP(T_RESERVED)
IDTVEC(rsvd184)
	pushl $184; TRAP(T_RESERVED)
IDTVEC(rsvd185)
	pushl $185; TRAP(T_RESERVED)
IDTVEC(rsvd186)
	pushl $186; TRAP(T_RESERVED)
IDTVEC(rsvd187)
	pushl $187; TRAP(T_RESERVED)
IDTVEC(rsvd188)
	pushl $188; TRAP(T_RESERVED)
IDTVEC(rsvd189)
	pushl $189; TRAP(T_RESERVED)
IDTVEC(rsvd190)
	pushl $190; TRAP(T_RESERVED)
IDTVEC(rsvd191)
	pushl $191; TRAP(T_RESERVED)
IDTVEC(rsvd192)
	pushl $192; TRAP(T_RESERVED)
IDTVEC(rsvd193)
	pushl $193; TRAP(T_RESERVED)
IDTVEC(rsvd194)
	pushl $194; TRAP(T_RESERVED)
IDTVEC(rsvd195)
	pushl $195; TRAP(T_RESERVED)
IDTVEC(rsvd196)
	pushl $196; TRAP(T_RESERVED)
IDTVEC(rsvd197)
	pushl $197; TRAP(T_RESERVED)
IDTVEC(rsvd198)
	pushl $198; TRAP(T_RESERVED)
IDTVEC(rsvd199)
	pushl $199; TRAP(T_RESERVED)
IDTVEC(rsvd200)
	pushl $200; TRAP(T_RESERVED)
IDTVEC(rsvd201)
	pushl $201; TRAP(T_RESERVED)
IDTVEC(rsvd202)
	pushl $202; TRAP(T_RESERVED)
IDTVEC(rsvd203)
	pushl $203; TRAP(T_RESERVED)
IDTVEC(rsvd204)
	pushl $204; TRAP(T_RESERVED)
IDTVEC(rsvd205)
	pushl $205; TRAP(T_RESERVED)
IDTVEC(rsvd206)
	pushl $206; TRAP(T_RESERVED)
IDTVEC(rsvd207)
	pushl $207; TRAP(T_RESERVED)
IDTVEC(rsvd208)
	pushl $208; TRAP(T_RESERVED)
IDTVEC(rsvd209)
	pushl $209; TRAP(T_RESERVED)
IDTVEC(rsvd210)
	pushl $210; TRAP(T_RESERVED)
IDTVEC(rsvd211)
	pushl $211; TRAP(T_RESERVED)
IDTVEC(rsvd212)
	pushl $212; TRAP(T_RESERVED)
IDTVEC(rsvd213)
	pushl $213; TRAP(T_RESERVED)
IDTVEC(rsvd214)
	pushl $214; TRAP(T_RESERVED)
IDTVEC(rsvd215)
	pushl $215; TRAP(T_RESERVED)
IDTVEC(rsvd216)
	pushl $216; TRAP(T_RESERVED)
IDTVEC(rsvd217)
	pushl $217; TRAP(T_RESERVED)
IDTVEC(rsvd218)
	pushl $218; TRAP(T_RESERVED)
IDTVEC(rsvd219)
	pushl $219; TRAP(T_RESERVED)
IDTVEC(rsvd220)
	pushl $220; TRAP(T_RESERVED)
IDTVEC(rsvd221)
	pushl $221; TRAP(T_RESERVED)
IDTVEC(rsvd222)
	pushl $222; TRAP(T_RESERVED)
IDTVEC(rsvd223)
	pushl $223; TRAP(T_RESERVED)
IDTVEC(rsvd224)
	pushl $224; TRAP(T_RESERVED)
IDTVEC(rsvd225)
	pushl $225; TRAP(T_RESERVED)
IDTVEC(rsvd226)
	pushl $226; TRAP(T_RESERVED)
IDTVEC(rsvd227)
	pushl $227; TRAP(T_RESERVED)
IDTVEC(rsvd228)
	pushl $228; TRAP(T_RESERVED)
IDTVEC(rsvd229)
	pushl $229; TRAP(T_RESERVED)
IDTVEC(rsvd230)
	pushl $230; TRAP(T_RESERVED)
IDTVEC(rsvd231)
	pushl $231; TRAP(T_RESERVED)
IDTVEC(rsvd232)
	pushl $232; TRAP(T_RESERVED)
IDTVEC(rsvd233)
	pushl $233; TRAP(T_RESERVED)
IDTVEC(rsvd234)
	pushl $234; TRAP(T_RESERVED)
IDTVEC(rsvd235)
	pushl $235; TRAP(T_RESERVED)
IDTVEC(rsvd236)
	pushl $236; TRAP(T_RESERVED)
IDTVEC(rsvd237)
	pushl $237; TRAP(T_RESERVED)
IDTVEC(rsvd238)
	pushl $238; TRAP(T_RESERVED)
IDTVEC(rsvd239)
	pushl $239; TRAP(T_RESERVED)
IDTVEC(rsvd240)
	pushl $240; TRAP(T_RESERVED)
IDTVEC(rsvd241)
	pushl $241; TRAP(T_RESERVED)
IDTVEC(rsvd242)
	pushl $242; TRAP(T_RESERVED)
IDTVEC(rsvd243)
	pushl $243; TRAP(T_RESERVED)
IDTVEC(rsvd244)
	pushl $244; TRAP(T_RESERVED)
IDTVEC(rsvd245)
	pushl $245; TRAP(T_RESERVED)
IDTVEC(rsvd246)
	pushl $246; TRAP(T_RESERVED)
IDTVEC(rsvd247)
	pushl $247; TRAP(T_RESERVED)
IDTVEC(rsvd248)
	pushl $248; TRAP(T_RESERVED)
IDTVEC(rsvd249)
	pushl $249; TRAP(T_RESERVED)
IDTVEC(rsvd250)
	pushl $250; TRAP(T_RESERVED)
IDTVEC(rsvd251)
	pushl $251; TRAP(T_RESERVED)
IDTVEC(rsvd252)
	pushl $252; TRAP(T_RESERVED)
IDTVEC(rsvd253)
	pushl $253; TRAP(T_RESERVED)
IDTVEC(rsvd254)
	pushl $254; TRAP(T_RESERVED)
IDTVEC(rsvd255)
	pushl $255; TRAP(T_RESERVED)

#endif

IDTVEC(fpu)
#if NNPX > 0
	/*
	 * Handle like an interrupt (except for accounting) so that we can
	 * call npx_intr to clear the error.  It would be better to handle
	 * npx interrupts as traps.  Nested interrupts would probably have
	 * to be converted to ASTs.
	 *
	 * Convert everything to a full trapframe
	 */
	pushl	$0			/* dummy error code */
	pushl	$0			/* dummy trap type */
	pushl	$0			/* dummy xflags */
	pushal
	pushl	%ds
	pushl	%es
	pushl	%fs
	pushl	%gs
	cld
	mov	$KDSEL,%ax
	mov	%ax,%ds
	mov	%ax,%es
	mov	%ax,%gs
	mov	$KPSEL,%ax
	mov	%ax,%fs
	FAKE_MCOUNT(15*4(%esp))

	incl	PCPU(cnt)+V_TRAP

	/* additional dummy pushes to fake an interrupt frame */
	pushl	$0			/* ppl */
	pushl	$0			/* vector */

	/* warning, trap frame dummy arg, no extra reg pushes */
	call	npx_intr		/* note: call might mess w/ argument */

	/* convert back to a trapframe for doreti */
	addl	$4,%esp
	movl	$0,(%esp)		/* DUMMY CPL FOR DORETI */
	MEXITCOUNT
	jmp	doreti
#else	/* NNPX > 0 */
	pushl $0; TRAP(T_ARITHTRAP)
#endif	/* NNPX > 0 */

IDTVEC(align)
	TRAP(T_ALIGNFLT)

IDTVEC(xmm)
	pushl $0; TRAP(T_XMMFLT)
	
	/*
	 * _alltraps entry point.  Interrupts are enabled if this was a trap
	 * gate (TGT), else disabled if this was an interrupt gate (IGT).
	 * Note that int0x80_syscall is a trap gate.  Only page faults
	 * use an interrupt gate.
	 *
	 * Note that we are MP through to the call to trap().
	 */

	SUPERALIGN_TEXT
	.globl	alltraps
	.type	alltraps,@function
alltraps:
	pushl	$0	/* xflags (inherits hardware err on pagefault) */
	pushal
	pushl	%ds
	pushl	%es
	pushl	%fs
	pushl	%gs
	.globl	alltraps_with_regs_pushed
alltraps_with_regs_pushed:
	mov	$KDSEL,%ax
	mov	%ax,%ds
	mov	%ax,%es
	mov	%ax,%gs
	mov	$KPSEL,%ax
	mov	%ax,%fs
	FAKE_MCOUNT(15*4(%esp))
calltrap:
	FAKE_MCOUNT(btrap)		/* init "from" _btrap -> calltrap */
	incl	PCPU(cnt)+V_TRAP
	/* warning, trap frame dummy arg, no extra reg pushes */
	cld
	pushl	%esp			/* pass frame by reference */
	call	trap
	addl	$4,%esp

	/*
	 * Return via doreti to handle ASTs.  Have to change trap frame
	 * to interrupt frame.
	 */
	pushl	$0			/* DUMMY CPL FOR DORETI */
	MEXITCOUNT
	jmp	doreti

/*
 * SYSCALL CALL GATE (old entry point for a.out binaries)
 *
 * The intersegment call has been set up to specify one dummy parameter.
 *
 * This leaves a place to put eflags so that the call frame can be
 * converted to a trap frame. Note that the eflags is (semi-)bogusly
 * pushed into (what will be) tf_err and then copied later into the
 * final spot. It has to be done this way because esp can't be just
 * temporarily altered for the pushfl - an interrupt might come in
 * and clobber the saved cs/eip.
 *
 * We do not obtain the MP lock, but the call to syscall2 might.  If it
 * does it will release the lock prior to returning.
 */
	SUPERALIGN_TEXT
IDTVEC(syscall)
	pushfl				/* save eflags in tf_err for now */
	pushl	$T_SYSCALL80		/* tf_trapno */
	pushl	$0			/* tf_xflags */
	pushal
	pushl	%ds
	pushl	%es
	pushl	%fs
	pushl	%gs
	cld
	mov	$KDSEL,%ax		/* switch to kernel segments */
	mov	%ax,%ds
	mov	%ax,%es
	mov	%ax,%gs
	mov	$KPSEL,%ax
	mov	%ax,%fs
	movl	TF_ERR(%esp),%eax	/* copy saved eflags to final spot */
	movl	%eax,TF_EFLAGS(%esp)
	movl	$7,TF_ERR(%esp) 	/* sizeof "lcall 7,0" */
	FAKE_MCOUNT(15*4(%esp))
	incl	PCPU(cnt)+V_SYSCALL	/* YYY per-cpu */
	/* warning, trap frame dummy arg, no extra reg pushes */
	push	%esp			/* pass frame by reference */
	call	syscall2
	addl	$4,%esp
	MEXITCOUNT
	cli				/* atomic reqflags interlock w/iret */
	cmpl    $0,PCPU(reqflags)
	je	doreti_syscall_ret
	pushl	$0			/* cpl to restore */
	jmp	doreti

/*
 * Trap gate entry for FreeBSD ELF and Linux/NetBSD syscall (int 0x80)
 *
 * Even though the name says 'int0x80', this is actually a TGT (trap gate)
 * rather then an IGT (interrupt gate).  Thus interrupts are enabled on
 * entry just as they are for a normal syscall.
 *
 * We do not obtain the MP lock, but the call to syscall2 might.  If it
 * does it will release the lock prior to returning.
 */
	SUPERALIGN_TEXT
IDTVEC(int0x80_syscall)
	pushl	$0			/* tf_err */
	pushl	$T_SYSCALL80		/* tf_trapno */
	pushl	$0			/* tf_xflags */
	pushal
	pushl	%ds
	pushl	%es
	pushl	%fs
	pushl	%gs
	cld
	mov	$KDSEL,%ax		/* switch to kernel segments */
	mov	%ax,%ds
	mov	%ax,%es
	mov	%ax,%gs
	mov	$KPSEL,%ax
	mov	%ax,%fs
	movl	$2,TF_ERR(%esp)		/* sizeof "int 0x80" */
	FAKE_MCOUNT(15*4(%esp))
	incl	PCPU(cnt)+V_SYSCALL
	/* warning, trap frame dummy arg, no extra reg pushes */
	push	%esp			/* pass frame by reference */
	call	syscall2
	addl	$4,%esp
	MEXITCOUNT
	cli				/* atomic reqflags interlock w/irq */
	cmpl    $0,PCPU(reqflags)
	je	doreti_syscall_ret
	pushl	$0			/* cpl to restore */
	jmp	doreti

/*
 * This function is what cpu_heavy_restore jumps to after a new process
 * is created.  The LWKT subsystem switches while holding a critical
 * section and we maintain that abstraction here (e.g. because 
 * cpu_heavy_restore needs it due to PCB_*() manipulation), then get out of
 * it before calling the initial function (typically fork_return()) and/or
 * returning to user mode.
 *
 * The MP lock is not held at any point but the critcount is bumped
 * on entry to prevent interruption of the trampoline at a bad point.
 *
 * This is effectively what td->td_switch() returns to.  It 'returns' the
 * old thread in %eax and since this is not returning to a td->td_switch()
 * call from lwkt_switch() we must handle the cleanup for the old thread
 * by calling lwkt_switch_return().
 *
 * fork_trampoline(%eax:otd, %esi:func, %ebx:arg)
 */
ENTRY(fork_trampoline)
	pushl	%eax
	call	lwkt_switch_return
	addl	$4,%esp
	movl	PCPU(curthread),%eax
	decl	TD_CRITCOUNT(%eax)

	/*
	 * cpu_set_fork_handler intercepts this function call to
	 * have this call a non-return function to stay in kernel mode.
	 *
	 * initproc has its own fork handler, start_init(), which DOES
	 * return.
	 *
	 * The function (set in pcb_esi) gets passed two arguments,
	 * the primary parameter set in pcb_ebx and a pointer to the
	 * trapframe.
	 *   void (func)(int arg, struct trapframe *frame);
	 */
	pushl	%esp			/* pass frame by reference */
	pushl	%ebx			/* arg1 */
	call	*%esi			/* function */
	addl	$8,%esp
	/* cut from syscall */

	sti
	call	splz

	/*
	 * Return via doreti to handle ASTs.
	 */
	pushl	$0			/* cpl to restore */
	MEXITCOUNT
	jmp	doreti


/*
 * Include vm86 call routines, which want to call doreti.
 */
#include "vm86bios.s"

