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
#include "aesd_ioctl.h"
#include <linux/uaccess.h>

int aesd_major =   0; // use dynamic major
int aesd_minor =   0;

MODULE_AUTHOR("Chase O'Connell");
MODULE_LICENSE("Dual BSD/GPL");

struct aesd_dev aesd_device;

//Reference: Asked ChatGPT for a helper function as my circular buffer kept the same contents between runs
static bool bufferCleared = false;
void clear_circular_buffer(void)
{
    for (int i = 0; i < AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED; i++) {
        kfree(aesd_device.buffer->entry[i].buffptr);  // Free each buffer entry
        aesd_device.buffer->entry[i].buffptr = NULL;   // Null out the pointer
        aesd_device.buffer->entry[i].size = 0;         // Reset the size
    }
}


int aesd_open(struct inode *inode, struct file *filp)
{
    PDEBUG("open");

    struct aesd_dev *dev; //Device information

    //Reference: Added cast to line below while debugging with Copilot AI
    dev = (struct aesd_dev *)container_of(inode->i_cdev, struct aesd_dev, cdev);
    filp->private_data = dev;

    PDEBUG("Finished aesd_open()\n");

    if (!bufferCleared) {
        clear_circular_buffer();
        bufferCleared = true;
    }
    

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
    size_t entryOffset; //The found char is stored at this offset after calling find_entry_offset_for_fpos
    size_t numBytesCopied = 0;

    printk("Read: fpos at %lld\n", *f_pos);

    mutex_lock(&dev->buffMutex);

    //Reference: Used Copilot AI for debugging to determine why my original code passed natively but not in QEMU.
    //Identified differences in Busybox implementation and native implementation. Used for assistance in modification 
    //of my code to now loop through all necessary entries instead of handling one buffer entry read per function call. 
    while (numBytesCopied < count) {
        foundEntry = aesd_circular_buffer_find_entry_offset_for_fpos(dev->buffer, *f_pos, &entryOffset);
        if (!foundEntry || entryOffset >= foundEntry->size) {
            break;
        }
        size_t bytesAvailable = foundEntry->size - entryOffset;
        size_t bytesToCopy = min(count - numBytesCopied, bytesAvailable);
        if (copy_to_user(buf + numBytesCopied, foundEntry->buffptr + entryOffset, bytesToCopy)) {
            retval = -EFAULT;
            goto out;
        }
        numBytesCopied += bytesToCopy;
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
        //Reference: Added the below 2 lines while debugging with Copilot AI to reset tempEntry.
        dev->tempEntry->size = 0;
        dev->tempEntry->buffptr = NULL;

        //Reset tempEntry flag
        dev->newEntryFlag = 0;
    }

    //Need to resize tempEntry buffptr when partial writes occur
    char* entryAppendedPtr = krealloc(dev->tempEntry->buffptr, dev->tempEntry->size + count, GFP_KERNEL); 
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


loff_t aesd_llseek(struct file *filp, loff_t off, int whence) {

	struct aesd_dev *dev = filp->private_data;
	loff_t newpos;

    mutex_lock(&dev->buffMutex);

	switch(whence) {
	  case 0: /* SEEK_SET */
		newpos = off;
		break;

	  case 1: /* SEEK_CUR */
		newpos = filp->f_pos + off;
		break;

	  case 2: /* SEEK_END */

        //Need to obtain end pointer
        //Should be zero-referenced


        //Starting point at buffer->out_offs
        size_t tempFpos = 0;
        int entryOffset = dev->buffer->out_offs;
        int numEntriesChecked = 0; //Want to stop if all entries have been checked

        printk("Before loop of llseek");

        //Stops when all entries have been checked and tempFpos contains end
        while (numEntriesChecked < AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED) {

            tempFpos += dev->buffer->entry[entryOffset].size;

            numEntriesChecked++;
            entryOffset++;
            if (entryOffset >= AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED) {
                entryOffset = 0;
            }


            if(!dev->buffer->full && entryOffset == dev->buffer->in_offs) {
                tempFpos += dev->buffer->entry[entryOffset].size; //***** */
                break;
            }
        }

        //tempFpos now contains the size of the circular buffer / last byte count
        newpos = tempFpos + off;

        //Reference: Copilot AI for the check below - I missed originally
        if (newpos < 0 || newpos > tempFpos) {
            mutex_unlock(&dev->buffMutex);
            return -EINVAL; //EINVAL means invalid argument
        }


		break;

	  default: /* can't happen */
        mutex_unlock(&dev->buffMutex);
		return -EINVAL;
	}
	if (newpos < 0) {
        mutex_unlock(&dev->buffMutex);
        return -EINVAL;
    } 


    mutex_unlock(&dev->buffMutex);
	filp->f_pos = newpos;
	return newpos;

}


long aesd_ioctl(struct file *filp, unsigned int cmd, unsigned long arg) {


    int err = 0;
	int retval = 0;
    
	/*
	 * extract the type and number bitfields, and don't decode
	 * wrong cmds: return ENOTTY (inappropriate ioctl) before access_ok()
	 */
	if (_IOC_TYPE(cmd) != AESD_IOC_MAGIC) return -ENOTTY;
	if (_IOC_NR(cmd) > AESDCHAR_IOC_MAXNR) return -ENOTTY;

	/*
	 * the direction is a bitmask, and VERIFY_WRITE catches R/W
	 * transfers. `Type' is user-oriented, while
	 * access_ok is kernel-oriented, so the concept of "read" and
	 * "write" is reversed
	 */
	if (_IOC_DIR(cmd) & _IOC_READ)
		err = !access_ok((void __user *)arg, _IOC_SIZE(cmd));
	else if (_IOC_DIR(cmd) & _IOC_WRITE)
		err =  !access_ok((void __user *)arg, _IOC_SIZE(cmd));
	if (err) return -EFAULT;


    switch(cmd) {

        case AESDCHAR_IOCSEEKTO:
            
            struct aesd_seekto temp;

            int status = copy_from_user(&temp, (const void __user*)arg, sizeof(temp));
            if (status != 0) {
                return -EFAULT;
            }

            uint32_t write_cmd = temp.write_cmd;
            uint32_t write_cmd_offset = temp.write_cmd_offset;

            printk("Ioctl: write_cmd as %d\n", write_cmd);
            printk("Ioctl: write_cmd_offset as %d\n", write_cmd_offset);

            struct aesd_dev* dev = (struct aesd_dev*)filp->private_data;

            size_t tempFpos = 0;
            int entryOffset = dev->buffer->out_offs;
            int numEntriesChecked = 0;

            //Stops when tempFpos and entryOffset at the start of write_cmd
            while (numEntriesChecked < write_cmd) {
            
                
                if (entryOffset >= AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED) {
                    entryOffset = 0;
                }

                if(!dev->buffer->full && entryOffset == dev->buffer->in_offs) {
                    break;
                }

                //TempFpos acts as total size after the loop
                tempFpos += dev->buffer->entry[entryOffset].size;

                entryOffset++;
                numEntriesChecked++;

            }

            if (write_cmd_offset >= dev->buffer->entry[entryOffset].size) {
                return -EINVAL;
            }
            tempFpos += write_cmd_offset;
            filp->f_pos = tempFpos;

            printk("Ioctl: f_pos updated to %lld\n", filp->f_pos);

            break;

        default:
            return -ENOTTY;
    }

    return retval;
}

struct file_operations aesd_fops = {
    .owner =    THIS_MODULE,
    .llseek =   aesd_llseek,
    .read =     aesd_read,
    .write =    aesd_write,
    .unlocked_ioctl = aesd_ioctl,
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
