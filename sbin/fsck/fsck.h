/*
 * Copyright (c) 1980, 1986, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
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
 *	@(#)fsck.h	8.4 (Berkeley) 5/9/95
 * $FreeBSD: src/sbin/fsck/fsck.h,v 1.12.2.1 2001/01/23 23:11:07 iedowse Exp $
 */

#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>

#define	MAXDUP		10	/* limit on dup blks (per inode) */
#define	MAXBAD		10	/* limit on bad blks (per inode) */
#define	MAXBUFSPACE	40*1024	/* maximum space to allocate to buffers */
#define	INOBUFSIZE	56*1024	/* size of buffer to read inodes in pass1 */

/*
 * Each inode on the filesystem is described by the following structure.
 * The linkcnt is initially set to the value in the inode. Each time it
 * is found during the descent in passes 2, 3, and 4 the count is
 * decremented. Any inodes whose count is non-zero after pass 4 needs to
 * have its link count adjusted by the value remaining in ino_linkcnt.
 */
struct inostat {
	char	ino_state;	/* state of inode, see below */
	char	ino_type;	/* type of inode */
	short	ino_linkcnt;	/* number of links not found */
};
/*
 * Inode states.
 */
#define	USTATE	01		/* inode not allocated */
#define	FSTATE	02		/* inode is file */
#define	DSTATE	03		/* inode is directory */
#define	DFOUND	04		/* directory found during descent */
#define	DCLEAR	05		/* directory is to be cleared */
#define	FCLEAR	06		/* file is to be cleared */
/*
 * Inode state information is contained on per cylinder group lists
 * which are described by the following structure.
 */
extern struct inostatlist {
	long	il_numalloced;	/* number of inodes allocated in this cg */
	struct inostat *il_stat;/* inostat info for this cylinder group */
} *inostathead;

/*
 * buffer cache structure.
 */
struct bufarea {
	struct bufarea *b_next;		/* free list queue */
	struct bufarea *b_prev;		/* free list queue */
	ufs_daddr_t b_bno;
	int b_size;
	int b_errs;
	int b_flags;
	union {
		char *b_buf;			/* buffer space */
		ufs_daddr_t *b_indir;		/* indirect block */
		struct fs *b_fs;		/* super block */
		struct cg *b_cg;		/* cylinder group */
		struct ufs1_dinode *b_dinode;	/* inode block */
	} b_un;
	char b_dirty;
};

#define	B_INUSE 1

#define	MINBUFS		5	/* minimum number of buffers required */
extern struct bufarea bufhead;		/* head of list of other blks in filesys */
extern struct bufarea sblk;		/* file system superblock */
extern struct bufarea cgblk;		/* cylinder group blocks */
extern struct bufarea *pdirbp;		/* current directory contents */
extern struct bufarea *pbp;		/* current inode block */

#define	dirty(bp)	(bp)->b_dirty = 1
#define	initbarea(bp) \
	(bp)->b_dirty = 0; \
	(bp)->b_bno = (ufs_daddr_t)-1; \
	(bp)->b_flags = 0;

#define	sbdirty()	sblk.b_dirty = 1
#define	cgdirty()	cgblk.b_dirty = 1
#define	sblock		(*sblk.b_un.b_fs)
#define	cgrp		(*cgblk.b_un.b_cg)

enum fixstate {DONTKNOW, NOFIX, FIX, IGNORE};

struct inodesc {
	enum fixstate id_fix;	/* policy on fixing errors */
	int (*id_func)(struct inodesc *);	/* function to be applied to blocks of inode */
	ufs1_ino_t id_number;	/* inode number described */
	ufs1_ino_t id_parent;	/* for DATA nodes, their parent */
	ufs_daddr_t id_blkno;	/* current block number being examined */
	int id_numfrags;	/* number of frags contained in block */
	quad_t id_filesize;	/* for DATA nodes, the size of the directory */
	int id_loc;		/* for DATA nodes, current location in dir */
	int id_entryno;		/* for DATA nodes, current entry number */
	struct direct *id_dirp;	/* for DATA nodes, ptr to current entry */
	char *id_name;		/* for DATA nodes, name to find or enter */
	char id_type;		/* type of descriptor, DATA or ADDR */
};
/* file types */
#define	DATA	1
#define	ADDR	2

/*
 * Linked list of duplicate blocks.
 *
 * The list is composed of two parts. The first part of the
 * list (from duplist through the node pointed to by muldup)
 * contains a single copy of each duplicate block that has been
 * found. The second part of the list (from muldup to the end)
 * contains duplicate blocks that have been found more than once.
 * To check if a block has been found as a duplicate it is only
 * necessary to search from duplist through muldup. To find the
 * total number of times that a block has been found as a duplicate
 * the entire list must be searched for occurences of the block
 * in question. The following diagram shows a sample list where
 * w (found twice), x (found once), y (found three times), and z
 * (found once) are duplicate block numbers:
 *
 *    w -> y -> x -> z -> y -> w -> y
 *    ^		     ^
 *    |		     |
 * duplist	  muldup
 */
struct dups {
	struct dups *next;
	ufs_daddr_t dup;
};
extern struct dups *duplist;		/* head of dup list */
extern struct dups *muldup;		/* end of unique duplicate dup block numbers */

/*
 * Linked list of inodes with zero link counts.
 */
struct zlncnt {
	struct zlncnt *next;
	ufs1_ino_t zlncnt;
};
extern struct zlncnt *zlnhead;		/* head of zero link count list */

/*
 * Inode cache data structures.
 */
struct inoinfo {
	struct	inoinfo *i_nexthash;	/* next entry in hash chain */
	ufs1_ino_t	i_number;		/* inode number of this entry */
	ufs1_ino_t	i_parent;		/* inode number of parent */
	ufs1_ino_t	i_dotdot;		/* inode number of `..' */
	size_t	i_isize;		/* size of inode */
	u_int	i_numblks;		/* size of block array in bytes */
	ufs_daddr_t i_blks[1];		/* actually longer */
};
extern struct inoinfo **inphead, **inpsort;
extern long numdirs, dirhash, listmax, inplast, dirhashmask;
extern long countdirs;			/* number of directories we actually found */

/*
 * Be careful about cache locality of reference, large filesystems may
 * have tens of millions of directories in them and if fsck has to swap
 * we want it to swap efficiently.  For this reason we try to group
 * adjacent inodes together by a reasonable factor.
 */
#define DIRHASH(ino)	((ino >> 3) & dirhashmask)

extern char	*cdevname;		/* name of device being checked */
extern long	dev_bsize;		/* computed value of DEV_BSIZE */
extern long	secsize;		/* actual disk sector size */
extern char	fflag;			/* force check, ignore clean flag */
extern char	nflag;			/* assume a no response */
extern char	yflag;			/* assume a yes response */
extern int	bflag;			/* location of alternate super block */
extern int	debug;			/* output debugging info */
extern int	cvtlevel;		/* convert to newer file system format */
extern int	doinglevel1;		/* converting to new cylinder group format */
extern int	doinglevel2;		/* converting to new inode format */
extern int	newinofmt;		/* filesystem has new inode format */
extern char	usedsoftdep;		/* just fix soft dependency inconsistencies */
extern char	preen;			/* just fix normal inconsistencies */
extern char	rerun;			/* rerun fsck. Only used in non-preen mode */
extern int	returntosingle;		/* 1 => return to single user mode on exit */
extern char	resolved;		/* cleared if unresolved changes => not clean */
extern char	havesb;			/* superblock has been read */
extern int	fsmodified;		/* 1 => write done to file system */
extern int	fsreadfd;		/* file descriptor for reading file system */
extern int	fswritefd;		/* file descriptor for writing file system */
extern int	lastmntonly;		/* Output the last mounted on only */

extern ufs_daddr_t maxfsblock;		/* number of blocks in the file system */
extern char	*blockmap;		/* ptr to primary blk allocation map */
extern ufs1_ino_t	maxino;			/* number of inodes in file system */

extern ufs1_ino_t	lfdir;			/* lost & found directory inode number */
extern char	*lfname;		/* lost & found directory name */
extern int	lfmode;			/* lost & found directory creation mode */

extern ufs_daddr_t n_blks;		/* number of blocks in use */
extern ufs_daddr_t n_files;		/* number of files in use */

extern int	got_siginfo;		/* received a SIGINFO */

#define	clearinode(dp)	(*(dp) = zino)
extern struct	ufs1_dinode zino;

#define	setbmap(blkno)	setbit(blockmap, blkno)
#define	testbmap(blkno)	isset(blockmap, blkno)
#define	clrbmap(blkno)	clrbit(blockmap, blkno)

#define	STOP	0x01
#define	SKIP	0x02
#define	KEEPON	0x04
#define	ALTERED	0x08
#define	FOUND	0x10

#define	EEXIT	8		/* Standard error exit. */

struct fstab;


extern void		adjust(struct inodesc *, int);
extern ufs_daddr_t	allocblk(long);
extern ufs1_ino_t		allocdir(ufs1_ino_t, ufs1_ino_t, int);
extern ufs1_ino_t		allocino(ufs1_ino_t, int);
extern void		blkerror(ufs1_ino_t, char *, ufs_daddr_t);
extern char	       *blockcheck(char *);
extern int		bread(int, char *, ufs_daddr_t, long);
extern void		bufinit(void);
extern void		bwrite(int, char *, ufs_daddr_t, long);
extern void		cacheino(struct ufs1_dinode *, ufs1_ino_t);
extern void		catch(int) __dead2;
extern void		catchquit(int);
extern int		changeino(ufs1_ino_t, char *, ufs1_ino_t);
extern int		checkfstab(int, int,
			int (*)(struct fstab *),
			int (*)(char *, char *, long, int));
extern int		chkrange(ufs_daddr_t, int);
extern void		ckfini(int);
extern int		ckinode(struct ufs1_dinode *, struct inodesc *);
extern void		clri(struct inodesc *, char *, int);
extern int		clearentry(struct inodesc *);
extern void		direrror(ufs1_ino_t, char *);
extern int		dirscan(struct inodesc *);
extern int		dofix(struct inodesc *, char *);
extern void		ffs_clrblock(struct fs *, u_char *, ufs_daddr_t);
extern void		ffs_fragacct(struct fs *, int, int32_t [], int);
extern int		ffs_isblock(struct fs *, u_char *, ufs_daddr_t);
extern void		ffs_setblock(struct fs *, u_char *, ufs_daddr_t);
extern void		fileerror(ufs1_ino_t, ufs1_ino_t, char *);
extern int		findino(struct inodesc *);
extern int		findname(struct inodesc *);
extern void		flush(int, struct bufarea *);
extern void		freeblk(ufs_daddr_t, long);
extern void		freeino(ufs1_ino_t);
extern void		freeinodebuf(void);
extern int		ftypeok(struct ufs1_dinode *);
extern void		getblk(struct bufarea *, ufs_daddr_t, long);
extern struct bufarea *getdatablk(ufs_daddr_t, long);
extern struct inoinfo *getinoinfo(ufs1_ino_t);
extern struct ufs1_dinode  *getnextinode(ufs1_ino_t);
extern void		getpathname(char *, ufs1_ino_t, ufs1_ino_t);
extern struct ufs1_dinode  *ginode(ufs1_ino_t);
extern void		infohandler(int);
extern void		inocleanup(void);
extern void		inodirty(void);
extern struct inostat *inoinfo(ufs1_ino_t);
extern int		linkup(ufs1_ino_t, ufs1_ino_t, char *);
extern int		makeentry(ufs1_ino_t, ufs1_ino_t, char *);
extern void		panic(const char *, ...) __dead2 __printflike(1, 2);
extern void		pass1(void);
extern void		pass1b(void);
extern int		pass1check(struct inodesc *);
extern void		pass2(void);
extern void		pass3(void);
extern void		pass4(void);
extern int		pass4check(struct inodesc *);
extern void		pass5(void);
extern void		pfatal(const char *, ...) __printflike(1, 2);
extern void		pinode(ufs1_ino_t);
extern void		propagate(void);
extern void		pwarn(const char *, ...) __printflike(1, 2);
extern int		reply(char *);
extern void		setinodebuf(ufs1_ino_t);
extern int		setup(char *);
extern void		voidquit(int);
