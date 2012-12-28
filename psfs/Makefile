PROG = psfs_fs
obj-m += ${PROG}.o
${PROG}-objs := super.o file.o inode.o psfs-module.o lib.o

all: 
	make -C  /lib/modules/$(shell uname -r)/build M=`pwd` modules
clean:
	make -C  /lib/modules/$(shell uname -r)/build M=`pwd` clean
