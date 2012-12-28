#define MODULE_OWNERSHIP
#include "psfs.h"
#include<linux/buffer_head.h>

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,99)
int psfs_get_sb(struct file_system_type *fs_type, int flags,
                const char *dev_name, void *data, struct vfsmount *mnt);
#else
struct dentry *psfs_get_sb(struct file_system_type *fs_type, int flags,
                const char *dev_name, void *data);
#endif /*LINUX_VERSION_CODE*/
extern void psfs_destroy_inode(struct inode *inode);
extern struct inode *psfs_get_inode(struct super_block *sb);
extern const struct inode_operations psfs_iops;
extern const struct file_operations psfs_fops;
extern struct psfs_inode_info *psfs_read_inode(struct super_block *sb, unsigned int ino,
                               struct psfs_inode_info *psi);
static void psfs_super_block_to_cpu(struct psfs_super_block *sb);
static void psfs_super_block_to_be(struct psfs_super_block *sb);
__u64 psfs_inode_bmp_block;
__u64 psfs_data_bmp_block;
/*
 * To convert from N bit to cpu use as MEMBER_TO_CPU(member,N).
 */
#define MEMBER_TO_CPU(member,size)\
({\
member = be##size##_to_cpu(member);\
})

#define MEMBER_TO_BE(member,size)\
({\
member = cpu_to_be##size(member);\
})

static void psfs_super_block_to_cpu(struct psfs_super_block *sb)
{
	MEMBER_TO_CPU(sb->psfs_nr_blocks,64);
	MEMBER_TO_CPU(sb->psfs_nr_inodes,64);
	MEMBER_TO_CPU(sb->psfs_boot_block,32);
	MEMBER_TO_CPU(sb->psfs_nr_boot_blocks,32);
	MEMBER_TO_CPU(sb->psfs_min_extent_length,32);
	MEMBER_TO_CPU(sb->psfs_super_flags,32);
	MEMBER_TO_CPU(sb->psfs_magic,32);
	MEMBER_TO_CPU(sb->psfs_block_size,32);
}
static void psfs_super_block_to_be(struct psfs_super_block *sb)
{
	MEMBER_TO_BE(sb->psfs_nr_blocks,64);
	MEMBER_TO_BE(sb->psfs_nr_inodes,64);
	MEMBER_TO_BE(sb->psfs_boot_block,32);
	MEMBER_TO_BE(sb->psfs_nr_boot_blocks,32);
	MEMBER_TO_BE(sb->psfs_min_extent_length,32);
	MEMBER_TO_BE(sb->psfs_super_flags,32);
	MEMBER_TO_BE(sb->psfs_magic,32);
	MEMBER_TO_BE(sb->psfs_block_size,32);
}
static const struct super_operations psfs_sops = {
        /*.write_inode   = psfs_write_inode,
        .delete_inode  = psfs_delete_inode,
        .put_super     = psfs_put_super,*/
	.statfs = simple_statfs,
        .destroy_inode = psfs_destroy_inode,
	.alloc_inode   =  psfs_get_inode	 
};

static int psfs_fill_super(struct super_block *sb, void *data, int silent)
{
        struct psfs_sb_info *psbi;
        struct psfs_super_block *ps;
        struct inode *root;
        struct buffer_head *bh = NULL;
	struct psfs_inode_info *psi=NULL;
	__u32 blocksize;
        psbi = kzalloc(sizeof(*psbi), GFP_KERNEL);
        if(!psbi)
                return -ENOMEM;
        sb->s_fs_info = psbi;
        blocksize = sb_min_blocksize(sb, PSFS_DFLT_BLOCKSIZE);
        bh = sb_bread(sb, PSFS_SUPERBLOCK);
        if(!bh) {
                printk("Unable to read superblock\n");
                goto fail;
        }
        ps = (struct psfs_super_block *)((char *)bh->b_data);
	psfs_super_block_to_cpu(ps);
        psbi->s_ps = ps; 
        psbi->s_bh = bh;
	sb->s_magic = ps->psfs_magic;
        if(sb->s_magic == (PSFS_MAGIC))
		printk (KERN_INFO " Yah found PSFS file system\n");
	else 
		goto cantfind_psfs;
	
	psfs_inode_bmp_block = get_inode_bmp_block(psbi,sb->s_blocksize);
	psfs_data_bmp_block = get_data_bmp_block(psbi,sb->s_blocksize);
	//psfs_inode_block = get_inode_block(psbi,sb->s_blocksize);
	printk(KERN_INFO PSFS_DBG_VAR("%llx \n",psfs_inode_bmp_block));
		
        sb->s_op = &psfs_sops;
        root = new_inode(sb);
	if (!root)
                goto cantfind_psfs;
	/* psfs_read_inode is now moved out from the psfs_get_inode function 
	 * to make psfs_get_inode function general for other call
	 */
	psi = PSFS_I(root);
	if( !psfs_read_inode(root->i_sb,PSFS_ROOT_INODE,psi))
        {
                printk(KERN_EMERG " cant read raw inode from disk\n");
                return (-ENOMEM);
        }
	psi->vfs_inode.i_fop = &psfs_fops;
	psi->vfs_inode.i_op = &psfs_iops;
	psi->vfs_inode.i_mode = S_IFDIR | 0755;
	psi->vfs_inode.i_mtime = psi->vfs_inode.i_ctime = psi->vfs_inode.i_atime = CURRENT_TIME_SEC;
	if (!S_ISDIR(psi->vfs_inode.i_mode)) {
                iput(root);
		printk(KERN_EMERG "cant read the inode properly from the disk\n");
                goto cantfind_psfs;
        }
	sb->s_root = d_alloc_root(root);
	if (!sb->s_root)
	{	
		iput(root);
		goto cantfind_psfs;
	}
	printk(KERN_INFO PSFS_DBG_VAR("%ux \n",sb->s_dev));
	printk(KERN_INFO PSFS_DBG_VAR("%lx \n",sb->s_blocksize));
	printk(KERN_INFO PSFS_DBG_VAR("%x \n",sb->s_blocksize_bits));
	printk(KERN_INFO PSFS_DBG_VAR("%x \n",ps->psfs_magic));
	printk(KERN_INFO PSFS_DBG_VAR("%llx \n",ps->psfs_nr_blocks));
	printk(KERN_INFO PSFS_DBG_VAR("%llx \n",ps->psfs_nr_inodes));
	printk(KERN_INFO PSFS_DBG_VAR("%x \n",ps->psfs_boot_block));
	printk(KERN_INFO PSFS_DBG_VAR("%x \n",ps->psfs_nr_boot_blocks));
	return 0;
cantfind_psfs:
	printk("Can't find the greatest file system so sad\n");
	brelse(bh);
	kfree(psbi);
fail:
	return -EINVAL;
}

#if LINUX_VERSION_CODE <= KERNEL_VERSION(2,6,99)
int psfs_get_sb(struct file_system_type *fs_type, int flags,
                const char *dev_name, void *data, struct vfsmount *mnt)
{
        return get_sb_bdev(fs_type, flags, dev_name, data, psfs_fill_super, mnt);
}
#else
struct dentry *psfs_get_sb (struct file_system_type *fs_type, int flags ,
			const char *dev_name, void *data)
{
	return mount_bdev(fs_type,flags,dev_name,data,psfs_fill_super);
}
#endif /*LINUX_VERSION_CODE*/
