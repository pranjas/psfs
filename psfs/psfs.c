#include "psfs.h"
#ifdef __USER__
#include <stdio.h>
#include <fcntl.h>
#include <stdlib.h>

u_int32_t be32_to_cpu(u_int32_t val)
{
        return ntohl(val);
}

u_int32_t cpu_to_be32(u_int32_t val)
{
        return htonl(val);
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
inline u_int16_t be16_to_cpu(u_int16_t val)
{
        return ntohs(val);
}
#define PSFS_EXTENT_TO_CPU(psfs_extent)\
({\
psfs_extent->block_no = be32_to_cpu(psfs_extent->block_no);\
psfs_extent->length = be32_to_cpu(psfs_extent->length);\
})

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
int main()
{
	printf("sizeof psfs_inode=%d bytes\n",sizeof(struct psfs_inode));
	printf("sizeof psfs_super_block=%d bytes\n",sizeof(struct psfs_super_block));
	printf("sizeof psfs_extent is %d bytes\n",sizeof(struct psfs_extent));

	int fd = open("/dev/loop0",O_RDONLY);
	if(fd < 0) {
		perror("Error opening file");
		return EXIT_FAILURE;
	}
	if(llseek(fd,4096,SEEK_SET) < 0) {
		perror("Error seeking file:");
		return EXIT_FAILURE;
	}
	struct psfs_inode inode;
	if (read(fd,&inode,sizeof(inode)) != sizeof(inode))
	{
		printf("couldn't read full inode\n");
		return EXIT_FAILURE;
	}
	psfs_inode_to_cpu(&inode);
	printf(PSFS_DBG_VAR("%llx\n",inode.size));
	printf(PSFS_DBG_VAR("%lu\n",inode.flags));
	printf(PSFS_DBG_VAR("%lx\n",inode.psfs_extent[0].block_no));
	struct psfs_dir_entry dirent;
	if (llseek(fd,(inode.psfs_extent[0].block_no)*4096,SEEK_SET)<0)
	{
		printf("couldn't seek\n");
		return EXIT_FAILURE;
	}
	if (read(fd,&dirent,PSFS_MIN_DIRENT_SIZE)!=PSFS_MIN_DIRENT_SIZE)
	{
		printf("error reading\n");
		return EXIT_FAILURE;
	}
	printf(PSFS_DBG_VAR("=%x\n",dirent.rec_len));
	printf(PSFS_DBG_VAR("=%x\n",dirent.name_len));
	printf(PSFS_DBG_VAR("=%x\n",dirent.flags));
	printf(PSFS_DBG_VAR("=%x\n",dirent.inode_nr));
return EXIT_SUCCESS;
}
#endif /*__USER__*/
