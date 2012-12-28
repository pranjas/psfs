#ifndef __PSFS_H__
#define __PSFS_H__

#ifndef __USER__
#include "../include/common.h"
#define PACKED_STRUCT	__attribute__((packed))
#else

#ifndef _LINUX_FS_H
#include <sys/types.h>
#endif /*_LINUX_FS_H*/ /* Included like this to avoid redeclaration error.*/

#define __u32 u_int32_t
#define __u64 u_int64_t
#define __u16 u_int16_t
#define __u8  u_int8_t
#define PACKED_STRUCT
#endif /*__USER__*/

#define PSFS_NR_DIRECT_EXTENTS	12
#define PSFS_MAGIC		0x80837083 /*PSFS*/
#define PSFS_DEFAULT_BLKSIZE    (1<<12) /*4096*/
#define PSFS_DEFAULT_EXTENT_LEN (1<<2)  /*4 blocks*/
#define KERNEL_SECTOR_SIZE      (1<<9) /*512*/
#define PSFS_DBG_VAR(fmt,X)      #X fmt,X
#define PSFS_DBG_MSG(msg) 	printk(KERN_INFO msg \
				"In Function %s at line %d\n"\
				,__FUNCTION__,__LINE__)
#define PSFS_DBG_NONE()		PSFS_DBG_MSG("")
#define PSFS_DFLT_BLOCKSIZE 4096
#define PSFS_SUPERBLOCK     0
#define PSFS_ROOT_INODE     0
#define PSFS_INFO_VFS(psfs_info_ptr,member)\
psfs_info_ptr->vfs_inode.member
#define PSFS_INFO_PSFS_INODE(psfs_info_ptr,member)\
psfs_info_ptr->psfs_inode.member

/*
 * All the values on-disk are stored in big-endian.
 * */

/*
 * On-Disk structures
 * */
struct psfs_extent {
	__u32	block_no;
	__u32	length;
}PACKED_STRUCT;

/*
 * Root directory is special, the formatter will take care of it.
 * Other files/directories reside within it and are its contents hence the
 * filename is part of that content and not stored here.
 * */
struct psfs_inode {
	struct psfs_extent 	psfs_extent[PSFS_NR_DIRECT_EXTENTS]; /*96 bytes*/
	__u64			size;
	__u32			indirect_extent;/*100 bytes*/
	__u32			double_indirect_extent;/*104 bytes*/
	__u32			triple_indirect_extent;/*108 bytes*/
	__u32			a_time,c_time,m_time;/*112 bytes*/
	__u32			inode_nr;/*116 bytes*/
	__u32			ext_flags;
	__u32                   type;
	__u16			flags;/*118 bytes*/
	__u16			owner;/*120 bytes*/
}PACKED_STRUCT;

struct psfs_super_block {
	__u64 		psfs_nr_blocks;	/*Total number of blocks there can be*/
	__u64		psfs_nr_inodes; /*Total number of inodes there can be*/
	__u32		psfs_boot_block;/*Block number of boot block*/
	__u32		psfs_nr_boot_blocks;/*Number of boot blocks*/
	__u32		psfs_min_extent_length;/*Minimum extent size*/
	__u32		psfs_super_flags;
	__u32		psfs_magic;
	__u32		psfs_block_size;
}PACKED_STRUCT; /*40 bytes*/

/*
 * Extent allocation is done twice the size of last extent allocated. The
 * minimum extent length shall be for 0.4% of total blocks. This however can be
 * configured while formatting the file system. If you got more space than
 * you care for, it might be worth considering a larger default extent size.
 *
 * The indirect pointer  will point to a block containing extents. The double
 * indirect pointer shall point to a block containing block addresses of blocks
 * containing extents. The triple indirect pointer shall point to block address
 * for double indirect pointers.
 **/

/*
 * Directories get lesser extents,since they need to store the file names only
 * while files get larger extents. We need to experiment and see what extent
 * size suits us and then choose an appropriate default value. For now let's
 * just work with what we got.
 */

/*
 * All blocks are in filesystem block size. Hence right after the first block,
 * the next blocks comprises of inode bitmap blocks. So we don't save anything
 * in the on-disk super blocks structure rather than we just find out where to
 * read the information from. The next few blocks are the bitmaps for data
 * blocks so we know where to get these as well by using simple calculations.
 *
 * ---------------------------------------------------------------------------
 * |		|	|	|	|	|	|	|	|
 * |boot block	|IBMAP	|IBMAP	|BBMAP	|BBMAP	|BLOCK	|BLOCK	|BLOCK	|...
 * |		|	|	|	|	|	|	|	|
 * ---------------------------------------------------------------------------
 *
 * In case we would like our inodes to be expanded, we can use the remaining
 * space of the boot block and put some extra information over there to
 * identify where to get those extra inodes block bitmap. This might be done
 * later though... Right now all I want is to get this blady thing working..
 */

#define PSFS_FILENAME_LEN	255
#define PSFS_DIR		(1<<0)
#define PSFS_REG		(1<<1)
#define PSFS_LNK		(1<<2)	/*For now we really don't use this.*/
#define PSFS_MET		(1<<3)	/*Meta data inode. Will not be shown in lookup.*/
#define PSFS_NON_REM		(1<<4)	/*A non removable file/directory like root.*/
#define PSFS_ROOT_DIR		(1<<5) /*Root inode*/

/*
 * This defines the contents of a directory. The first 13 bytes would be there for all entries.
 * */
struct psfs_dir_entry {
	__u32	inode_nr;/*The inode number of this directory entry.*/
	__u16	flags;
	__u16	rec_len;
	__u8	name_len;
	unsigned char name[PSFS_FILENAME_LEN];
}PACKED_STRUCT;
#define PSFS_MIN_DIRENT_SIZE	(sizeof(struct psfs_dir_entry)-PSFS_FILENAME_LEN)

/*
 * In memory super block of psfs
 *
 */
#ifndef __USER__
struct psfs_inode_info {
	struct inode vfs_inode;
	struct psfs_inode psfs_inode;
	struct list_head bh_list;
        __u16 flags;
};
	
struct psfs_sb_info {
        struct psfs_super_block *s_ps;
	void *data_block_bmap;
	void *inode_block_bmap;
	void *inode_table;
        struct buffer_head *s_bh;
};
static inline struct psfs_inode_info *PSFS_I(struct inode *inode)
{
        return container_of(inode, struct psfs_inode_info, vfs_inode);
}
static inline struct psfs_sb_info *PSFS_SB(struct super_block *sb)
{
        return sb->s_fs_info;
}
struct bh_list {
	struct list_head list;
	struct buffer_head *bh;
};
extern int32_t alloc_bmap(char *bitmap,int32_t bmap_len);
extern int32_t free_bmap(char *bitmap,int32_t bmap_len,int32_t bit_no);
extern __u64 get_inode_bmp_block(struct psfs_sb_info *psbi,__u32 blocksize);
extern __u64 get_data_bmp_block(struct psfs_sb_info *psbi,__u32 blocksize);
extern __u64 psfs_inode_bmp_block;
extern __u64 psfs_data_bmp_block;
#endif /*__USER__*/
#endif /*__PSFS_H__*/
