/*
 * Copyright (c) 1998 Robert Nordier
 * Copyright (c) 2004 by Matthew Dillon, All rights reserved.
 *
 * Redistribution and use in source and binary forms are freely
 * permitted provided that the above copyright notice and this
 * paragraph and the following disclaimer are duplicated in all
 * such forms.
 *
 * This software is provided "AS IS" and without any express or
 * implied warranties, including, without limitation, the implied
 * warranties of merchantability and fitness for a particular
 * purpose.
 *
 * $DragonFly: src/sys/boot/i386/bootn0/Attic/boot0.S,v 1.1 2004/06/26 02:00:11 dillon Exp $
 *
 * A 512-byte boot manager.
 */

		.set NHRDRV,0x475	# Number of hard drives
		.set FAKE,0x800 	# Partition entry
		.set BIOS_LOAD,0x7c00	# BIOS Load address
		.set ORG_ADDR,0x91000	# 'start' symbol
		.set ORG_SEG,0x900	# leave room in segment for stk
		.set ORG_OFF,0x1000

		.set PRT_OFF,0x1be	# Partition table

		.set TBL0SZ,3 		# Table 0 size
		.set TBL1SZ,9	 	# Table 1 size

		.set MAGIC,0xaa55	# Magic: bootable
		.set B0MAGIC,0xbb66	# Identification

		.set KEY_ENTER,0x1c	# Enter key scan code
		.set KEY_F1,0x3b	# F1 key scan code
		.set KEY_1,0x02		# #1 key scan code

		#
		# Addresses in the sector of embedded data values.  Accessed
		# with negative offsets from the end of the relocated sector
		# (%ebp)
		#
		.set _NXTDRV,-0x48	# Next drive
		.set _OPT,-0x47 	# Default option
		.set _SETDRV,-0x46	# Drive to force
		.set _FLAGS,-0x45	# Flags
		.set _TICKS,-0x44	# Timeout ticks
		.set _FAKE,0x0		# Fake partition entry
		.set _MNUOPT,0xc	# Menu options

#define DRELOC(symbol)	(symbol-start+ORG_OFF)

		.globl start		# Entry point
		.code16			# This runs in real mode

		# Initialize segments and relocate the boot sector.  Our
		# stack starts at our target relocation address and grows
		# downward while the copy of boot0 grows upwards.
		#

start:		cld			# String ops inc
		movw $ORG_SEG,%ax	# Setup various segments
		movw %ax,%es
		movw %ax,%ss		# (coupled with next instruction)
		movw $ORG_OFF,%sp	# stack downward from start

		xorw %ax,%ax
		movw %ax,%ds		# source is in segment 0
		movw $BIOS_LOAD,%si	# source is the BIOS load address
		movw %sp,%di
		movw $0x100,%cx		# 256x2 = 512 bytes
		rep
		movsw

		# Note: %ds points at segment 0
		#	%es points at our relocated segment
		#	%ss points at our relocated segment
		#	(%ax still 0)
		#
		# Remember that %bp relative addresses uses the stack segment

		movw %di,%bp		# address variables
		movb $8,%cl		# clear 16 bytes
		rep
		stosw

		# Point %ds at our relocated segment, cleanup, and
		# jump to our relocated segment.
		#
		movw %ss,%ax
		movw %ax,%ds
		incb _FAKE+2(%bp)	# Start with sector 1
		ljmp $ORG_SEG,$DRELOC(main)

		# Check what flags were loaded with us.  Specifically, use a
		# predefined drive.  If the BIOS gives us junk use the '0'
		# in the block instead, as well.
		#
main:		testb $0x20,_FLAGS(%bp)	# Set number drive?
		jnz main.1		# Yes
		testb %dl,%dl		# Drive number valid?
		js main.2		# Possibly (0x80 set)
main.1: 	movb _SETDRV(%bp),%dl	# Drive number to use

		# Save our working drive in a temporary variable
		#
main.2: 	movb %dl,_FAKE(%bp)	# Save drive number
		callw putn		# To new line
		pushw %dx		# Save drive number

		# Start out with a pointer to the 4th byte of the first table
		# entry so that after 4 iterations it is beyond the end of
		# the sector (hits a 256 byte boundary and overflows 8 bits)
		#
		# Remember that the table starts 2 bytes earlier then you
		# would expect as the bootable flag is after it in the block.

		movw $DRELOC(partbl+0x4),%bx	# Partition table (+4)
		xorw %dx,%dx		# Item number

		# Loop on the partition table printing values until we 
		# hit the 256 byte boundary (at main.5).
		#
main.3: 	movb %ch,-4(%bx)	# Zero active flag (ch == 0)
		btw %dx,_FLAGS(%bp)	# Entry enabled?
		jnc main.5		# No

		# If any of the table entries match 'type' in the slice
		# table entry, then this is an empty or non bootable partition.
		# Skip it.
		#
		movb (%bx),%al		# Load type
		movw $DRELOC(tables),%di # Lookup tables
		movb $TBL0SZ,%cl	# Number of entries
		repne			# Exclude partition?
		scasb			# (note: uses %es by default)
		je main.5		# Yes

		#
		# Now scan the table of known types
		#
		movb $TBL1SZ,%cl	# Number of entries
		repne			# Known type ?
		scasb
		jne main.4		# No

		/*
		 * If it matches get the matching element in the next
		 * array.  If it doesn't we are already pointing at the
		 * first element, which points to a "?".
		 */
		addw $TBL1SZ,%di	# Adjust
main.4: 	movb (%di),%cl		# Partition description
		addw %cx,%di
		callw putx		# Display it
main.5: 	incw %dx		# Next item 
		addb $0x10,%bl		# Next entry
		jnc main.3		# Till done

		# End of loop.  Add one to the drive number and check validity.
		# 
		popw %ax		# Drive number
		subb $0x80-0x1,%al	# Does next drive exist? (BIOS)
		cmpb NHRDRV,%al
		jb main.6		# Yes

		# If %ax is 1 there is only one drive in the system and we
		# skip the drive selection code.  Note that %ax becomes an
		# index.
		#
		decw %ax		# Already drive 0?
		jz main.7		# Yes

		# No, loop on drive selection starting with 0
		#
		xorb %al,%al		# Drive 0
main.6: 	addb $'0'|0x80,%al	# Save next drive number
		movb %al,_NXTDRV(%bp)
		movw $DRELOC(drive),%di	# Display item
		callw putx

		# Prompt and setup for input but do not yet initiate the
		# input.
		#
main.7: 	movw $DRELOC(prompt),%si # Display prompt
		callw putstr
		movb _OPT(%bp),%dl	# Display default key
		decw %si
		callw putkey
		xorb %ah,%ah		# BIOS: Get system time
		int $0x1a
		movw %dx,%di		# Figure out tick value of timeout
		addw _TICKS(%bp),%di

		# Initiate input
		#
main.8: 	movb $0x1,%ah		# BIOS: Check for keypress
		int $0x16
		jnz main.11
		xorb %ah,%ah		# BIOS: Get system time
		int $0x1a
		cmpw %di,%dx		# loop if no timeout
		jb main.8

		# A timeout or default comes to here
		#
main.9: 	movb _OPT(%bp),%al	# Load default
		jmp main.12		# Join common code

		/* 
		 * If the user's last try was bad, beep.  Since nothing was
		 * printed, continue as if the user hadn't done anything.
		 */
main.10:	movb $0x7,%al		# Signal error
		callw putchr

		# Obtain the keystroke we previously polled successfully for
		#
main.11:	xorb %ah,%ah		# BIOS: Get keypress
		int $0x16
		movb %ah,%al		# Scan code

		# An ENTER is equivalent to a timeout
		#
		cmpb $KEY_ENTER,%al	# Enter pressed?
		je main.9		# Yes

		# Look for a legal keystroke, jump to main.10 (beep) and
		# retry if it is not legal.
		#
		subb $KEY_F1,%al	# Less F1 scan code
		cmpb $0x4,%al		# F1..F5?
		jna main.12
		subb $(KEY_1-KEY_F1),%al # Less #1 scan code
		cmpb $0x4,%al		# #1..#5?
		ja main.10

		# Check the selection, complain if its bad.  Only options
		# that were printed (MNUOPT bit set) are valid.
		#
main.12:	cbtw			# Option enabled?
		btw %ax,_MNUOPT(%bp)
		jnc main.10		# No

		/*
		 * We're good.  Save the data for later rewriting back to disk
		 */
		movb %al,_OPT(%bp)	# Save for future action
		leaw _FAKE(%bp),%si	# Partition for write
		movb (%si),%dl		# Drive number
		movw %si,%bx		# Partition for read
		cmpb $0x4,%al		# F5 pressed?
		pushf			# Save result of check
		je main.13		# Yes
		shlb $0x4,%al		# Point to selected partition
		addw $DRELOC(partbl),%ax
		xchgw %bx,%ax
		movb $0x80,(%bx)	# Flag active

		# Only writeback if we were told to in _FLAGS
		#
main.13:	pushw %bx		# Save
		testb $0x40,_FLAGS(%bp)	# No updates?
		jnz main.14		# Yes
		movw $DRELOC(start),%bx	# Data to write (relative to %es)
		movb $0x3,%ah		# Write sector to disk
		callw intx13
main.14:	popw %si		# Restore %si for next intx13

		# Retrieve the perviously saved F5 (next drive) request
		# status and move us to the next drive if necessary
		#
		popf
		jne main.15
		movb _NXTDRV(%bp),%dl
		subb $'0',%dl

		/*
		 * Load the selected bootsector to the LOAD location in RAM.
		 * We are already relocated so it will not overwrite us
		 * in particular.  If it fails to read or isn't marked bootable,
		 * treat it was a bad selection.
		 *
		 * note: drive number in %dl
		 */
main.15:	movw $BIOS_LOAD,%bx	# Address for read
		pushw $0
		popw %es
		movb $0x2,%ah		# Read sector from disk
		callw intx13
		movw %es:0x1fe(%bx),%ax # (these do not effect carry)
		pushw %ss
		popw %es
		jc main.10		# error on carry
		subw $MAGIC,%ax		# Bootable?
		jnz main.10
		pushw %ax		# seg: setup for lret (note %ax is 0)
		pushw %bx		# offset: setup for lret
		pushw %ax		# (so we can clear %ds)
		callw putn
		popw %ds
		lret

		# SUPPORT ROUTINES
		#
		# Display functions.  %bx is not modified by any of these
		# calls.
		#
putkey: 	movb $'F',%al		# Display 'F'
		callw putchr
		movb $'1',%al		# Display digit and remainder of prompt
		addb %dl,%al
		jmp putstr.1

		# Display the option and note that it is a valid option
		# by setting the appropriate bit in _MNUOPT.
		#
putx:		btsw %dx,_MNUOPT(%bp)	# Enable menu option
		movw $DRELOC(item),%si	# Display key
		callw putkey
		movw %di,%si		# Display the option string
puts:		callw putstr		# Display string in %si

putn:		movw $DRELOC(crlf),%si	# Output a CR+LF
		lodsb
putstr.1:	callw putchr		# Output %al + string at (%si)
putstr:		lodsb			# Output string at (%si)
		testb %al,%al
		jns   putstr.1
		andb $0x7f,%al
putchr: 	pushw %bx		# %bx is not modified by this call
		movw $0x7,%bx	 	# Page:attribute
		movb $0xe,%ah		# BIOS: Display character
		int $0x10
		popw %bx
		retw

		# One-sector disk I/O routine
		#
		# note: drive number in %dl, %si points to local variable
		# area holding (driveno, head, w:cylinder/sector)
		#
intx13: 	movb 0x1(%si),%dh	# Load head
		movw 0x2(%si),%cx	# Load cylinder:sector
		movb $0x1,%al		# Sector count
		movw %sp,%di
		testb $0x80,_FLAGS(%bp)	# Use packet interface?
		jz intx13.1
		pushl $0x0		# Set the LBA Address
		pushl 0x8(%si)
		pushw %es		# Set the Transfer Buffer Address
		pushw %bx
		push  $0x1		# Block count
		push  $0x10		# Packet size
		movw %sp,%si		# Packet pointer (%es?)
		decw %ax		# Verify off
		orb $0x40,%ah		# Use disk packet
intx13.1:	int $0x13		# BIOS: Disk I/O
		movw %di,%sp		# Restore
		retw			# WARNING: carry must be preserved

		/*
		 * MENU STRINGS
		 */

item:		.ascii "  "
		.byte ' '|0x80
prompt: 	.ascii "\nDefault:"
		.byte ' '|0x80
crlf:		.ascii "\r"
		.byte '\n'|0x80

		/*
		 * Partition table types
		 */
tables:
		/*
		 * These entries identify invalid or NON BOOT types and
		 * partitions.
		 */
		.byte 0x0, 0x5, 0xf

		/*
		 * These values indicate bootable types we know the names of
		 */
		.byte 0xb, 0xc, 0xe, 0x83
		.byte 0xa5, 0xa6, 0xa9

		/*
		 * These are offsets that match the known names above and
		 * point to the strings that will be printed.
		 */
		.byte os_misc-. 		# Unknown
		.byte os_dos-.			# Windows
		.byte os_dos-.			# Windows
		.byte os_dos-.			# Windows
		.byte os_linux-.		# Linux
		.byte os_bsd-.			# FreeBSD/DragonFly
		.byte os_bsd-.			# OpenBSD
		.byte os_bsd-.			# NetBSD

		/*
		 * And here are the strings themselves. 0x80 or'd into a 
		 * byte indicates the end of the string. (not so great
		 * for Russians but...)
		 */
os_misc:	.ascii "?";    .byte '?'|0x80
os_dos: 	.ascii "DO";   .byte 'S'|0x80
os_linux:	.ascii "Linu"; .byte 'x'|0x80
os_bsd: 	.ascii "BS";   .byte 'D'|0x80

		.org PRT_OFF-0xe,0x90

		/*
		 * 14 bytes then partition table.  Some of these values may
		 * be adjusted by the above code and then written back to the
		 * drive.  Be especially careful in that nxtdrv: must come 
		 * after drive:, as it is part of the same string.
		 */
		.word B0MAGIC			# Magic number

drive:		.ascii "Drive "
nxtdrv: 	.byte 0x0			# Next drive number
opt:		.byte 0x0			# Option
setdrv: 	.byte 0x80			# Drive to force
flags:		.byte FLAGS			# Flags
ticks:		.word TICKS			# Delay

		/*
		 * here is the 64 byte partition table that fdisk would
		 * fiddle with.
		 */
partbl: 	.fill 0x40,0x1,0x0		# Partition table
		.word MAGIC			# Magic number
