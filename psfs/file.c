#define MODULE_OWNERSHIP
#include "psfs.h"
#include<linux/buffer_head.h>

int psfs_readdir(struct file * file, void * dirent, filldir_t filldir);

struct buffer_head * psfs_get_direct_extent(struct psfs_inode_info * psi,__u8 extent_no,__u32 block_no);

static struct psfs_dir_entry * psfs_read_dirent(void * buff,struct psfs_dir_entry * dirent);

static int psfs_open(struct inode *inode, struct file *file) 
{
	if(file->f_dentry->d_inode == inode)
		printk("INODE AND FILE pointer refer to same file\n");
	if(file && file->f_dentry) {
		printk (KERN_INFO "pappu paas ho gaya\n");
		printk (KERN_INFO "Opening file %s and inode is %p ",file->f_dentry->d_name.name,inode);
	}
	else if (file)
		printk (KERN_INFO "Opening file pointer %p and inode is %p ",file,inode);
	else
		printk(KERN_INFO "FAT GYA SAB");
	PSFS_DBG_NONE();
return 0;
}

const struct file_operations psfs_fops = {
         .open = psfs_open,
         .readdir = psfs_readdir,
         .read = generic_read_dir,
         .llseek = generic_file_llseek,
};

int psfs_readdir(struct file * file, void * dirent, filldir_t filldir)
{  
	
	struct dentry *de = file->f_dentry;
	__u8 direct_extent_no = 0;
	__u32 block_no = 0;
	long  bytes_read = PAGE_SIZE;
	struct psfs_inode_info *psi = PSFS_I(de->d_inode);
	struct buffer_head *bh;
	char *buff;
	struct psfs_dir_entry *dentry=NULL;
	struct psfs_dir_entry dent;

	printk(KERN_INFO PSFS_DBG_VAR(" = %llu\n",file->f_pos));  
	if(file->f_pos >= psi->psfs_inode.size)
		return 0;
	if(psi->vfs_inode.i_sb->s_blocksize - file->f_pos < PSFS_MIN_DIRENT_SIZE)
		file->f_pos +=psi->vfs_inode.i_sb->s_blocksize - file->f_pos; /*MOVE TO NEXT BLOCK*/
	printk(KERN_INFO PSFS_DBG_VAR(" = %llx\n",psi->psfs_inode.size));
	PSFS_DBG_NONE();
	if (S_ISDIR(psi->psfs_inode.type))
	{
		PSFS_DBG_MSG("its a directory \n");
	}
	block_no = file->f_pos/de->d_inode->i_sb->s_blocksize;
	printk(KERN_INFO PSFS_DBG_VAR(" = %u\n",block_no));
	for(direct_extent_no = 0;direct_extent_no < PSFS_NR_DIRECT_EXTENTS;direct_extent_no++) {

		if((PSFS_INFO_PSFS_INODE(psi,psfs_extent)[direct_extent_no].length)<block_no)
			block_no = block_no - (PSFS_INFO_PSFS_INODE(psi,psfs_extent)[direct_extent_no].length);
		else
			break; 
	}	
	bh = psfs_get_direct_extent(psi,direct_extent_no,block_no );
	if(!bh)
	{
		PSFS_DBG_MSG("BH IS NULL!!! WT");
		return -ENOMEM;
	}
	buff = bh->b_data;
	printk(KERN_INFO PSFS_DBG_VAR("=%lx\n",bh->b_size));
	while( (bytes_read - PSFS_MIN_DIRENT_SIZE) >=0 && file->f_pos < psi->psfs_inode.size) {
		dentry = psfs_read_dirent(buff,&dent);
		printk(KERN_INFO PSFS_DBG_VAR("=%d\n",dentry->rec_len));
                printk(KERN_INFO PSFS_DBG_VAR("=%d\n",dentry->name_len));
		printk(KERN_INFO PSFS_DBG_VAR("=%llu\n",file->f_pos));
		if(dentry->name_len) {
			if(filldir(dirent,dentry->name,dentry->name_len,file->f_pos,de->d_inode->i_ino,psi->psfs_inode.flags&PSFS_DIR?DT_DIR:DT_REG))
				return 0;
		}
		file->f_pos += dentry->rec_len;
		printk(KERN_INFO PSFS_DBG_VAR("=%d\n",dentry->rec_len));
		printk(KERN_INFO PSFS_DBG_VAR("=%d\n",dentry->name_len));
		buff += dentry->rec_len;
		bytes_read -= dentry->rec_len;
		printk(KERN_INFO PSFS_DBG_VAR("=%ld\n",bytes_read));
	}
return 1;
}

/*	
	
#if 0
        struct dentry *de = file->f_dentry;
	printk("de->d_inode->i_ino = %lu\n",de->d_inode->i_ino);
        if(file->f_pos > 1)
                return 1;
        if(filldir(dirent, ".", 1, file->f_pos++, de->d_inode->i_ino, DT_DIR)) {
                return 0;
	}
         if(filldir(dirent, "..", 2, file->f_pos++, de->d_parent->d_inode->i_ino, DT_DIR))
                return 0;
        return 1;
#endif
}*/
/*
 * Read block within the given direct extent.
 * @block_no: Must be between 0(inclusive) and extent length(exclusive).
 * @extent_no: index of the direct extent_no.
 * */
struct buffer_head *psfs_get_direct_extent(struct psfs_inode_info * psi,__u8 extent_no,__u32 block_no)
{
	struct buffer_head *bh=NULL;
	__u32 disk_block_no = psi->psfs_inode.psfs_extent[extent_no].block_no+block_no;


	if(extent_no >=PSFS_NR_DIRECT_EXTENTS)
		return bh;
	if(block_no < psi->psfs_inode.psfs_extent[extent_no].length) {
		bh = sb_bread(psi->vfs_inode.i_sb,psi->psfs_inode.psfs_extent[extent_no].block_no+block_no);
	}

	printk("disk_block_no= %x\n",disk_block_no);
return bh;
}	
static struct psfs_dir_entry * psfs_read_dirent(void * buff,struct psfs_dir_entry * dirent)
{
	memcpy(dirent,buff,PSFS_MIN_DIRENT_SIZE);
	dirent->inode_nr = be32_to_cpu(dirent->inode_nr);
	dirent->flags = be16_to_cpu(dirent->flags);
	dirent->rec_len = be16_to_cpu(dirent->rec_len);
	memcpy(dirent->name,(char*)buff+PSFS_MIN_DIRENT_SIZE,dirent->name_len);
	return dirent;
}
