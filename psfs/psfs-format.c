#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <errno.h>
#include <linux/fs.h> /*This is for BLKGETSIZE64*/
#include <stdlib.h>
#include <arpa/inet.h>
#include <string.h>
/*
 * Must be included after linux/fs.h to avoid redeclaration
 * error for types.
 * */
#ifndef __USER__
#define __USER__
#include "psfs.h"
#endif

#define OPTSTRING	"b:i:N"


/*
 * The maximum length of a single bitmap can be MAX_32_BIT_INTEGER(signed).
 * If you have longer bitmaps, then call this function again.
 * @bmap_len: size of bitmap in bytes.
 *
 * Returns the block/inode of the block/inode allocated.
 */
static int32_t alloc_bmap(char *bitmap,int32_t bmap_len)
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
 *Supported options for filesystems include the number of inodes,
 *the total number of blocks.
 */
extern const char *__progname;
/*
 *For default value of any of the last 3 parameters, use argument value as 0.
 */

inline u_int16_t cpu_to_be16(u_int16_t val)
{
	return htons(val);
}
inline u_int16_t be16_to_cpu(u_int16_t val)
{
	return ntohs(val);
}
u_int64_t cpu_to_be64(u_int64_t val)
{
	/*We haven't got direct ntohl function so got to do it by hand.*/
	#ifdef LITTLE_ENDIAN
		u_int64_t u = (val & 0x0ffffffff);
		u=htonl(u);
		u<<=32;
		u|=htonl(val>>32);
		return u;
	#else
		return val;	/*Already a big endian machine*/
	#endif
}

u_int64_t be64_to_cpu(u_int64_t val)
{
	#ifdef LITTLE_ENDIAN
		u_int64_t u = (val & 0x0ffffffff);
		u=ntohl(u);
		u<<=32;
		u|=ntohl(val>>32);
		return u;
	#else
		return val;
	#endif
}

u_int32_t be32_to_cpu(u_int32_t val)
{
	return ntohl(val);
}

u_int32_t cpu_to_be32(u_int32_t val)
{
	return htonl(val);
}

/*
 * This is relative to the start of the INODE block.
 * It tells in which block does the inode resides.
 */
static inline u_int64_t inode_block_from_ino(u_int64_t ino,struct psfs_super_block *psfs_sb) {
	return (ino / (be32_to_cpu(psfs_sb->psfs_block_size)/sizeof(struct psfs_inode)));
}
/*
 * This one tells where in that block is the inode located.
 * This tells the offset.
 * */
static inline u_int32_t inode_offset_from_ino(u_int64_t ino,struct psfs_super_block *psfs_sb) {
	u_int64_t max_inodes_per_block = be32_to_cpu(psfs_sb->psfs_block_size)/sizeof(struct psfs_inode);
	ino = ino-(max_inodes_per_block * inode_block_from_ino(ino,psfs_sb));
	printf ("Offset is %u\n",( ino % max_inodes_per_block)*sizeof(struct psfs_inode));
	return ( ino % max_inodes_per_block)*sizeof(struct psfs_inode);
}

int format_psfs(const char *device, u_int32_t block_size, u_int64_t nr_inodes,
			u_int64_t nr_blocks, u_int32_t min_extent_length)
{
	int dev_fd=open(device,O_RDWR);
	char *fs_block_buffer;
	off_t disk_offset = 0;
	int32_t ino = -1;
	time_t tm;
	u_int64_t inodes_written = 0,inode_bmap_blocks,bmap_blocks;
	u_int64_t total_blocks_written = 0;
	u_int64_t data_bmap_block;
	u_int64_t inode_bmap_block;
	u_int64_t tmp_var;

	if (dev_fd < 0) {
		printf("Error opening device, open returned with status %d\n",errno);
		perror("FATAL:");
		return -1;
	}
	/*long nr_512sectors;*/
	u_int64_t nr_512sectors;
	u_int32_t scaling_factor;
	if (ioctl(dev_fd,BLKGETSIZE64,&nr_512sectors) < 0 )
	{
		printf("Error getting device size, ioctl returned with status %d\n",errno);
		perror("FATAL:");
		return -1;
	}
	nr_512sectors/=KERNEL_SECTOR_SIZE;
	printf("Total 512 sectors on disk are %llu\n",nr_512sectors);
	/*
	 *Check if default values are to be used.
	 */
	if (!block_size)
		block_size = PSFS_DEFAULT_BLKSIZE;

	scaling_factor=block_size/KERNEL_SECTOR_SIZE;
	printf(PSFS_DBG_VAR("%016llX \n",scaling_factor));
	if (!nr_blocks)
		nr_blocks = (nr_512sectors)/scaling_factor;
	printf (PSFS_DBG_VAR("%lu \n",nr_blocks));

	/*
	 *Simple heuristics--> 10% of total number of blocks = number of inodes*/
	if (!nr_inodes)
		nr_inodes = nr_blocks/10;
	if (!min_extent_length)
		min_extent_length=PSFS_DEFAULT_EXTENT_LEN;

	struct psfs_super_block super;
	struct psfs_inode inode;
	memset(&inode,0,sizeof(inode));
	fs_block_buffer = calloc(1,block_size);
	if (!fs_block_buffer) {
		printf("Unable to allocate memory for formatting!\n");
		return -1;
	}
	super.psfs_nr_blocks = cpu_to_be64(nr_blocks);
	super.psfs_nr_inodes = cpu_to_be64(nr_inodes);
	super.psfs_boot_block = 0;/*cpu_to_be64(1);*/
	/*
	 * use the ceiling value.
	 */
	super.psfs_nr_boot_blocks = cpu_to_be32( 
			/*
			 * Number of blocks taken by psfs_inode structures.
			 * */
			(nr_inodes *sizeof(struct psfs_inode)/block_size)+
			(nr_inodes *sizeof(struct psfs_inode)%block_size?1:0)
					+
			/*
			 * Number of blocks taken by block bitmaps.
			 * */
			(nr_blocks/(block_size*8)+ (nr_blocks %(block_size*8)?1:0))+
			/*
			 * Number of blocks taken by inode bitmaps.
			 * */
			(nr_inodes/(block_size*8)+ (nr_inodes %(block_size*8)?1:0))
			 		+
			/*
			 * The super block always takes up the first block, so
			 * accomodate it as well.
			 * */
					1
			);
	super.psfs_min_extent_length = cpu_to_be32(min_extent_length);
	super.psfs_super_flags = 0;
	super.psfs_magic = cpu_to_be32(PSFS_MAGIC);
	super.psfs_block_size = cpu_to_be32(block_size);
	memcpy(fs_block_buffer,&super,sizeof(super));
	/*
	 * Write the super block. The super block also takes up one whole FS block
	 */
	if (write(dev_fd,fs_block_buffer,block_size) < 0)
	{
		perror("FATAL Error writing super block:\n");
		return -1;
	}
	total_blocks_written++; /* increment total blocks written.*/
	/*
	 * Write the inodes right after the super block's block. So inodes are always
	 * in a fixed location, i.e. right after the super block's block.
	 */
	while (inodes_written<nr_inodes) {
		int buffer_left = block_size;
		int buffer_inodes = 0;
		memset(fs_block_buffer,0,block_size);
		while (buffer_left>=sizeof(inode) && inodes_written < nr_inodes) {
			memcpy(fs_block_buffer+sizeof(inode)*buffer_inodes,&inode,sizeof(inode));
			buffer_left -= sizeof(inode);
			buffer_inodes++;
			inodes_written++;
		}
		if (write(dev_fd,fs_block_buffer,block_size) < 0)
		{
			perror("FATAL Error while writing inodes:");
			return -1;
		}
		total_blocks_written++; /* increment total blocks written.*/
	}
	inode_bmap_block = total_blocks_written;/*We save the block number of inode bmap*/
	/*
	 * Write the inode's bitmap. Figure out how many FS blocks are required and just
	 * zero them out.
	 */

	 /*Reusing the inodes_written variable here.*/
	 inodes_written = 0;
	 inode_bmap_blocks = (nr_inodes/(block_size*8)+ (nr_inodes %(block_size*8)?1:0));
	 memset(fs_block_buffer,0,block_size);
	 while (inodes_written < inode_bmap_blocks) {
	 	if (write(dev_fd,fs_block_buffer,block_size) < 0) {
			perror("FATAL Error while writing inode bitmap");
			return -1;
		}
		inodes_written++;
		total_blocks_written++; /* increment total blocks written.*/
	 }
	 /*
	  * Till now the super block and the inode blocks and inode bmap 
	  * have been written.
	  * We record here the next block address since now we'll write the
	  * block map and we would need to modify the allocated blocks bmap
	  * after we are done.
	  */
	 data_bmap_block = total_blocks_written; /*We save the block number for bmap*/
	 bmap_blocks = nr_blocks/(block_size*8)+ (nr_blocks %(block_size*8)?1:0);
	 printf(PSFS_DBG_VAR("%llu \n",bmap_blocks));
	 memset(fs_block_buffer,0,block_size); /*Zero out the block for bitmap*/
	 tmp_var=bmap_blocks;
	 while (tmp_var) {
	 	if (write(dev_fd,fs_block_buffer,block_size) < 0) {
			perror("FATAL Error while writing block bitmap");
			return -1;
		}
		tmp_var--;
		total_blocks_written++;
	 }
	 /*
	  * Modify the bitmap for blocks allocated.
	  * */
	tmp_var = total_blocks_written;
	printf (PSFS_DBG_VAR("% llu\n",total_blocks_written));
	u_int64_t blocks_used = 0;
	memset(fs_block_buffer,0,block_size);
	printf(PSFS_DBG_VAR("%llu \n",tmp_var));
	while (tmp_var && (blocks_used < bmap_blocks)) {
#ifdef PSFS_DEBUG
		int64_t block_nr_alloced;
		if ( (block_nr_alloced=alloc_bmap(fs_block_buffer,block_size)) < 0) {
#else
		if (alloc_bmap(fs_block_buffer,block_size) < 0) {
#endif
			/*
			 * Write this bmap first to its position.
			 * */
			if (llseek(dev_fd,(data_bmap_block+blocks_used)*block_size,SEEK_SET) < 0)
			{
				perror("FATAL Error while seeking in device:");
				return -1;
			}
			if (write(dev_fd,fs_block_buffer,block_size) < 0)
			{
				perror("FATAL Error writing back block bitmap:");
				return -1;
			}
			blocks_used++;
			memset(fs_block_buffer,0,block_size); /*Clean the slate for next bmap block*/
		}
		else {
			tmp_var--;
#ifdef PSFS_DEBUG
			printf ("Allocated block number = %llu\n",block_nr_alloced);
#endif
		}
	}
	/*
	 * Check if we were really able to modify the bitmap. That is all bitmap
	 * allocations were done successfully. The last bitmap block still has to be
	 * written to disk.
	 * */
	if(tmp_var)
	{
		printf(PSFS_DBG_VAR("%llu \n",tmp_var));
		printf("FATAL Error, block allocation failed!!! NOT ENOUGH BLOCKS!!\n");
		return -1;
	}
	else if (write(dev_fd,fs_block_buffer,block_size) < 0)
	{
		perror("FATAL Error writing back block bitmap:");
		return -1;
	}
	/*
	 * Now we've set all the blocks we've taken. Setup the root directory
	 * and allocate it an extent and an inode.
	 * @blocks_used :The number of bitmap blocks used.
	 * @fs_block_buffer: The last bitmap block that was written to disk.
	 * */

	struct psfs_inode *root=&inode;
	memset(root,0,sizeof(*root));
	
	if (alloc_psfs_extent (&root->psfs_extent[0],min_extent_length*500,fs_block_buffer,block_size) < 0) {
		printf("Unable to allocate extent for root directory!!!\n");
		return -1;
	}
	root->psfs_extent[0].block_no+=blocks_used*(block_size * 8);
	/*
	 * Get an inode, set the bit in inode bitmap. Grab an extent
	 * and give it the root directory inode number.
	 * */

	if (llseek(dev_fd, (inode_bmap_block)*block_size,SEEK_SET) < 0) {
		perror("FATAL Error: Unable to seek into device for root directory!!!\n");
		return -1;
	}
	/*
	 * There are no inodes allocated yet so we don't need to read from device.
	 * */
	memset(fs_block_buffer,0,block_size);
	if ( (ino = alloc_bmap(fs_block_buffer,block_size)) < 0) {
		perror("FATAL Error: Unable to allocate an inode from bitmap for root directory!\n");
		return -1;
	}
	/*
	 *Write the inode bitmap block.
	 */
	 if (write(dev_fd,fs_block_buffer,block_size) < 0) {
	 	perror("FATAL Error: While writing bitmap block...\n");
		return -1;
	 }
	 
	 /*
	 *Finally write the directory entries . and .. for root directory.
	 *in the extent just created.
	 */
	struct psfs_dir_entry dirent_dot,dirent_dotdot;
	dirent_dot.inode_nr = dirent_dotdot.inode_nr = cpu_to_be32(root->inode_nr);
	dirent_dot.flags = dirent_dotdot.flags = cpu_to_be16(PSFS_ROOT_DIR|PSFS_NON_REM|PSFS_DIR);

	dirent_dot.name_len = 1;
	dirent_dotdot.name_len = 2;
	dirent_dot.name[0]=dirent_dotdot.name[0]='.';
	dirent_dotdot.name[1]='.';
	
	dirent_dot.rec_len = cpu_to_be16(PSFS_MIN_DIRENT_SIZE + 1);
	dirent_dotdot.rec_len = cpu_to_be16(PSFS_MIN_DIRENT_SIZE+2);
	memset(fs_block_buffer,0,block_size);
	root->size = cpu_to_be64(be16_to_cpu(dirent_dot.rec_len)+be16_to_cpu(dirent_dotdot.rec_len));
	root->inode_nr = cpu_to_be32(ino);
	printf(PSFS_DBG_VAR("=%x\n",root->psfs_extent[0].block_no));
	root->psfs_extent[0].block_no = cpu_to_be32(root->psfs_extent[0].block_no/*+be32_to_cpu(super.psfs_nr_boot_blocks)*/);
	root->psfs_extent[0].length = cpu_to_be32(root->psfs_extent[0].length);
	root->flags = (PSFS_NON_REM|PSFS_DIR|PSFS_ROOT_DIR);
	printf(PSFS_DBG_VAR("%x\n",root->flags));
	root->flags = cpu_to_be16(root->flags);
	root->a_time = root->c_time = root->m_time = cpu_to_be32(time(&tm));
	root->owner = cpu_to_be16(getuid() & 0x0ffff);
	root->type = cpu_to_be32(S_IFDIR|0755);
	memcpy(fs_block_buffer+inode_offset_from_ino(ino,&super),root,sizeof(*root));
	
	printf(PSFS_DBG_VAR("%016llX\n",root->size));
	printf(PSFS_DBG_VAR("=%x \n",root->flags));
	printf(PSFS_DBG_VAR("=%x \n",root->psfs_extent[0].block_no));
	printf(PSFS_DBG_VAR("=%x \n",root->psfs_extent[0].length));


	/*
	 *Seek to the required disk block to write inode. Since inode blocks begin
	 *after the first block, we just add 1 to the block number returned by
	 *inode_block_from_ino.
	 */
	if (llseek(dev_fd,(inode_block_from_ino(ino,&super)+1)*block_size,SEEK_SET) < 0) {
		perror("FATAL Error: While seeking into device\n");
		return -1;
	}
	if (write(dev_fd,fs_block_buffer,block_size) < 0) {
		perror("FATAL Error: While writing root inode in inode block\n");
		return -1;
	}
	
	if (llseek(dev_fd,(be32_to_cpu(root->psfs_extent[0].block_no))*block_size,SEEK_SET) < 0) {
	 	perror("FATAL Error: While seeking into device\n");
		return -1;
	}
	if (write(dev_fd,&dirent_dot,be16_to_cpu(dirent_dot.rec_len)) < 0) {
	 	perror("FATAL Error: While writing dirent . \n");
		return -1;
	}
	if (write(dev_fd,&dirent_dotdot,be16_to_cpu(dirent_dotdot.rec_len)) <0) {
	 	perror("FATAL Error: While writing dirent ..\n");
		return -1;
	}
	printf(PSFS_DBG_VAR("%llX\n",super.psfs_nr_blocks));
	printf(PSFS_DBG_VAR("%llX\n",super.psfs_nr_inodes));
	printf(PSFS_DBG_VAR("%X\n",super.psfs_boot_block));
	printf(PSFS_DBG_VAR("%X\n",super.psfs_nr_boot_blocks));
	printf(PSFS_DBG_VAR("%X\n",super.psfs_min_extent_length));
	printf(PSFS_DBG_VAR("%X\n",super.psfs_super_flags));
	printf(PSFS_DBG_VAR("%X\n",super.psfs_magic));
	printf(PSFS_DBG_VAR("%X\n",super.psfs_block_size));
	return 0;
}

int main(int argc,char *argv[])
{
	int32_t block_size=0,extent_length=0;
	int64_t nr_blocks=0,nr_inodes=0;
	extern int optind;
	char *strtol_ptr;
	int c;
	optind=2; /*The first is the device name, next comes options.*/
	if(argc<2)
	{
		printf("Usage %s <device_file> [-b block_size] [-i nr_inodes] [-N nr_blocks] [-L min extent length]\n",__progname);
		exit(EXIT_FAILURE);
	}
	while ( (c = getopt(argc,argv,OPTSTRING)) != -1) {
		switch (c) {
			case 'b':
				if ( (block_size = (int32_t)strtol(optarg,&strtol_ptr,10)) < 0 ) {
					printf("Invalid block_size value");
					exit(EXIT_FAILURE);
				}
				/*
				 *Block size is going to be in multiple of 4KB.
				 */
				if( (block_size & 0x0fff) ) {
					printf("Block size must"
						" be a multiple of 4KB\n");
					exit(EXIT_FAILURE);
				}
				break;
			case 'i':
				if ( (nr_inodes = (int64_t)strtoll(optarg,&strtol_ptr,10)) < 0) {
					printf("Invalid value used for number of inodes\n");
					exit(EXIT_FAILURE);
				}
				break;
			case 'N':
				if ( (nr_blocks = (int32_t)strtoll(optarg,&strtol_ptr,10)) < 0) {
					printf("Invalid value used for number of blocks\n");
					exit(EXIT_FAILURE);
				}
				break;
			case 'L':
				if ( (extent_length = (int64_t)strtol(optarg,&strtol_ptr,10)) < 0) {
					printf("Invalid value used for minimum extent length\n");
					exit(EXIT_FAILURE);
				}
				if (extent_length < 4)
				{
					printf("Minimum extent length must be 4\n");
					exit(EXIT_FAILURE);
				}
				break;
			default:
				printf("Ignoring unknown option %s and continuing...\n",optarg);
				break;
		}
	}
	if (format_psfs(argv[1],block_size,nr_inodes,nr_blocks,extent_length) < 0) {
		printf("Error in formatting device %s\n",argv[1]);
		exit(EXIT_FAILURE);
	}
return 0;
}
