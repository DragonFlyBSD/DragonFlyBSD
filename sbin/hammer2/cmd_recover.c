/*
 * Copyright (c) 2023 The DragonFly Project.  All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Matthew Dillon <dillon@dragonflybsd.org>
 * by Venkatesh Srinivas <vsrinivas@dragonflybsd.org>
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
 */
/*
 * hammer2 recover <devpath> <path> <destdir>
 *
 * Recover files from corrupted media, recover deleted files from good
 * media, generally ignoring the data structure topology outside of the
 * structures that hang off of inodes and directory entries.  Files are
 * validated during recovery and renamed to .corrupted when a version of
 * a file cannot be completely recovered.
 *
 * This is a "try to get everything we can out of the filesystem"
 * directive when you've done something terrible to the filesystem.
 * The resulting <destdir> tree cannot directly replace the lost topology
 * as many versions of the same file will be present, so filenames are
 * suffixed.
 *
 * <path> may be a relative file, directory, directory path, or absolute
 * (absolute is relative to the mount) file or directory path.
 *
 * For example, "hammer2 recover /dev/da0s1d /home/charlie /tmp/" will
 * recover all possible versions of the /home/charlie sub-tree.  If
 * you said "home/charlie" instead, then it would recover the same but
 * also include any sub-directory paths that match home/charlie, not
 * just root paths.  If you want a specific file, then e.g. ".cshrc"
 * would recover every single .cshrc that can be found on the media.
 *
 * The command checks ALL PFSs and snapshots.  Redundant files with the same
 * path are ignored.  You basically get everything that can possibly be
 * recovered from the media.
 */
#include "hammer2.h"

#define INUM_HSIZE	(1024*1024)
#define INUM_HMASK	(INUM_HSIZE - 1)

#include <openssl/sha.h>

typedef struct dirent_entry {
	struct dirent_entry *next;
	char		*filename;
	size_t		len;
	hammer2_key_t	inum;
	hammer2_off_t	bref_off;
} dirent_entry_t;

typedef struct inode_entry {
	struct inode_entry *next;
	struct inode_entry *first_parent;
	dirent_entry_t	*first_dirent;
	hammer2_off_t	data_off;
	hammer2_key_t	inum;
	hammer2_inode_data_t inode;
	long		encounters;
	char		*link_file_path;
} inode_entry_t;

typedef struct topology_inode {
	struct topology_inode *next;
	struct inode_entry *inode;
} topology_inode_t;

typedef struct topology_entry {
	struct topology_entry *next;
	char		*path;
	long		iterator;
	topology_inode_t *inodes;
} topology_entry_t;

static dirent_entry_t **DirHash;
static inode_entry_t **InodeHash;
static topology_entry_t **TopologyHash;

/*static void resolve_topology(void);*/
static int check_filename(hammer2_blockref_t *bref,
			const char *filename, char *buf, size_t flen);
//static void enter_dirent(hammer2_blockref_t *bref, hammer2_off_t loff,
//			const char *filename, size_t flen);
static topology_entry_t *enter_topology(const char *path);
static int topology_check_duplicate_inode(topology_entry_t *topo,
			inode_entry_t *iscan);
static void enter_inode(hammer2_blockref_t *bref);
static void enter_inode_untested(hammer2_inode_data_t *ip, hammer2_off_t loff);
static inode_entry_t *find_first_inode(hammer2_key_t inum);
/*static dirent_entry_t *find_first_dirent(hammer2_key_t inum);*/
static void dump_tree(inode_entry_t *iscan, const char *dest,
			const char *remain, int depth, int isafile);
static int dump_inum_file(inode_entry_t *iscan, const char *path);
static int dump_inum_softlink(hammer2_inode_data_t *inode, const char *path);
static int dump_dir_data(const char *dest, const char *remain,
			hammer2_blockref_t *base, int count,
			int depth, int isafile);
static int dump_file_data(int wfd, hammer2_off_t fsize,
			hammer2_blockref_t *bref, int count);
static int validate_crc(hammer2_blockref_t *bref, void *data, size_t bytes);
static uint32_t hammer2_to_unix_xid(const uuid_t *uuid);

static long InodeCount;
static long MediaBytes;
static int StrictMode = 1;

#define INODES_PER_BLOCK	\
	(sizeof(union hammer2_media_data) / sizeof(hammer2_inode_data_t))
#define REPINODEDEPTH		256

/*
 * Recover the specified file.
 *
 * Basically do a raw scan of the drive image looking for directory entries
 * and inodes.  Index all inodes found, including copies, and filter
 * directory entries for the requested filename to locate inode numbers.
 *
 * All copies that are located are written to destdir with a suffix .00001,
 * .00002, etc.
 */
int
cmd_recover(const char *devpath, const char *pathname,
	    const char *destdir, int strict, int isafile)
{
	hammer2_media_data_t data;
	hammer2_volume_t *vol;
	hammer2_off_t loff;
	hammer2_off_t poff;
	size_t i;

	StrictMode = strict;
	hammer2_init_volumes(devpath, 1);
	DirHash = calloc(INUM_HSIZE, sizeof(dirent_entry_t *));
	InodeHash = calloc(INUM_HSIZE, sizeof(inode_entry_t *));
	TopologyHash = calloc(INUM_HSIZE, sizeof(topology_entry_t *));

	/*
	 * Media Pass
	 *
	 * Look for blockrefs that point to inodes.  The blockrefs could
	 * be bogus since we aren't validating them, but the combination
	 * of a CRC that matches the inode content is fairly robust in
	 * finding actual inodes.
	 *
	 * We also enter unvalidated inodes for inode #1 (PFS roots),
	 * because there might not be any blockrefs pointing to some of
	 * them.  We need these to be able to locate directory entries
	 * under the roots.
	 *
	 * At the moment we do not try to enter unvalidated directory
	 * entries, since this will result in a massive number of false
	 * hits
	 */
	printf("MEDIA PASS\n");

	loff = 0;
	while ((vol = hammer2_get_volume(loff)) != NULL) {
		int fd;
		int xdisp;
		hammer2_off_t vol_size;

		fd = vol->fd;
		poff = loff - vol->offset;
		vol_size = lseek(fd, 0L, SEEK_END);
		xdisp = 0;

		while (poff < vol->size) {
			if (pread(fd, &data, sizeof(data), poff) !=
			    sizeof(data))
			{
				/* try to skip possible I/O error */
				poff += sizeof(data);
				continue;
			}
			for (i = 0; i < HAMMER2_IND_COUNT_MAX; ++i) {
				hammer2_blockref_t *bref;
				// char filename_buf[1024+1];

				bref = &data.npdata[i];

				/*
				 * Found a possible inode
				 */
				switch(bref->type) {
				case HAMMER2_BREF_TYPE_INODE:
					enter_inode(bref);
					break;
				case HAMMER2_BREF_TYPE_DIRENT:
					/*
					 * Go overboard and try to index
					 * anything that looks like a
					 * directory entry.  This might find
					 * entries whos inodes are no longer
					 * available, but will also generate
					 * a lot of false files.
					 */
#if 0
					if (filename &&
					    flen != bref->embed.dirent.namlen)
					{
						/* single-file shortcut */
						break;
					}
					if (check_filename(bref, filename,
						   filename_buf,
						   bref->embed.dirent.namlen))
					{
						enter_dirent(bref,
						    poff + vol->offset +
						    (i *
						    sizeof(hammer2_blockref_t)),
						    filename_buf,
						    bref->embed.dirent.namlen);
					}
#endif
					break;
				default:
					break;
				}
			}

			/*
			 * Look for possible root inodes.  We generally can't
			 * find these by finding BREFs pointing to them because
			 * the BREFs often hang off the volume header.
			 *
			 * These "inodes" could be seriously corrupt, but if
			 * the bref tree is intact that is what we need to
			 * get top-level directory entries.
			 */
			for (i = 0; i < INODES_PER_BLOCK; ++i) {
				hammer2_inode_data_t *ip;

				ip = (void *)(data.buf + i * sizeof(*ip));
				if (ip->meta.inum == 1 &&
				    ip->meta.iparent == 0 &&
				    ip->meta.type ==
					HAMMER2_OBJTYPE_DIRECTORY &&
				    ip->meta.op_flags & HAMMER2_OPFLAG_PFSROOT)
				{
					enter_inode_untested(ip,
						poff + vol->offset +
						(i * sizeof(*ip)));
				}
			}
			poff += sizeof(data);

			MediaBytes += sizeof(data);

			/*
			 * Update progress
			 */
			if (QuietOpt == 0 &&
			    (++xdisp == 32 || poff == vol->size - sizeof(data)))
			{
				xdisp = 0;
				printf("%ld inodes scanned, "
				       "media %6.2f/%-3.2fG\r",
					InodeCount,
					MediaBytes / 1e9,
					vol_size / 1e9);
				fflush(stdout);
			}
		}
		loff = vol->offset + vol->size;
	}

	/*
	 * Restoration Pass
	 *
	 * Run through available directory inodes, which allows us to locate
	 * and validate (crc check) their directory entry blockrefs and
	 * construct absolute or relative paths through a recursion.
	 *
	 * When an absolute path is obtained the search is anchored on a
	 * root inode.  When a relative path is obtained the search is
	 * unanchored and will find all matching sub-paths.  For example,
	 * if you look for ".cshrc" it will find ALL .cshrc's.  If you
	 * look for "fubar/.cshsrc" it will find ALL .cshrc's residing
	 * in a directory called fubar, however many there are.  But if
	 * you look for "/fubar/srcs" it will only find the sub-tree
	 * "/fubar/srcs" relative to PFS roots.
	 *
	 * We may not have indexed the PFS roots themselves, because they
	 * often hang off of the volume header and might not have COW'd
	 * references to them, so we use the "iparent" field in the inode
	 * to detect top-level directories under those roots.
	 */
	printf("\nInodes Scanned: %ld\n", InodeCount);
	printf("RESTORATION PASS\n");

	{
		int abspath = 0;

		/*
		 * Check for absolute path, else relative
		 */
		if (pathname[0] == '/') {
			abspath = 1;
			while (*pathname == '/')
				++pathname;
		}

		/*
		 * Run through all directory inodes to locate validated
		 * directory entries.  If an absolute path was specified
		 * we start at root inodes.
		 */
		for (i = 0; i < INUM_HSIZE; ++i) {
			inode_entry_t *iscan;

			for (iscan = InodeHash[i];
			     iscan;
			     iscan = iscan->next)
			{
				if (abspath && iscan->inode.meta.inum != 1)
					continue;
				if (iscan->inode.meta.type !=
				    HAMMER2_OBJTYPE_DIRECTORY)
				{
					continue;
				}
				dump_tree(iscan, destdir, pathname,
					  1, isafile);
			}
			if (QuietOpt == 0 && (i & 31) == 31) {
				printf("Progress %zd/%d\r", i, INUM_HSIZE);
				fflush(stdout);
			}
		}
		printf("\n");
	}

	printf("CLEANUP\n");

	/*
	 * Cleanup
	 */
	hammer2_cleanup_volumes();

	for (i = 0; i < INUM_HSIZE; ++i) {
		dirent_entry_t *dscan;
		inode_entry_t *iscan;
		topology_entry_t *top_scan;
		topology_inode_t *top_ino;

		while ((dscan = DirHash[i]) != NULL) {
			DirHash[i] = dscan->next;
			free(dscan->filename);
			free(dscan);
		}
		while ((iscan = InodeHash[i]) != NULL) {
			InodeHash[i] = iscan->next;
			free(iscan);
		}
		while ((top_scan = TopologyHash[i]) != NULL) {
			TopologyHash[i] = top_scan->next;
			while ((top_ino = top_scan->inodes) != NULL) {
				top_scan->inodes = top_ino->next;
				free(top_ino);
			}
			free(top_scan->path);
			free(top_scan);
		}
	}
	free(TopologyHash);
	free(DirHash);
	free(InodeHash);

	return 0;
}

/*
 * Check for a matching filename, Directory entries can directly-embed
 * filenames <= 64 bytes.  Otherwise the directory entry has a data
 * reference to the location of the filename.
 *
 * If filename is NULL, check for a valid filename, and copy it into buf.
 */
static int
check_filename(hammer2_blockref_t *bref, const char *filename, char *buf,
	       size_t flen)
{
	/* filename too long */
	if (flen > 1024)
		return 0;

	if (flen <= 64) {
		/*
		 * Filename is embedded in bref
		 */
		if (buf)
			bcopy(bref->check.buf, buf, flen);
		buf[flen] = 0;
		if (filename == NULL)
			return 1;
		if (bcmp(filename, bref->check.buf, flen) == 0)
			return 1;
	} else {
		/*
		 * Filename requires media access
		 */
		hammer2_media_data_t data;
		hammer2_volume_t *vol;
		hammer2_off_t poff;
		hammer2_off_t psize;
		int vfd;

		/*
		 * bref must represent a data reference to a 1KB block or
		 * smaller.
		 */
		if ((bref->data_off & 0x1F) == 0 ||
		    (bref->data_off & 0x1F) > 10)
		{
			return 0;
		}

		/*
		 * Indirect block containing filename must be large enough
		 * to contain the filename.
		 */
		psize = 1 << (bref->data_off & 0x1F);
		if (flen > psize)
			return 0;

		/*
		 * In strict mode we disallow bref's set to HAMMER2_CHECK_NONE
		 * or HAMMER2_CHECK_DISABLED.  Do this check before burning
		 * time on an I/O.
		 */
		if (StrictMode) {
			if (HAMMER2_DEC_CHECK(bref->methods) ==
				HAMMER2_CHECK_NONE ||
			    HAMMER2_DEC_CHECK(bref->methods) ==
				HAMMER2_CHECK_DISABLED)
			{
				return 0;
			}
		}

		/*
		 * Read the data, check CRC and such
		 */
		vol = hammer2_get_volume(bref->data_off);
		if (vol == NULL)
			return 0;

		vfd = vol->fd;
		poff = (bref->data_off - vol->offset) & ~0x1FL;
		if (pread(vfd, &data, psize, poff) != (ssize_t)psize)
			return 0;

		if (validate_crc(bref, &data, psize) == 0)
			return 0;

		if (buf)
			bcopy(data.buf, buf, flen);
		buf[flen] = 0;
		if (filename == NULL)
			return 1;
		if (bcmp(filename, data.buf, flen) == 0)
			return 1;
	}
	return 0;
}

#if 0
static void
enter_dirent(hammer2_blockref_t *bref, hammer2_off_t loff,
	     const char *filename, size_t flen)
{
	hammer2_key_t inum = bref->embed.dirent.inum;
	dirent_entry_t *entry;
	uint32_t hv = (inum ^ (inum >> 16)) & INUM_HMASK;

	for (entry = DirHash[hv]; entry; entry = entry->next) {
		if (entry->inum == inum &&
		    entry->len == flen &&
		    bcmp(entry->filename, filename, flen) == 0)
		{
			return;
		}
	}
	entry = malloc(sizeof(*entry));
	bzero(entry, sizeof(*entry));
	entry->inum = inum;
	entry->next = DirHash[hv];
	entry->filename = malloc(flen + 1);
	entry->len = flen;
	entry->bref_off = loff;
	bcopy(filename, entry->filename, flen);
	entry->filename[flen] = 0;
	DirHash[hv] = entry;
}
#endif

static topology_entry_t *
enter_topology(const char *path)
{
	topology_entry_t *topo;
	uint32_t hv = 0;
	size_t i;

	for (i = 0; path[i]; ++i)
		hv = (hv << 5) ^ path[i] ^ (hv >> 24);
	hv = (hv ^ (hv >> 16)) & INUM_HMASK;
	for (topo = TopologyHash[hv]; topo; topo = topo->next) {
		if (strcmp(path, topo->path) == 0)
			return topo;
	}
	topo = malloc(sizeof(*topo));
	bzero(topo, sizeof(*topo));

	topo->next = TopologyHash[hv];
	TopologyHash[hv] = topo;
	topo->path = strdup(path);
	topo->iterator = 1;

	return topo;
}

static int
topology_check_duplicate_inode(topology_entry_t *topo, inode_entry_t *inode)
{
	topology_inode_t *scan;

	for (scan = topo->inodes; scan; scan = scan->next) {
		if (scan->inode == inode)
			return 1;
	}
	scan = malloc(sizeof(*scan));
	bzero(scan, sizeof(*scan));
	scan->inode = inode;
	scan->next = topo->inodes;
	topo->inodes = scan;

	return 0;
}

/*
 * Dump the inode content to a file
 */
static void
enter_inode(hammer2_blockref_t *bref)
{
	uint32_t hv = (bref->key ^ (bref->key >> 16)) & INUM_HMASK;
	inode_entry_t *scan;
	hammer2_volume_t *vol;
	hammer2_off_t poff;
	hammer2_inode_data_t inode;
	int vfd;

	/*
	 * Ignore duplicate inodes.
	 */
	for (scan = InodeHash[hv]; scan; scan = scan->next) {
		if (bref->key == scan->inum &&
		    bref->data_off == scan->data_off)
		{
			return;
		}
	}

	/*
	 * Validate the potential blockref.  Note that this might not be a
	 * real blockref.  Don't trust anything, really.
	 */
	if ((1 << (bref->data_off & 0x1F)) != sizeof(inode))
		return;
	if ((bref->data_off & ~0x1FL & (sizeof(inode) - 1)) != 0)
		return;
	vol = hammer2_get_volume(bref->data_off);
	if (vol == NULL)
		return;

	vfd = vol->fd;
	poff = (bref->data_off - vol->offset) & ~0x1FL;
	if (pread(vfd, &inode, sizeof(inode), poff) != sizeof(inode))
		return;

	/*
	 * The blockref looks ok but the real test is whether the
	 * inode data it references passes the CRC check.  If it
	 * does, it is highly likely that we have a valid inode.
	 */
	if (validate_crc(bref, &inode, sizeof(inode)) == 0)
		return;
	if (inode.meta.inum != bref->key)
		return;

	scan = malloc(sizeof(*scan));
	bzero(scan, sizeof(*scan));

	scan->inum = bref->key;
	scan->data_off = bref->data_off;
	scan->inode = inode;
	scan->next = InodeHash[hv];
	InodeHash[hv] = scan;

	++InodeCount;
}

static void
enter_inode_untested(hammer2_inode_data_t *ip, hammer2_off_t loff)
{
	uint32_t hv = (ip->meta.inum ^ (ip->meta.inum >> 16)) & INUM_HMASK;
	inode_entry_t *scan;

	for (scan = InodeHash[hv]; scan; scan = scan->next) {
		if (ip->meta.inum == scan->inum &&
		    loff == scan->data_off)
		{
			return;
		}
	}

	scan = malloc(sizeof(*scan));
	bzero(scan, sizeof(*scan));

	scan->inum = ip->meta.inum;
	scan->data_off = loff;
	scan->inode = *ip;
	scan->next = InodeHash[hv];
	InodeHash[hv] = scan;

	++InodeCount;
}

static inode_entry_t *
find_first_inode(hammer2_key_t inum)
{
	inode_entry_t *entry;
	uint32_t hv = (inum ^ (inum >> 16)) & INUM_HMASK;

	for (entry = InodeHash[hv]; entry; entry = entry->next) {
		if (entry->inum == inum)
			return entry;
	}
	return NULL;
}

/*
 * Dump the specified inode
 */
static void
dump_tree(inode_entry_t *iscan, const char *dest, const char *remain,
	  int depth, int isafile)
{
	topology_entry_t *topo;
	struct stat st;
	char *path;

	/*
	 * Try to limit potential infinite loops
	 */
	if (depth > REPINODEDEPTH && iscan->encounters)
		return;

	/*
	 * Get rid of any dividing slashes
	 */
	while (*remain == '/')
		++remain;

	/*
	 * Create/lookup destination path (without the iterator), to acquire
	 * an iterator for different versions of the same file.
	 *
	 * Due to the COW mechanism, a lot of repeated snapshot-like
	 * directories may be encountered so we use the topology tree
	 * to weed out duplicates that attempt to use the same pathname.
	 *
	 * We also don't iterate copies of directories since this would
	 * create a major mess due to the many versions that might be
	 * laying around.  Directories use unextended names.
	 */
	topo = enter_topology(dest);

	if (topology_check_duplicate_inode(topo, iscan)) {
		return;
	}

	switch(iscan->inode.meta.type) {
	case HAMMER2_OBJTYPE_DIRECTORY:
		/*
		 * If we have exhausted the path and isafile is TRUE,
		 * stop.
		 */
		if (isafile && *remain == 0)
			return;

		/*
		 * Create / make usable the target directory.  Note that
		 * it might already exist.
		 */
		if (mkdir(dest, 0755) == 0)
			++iscan->encounters;
		if (stat(dest, &st) == 0) {
			if (st.st_flags)
				chflags(dest, 0);
			if ((st.st_mode & 0700) != 0700)
				chmod(dest, 0755);
		}

		/*
		 * Dump directory contents (scan the directory)
		 */
		(void)dump_dir_data(dest, remain,
				    &iscan->inode.u.blockset.blockref[0],
				    HAMMER2_SET_COUNT, depth, isafile);

		/*
		 * Final adjustment to directory inode
		 */
		{
			struct timeval tvs[2];

			tvs[0].tv_sec = iscan->inode.meta.atime / 1000000;
			tvs[0].tv_usec = iscan->inode.meta.atime % 1000000;
			tvs[1].tv_sec = iscan->inode.meta.mtime / 1000000;
			tvs[1].tv_usec = iscan->inode.meta.mtime % 1000000;

			if (lutimes(dest, tvs) < 0)
				perror("futimes");
			lchown(dest,
			       hammer2_to_unix_xid(&iscan->inode.meta.uid),
			       hammer2_to_unix_xid(&iscan->inode.meta.gid));

			lchmod(dest, iscan->inode.meta.mode);
			lchflags(dest, iscan->inode.meta.uflags);
		}
		break;
	case HAMMER2_OBJTYPE_REGFILE:
		/*
		 * If no more path to match, dump the file contents
		 */
		if (*remain == 0) {
			asprintf(&path, "%s.%05ld", dest, topo->iterator);
			++topo->iterator;

			if (stat(path, &st) == 0) {
				if (st.st_flags)
					chflags(path, 0);
				if ((st.st_mode & 0600) != 0600)
					chmod(path, 0644);
			}
			++iscan->encounters;
			(void)dump_inum_file(iscan, path);
			free(path);
		}
		break;
	case HAMMER2_OBJTYPE_SOFTLINK:
		/*
		 * If no more path to match, dump the file contents
		 */
		if (*remain == 0) {
			asprintf(&path, "%s.%05ld", dest, topo->iterator);
			++topo->iterator;

			if (stat(path, &st) == 0) {
				if (st.st_flags)
					chflags(path, 0);
				if ((st.st_mode & 0600) != 0600)
					chmod(path, 0644);
			}
			(void)dump_inum_softlink(&iscan->inode, path);
			free(path);
		}
		break;
	}
}

/*
 * [re]create a regular file and attempt to restore the originanl perms,
 * modes, flags, times, uid, and gid if successful.
 *
 * If the data block recursion fails the file will be renamed
 * .corrupted.
 */
static int
dump_inum_file(inode_entry_t *iscan, const char *path1)
{
	hammer2_inode_data_t *inode = &iscan->inode;
	char *path2;
	int wfd;
	int res;

	/*
	 * If this specific inode has already been generated, try to
	 * hardlink it instead of regenerating the same file again.
	 */
	if (iscan->link_file_path) {
		if (link(iscan->link_file_path, path1) == 0)
			return 1;
		chflags(iscan->link_file_path, 0);
		chmod(iscan->link_file_path, 0600);
		if (link(iscan->link_file_path, path1) == 0) {
			chmod(iscan->link_file_path, inode->meta.mode);
			chflags(iscan->link_file_path, inode->meta.uflags);
			return 1;
		}
	}

	/*
	 * Cleanup potential flags and modes to allow us to write out a
	 * new file.
	 */
	chflags(path1, 0);
	chmod(path1, 0600);
	wfd = open(path1, O_RDWR|O_CREAT|O_TRUNC, 0600);

	if (inode->meta.op_flags & HAMMER2_OPFLAG_DIRECTDATA) {
		/*
		 * Direct data case
		 */
		if (inode->meta.size > 0 &&
		    inode->meta.size <= sizeof(inode->u.data))
		{
			write(wfd, inode->u.data, inode->meta.size);
		}
		res = 1;
	} else {
		/*
		 * file content, indirect blockrefs
		 */
		res = dump_file_data(wfd, inode->meta.size,
				     &inode->u.blockset.blockref[0],
				     HAMMER2_SET_COUNT);
	}

	/*
	 * On success, set perms, mtime, flags, etc
	 * On failure, rename file to .corrupted
	 */
	if (res) {
		struct timeval tvs[2];

		tvs[0].tv_sec = inode->meta.atime / 1000000;
		tvs[0].tv_usec = inode->meta.atime % 1000000;
		tvs[1].tv_sec = inode->meta.mtime / 1000000;
		tvs[1].tv_usec = inode->meta.mtime % 1000000;

		ftruncate(wfd, inode->meta.size);
		if (futimes(wfd, tvs) < 0)
			perror("futimes");
		fchown(wfd,
		       hammer2_to_unix_xid(&inode->meta.uid),
		       hammer2_to_unix_xid(&inode->meta.gid));

		fchmod(wfd, inode->meta.mode);
		fchflags(wfd, inode->meta.uflags);
		iscan->link_file_path = strdup(path1);
	} else {
		struct timeval tvs[2];

		tvs[0].tv_sec = inode->meta.atime / 1000000;
		tvs[0].tv_usec = inode->meta.atime % 1000000;
		tvs[1].tv_sec = inode->meta.mtime / 1000000;
		tvs[1].tv_usec = inode->meta.mtime % 1000000;

		ftruncate(wfd, inode->meta.size);
		if (futimes(wfd, tvs) < 0)
			perror("futimes");
		fchown(wfd,
		       hammer2_to_unix_xid(&inode->meta.uid),
		       hammer2_to_unix_xid(&inode->meta.gid));

		asprintf(&path2, "%s.corrupted", path1);
		rename(path1, path2);
		iscan->link_file_path = path2;
	}

	/*
	 * Cleanup
	 */
	close(wfd);

	return res;
}

static int
dump_inum_softlink(hammer2_inode_data_t *inode __unused,
		   const char *path __unused)
{
	return 0;
}

/*
 * Scan the directory for a match against the next component in
 * the (remain) path.
 */
static int
dump_dir_data(const char *dest, const char *remain,
	      hammer2_blockref_t *base, int count,
	      int depth, int isafile)
{
	hammer2_media_data_t data;
	hammer2_volume_t *vol;
	hammer2_off_t poff;
	hammer2_off_t psize;
	int res = 1;
	int rtmp;
	int n;
	size_t flen;

	/*
	 * Calculate length of next path component to match against.
	 * A length of zero matches against the entire remaining
	 * directory sub-tree.
	 */
	for (flen = 0; remain[flen] && remain[flen] != '/'; ++flen)
		;

	/*
	 * Scan the brefs associated with the directory
	 */
	for (n = 0; n < count; ++n) {
		hammer2_blockref_t *bref;
		char filename_buf[1024+1];

		bref = &base[n];

		if (bref->type == HAMMER2_BREF_TYPE_EMPTY)
			continue;

		vol = NULL;
		poff = 0;
		psize = 0;
		if (bref->data_off & 0x1F) {
			vol = hammer2_get_volume(bref->data_off);
			if (vol == NULL) {
				res = 0;
				continue;
			}
			poff = (bref->data_off - vol->offset) & ~0x1FL;
			psize = 1 << (bref->data_off & 0x1F);
			if (psize > sizeof(data)) {
				res = 0;
				continue;
			}
		}

		switch(bref->type) {
		case HAMMER2_BREF_TYPE_DIRENT:
			/*
			 * Match the path element or match all directory
			 * entries if we have exhausted (remain).
			 *
			 * Locate all matching inodes and continue the
			 * traversal.
			 */
			if ((flen == 0 &&
			    check_filename(bref, NULL, filename_buf,
					   bref->embed.dirent.namlen)) ||
			    (flen &&
			     flen == bref->embed.dirent.namlen &&
			     check_filename(bref, remain, filename_buf, flen)))
			{
				inode_entry_t *iscan;
				hammer2_key_t inum;
				char *path;

				inum = bref->embed.dirent.inum;

				asprintf(&path, "%s/%s", dest, filename_buf);
				iscan = find_first_inode(inum);
				while (iscan) {
					if (iscan->inum == inum &&
					    iscan->inode.meta.type ==
					    bref->embed.dirent.type)
					{
						dump_tree(iscan, path,
							  remain + flen,
							  depth + 1,
							  isafile);
					}
					iscan = iscan->next;
				}
				free(path);
			}
			break;
		case HAMMER2_BREF_TYPE_INDIRECT:
			if (psize == 0) {
				res = 0;
				break;
			}
			if (pread(vol->fd, &data, psize, poff) !=
			    (ssize_t)psize)
			{
				res = 0;
				break;
			}

			if (validate_crc(bref, &data, psize) == 0) {
				res = 0;
				break;
			}

			rtmp = dump_dir_data(dest, remain,
					  &data.npdata[0],
					  psize / sizeof(hammer2_blockref_t),
					  depth, isafile);
			if (res)
				res = rtmp;
			break;
		}
	}
	return res;
}

/*
 * Dumps the data records for an inode to the target file (wfd), returns
 * TRUE on success, FALSE if corruption was detected.
 */
static int
dump_file_data(int wfd, hammer2_off_t fsize,
	       hammer2_blockref_t *base, int count)
{
	hammer2_media_data_t data;
	hammer2_blockref_t *bref;
	hammer2_volume_t *vol;
	hammer2_off_t poff;
	hammer2_off_t psize;
	hammer2_off_t nsize;
	int res = 1;
	int rtmp;
	int n;

	for (n = 0; n < count; ++n) {
		char *dptr;
		int res2;

		bref = &base[n];

		if (bref->type == HAMMER2_BREF_TYPE_EMPTY ||
		    bref->data_off == 0)
		{
			continue;
		}
		vol = hammer2_get_volume(bref->data_off);
		if (vol == NULL)
			continue;
		if (bref->type == HAMMER2_BREF_TYPE_EMPTY ||
		    bref->data_off == 0)
		{
			continue;
		}

		poff = (bref->data_off - vol->offset) & ~0x1FL;
		psize = 1 << (bref->data_off & 0x1F);
		if (psize > sizeof(data)) {
			res = 0;
			continue;
		}

		if (pread(vol->fd, &data, psize, poff) != (ssize_t)psize) {
			res = 0;
			continue;
		}

		if (validate_crc(bref, &data, psize) == 0) {
			res = 0;
			continue;
		}

		switch(bref->type) {
		case HAMMER2_BREF_TYPE_DATA:
			dptr = (void *)&data;

			nsize = 1L << bref->keybits;
			if (nsize > sizeof(data)) {
				res = 0;
				break;
			}

			switch (HAMMER2_DEC_COMP(bref->methods)) {
			case HAMMER2_COMP_LZ4:
				dptr = hammer2_decompress_LZ4(dptr, psize,
							      nsize, &res2);
				if (res)
					res = res2;
				psize = nsize;
				break;
			case HAMMER2_COMP_ZLIB:
				dptr = hammer2_decompress_ZLIB(dptr, psize,
							       nsize, &res2);
				if (res)
					res = res2;
				psize = nsize;
				break;
			case HAMMER2_COMP_NONE:
			default:
				/* leave in current form */
				break;
			}

			if (bref->key + psize > fsize)
				pwrite(wfd, dptr, fsize - bref->key,
				       bref->key);
			else
				pwrite(wfd, dptr, psize, bref->key);
			break;
		case HAMMER2_BREF_TYPE_INDIRECT:
			rtmp = dump_file_data(wfd, fsize,
					  &data.npdata[0],
					  psize / sizeof(hammer2_blockref_t));
			if (res)
				res = rtmp;
			break;
		}
	}
	return res;
}

/*
 * Validate the bref data target.  The recovery scan will often attempt to
 * validate invalid elements, so don't spew errors to stderr on failure.
 */
static int
validate_crc(hammer2_blockref_t *bref, void *data, size_t bytes)
{
	uint32_t cv;
	uint64_t cv64;
	SHA256_CTX hash_ctx;
	int success = 0;
	union {
		uint8_t digest[SHA256_DIGEST_LENGTH];
		uint64_t digest64[SHA256_DIGEST_LENGTH/8];
	} u;

	switch(HAMMER2_DEC_CHECK(bref->methods)) {
	case HAMMER2_CHECK_NONE:
		if (StrictMode == 0)
			success = 1;
		break;
	case HAMMER2_CHECK_DISABLED:
		if (StrictMode == 0)
			success = 1;
		break;
	case HAMMER2_CHECK_ISCSI32:
		cv = hammer2_icrc32(data, bytes);
		if (bref->check.iscsi32.value == cv) {
			success = 1;
		}
		break;
	case HAMMER2_CHECK_XXHASH64:
		cv64 = XXH64(data, bytes, XXH_HAMMER2_SEED);
		if (bref->check.xxhash64.value == cv64) {
			success = 1;
		}
		break;
	case HAMMER2_CHECK_SHA192:
		SHA256_Init(&hash_ctx);
		SHA256_Update(&hash_ctx, data, bytes);
		SHA256_Final(u.digest, &hash_ctx);
		u.digest64[2] ^= u.digest64[3];
		if (memcmp(u.digest, bref->check.sha192.data,
			   sizeof(bref->check.sha192.data)) == 0)
		{
			success = 1;
		}
		break;
	case HAMMER2_CHECK_FREEMAP:
		cv = hammer2_icrc32(data, bytes);
		if (bref->check.freemap.icrc32 == cv) {
			success = 1;
		}
		break;
	}
	return success;
}

/*
 * Convert a hammer2 uuid to a uid or gid.
 */
static uint32_t
hammer2_to_unix_xid(const uuid_t *uuid)
{
        return(*(const uint32_t *)&uuid->node[2]);
}
