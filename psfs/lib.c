#include "psfs.h"
/*
 * The maximum length of a single bitmap can be MAX_32_BIT_INTEGER(signed).
 * If you have longer bitmaps, then call this function again.
 * @bmap_len: size of bitmap in bytes.
 *
 * Returns the block/inode of the block/inode allocated.
 */
int32_t alloc_bmap(char *bitmap,int32_t bmap_len)
{
        int32_t i=0,j=0;
        for (;i<bmap_len;i++) {
                if ( (bitmap[i] & 0xff) != 0xff) {
                        for (j=0;j<8;j++) {
                                if ( !(bitmap[i] & (1<<j))) {
                                        bitmap[i]|=(1<<j);
                                        break;
                                }
                        }
                        return i*8+j;
                }
        }
        return -1;
}

/*
 *Free a bit number from a bitmap given to this function.
 *Returns -1 when bit no is bigger or same as bmap_len otherwise
 *return 0 on success.
 */
int32_t free_bmap(char *bitmap,int32_t bmap_len,int32_t bit_no)
{
	int32_t which_byte = bit_no/8; /*Which byte this bit_no belongs to*/
	if (which_byte >=bmap_len)
		return -1; /*Error if bit number too great!*/
	bitmap[which_byte]|=~(1<<(bit_no%8));
	return 0;
}
		 
__u64 get_inode_bmp_block(struct psfs_sb_info *psbi,__u32 blocksize)
{
	return((psbi->s_ps->psfs_nr_inodes *sizeof(struct psfs_inode)/(blocksize))+
		(psbi->s_ps->psfs_nr_inodes *sizeof(struct psfs_inode)%(blocksize)?1:0)+1);
}
__u64 get_data_bmp_block(struct psfs_sb_info *psbi,__u32 blocksize)
{
	return((psbi->s_ps->psfs_nr_inodes/(blocksize*8)+ (psbi->s_ps->psfs_nr_inodes %(blocksize*8)?1:0))+psfs_inode_bmp_block);
	
}
