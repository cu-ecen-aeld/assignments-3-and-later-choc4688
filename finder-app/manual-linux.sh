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
#------------------------------------------
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

#------------------------------------------
    cp ${OUTDIR}/linux-stable/arch/${ARCH}/boot/Image ${OUTDIR}/ #Had issues forgetting this step. Used Copilot AI to help recognize the issue initially + got TA help.
#------------------------------------------

echo "Creating the staging directory for the root filesystem"
cd "$OUTDIR"
if [ -d "${OUTDIR}/rootfs" ]
then
	echo "Deleting rootfs directory at ${OUTDIR}/rootfs and starting over"
    sudo rm  -rf ${OUTDIR}/rootfs
fi

# TODO: Create necessary base directories
#------------------------------------------
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
    #------------------------------------------


    #?????? Nothing

    #-----------------------------------------------
else
    cd busybox
fi


# TODO: Make and install busybox

#------------------------------------------
make distclean
make defconfig #Had Cross Compilation originally, was not working until both that and Arch args were removed.
make ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE}
#Install step copies busybox into root fs and creates all symbolic links necessary
make CONFIG_PREFIX=${OUTDIR}/rootfs ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} install
#-----------------------------------------------

echo "Library dependencies"

#Modified these commands because I was getting error saying bin/busybox didn't exist
${CROSS_COMPILE}readelf -a ${OUTDIR}/rootfs/bin/busybox | grep "program interpreter" 
${CROSS_COMPILE}readelf -a ${OUTDIR}/rootfs/bin/busybox | grep "Shared library" 

# TODO: Add library dependencies to rootfs

#-----------------------------------------------
    #Find sysroot dir, arm-gcc sysroot outputs, assign to variable, then sudo cp
    SYSROOT="$(aarch64-none-linux-gnu-gcc -print-sysroot)"

    cp ${SYSROOT}/lib/ld-linux-aarch64.so.1 ${OUTDIR}/rootfs/lib 

    cp ${SYSROOT}/lib64/libm.so.6 ${OUTDIR}/rootfs/lib64
    cp ${SYSROOT}/lib64/libresolv.so.2 ${OUTDIR}/rootfs/lib64
    cp ${SYSROOT}/lib64/libc.so.6 ${OUTDIR}/rootfs/lib64
#-----------------------------------------------

# TODO: Make device nodes

#------------------------------------------
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
    if [ -f "${OUTDIR}/rootfs/dev/console" ]
    then
        :
    else
        sudo rm -f "${OUTDIR}/rootfs/dev/console"
        sudo mknod -m 600 ${OUTDIR}/rootfs/dev/console c 5 1
    fi
    #-----------------------------------------------

# TODO: Clean and build the writer utility

 #-----------------------------------------------
    echo "Clean and build the writer utility"

    cd ${FINDER_APP_DIR}

    make clean
    make CROSS_COMPILE=aarch64-none-linux-gnu-
 #-----------------------------------------------


# TODO: Copy the finder related scripts and executables to the /home directory
# on the target rootfs

#-----------------------------------------------
    echo "Copy Finder files"

    cp writer ${OUTDIR}/rootfs/home

    cp finder.sh ${OUTDIR}/rootfs/home

    mkdir ${OUTDIR}/rootfs/home/conf
    cp ../conf/username.txt ${OUTDIR}/rootfs/home/conf
    cp ../conf/assignment.txt ${OUTDIR}/rootfs/home/conf

    cp autorun-qemu.sh ${OUTDIR}/rootfs/home

    cp finder-test.sh ${OUTDIR}/rootfs/home
#-----------------------------------------------

# TODO: Chown the root directory

#-----------------------------------------------
echo "Chown the root directory"

cd "$OUTDIR/rootfs"
sudo chown -R root:root *
#-----------------------------------------------

# TODO: Create initramfs.cpio.gz

#-----------------------------------------------
    #To use the rootfs with the target
    #QEMU emulation environment acts as bootloader in this case
    cd "$OUTDIR/rootfs" 
    find . | cpio -H newc -ov --owner root:root > ${OUTDIR}/initramfs.cpio

    cd "$OUTDIR"
    gzip -f initramfs.cpio

    echo "Complete!"
    exit 0
#-----------------------------------------------


