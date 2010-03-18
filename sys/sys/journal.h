/*
 * Copyright (c) 2004 The DragonFly Project.  All rights reserved.
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
 * $DragonFly: src/sys/sys/journal.h,v 1.13 2007/05/09 00:53:35 dillon Exp $
 */

#ifndef _SYS_JOURNAL_H_
#define _SYS_JOURNAL_H_

#ifndef _SYS_TYPES_H_
#include <sys/types.h>
#endif
#ifndef _SYS_TIME_H_
#include <sys/time.h>
#endif

/*
 * Physical file format (binary)
 *
 * All raw records are 128-bit aligned, but all record sizes are actual.
 * This means that any scanning code must 16-byte-align the recsize field
 * when calculating skips.  The top level raw record has a header and a
 * trailer to allow both forwards and backwards scanning of the journal.
 * The alignment requirement allows the worker thread FIFO reservation
 * API to operate efficiently, amoung other things.
 *
 * Logical data stream records are usually no larger then the journal's
 * in-memory FIFO, since the journal's transactional APIs return contiguous
 * blocks of buffer space and since logical stream records are used to avoid
 * stalls when concurrent blocking operations are being written to the journal.
 * Programs can depend on a logical stream record being a 'reasonable' size.
 *
 * Multiple logical data streams may operate concurrently in the journal,
 * reflecting the fact that the system may be executing multiple blocking
 * operations on the filesystem all at the same time.  These logical data
 * streams are short-lived transactional entities which use a 13 bit id
 * plus a transaction start bit, end bit, and abort bit.
 *
 * Stream identifiers in the 0x00-0xFF range are special and not used for
 * normal transactional commands. 
 *
 * Stream id 0x00 indicates that no other streams should be active at that
 * point in the journal, which helps the journaling code detect corruption.
 *
 * Stream id 0x01 is used for pad.  Pads are used to align data on convenient
 * boundaries and to deal with dead space.
 *
 * Stream id 0x02 indicates a discontinuity in the streamed data and typically
 * contains information relating to the reason for the discontinuity.
 * JTYPE_ASSOCIATE and JTYPE_DISASSOCIATE are usually emplaced in stream 0x02.
 *
 * Stream id 0x03 may be used to annotate the journal with text comments
 * via mountctl commands.  This can be extremely useful to note situations
 * that may help with later recovery or audit operations.
 *
 * Stream id 0x04-0x7F are reserved by DragonFly for future protocol expansion.
 *
 * Stream id 0x80-0xFF may be used for third-party protocol expansion.
 *
 * Stream id's 0x0100-0x1FFF typically represent short-lived transactions
 * (i.e. an id may be reused once the previous use has completed).  The
 * journaling system runs through these id's sequentially which means that
 * the journaling code can handle up to 8192-256 = 7936 simultanious
 * transactions at any given moment.
 *
 * The sequence number field is context-sensitive.  It is typically used by
 * a journaling stream to provide an incrementing counter and/or timestamp
 * so recovery utilities can determine if any data is missing.
 *
 * The check word in the trailer may be used to provide an integrity check
 * on the journaled data.  A value of 0 always means that no check word
 * has been calculated.
 *
 * The journal_rawrecbeg structure MUST be a multiple of 16 bytes.
 * The journal_rawrecend structure MUST be a multiple of 8 bytes.
 *
 * NOTE: PAD RECORD SPECIAL CASE.  Pad records can be 16 bytes and have the
 * rawrecend structure overlayed on the sequence number field of the 
 * rawrecbeg structure.  This is necessary because stream records are
 * 16 byte aligned, not 24 byte aligned, and dead space is not allowed.
 * So the pad record must fit into any dead space.  THEREFORE, THE TRANSID
 * FIELD FOR A PAD RECORD MUST BE IGNORED.
 *
 * NOTE: ENDIAN HANDLING.  Data records can be in little or big endian form.
 * The receiver detects the state by observing the 'begmagic' field.  Each
 * direction in a full-duplex connection can be operating with different
 * endianess.  Checksum data is always calculated on the raw record (including
 * dead space) in a byte-stream fashion, and then converted to the transmit
 * endianess like everything else.  If the receiver's endianess is different
 * it must convert it back to host normal form to compare it against the
 * calculated checksum.
 */
struct journal_rawrecbeg {
	u_int16_t begmagic;	/* recovery scan, endianess detection */
	u_int16_t streamid;	/* start/stop bits and stream identifier */
	int32_t recsize;	/* stream data block (incls beg & end) */
	int64_t transid;	/* sequence number or transaction id */
	/* ADDITIONAL DATA */
};

struct journal_rawrecend {
	u_int16_t endmagic;	/* recovery scan, endianess detection */
	u_int16_t check;	/* check word or 0 */
	int32_t recsize;	/* same as rawrecbeg->recsize, for rev scan */
};

struct journal_ackrecord {
	struct journal_rawrecbeg	rbeg;
	int32_t				filler0;
	int32_t				filler1;
	struct journal_rawrecend	rend;
};

/*
 * Constants for stream record magic numbers.    The incomplete magic
 * number code is used internally by the memory FIFO reservation API
 * and worker thread, allowing a block of space in the journaling
 * stream (aka a stream block) to be reserved and then populated without
 * stalling other threads doing their own reservation and population.
 */
#define JREC_BEGMAGIC		0x1234
#define JREC_ENDMAGIC		0xCDEF
#define JREC_INCOMPLETEMAGIC	0xFFFF

/*
 * Stream ids are 14 bits.  The top 2 bits specify when a new logical
 * stream is being created or an existing logical stream is being terminated.
 * A single raw stream record will set both the BEGIN and END bits if the
 * entire transaction is encapsulated in a single stream record.
 */
#define JREC_STREAMCTL_MASK	0xE000
#define JREC_STREAMCTL_BEGIN	0x8000	/* start a new logical stream */
#define JREC_STREAMCTL_END	0x4000	/* terminate a logical stream */
#define JREC_STREAMCTL_ABORTED	0x2000

#define JREC_STREAMID_MASK	0x1FFF
#define JREC_STREAMID_SYNCPT	(JREC_STREAMCTL_BEGIN|JREC_STREAMCTL_END|0x0000)
#define JREC_STREAMID_PAD	(JREC_STREAMCTL_BEGIN|JREC_STREAMCTL_END|0x0001)
#define JREC_STREAMID_DISCONT	0x0002	/* discontinuity */
#define JREC_STREAMID_ANNOTATE	0x0003	/* annotation */
#define JREC_STREAMID_ACK	0x0004	/* acknowledgement */
#define JREC_STREAMID_RESTART	0x0005	/* disctoninuity - journal restart */
				/* 0x0006-0x007F reserved by DragonFly */
				/* 0x0080-0x00FF for third party use */
#define JREC_STREAMID_JMIN	0x0100	/* lowest allowed general id */
#define JREC_STREAMID_JMAX	0x2000	/* (one past the highest allowed id) */

#define JREC_DEFAULTSIZE	64	/* reasonable initial reservation */
#define JREC_MINRECSIZE		16	/* (after alignment) */
#define	JREC_MAXRECSIZE		(128*1024*1024)

/*
 * Each logical journaling stream typically represents a transaction...
 * that is, a VFS operation.  The VFS operation is written out using 
 * sub-records and may contain multiple, possibly nested sub-transactions.
 * multiple sub-transactions occur when a VFS operation cannot be represented
 * by a single command.  This is typically the case when a journal is 
 * configured to be reversable because UNDO sequences almost always have to
 * be specified in such cases.  For example, if you ftruncate() a file the
 * journal might have to write out a sequence of WRITE records representing
 * the lost data, otherwise the journal would not be reversable.
 * Sub-transactions within a particular stream do not have their own sequence
 * number field and thus may not be parallelized (the protocol is already
 * complex enough!).
 *
 * In order to support streaming operation with a limited buffer the recsize
 * field is allowed to be 0 for subrecords with the JMASK_NESTED bit set.
 * If this case occurs a scanner can determine that the recursion has ended
 * by detecting a nested subrecord with the JMASK_LAST bit set.  A scanner
 * may also set the field to the proper value after the fact to make later
 * operations more efficient. 
 *
 * Note that this bit must be properly set even if the recsize field is
 * non-zero.  The recsize must always be properly specified for 'leaf'
 * subrecords, however in order to allow subsystems to potentially allocate
 * more data space then they use the protocol allows any 'dead' space to be
 * filled with JLEAF_PAD records.
 *
 * The recsize field may indicate data well past the size of the current
 * raw stream record.  That is, the scanner may have to glue together
 * multiple stream records with the same stream id to fully decode the
 * embedded subrecords.  In particular, a subrecord could very well represent
 * hundreds of megabytes of data (e.g. if a program were to do a
 * multi-megabyte write()) and be split up across thousands of raw streaming
 * records, possibly interlaced with other unrelated streams from other
 * unrelated processes.  
 *
 * If a large sub-transaction is aborted the logical stream may be
 * terminated without writing out all the expected data.  When this occurs
 * the stream's ending record must also have the JREC_STREAMCTL_ABORTED bit
 * set.  However, scanners should still be robust enough to detect such
 * overflows even if the aborted bit is not set and consider them data
 * corruption.
 * 
 * Aborts may also occur in the normal course of operations, especially once
 * the journaling API is integrated into the cache coherency API.  A normal
 * abort is issued by emplacing a JLEAF_ABORT record within the transaction
 * being aborted.  Such records must be the last record in the sub-transaction,
 * so JLEAF_LAST is also usually set.  In a transaction with many 
 * sub-transactions only those sub-transactions with an abort record are
 * aborted, the rest remain valid.  Abort records are considered S.O.P. for
 * two reasons:  First, limited memory buffer space may make it impossible
 * to delete the portion of the stream being aborted (the data may have
 * already been sent to the target).  Second, the journaling code will
 * eventually be used to support a cache coherency layer which may have to
 * abort operations as part of the cache coherency protocol.  Note that
 * subrecord aborts are different from stream record aborts.  Stream record
 * aborts are considered to be extrodinary situations while subrecord aborts
 * are S.O.P.
 */

struct journal_subrecord {
	u_int16_t rectype;	/* 2 control bits, 14 record type bits */
	int16_t reserved;	/* future use */
	int32_t recsize;	/* record size (mandatory if not NESTED) */
	/* ADDITIONAL DATA */
};

#define	JDATA_KERN		0x0001
#define	JDATA_USER		0x0002
#define	JDATA_XIO		0x0003

#define JMASK_NESTED		0x8000	/* data is a nested recursion */
#define JMASK_LAST		0x4000
#define JMASK_SUBRECORD		0x0400
#define JTYPE_MASK		(~JMASK_LAST)

#define JLEAF_PAD		0x0000
#define JLEAF_ABORT		0x0001
#define JTYPE_ASSOCIATE		0x0002
#define JTYPE_DISASSOCIATE	0x0003
#define JTYPE_UNDO		(JMASK_NESTED|0x0004)
#define JTYPE_AUDIT		(JMASK_NESTED|0x0005)
#define JTYPE_REDO		(JMASK_NESTED|0x0006)

#define JTYPE_SETATTR		(JMASK_NESTED|0x0010)
#define JTYPE_WRITE		(JMASK_NESTED|0x0011)
#define JTYPE_PUTPAGES		(JMASK_NESTED|0x0012)
#define JTYPE_SETACL		(JMASK_NESTED|0x0013)
#define JTYPE_SETEXTATTR	(JMASK_NESTED|0x0014)
#define JTYPE_CREATE		(JMASK_NESTED|0x0015)
#define JTYPE_MKNOD		(JMASK_NESTED|0x0016)
#define JTYPE_LINK		(JMASK_NESTED|0x0017)
#define JTYPE_SYMLINK		(JMASK_NESTED|0x0018)
#define JTYPE_WHITEOUT		(JMASK_NESTED|0x0019)
#define JTYPE_REMOVE		(JMASK_NESTED|0x001A)
#define JTYPE_MKDIR		(JMASK_NESTED|0x001B)
#define JTYPE_RMDIR		(JMASK_NESTED|0x001C)
#define JTYPE_RENAME		(JMASK_NESTED|0x001D)

#define JTYPE_VATTR		(JMASK_NESTED|0x0100)
#define JTYPE_CRED		(JMASK_NESTED|0x0101)

/*
 * Low level record types
 */
#define JLEAF_FILEDATA		0x0401
#define JLEAF_PATH1		0x0402
#define JLEAF_PATH2		0x0403
#define JLEAF_PATH3		0x0404
#define JLEAF_PATH4		0x0405
#define JLEAF_UID		0x0406
#define JLEAF_GID		0x0407
#define JLEAF_MODES		0x0408
#define JLEAF_FFLAGS		0x0409
#define JLEAF_PID		0x040A
#define JLEAF_PPID		0x040B
#define JLEAF_COMM		0x040C
#define JLEAF_ATTRNAME		0x040D
#define JLEAF_PATH_REF		0x040E
#define JLEAF_RESERVED_0F	0x040F
#define JLEAF_SYMLINKDATA	0x0410
#define JLEAF_SEEKPOS		0x0411
#define JLEAF_INUM		0x0412
#define JLEAF_NLINK		0x0413
#define JLEAF_FSID		0x0414
#define JLEAF_SIZE		0x0415
#define JLEAF_ATIME		0x0416
#define JLEAF_MTIME		0x0417
#define JLEAF_CTIME		0x0418
#define JLEAF_GEN		0x0419
#define JLEAF_FLAGS		0x041A
#define JLEAF_UDEV		0x041B
#define JLEAF_FILEREV		0x041C
#define JLEAF_VTYPE		0x041D
#define JLEAF_ERROR		0x041E
#define JLEAF_UMAJOR		0x041F
#define JLEAF_UMINOR		0x0420

/*
 * Low level journal data file structures
 *
 * NOTE: embedded strings may use the full width of the field and thus
 * may not be 0-terminated.
 */
struct jleaf_path {
	char	path[4];	/* path from base of mount point */
	/* path is variable length and 0-terminated */
};

struct jleaf_vattr {
	int32_t	modes;
	int32_t fflags;
	struct timespec atime;
	struct timespec mtime;
	struct timespec ctime;
	int64_t inum;
};

struct jleaf_cred {
	int32_t	uid;
	int32_t gid;
	int32_t pid;
	int32_t flags;		/* suid/sgid and other flags */
	char	line[8];	/* ttyname or other session identification */
	char	comm[8];	/* simplified command name for reference */
};

struct jleaf_ioinfo {
	int64_t offset;
};

#endif
