/**
 * @file aesdchar.c
 * @brief Functions and data related to the AESD char driver implementation
 *
 * Based on the implementation of the "scull" device driver, found in
 * Linux Device Drivers example code.
 *
 * @author Dan Walkes
 * @date 2019-10-22
 * @copyright Copyright (c) 2019
 *
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/printk.h>
#include <linux/types.h>
#include <linux/cdev.h>
#include <linux/fs.h> // file_operations
#include "aesdchar.h"
#include "aesd-circular-buffer.h"

#include <linux/string.h>

int aesd_major =   0; // use dynamic major
int aesd_minor =   0;

MODULE_AUTHOR("Chase O'Connell");
MODULE_LICENSE("Dual BSD/GPL");

struct aesd_dev aesd_device;

int aesd_open(struct inode *inode, struct file *filp)
{
    PDEBUG("open");

    struct aesd_dev *dev; //Device information

    //Reference: Added cast to line below while debugging with Copilot AI
    dev = (struct aesd_dev *)container_of(inode->i_cdev, struct aesd_dev, cdev);
    filp->private_data = dev;

    PDEBUG("Finished aesd_open()\n");

    return 0; //Success
}

int aesd_release(struct inode *inode, struct file *filp)
{
    PDEBUG("release");
    //Need to deallocate anything open() allocated in filp->private_data
    //If allocated in init_module, free in module_exit, not here.
    //So just return 0 here, complete.
    return 0;
}

ssize_t aesd_read(struct file *filp, char __user *buf, size_t count,
                loff_t *f_pos)
{
    ssize_t retval = 0;
    PDEBUG("read %zu bytes with offset %lld",count,*f_pos);
    PDEBUG("Starting aesd_read()\n");

    //Casting here to avoid "dereferencing 'void*' pointer" error
    struct aesd_dev *dev = (struct aesd_dev*)filp->private_data;
    struct aesd_buffer_entry* foundEntry;
    size_t entryOffset; //The found char is stored at this offset after calling the below function:

    mutex_lock(&dev->buffMutex);
    foundEntry = aesd_circular_buffer_find_entry_offset_for_fpos(dev->buffer, *f_pos, &entryOffset);
    mutex_unlock(&dev->buffMutex);

    //Reference: Copilot AI assistance in modification of my original code to now catch the case of 
    //count being larger than the number of available bytes.
    size_t bytesAvailable = foundEntry->size - entryOffset;
    size_t bytesToCopy = min(count, bytesAvailable);
    if (copy_to_user(buf, foundEntry->buffptr + entryOffset, bytesToCopy))
        return -EFAULT;

    retval = bytesToCopy;
    //Reference: Caught errors and modified line below while debugging with Copilot AI
    *f_pos += retval;

    PDEBUG("Finished aesd_read()\n");

    return retval;
}

ssize_t aesd_write(struct file *filp, const char __user *buf, size_t count,
                loff_t *f_pos)
{
    ssize_t retval = -ENOMEM;
    PDEBUG("write %zu bytes with offset %lld",count,*f_pos);
    PDEBUG("Starting aesd_write()\n");

    //Casting here to avoid "dereferencing 'void*' pointer" error
    struct aesd_dev *dev = (struct aesd_dev*)filp->private_data;
    int writeContainsNLFlag = 0;

    mutex_lock(&dev->buffMutex);

    if (dev->newEntryFlag) {
        dev->tempEntry = kmalloc(sizeof(struct aesd_buffer_entry), GFP_KERNEL);
        if (dev->tempEntry == NULL) {
            retval = -ENOMEM;
            goto out;
        }
        //Reference: Added the below 2 lines while debugging with Copilot AI
        dev->tempEntry->size = 0;
        dev->tempEntry->buffptr = NULL;

        //Reset tempEntry flag
        dev->newEntryFlag = 0;
    }

        
    //Need to resize tempEntry buffptr when partial writes occur
    char* entryAppendedPtr = krealloc(dev->tempEntry->buffptr, dev->tempEntry->size + count, GFP_KERNEL); //+1 for \0****
    if (entryAppendedPtr == NULL) {
        retval = -ENOMEM;
        goto out;
    }
    dev->tempEntry->buffptr = entryAppendedPtr;
    
    //Returns #bytes that could not be copied
    size_t numNotCopied = 0;
    numNotCopied = copy_from_user((void*)(dev->tempEntry->buffptr + dev->tempEntry->size), buf, count);
    if (numNotCopied != 0) {
        retval = -EFAULT;
        //Not all bytes written, may retry IN USER SPACE
    }
    else {
        retval = count - numNotCopied;

        //Then check for newline char
        for (size_t i = dev->tempEntry->size; i < dev->tempEntry->size + count; i++) {
            if (dev->tempEntry->buffptr[i] == '\n') {
                writeContainsNLFlag = 1;
            }
        }
    
        dev->tempEntry->size += count;

        char debugString[dev->tempEntry->size + 1];
        memcpy(debugString, dev->tempEntry->buffptr, dev->tempEntry->size);
        debugString[dev->tempEntry->size] = '\0';
        PDEBUG("TempEntry contents: %s\n", debugString);

        if (writeContainsNLFlag) {
            //Add entry to the circular buffer
            const char* overwrittenEntryBuff = aesd_circular_buffer_add_entry(dev->buffer, dev->tempEntry);

            //Should free the overwritten entry 
            if (overwrittenEntryBuff) { //Reference: Added check so kfree doesn't try to free 'NULL' when debugging with Copilot AI
                kfree(overwrittenEntryBuff);
            }

            //Since tempEntry contents get copied into the circular buffer, can now free tempEntry
            kfree(dev->tempEntry);
            dev->newEntryFlag = 1; 
            dev->tempEntry = NULL;

        }
    }

    PDEBUG("Finished aesd_write()\n");
     
    out:
        mutex_unlock(&dev->buffMutex);
        return retval;
}

struct file_operations aesd_fops = {
    .owner =    THIS_MODULE,
    .read =     aesd_read,
    .write =    aesd_write,
    .open =     aesd_open,
    .release =  aesd_release,
};

static int aesd_setup_cdev(struct aesd_dev *dev)
{
    int err, devno = MKDEV(aesd_major, aesd_minor);

    cdev_init(&dev->cdev, &aesd_fops);
    dev->cdev.owner = THIS_MODULE;
    dev->cdev.ops = &aesd_fops;
    err = cdev_add (&dev->cdev, devno, 1);
    if (err) {
        printk(KERN_ERR "Error %d adding aesd cdev", err);
    }
    return err;
}


int aesd_init_module(void)
{
    dev_t dev = 0;
    int result;
    result = alloc_chrdev_region(&dev, aesd_minor, 1,
            "aesdchar");
    aesd_major = MAJOR(dev);
    if (result < 0) {
        printk(KERN_WARNING "Can't get major %d\n", aesd_major);
        return result;
    }
    memset(&aesd_device,0,sizeof(struct aesd_dev));


    aesd_device.buffer = kmalloc(sizeof(struct aesd_circular_buffer), GFP_KERNEL);
    if (aesd_device.buffer == NULL) {
        result = -ENOMEM;
        goto out;
    }

    //Reference: Originally forgot to include the line below, caught while debugging with Copilot AI.
    aesd_circular_buffer_init(aesd_device.buffer);

    //Set to 1 so the first write starts with the newEntryFlag correctly
    aesd_device.newEntryFlag = 1; 
    aesd_device.tempEntry = NULL;

    mutex_init(&aesd_device.buffMutex);

    result = aesd_setup_cdev(&aesd_device);

    if( result ) {
        unregister_chrdev_region(dev, 1);
    }

    PDEBUG("Finished aesd_init_module()\n");

    out:
        return result;

}

void aesd_cleanup_module(void)
{
    dev_t devno = MKDEV(aesd_major, aesd_minor);

    cdev_del(&aesd_device.cdev);

    //Need to kfree all entries in the circular buffer
    for (int i = 0; i < AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED; i++) {
        kfree(aesd_device.buffer->entry[i].buffptr);
    }

    kfree(aesd_device.buffer);

    if (aesd_device.tempEntry) {
        kfree(aesd_device.tempEntry);
    }

    PDEBUG("Finished aesd_cleanup_module()\n");

    unregister_chrdev_region(devno, 1);
}



module_init(aesd_init_module);
module_exit(aesd_cleanup_module);
