#define MODULE_OWNERSHIP
#include "psfs.h"
#include<linux/buffer_head.h>

static struct kmem_cache *psfs_inode_cachep;
extern const struct inode_operations psfs_iops;
extern const struct file_operations psfs_fops;
static struct psfs_inode_info *psfs_alloc_inode(struct super_block *sb);
static struct dentry *psfs_lookup(struct inode * dir, struct dentry *dentry,
					struct nameidata *nd);
struct inode* psfs_get_inode(struct super_block *sb);
struct psfs_inode *psfs_read_inode(struct super_block *sb, unsigned int ino,
                               struct psfs_inode_info *psi);


/*
 *Convert PSFS inode to/from big endian format
 *to/from cpu format.
 */
static void psfs_inode_to_cpu(struct psfs_inode *inode);
static void psfs_inode_to_be(struct psfs_inode *inode);
#define PSFS_EXTENT_TO_CPU(psfs_extent)\
({\
psfs_extent->block_no = be32_to_cpu(psfs_extent->block_no);\
psfs_extent->length = be32_to_cpu(psfs_extent->length);\
})
/*
 *PSFS_EXTENT to big endian
 */
#define PSFS_EXTENT_TO_BE(psfs_extent)\
({\
psfs_extent->block_no = cpu_to_be32(psfs_extent->block_no);\
psfs_extent->length = cpu_to_be32(psfs_extent->length);\
})

void psfs_destroy_inode(struct inode *inode);

/*
 * This function attempts to allocate the requested extent.
 * @extent: The extent that needs to be initialized.
 * @nr_blocks: consecutive blocks requested for extent.
 * @bitmap: The block of FS containing bitmap.
 * @bmap_len: The length of the passed in bitmap.
 *
 * Return Value: -1 is an error state, 0 is a success state, 1 is a partial
 * success state returned. You must check the extent->nr_blocks to know
 * how many blocks long is the extent if the return state is 1.
 *
 * The function will not attempt to move over to next bitmap if it can't
 * fulfill the request entirely. So return value must be checked by caller.
 *
 * Larger extents maybe allocated by maintaining 2 extents, one will be
 * modified by this code the other external. However appropriate locking 
 * is the responsibility of the caller.
 * */
static int alloc_psfs_extent(struct psfs_extent *extent, int64_t nr_blocks,
                                char *bitmap, int32_t bmap_len)
{
        int64_t blocks_alloced = 0;
        int32_t start_block = -1;
        while(blocks_alloced<nr_blocks)
        {
                if (start_block < 0) {
                        start_block = alloc_bmap(bitmap,bmap_len);
                        if (start_block < 0)
                                return -1;
                }
                else if (alloc_bmap(bitmap,bmap_len) < 0)
                        break;
                blocks_alloced++;
        }
        extent->block_no = start_block;
        extent->length = blocks_alloced;
        return !(blocks_alloced==nr_blocks);
}

/*
 *Given block bitmap allocate an indirect extent.
 *@nr_blocks: number of blocks to allocate.
 *@bitmap: the block bitmap to use,
 *@bmap_len: length of bitmap.
 *@block_size: size of the block_data in bytes.
 *@block_no_offset: number of blocks to add to the allocated extent.
 *@block_data: the block to use for setting these extents.
 *
 *To use this function, use alloc_psfs_extent first on the indirect extent
 *pointer given to this function. Make sure you give a lower value of
 *nr_blocks while allocating the indirect extent itself.
 *
 *This function is a helper to allocate further psfs extents within the 
 *indirect extent. IT ASSUMES that indirect extent itself has been initialized
 *.
 *Any extent which is not allocated will have its length as 0.
 *Now you give this pointer and the rest of parameters
 *
 *Returns number of blocks allocated. In case this function wasn't able to
 *allocate call this function again but with a new block bitmap.
 *-1 is returned if there's was no extent found free in the block
 *provided. The caller must check return value and take appropriate action
 *either to give a new bitmap or give a new block to read psfs_extents from.
 */
static int alloc_psfs_extent_indirect(int64_t nr_blocks, char *bitmap,
				int32_t bmap_len,const int32_t block_size,
				int32_t block_no_offset,char *block
				)
{
	/*We scan the indirect offset for the first
	 *entry whose extent's length is 0.
	 *Scanning is done for all possible extents in the FS block size.
	 */
	 int64_t blocks_alloced=0;
	 int8_t found_free = -1;
	 const int32_t extent_per_block = sizeof(struct psfs_extent)/block_size;
	 struct psfs_extent *direct_extent = NULL;
	 if (block) {
	 	int32_t i = 0;
		for (i=0;i<extent_per_block && 
				blocks_alloced < nr_blocks
					;i++) {
			direct_extent = ((struct psfs_extent*)block)+i;
			if (direct_extent->length == 0) {
				found_free=1;
				if (alloc_psfs_extent(direct_extent,
						nr_blocks, bitmap,
						bmap_len) < 0 )
					break;
				blocks_alloced += direct_extent->length;
				direct_extent->block_no+=block_no_offset;
			}
		}
	 }
return (found_free==1?blocks_alloced:found_free);
}

/*
 *The bitmap must correspond to the one which holds the blocks
 *for this extent. This can be calculated as
 *(extent->block_inode/bits_per_block.) which will be relative
 *to the start of the beginning of block bitmap.
 *
 *If there's such an extent whose extent spans across more than 1
 *bitmap then extent->length will be non-zero and the caller must
 *call this function again with the new bitmap.
 *
 *After this function is over extent->block_no will be at the new
 *block_no which the caller can use to identify the newer bitmap
 *where following blocks will be accounted for.
 *
 *Returns: the total number of blocks freed.
 */
static int32_t free_psfs_extent(char *bitmap,int32_t bmap_len,
				struct psfs_extent *extent)
{
	__u64 which_byte = extent->block_no/8;
	__u32 start_block = extent->block_no; /*Old value of block_no*/
	while(which_byte < bmap_len && extent->length > 0) {
		/*
		 *First find the bitmap block of this,
		 *extent's starting block.
		 */
		int32_t bit_pos = extent->block_no%8;
		for( ;bit_pos < 8 && extent->length > 0;bit_pos++)
		{
			bitmap[which_byte]|=~(1<<(bit_pos%8));
			extent->block_no++;
			extent->length--;
		}
		which_byte++;
	}	
return (int32_t)(extent->block_no - start_block);
}

/*
 *This function frees a block of the indirect extent.
 *
 *@extent :the extent from which we need to free block.
 *@block_size: size of file system block size in bytes.
 *@bmap: the bitmap where the blocks of the direct extent are set.
 *@bmap_len: the length of the bitmap.
 *@block : the extent block to be freed.
 *@get_bitmap: function to be used if bitmap is walked over completely.
 */
static int32_t free_psfs_extent_indirect_block
				(struct psfs_extent *extent,
				const int32_t block_size,
				char *bmap, int32_t bmap_len,
				char *block,
				char* (*get_bitmap)(int32_t which_block))
{
	const int32_t nr_extents_per_block = sizeof(struct psfs_extent)/block_size;
	char *bmap_to_use;
	int32_t i=0;
	for ( i=0;i < nr_extents_per_block; i++) {
		struct psfs_extent *direct_extent = 
				( (struct psfs_extent*)block)+i;
		bmap_to_use = bmap;
free_extent:
		free_psfs_extent(bmap_to_use,bmap_len,direct_extent);
		if (direct_extent->length != 0)
		{
			bmap_to_use = get_bitmap(direct_extent->block_no);
			goto free_extent;
		}
	}
}

static void psfs_inode_to_cpu(struct psfs_inode *inode)
{
	int nr_direct_extent = PSFS_NR_DIRECT_EXTENTS;
	while (--nr_direct_extent>=0) {
		struct psfs_extent *extent = &inode->psfs_extent[nr_direct_extent];
		PSFS_EXTENT_TO_CPU(extent);
	}
	inode->size = be64_to_cpu(inode->size);
	inode->indirect_extent = be32_to_cpu(inode->indirect_extent);
	inode->double_indirect_extent = be32_to_cpu(inode->double_indirect_extent);
	inode->triple_indirect_extent = be32_to_cpu(inode->triple_indirect_extent);
	inode->a_time = be32_to_cpu(inode->a_time);
	inode->m_time = be32_to_cpu(inode->m_time);
	inode->c_time = be32_to_cpu(inode->c_time);
	inode->inode_nr = be32_to_cpu(inode->inode_nr);
	inode->ext_flags = be32_to_cpu(inode->ext_flags);
	inode->type = be32_to_cpu(inode->type);
	inode->flags = be16_to_cpu(inode->flags);
	inode->owner = be16_to_cpu(inode->owner);
}

static void psfs_inode_to_be(struct psfs_inode *inode)
{
	int nr_direct_extent = PSFS_NR_DIRECT_EXTENTS;
	while (--nr_direct_extent>=0) {
		struct psfs_extent *extent = &inode->psfs_extent[nr_direct_extent];
		PSFS_EXTENT_TO_CPU(extent);
	}
	inode->size = cpu_to_be64(inode->size);
	inode->indirect_extent = cpu_to_be32(inode->indirect_extent);
	inode->double_indirect_extent = cpu_to_be32(inode->double_indirect_extent);
	inode->triple_indirect_extent = cpu_to_be32(inode->triple_indirect_extent);
	inode->a_time = cpu_to_be32(inode->a_time);
	inode->m_time = cpu_to_be32(inode->m_time);
	inode->c_time = cpu_to_be32(inode->c_time);
	inode->inode_nr = cpu_to_be32(inode->inode_nr);
	inode->ext_flags = cpu_to_be32(inode->ext_flags);
	inode->type = cpu_to_be32(inode->type);
	inode->flags = cpu_to_be16(inode->flags);
	inode->owner = cpu_to_be16(inode->owner);
}

static struct dentry *psfs_lookup(struct inode * dir, struct dentry *dentry, struct nameidata *nd)
{
        printk("i am in function %s\n",__FUNCTION__);
        return NULL;
}

void psfs_destroy_inode(struct inode *inode)
{
        kmem_cache_free(psfs_inode_cachep,container_of(inode,struct psfs_inode_info,vfs_inode));
}

const struct inode_operations psfs_iops = {
        .lookup = psfs_lookup,
//	.create = psfs_create,
};

static inline struct psfs_inode_info *psfs_alloc_inode(struct super_block *sb)
{      
        PSFS_DBG_NONE();
	return((struct  psfs_inode_info*)kmem_cache_alloc(psfs_inode_cachep, GFP_KERNEL));
}

static void init_once(void *object)
{
	struct psfs_inode_info * psi = (struct psfs_inode_info *)object;
        PSFS_DBG_NONE();
        inode_init_once(&psi->vfs_inode);
        return;
}
int init_inodecache(void)
{
        PSFS_DBG_NONE();
	psfs_inode_cachep = kmem_cache_create("psfs_inode_cache",
                                                sizeof(struct psfs_inode_info),
                                                0, (SLAB_RECLAIM_ACCOUNT|SLAB_MEM_SPREAD),
                                                init_once);
        if(!psfs_inode_cachep)
                return -ENOMEM;
        return 0;

}

void destroy_inode_cache(void)
{
        PSFS_DBG_NONE();
	kmem_cache_destroy(psfs_inode_cachep);
}

struct psfs_inode *psfs_read_inode(struct super_block *sb, unsigned int ino,
                               struct psfs_inode_info *psi)
{    
        struct buffer_head *bh;
        __u64 block;
        __u32 inodes_per_block;
	PSFS_DBG_NONE(); 
        inodes_per_block = sb->s_blocksize/(sizeof(struct psfs_inode));
        block = ino/inodes_per_block;
        bh = sb_bread(sb, block + 1);
        if (!bh) {
                return NULL;
        }
#if 1
	{
		int i = 0;
		for(;i<sizeof(struct psfs_inode);i++)
		{
			printk (KERN_INFO "=0x%x  ",bh->b_data[i]);
			if( (i % 0x10) == 0 )
				printk(KERN_INFO "\n");
		}
		printk("Buffer head is at %p and pointer is at %p\n",bh->b_data,
			(((struct psfs_inode *)(bh->b_data)) + (ino-(block*inodes_per_block))));
	}
#endif
	/*
	 *FIXME: 
	 * Change this when using bio istead of bread. This only works
	 * when blocksize<=PAGE_SIZE. blocksize must be a multiple of
	 * KERNEL_SECTOR_SIZE=512.
	 */
	memcpy(&psi->psfs_inode,(((struct psfs_inode *)(bh->b_data)) + (ino-(block*inodes_per_block))),sizeof(struct psfs_inode));
	psfs_inode_to_cpu(&psi->psfs_inode);
        psi->vfs_inode.i_size = psi->psfs_inode.size;
        printk(KERN_INFO PSFS_DBG_VAR(" = %x\n",psi->psfs_inode.flags));
        printk(KERN_INFO PSFS_DBG_VAR("size = %llx\n",psi->psfs_inode.size));
	brelse(bh);
	return (&psi->psfs_inode);
}

struct inode *psfs_get_inode(struct super_block *sb)
{
        struct psfs_inode_info *psi;
	psi = psfs_alloc_inode(sb);
	printk(KERN_INFO PSFS_DBG_VAR(" = %p\n",psi));
        if(!psi)
                return ERR_PTR(-ENOMEM);
/*      psi->vfs_inode.i_sb = sb;   here is the culprit set the super block in the inode because inode_int_always is not called upto this point
        if( !psfs_read_inode(psi->vfs_inode.i_sb,PSFS_ROOT_INODE,psi))
        {
                printk(KERN_EMERG " cant read raw inode from disk\n");
                return ERR_PTR(-ENOMEM);
        }
	psfs_inode_to_cpu(&psi->psfs_inode);
	psi->vfs_inode.i_size = psi->psfs_inode.size;
	printk(KERN_INFO PSFS_DBG_VAR(" = %x\n",psi->psfs_inode.flags));	
	printk(KERN_INFO PSFS_DBG_VAR("size = %llx\n",psi->psfs_inode.size));*/
        return &psi->vfs_inode;
}
	
static int psfs_create(struct inode *parent_inode ,struct dentry *dentry,int mode,struct nameidata *nadata)
{
	struct buffer_head *inode_bmp_bh;
	struct buffer_head *data_bmp_bh;
	struct psfs_inode_info *psi;
	__u32 ino =0;
	struct inode *inode = new_inode(parent_inode->i_sb);
	insert_inode_hash(inode);
	if(!inode)
	{
		PSFS_DBG_MSG("could not allocate inode from inode cache");
		return -ENOMEM; 
	}
	psi = PSFS_I(inode);
	inode_bmp_bh = sb_bread(parent_inode->i_sb,psfs_inode_bmp_block);
	ino = alloc_bmap((char *)inode_bmp_bh,parent_inode->i_sb->s_blocksize);
	if(!ino)
	{
		PSFS_DBG_MSG("could not allocate inode no");
		return -ENOSPC;
	}
	inode->i_ino = ino;
	inode->i_mode = mode;
	inode->i_mtime = inode->i_atime = inode->i_ctime = CURRENT_TIME_SEC;
	inode->i_fop = &psfs_fops;
	inode->i_op = &psfs_iops;
	mark_inode_dirty(inode);
        mark_buffer_dirty(inode_bmp_bh);
	sync_dirty_buffer(inode_bmp_bh);
}
	
	
	
		
