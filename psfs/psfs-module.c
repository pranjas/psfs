#define MOD_AUTHOR "Shwetabh and Pranay <pranay_shwetabh@lulu.com>"
#include "psfs.h"
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,99)
extern int psfs_get_sb(struct file_system_type *fs_type, int flags,
                const char *dev_name, void *data, struct vfsmount *mnt);
#else
extern struct dentry *psfs_get_sb(struct file_system_type *fs_type, int flags,
                const char *dev_name, void *data);
#endif /*LINUX_VERSION_CODE*/

extern int init_inodecache(void);
extern void destroy_inode_cache(void);

static struct file_system_type psfs_type = {
        .owner = THIS_MODULE,
        .name  = "psfs",
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,99)
        .get_sb = psfs_get_sb,
#else
	.mount = psfs_get_sb,
#endif /*LINUX_VERSION_CODE*/
        .kill_sb = kill_block_super,
        .fs_flags = FS_REQUIRES_DEV,

};
static int  __init init_psfs(void)
{
        int err = 0;
        err = init_inodecache();
        err += register_filesystem(&psfs_type);
	printk(KERN_INFO "file system type is at %p\n",&psfs_type);
        return err;
}
static void __exit exit_psfs(void)
{
        unregister_filesystem(&psfs_type);
        destroy_inode_cache();
        return;
}
module_init(init_psfs);
module_exit(exit_psfs);
MODULE_DESCRIPTION("Highly optimised filesystem");
