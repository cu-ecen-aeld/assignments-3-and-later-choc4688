#!/bin/bash
# Script outline to install and build kernel.
# Author: Siddhant Jajoo.

set -e
set -u

OUTDIR=/tmp/aeld
KERNEL_REPO=git://git.kernel.org/pub/scm/linux/kernel/git/stable/linux-stable.git
KERNEL_VERSION=v5.15.163
BUSYBOX_VERSION=1_33_1
FINDER_APP_DIR=$(realpath $(dirname $0))
ARCH=arm64
CROSS_COMPILE=aarch64-none-linux-gnu-

export PATH=$PATH:/home/chaseo/Documents/ARM/arm-gnu-toolchain-13.3.rel1-x86_64-aarch64-none-linux-gnu/bin


if [ $# -lt 1 ]
then
	echo "Using default directory ${OUTDIR} for output"
else
	OUTDIR=$1
	echo "Using passed directory ${OUTDIR} for output"
fi

mkdir -p ${OUTDIR}

cd "$OUTDIR"
if [ ! -d "${OUTDIR}/linux-stable" ]; then
    #Clone only if the repository does not exist.
	echo "CLONING GIT LINUX STABLE VERSION ${KERNEL_VERSION} IN ${OUTDIR}"
	git clone ${KERNEL_REPO} --depth 1 --single-branch --branch ${KERNEL_VERSION}
fi
if [ ! -e ${OUTDIR}/linux-stable/arch/${ARCH}/boot/Image ]; then
    cd linux-stable
    echo "Checking out version ${KERNEL_VERSION}"
    git checkout ${KERNEL_VERSION}

    # TODO: Add your kernel build steps here

        #Deep cleans the kernel build tree, removes .config file with any configs
    echo "make ARCH=arm64 CROSS_COMPILE=${CROSS_COMPILE} mrproper"
    make ARCH=arm64 CROSS_COMPILE=${CROSS_COMPILE} mrproper
        #Configure for the "virt" arm dev board, will simulate in QEMU
    echo "make ARCH=arm64 CROSS_COMPILE=${CROSS_COMPILE} defconfig"
    make ARCH=arm64 CROSS_COMPILE=${CROSS_COMPILE} defconfig
    
        #Build a kernel image for booting with QEMU
        #-j4 allows running multiple files at ones, can speed up process
    echo "make -j4 ARCH=arm64 CROSS_COMPILE=${CROSS_COMPILE} all"
    make -j4 ARCH=arm64 CROSS_COMPILE=${CROSS_COMPILE} all
    

        #Build any kernel modules
    echo "make ARCH=arm64 CROSS_COMPILE=${CROSS_COMPILE} modules"
    make ARCH=arm64 CROSS_COMPILE=${CROSS_COMPILE} modules
        #Build the device tree
    echo "make ARCH=arm64 CROSS_COMPILE=${CROSS_COMPILE} dtbs "
    make ARCH=arm64 CROSS_COMPILE=${CROSS_COMPILE} dtbs  

#------------------------------------------


fi

echo "Adding the Image in outdir"

    #Note: Used Copilot to determine why I was getting a missing kernel image error in start-qemu-terminal.sh
    # (It was because I wasn't copying the Image to /tmp/aeld/Image)
    cp ${OUTDIR}/linux-stable/arch/${ARCH}/boot/Image ${OUTDIR} #Just copy into outdir*************

echo "Creating the staging directory for the root filesystem"
cd "$OUTDIR"
if [ -d "${OUTDIR}/rootfs" ]
then
	echo "Deleting rootfs directory at ${OUTDIR}/rootfs and starting over"
    sudo rm  -rf ${OUTDIR}/rootfs
fi

# TODO: Create necessary base directories

mkdir rootfs
cd "${OUTDIR}/rootfs"

mkdir -p bin dev etc home lib lib64 proc sbin sys tmp usr var
mkdir -p usr/bin usr/lib usr/sbin
mkdir -p var/log



#-----------------------------------------------

cd "$OUTDIR"
if [ ! -d "${OUTDIR}/busybox" ]
then
git clone git://busybox.net/busybox.git
    cd busybox
    git checkout ${BUSYBOX_VERSION}
    # TODO:  Configure busybox


    #??????

    #-----------------------------------------------
else
    cd busybox
fi

    # export TOOLCHAIN_DIR=/home/chaseo/Documents/ARM/arm-gnu-toolchain-13.3.rel1-x86_64-aarch64-none-linux-gnu/bin
    # export PATH=$TOOLCHAIN_DIR:$PATH
    # export CROSS_COMPILE=aarch64-none-linux-gnu-

    # which ${CROSS_COMPILE}gcc

# echo "1"

# echo "Compiler path: $(which ${CROSS_COMPILE}gcc)"


# TODO: Make and install busybox

export ARCH=arm64
export CROSS_COMPILE=aarch64-none-linux-gnu-

# cd ${OUTDIR}/rootfs #****************

make distclean
# make CROSS_COMPILE=aarch64-none-linux-gnu- ARCH=arm64 defconfig
make defconfig #***************
make ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE}
#Install step copies busybox into root fs and creates all symbolic links necessary
make CONFIG_PREFIX=${OUTDIR}/rootfs ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} install

#Instructions say print sysroot. Find sysroot dir, arm-gcc sysroot outputs, assign to variable, then sudo cp
#Should have installed ARM toolchain into root, not Documents
#Use sudo to install in root lib

#-----------------------------------------------

echo "Library dependencies"

#Was getting error saying bin/busybox didn't exist as a directory..... But I did get the correct output with ${OUTDIR}....?
${CROSS_COMPILE}readelf -a ${OUTDIR}/rootfs/bin/busybox | grep "program interpreter" 
${CROSS_COMPILE}readelf -a ${OUTDIR}/rootfs/bin/busybox | grep "Shared library" 

# TODO: Add library dependencies to rootfs

    cp /home/chaseo/Documents/ARM/arm-gnu-toolchain-13.3.rel1-x86_64-aarch64-none-linux-gnu/aarch64-none-linux-gnu/libc/lib/ld-linux-aarch64.so.1 ${OUTDIR}/rootfs/lib 

    cp /home/chaseo/Documents/ARM/arm-gnu-toolchain-13.3.rel1-x86_64-aarch64-none-linux-gnu/aarch64-none-linux-gnu/libc/lib64/libm.so.6 ${OUTDIR}/rootfs/lib64
    cp /home/chaseo/Documents/ARM/arm-gnu-toolchain-13.3.rel1-x86_64-aarch64-none-linux-gnu/aarch64-none-linux-gnu/libc/lib64/libresolv.so.2 ${OUTDIR}/rootfs/lib64
    cp /home/chaseo/Documents/ARM/arm-gnu-toolchain-13.3.rel1-x86_64-aarch64-none-linux-gnu/aarch64-none-linux-gnu/libc/lib64/libc.so.6 ${OUTDIR}/rootfs/lib64

    #-----------------------------------------------

# TODO: Make device nodes

    echo "Make device nodes"

    #Null device provides zeros, place to dump unused content
        #Expected by some startup scripts
    #Make node command
        #-m ### for access permissions

        #Then <major> and <minor> params, known / defined for null and console devices
    # sudo rm -f "${OUTDIR}/dev/null" #Done because error otherwise when it already exists

    if [ -f "${OUTDIR}/rootfs/dev/null" ]
    then
        : 
    else
        sudo rm -f "${OUTDIR}/rootfs/dev/null"
        sudo mknod -m 666 ${OUTDIR}/rootfs/dev/null c 1 3
    fi
    

    echo "Now creating console"
    
    #Console device for interacting through the terminal
    # sudo rm -f "${OUTDIR}/dev/console"

    if [ -f "${OUTDIR}/rootfs/dev/console" ]
    then
        :
    else
        sudo rm -f "${OUTDIR}/rootfs/dev/console"
        sudo mknod -m 600 ${OUTDIR}/rootfs/dev/console c 5 1
    fi

    

    #-----------------------------------------------

# TODO: Clean and build the writer utility

    echo "Clean and build the writer utility"

    #Get real path, don't use this********************
    cd /home/chaseo/Documents/AESD/Assignment3+/assignments-3-and-later-choc4688/finder-app

    make clean
    make CROSS_COMPILE=aarch64-none-linux-gnu-


# TODO: Copy the finder related scripts and executables to the /home directory
# on the target rootfs

    echo "Copy Finder files"

    cp writer ${OUTDIR}/rootfs/home

    cp finder.sh ${OUTDIR}/rootfs/home
    cp ../conf/username.txt ${OUTDIR}/rootfs/home
    cp ../conf/assignment.txt ${OUTDIR}/rootfs/home

    cp autorun-qemu.sh ${OUTDIR}/rootfs/home

#-----------------------------------------------

# TODO: Chown the root directory

echo "Chown the root directory"

cd "$OUTDIR/rootfs"
sudo chown -R root:root *

#-----------------------------------------------

# TODO: Create initramfs.cpio.gz





    #To use the rootfs with the target
    #QEMU emulation environment acts as bootloader in this case
    cd "$OUTDIR/rootfs" 
    find . | cpio -H newc -ov --owner root:root > ${OUTDIR}/initramfs.cpio

    cd "$OUTDIR"
    gzip -f initramfs.cpio

    echo "Complete!"

    # #Debug
    # mkdir /tmp/testfs
    # cd /tmp/testfs
    # gzip -dc ${OUTDIR}/initramfs.cpio.gz | cpio -idmv
    # ls -l bin/sh
    # ls -l dev/console
    # #******

    exit 0


