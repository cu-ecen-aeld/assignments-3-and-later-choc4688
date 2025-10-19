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
    //Need to set flip->private_data with the aesd_dev device struct
    //Use inode->i_cdev with container_of to locate within aesd_dev

    struct aesd_dev *dev; //Device information

    //Reference: Debugging with Copilot AI, added cast to line below.
    dev = (struct aesd_dev *)container_of(inode->i_cdev, struct aesd_dev, cdev);
    filp->private_data = dev;
    
    // filp->private_data->newEntryFlag = 1;
    // filp->private_data->tempEntry = NULL;

    printk(KERN_DEBUG "Finished aesd_open()\n");

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
    /**
     * TODO: handle read
     */

    //Casting here to avoid "dereferencing 'void*' pointer" error
    struct aesd_dev *dev = (struct aesd_dev*)filp->private_data;


    //buf is the buffer to fill for the read from the userspace
    //Can't access buffer directly, have to use copy_to_user()

    //Count = max number of bytes to write to the buff
    //You may want / need to write less than this

    //fpos is a pointer to the read offset
    //References a location in the virtual device
    //In our case, the char_offset byte of the circular buffer linear content
        //Start the read at this offset
    //Update the pointer to point to the next offset after reading and sending off the current entry********

    //Look at the slide for all the diff return values / partial read handling

    PDEBUG("Starting aesd_read()\n");

    struct aesd_buffer_entry* foundEntry;
    size_t entryOffset; //The found char is stored at this offset after calling find_entry_offset_for_fpos
    size_t numBytesCopied = 0;

    mutex_lock(&dev->buffMutex);

    while (numBytesCopied < count) {


        foundEntry = aesd_circular_buffer_find_entry_offset_for_fpos(dev->buffer, *f_pos, &entryOffset);
        if (!foundEntry || entryOffset >= foundEntry->size) {
            break;
        }

        //Original:
        // //Returns #bytes that could not be copied
        // size_t numNotCopied = 0;
        // numNotCopied = copy_to_user(buf, foundEntry.buffptr, count); //*****Put full entry in buf or just the contents?
        // if (numNotCopied != 0) {
        //     retval = -EFAULT; 
        //     //Not all bytes written, may retry IN USER SPACE
        // }
        // retval = count - numNotCopied;

        //Reference: Copilot AI assistance in modification of the above code to catch the case of 
        //count being larger than the number of available bytes.
        size_t bytesAvailable = foundEntry->size - entryOffset;
        size_t bytesToCopy = min(count, bytesAvailable);
        if (copy_to_user(buf + numBytesCopied, foundEntry->buffptr + entryOffset, bytesToCopy)) {
            retval = -EFAULT;
            goto out;
        }
        retval += bytesToCopy;

        //Update the f_pos pointer to point to the next offset to read******** (Next entry???)
        //Reference: Copilot AI - Change in the line below, was originally updating the pointer incorrectly
        //Now updates the value pointed to.
        *f_pos += bytesToCopy;
    }

    retval = numBytesCopied;

    PDEBUG("Finished aesd_read()\n");

    out:
        mutex_unlock(&dev->buffMutex);
        return retval;
}

ssize_t aesd_write(struct file *filp, const char __user *buf, size_t count,
                loff_t *f_pos)
{
    ssize_t retval = -ENOMEM;
    PDEBUG("write %zu bytes with offset %lld",count,*f_pos);
    /**
     * TODO: handle write
     */
    

    PDEBUG("Starting aesd_write()\n");

    //Casting here to avoid "dereferencing 'void*' pointer" error
    struct aesd_dev *dev = (struct aesd_dev*)filp->private_data;

    int writeContainsNLFlag = 0;
    
    //buf is a user space pointer, can't be directly accessed by kernel code
    //WE ARE READING FROM BUF HERE, CONTAINS THE THING TO WRITE, write to the circBuffer in the struct here

    mutex_lock(&dev->buffMutex);

    if (dev->newEntryFlag) {
        //Need to malloc the tempEntry here
        dev->tempEntry = kmalloc(sizeof(struct aesd_buffer_entry), GFP_KERNEL);
        if (dev->tempEntry == NULL) {
            retval = -ENOMEM;
            goto out;
        }

        //(AI Debugging, added below 2 lines)***************************
        dev->tempEntry->size = 0;
        dev->tempEntry->buffptr = NULL;

        //Reset flag
        dev->newEntryFlag = 0;
    }

        
    //For the buffer contents within an entry
    char* entryAppendedPtr = krealloc(dev->tempEntry->buffptr, dev->tempEntry->size + count, GFP_KERNEL); //+1 for \0****
    if (entryAppendedPtr == NULL) {
        retval = -ENOMEM;
        goto out;
    }
    dev->tempEntry->buffptr = entryAppendedPtr; //Resizes tempEntry buffptr
    


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

        //What does memset do? Fills block of mem with specific byte val (good for clearing after allocating)

        if (writeContainsNLFlag) {
            //Add entry to the circular buffer
            const char* overwrittenEntryBuff = aesd_circular_buffer_add_entry(dev->buffer, dev->tempEntry);

            //Should free the overwritten entry 
            if (overwrittenEntryBuff) { //Reference: Debugging with Copilot AI, added check so kfree doesn't try to free 'NULL'
                kfree(overwrittenEntryBuff);
            }
            else {
                retval = -ENOMEM;
            }

            //Since tempEntry contents get copied into the circular buffer, can now free tempEntry
            kfree(dev->tempEntry);

            dev->newEntryFlag = 1; 
            dev->tempEntry = NULL;

        }
    }

    PDEBUG("Finished aesd_write()\n");
     
    //Retval possible values:
    //Retval == count, requested number of bytes written successfully.
    //Positive but smaller than count, partialy write, may retry from userspace
    //0, nothing written, may retry from userspace
    //Negative - error occurred (-ENOMEM, -EFAULT)

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

    /**
     * TODO: initialize the AESD specific portion of the device
     */

    //Initialize everything added into the aesd_dev struct
    //Includes the lock

    aesd_device.buffer = kmalloc(sizeof(struct aesd_circular_buffer), GFP_KERNEL);
    if (aesd_device.buffer == NULL) {
        result = -ENOMEM;
        goto out;
    }

    //Reference: Copilot AI Debugging. Originally forgot to include the line below.
    aesd_circular_buffer_init(aesd_device.buffer);

    aesd_device.newEntryFlag = 1; //So the first write starts with the newEntryFlag correctly
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

    /**
     * TODO: cleanup AESD specific poritions here as necessary
     */

    //Need to kfree all entries in the circular buffer

    for (int i = 0; i < AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED; i++) {

        kfree(aesd_device.buffer->entry[i].buffptr);
        // kfree(&aesd_device.buffer->entry[i]); //Is this correct...?********

    }

    kfree(aesd_device.buffer);

    if (aesd_device.tempEntry) {
        kfree(aesd_device.tempEntry);
    }

    //tempEntry after a write always is the same pointer as something now in the
    //buffer, so don't need a separate kfree for it.

    PDEBUG("Finished aesd_cleanup_module()\n");


    unregister_chrdev_region(devno, 1);
}



module_init(aesd_init_module);
module_exit(aesd_cleanup_module);
