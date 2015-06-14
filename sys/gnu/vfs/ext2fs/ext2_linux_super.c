/*
 * Copyright (c) 2015 The DragonFly Project.  All rights reserved.
 */
/*
 *  linux/fs/ext2/super.c
 *
 * Copyright (C) 1992, 1993, 1994, 1995
 * Remy Card (card@masi.ibp.fr)
 * Laboratoire MASI - Institut Blaise Pascal
 * Universite Pierre et Marie Curie (Paris VI)
 *
 *  from
 *
 *  linux/fs/minix/inode.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/mount.h>
#include <sys/vnode.h>

#include "dir.h"
#include "dinode.h"
#include "quota.h"
#include "ext2_fs.h"
#include "ext2_fs_sb.h"
#include "ext2_mount.h"
#include "ext2_extern.h"

/*
 * Checks that the data in the descriptor blocks make sense.
 */
int
ext2_check_descriptors(struct ext2_sb_info *sb)
{
        int i;
        int desc_block = 0;
        unsigned long block = sb->s_es->s_first_data_block;
        struct ext2_group_desc *gdp = NULL;

        /* ext2_debug ("Checking group descriptors"); */

        for (i = 0; i < sb->s_groups_count; i++)
        {
		/* examine next descriptor block */
                if ((i % EXT2_DESC_PER_BLOCK(sb)) == 0)
                        gdp = (struct ext2_group_desc *)
				sb->s_group_desc[desc_block++]->b_data;
                if (gdp->bg_block_bitmap < block ||
                    gdp->bg_block_bitmap >= block + EXT2_BLOCKS_PER_GROUP(sb))
                {
                        kprintf ("ext2_check_descriptors: "
                                    "Block bitmap for group %d"
                                    " not in group (block %lu)!\n",
                                    i, (unsigned long) gdp->bg_block_bitmap);
                        return 0;
                }
                if (gdp->bg_inode_bitmap < block ||
                    gdp->bg_inode_bitmap >= block + EXT2_BLOCKS_PER_GROUP(sb))
                {
                        kprintf ("ext2_check_descriptors: "
                                    "Inode bitmap for group %d"
                                    " not in group (block %lu)!\n",
                                    i, (unsigned long) gdp->bg_inode_bitmap);
                        return 0;
                }
                if (gdp->bg_inode_table < block ||
                    gdp->bg_inode_table + sb->s_itb_per_group >=
                    block + EXT2_BLOCKS_PER_GROUP(sb))
                {
                        kprintf ("ext2_check_descriptors: "
                                    "Inode table for group %d"
                                    " not in group (block %lu)!\n",
                                    i, (unsigned long) gdp->bg_inode_table);
                        return 0;
                }
                block += EXT2_BLOCKS_PER_GROUP(sb);
                gdp++;
        }
        return 1;
}

/*
 * Get file system statistics.
 */
int
ext2_statfs(struct mount *mp, struct statfs *sbp, struct ucred *cred)
{
        unsigned long overhead;
	struct ext2_mount *ump;
	struct ext2_sb_info *fs;
	struct ext2_super_block *es;
	int i, nsb;

	ump = VFSTOEXT2(mp);
	fs = ump->um_e2fs;
	es = fs->s_es;

	if (es->s_magic != EXT2_SUPER_MAGIC)
		panic("ext2_statfs - magic number spoiled");

	/*
	 * Compute the overhead (FS structures)
	 */
	if (es->s_feature_ro_compat & EXT2_FEATURE_RO_COMPAT_SPARSE_SUPER) {
		nsb = 0;
		for (i = 0 ; i < fs->s_groups_count; i++)
			if (ext2_group_sparse(i))
				nsb++;
	} else
		nsb = fs->s_groups_count;
	overhead = es->s_first_data_block +
	    /* Superblocks and block group descriptors: */
	    nsb * (1 + fs->s_db_per_group) +
	    /* Inode bitmap, block bitmap, and inode table: */
	    fs->s_groups_count * (1 + 1 + fs->s_itb_per_group);

	sbp->f_bsize = EXT2_FRAG_SIZE(fs);
	sbp->f_iosize = EXT2_BLOCK_SIZE(fs);
	sbp->f_blocks = es->s_blocks_count - overhead;
	sbp->f_bfree = es->s_free_blocks_count;
	sbp->f_bavail = sbp->f_bfree - es->s_r_blocks_count;
	sbp->f_files = es->s_inodes_count;
	sbp->f_ffree = es->s_free_inodes_count;
	if (sbp != &mp->mnt_stat) {
		sbp->f_type = mp->mnt_vfc->vfc_typenum;
		bcopy((caddr_t)mp->mnt_stat.f_mntfromname,
			(caddr_t)&sbp->f_mntfromname[0], MNAMELEN);
	}
	return (0);
}
