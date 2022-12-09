// Declare what kind of code we want
// from the header files. Defining __KERNEL__
// and MODULE allows us to access kernel-level
// code not usually available to userspace programs.
#undef __KERNEL__
#define __KERNEL__
#undef MODULE
#define MODULE


#include <linux/kernel.h>   /* We're doing kernel work */
#include <linux/module.h>   /* Specifically, a module */
#include <linux/fs.h>       /* for register_chrdev */
#include <linux/uaccess.h>  /* for get_user and put_user */
#include <linux/string.h>   /* for memset. NOTE - not string.h!*/
#include <linux/errno.h>

MODULE_LICENSE("GPL");


//Our custom definitions of IOCTL operations
#include "message_slot.h"

static struct list_head device_list_head;

//================== HELPER FUNCTIONS ===========================

struct device *get_device(unsigned long int device_minor) {
    struct device  *entry = NULL;
    list_for_each_entry ( entry , & device_list_head, device_list )
    {
        if (entry->device_minor == device_minor)
            return entry;
    }
    return NULL;
}

struct channel *get_channel(unsigned int channel_id, unsigned long int device_minor) {
    struct device *d = get_device(device_minor);
    if (d == NULL) {
        return NULL;
    }
    return get_channel_from_device(channel_id, d);
}

struct channel *get_channel_from_device_ptr(unsigned int channel_id, struct device * d) {
    struct channel  *entry = NULL;
    list_for_each_entry ( entry , & d->channel_list_head, channel_list )
    {
        if (entry->channel_id == channel_id)
            return entry;
    }
    return NULL;
}

void delete_device(unsigned long int device_minor) {
    struct device *d = get_device(device_minor);
    delete_device_from_ptr(d);
}

void delete_device_from_ptr(struct device *d) {
    // delete all device's channels
    delete_all_channels(d->channel_list_head);
    // delete device from device list
    list_del(&d->device_list);
    // delete device struct from memory
    kfree(d);
}

void delete_all_channels(struct list_head channel_list_head) {
    struct channel  *entry, *temp = NULL ;
    list_for_each_entry_safe ( entry , temp, &channel_list_head, channel_list )
    {
        // removing channel from list
        list_del(&entry->channels_list);
        // removing message from memory
        kfree(entry->message);
        // removing channel struct from memory
        kfree(entry);
    }
    return NULL;
}

int create_device(unsigned long int device_minor) {
    // if device already exists no need for that
    struct device *d = get_device(device_minor);
    if (d != NULL) {
        return SUCCESS;
    }
    # TODO: check if device* or simply device is the correct one
    d = (struct device *)kmalloc(sizeof(struct device), GFP_KERNEL);
    if (d == NULL) {
        return -ENOMEM;
    }
    d->device_minor = device_minor;
    INIT_LIST_HEAD(&d->channel_list_head); // init channel list
    list_add(&d->device_list, &device_list_head); // add device to device list
    return SUCCESS;
}

struct channel* create_channel(unsigned int channel_id, struct device * d) {
    # TODO: check if channel* or simply channel is the correct one
    struct channel* c = (struct channel *)kmalloc(sizeof(struct channel), GFP_KERNEL);
    if (c == NULL) {
        return NULL;
    }
    c->channel_id = channel_id;
    list_add(&c->channel_list, &d->channel_list_head); // add channel to channel list
    return c;
}

void delete_all_devices() {
    struct device *entry, *temp = NULL ;
    list_for_each_entry_safe ( entry , temp, &device_list_head, device_list )
    {
        delete_device_from_ptr(entry);
    }
}


//================== DEVICE FUNCTIONS ===========================
static int device_open( struct inode* inode,
                        struct file*  file )
{
    printk("Invoking device_open(%p)\n", file);
    unsigned long int minor = iminor(inode);
    int status = create_device(minor);
    if (status == SUCCESS) {
        return SUCCESS;
    }
    return status;
}

//---------------------------------------------------------------
static int device_release( struct inode* inode,
                           struct file*  file)
{
    printk("Invoking device_release(%p,%p)\n", inode, file);
    // deleting all device channels
    unsigned long int minor = iminor(inode);
    delete_device(minor);
    return SUCCESS;
}

//---------------------------------------------------------------
// a process which has already opened
// the device file attempts to read from it
static ssize_t device_read( struct file* file,
                            char __user* buffer,
                            size_t       length,
                            loff_t*      offset )
{

    if (file->private_data == NULL) {
        // no channel has been set on the file descriptor
        return -EINVAL;
    }

    unsigned int channel_id = (unsigned int) file->private_data;

    if (channel_id == 0) {
        // no channel has been set on the file descriptor
        return -EINVAL;
    }

    unsigned long int device_minor = iminor(file->f_path.dentry->d_inode);
    struct device *d = get_device(device_minor);
    struct channel *c = get_channel_from_device_ptr(channel_id, d);

    if (c == NULL || c->length != 0) {
        // no message in channel
        return -EWOULDBLOCK;
    }

    if (c->length > length) {
        // the buffer provided is too small
        return -ENOSPC;
    }

    ssize_t i;
    for( i = 0; i < c->length; ++i ) {
        put_user(c->message[i], &buffer[i]);
    }

    // return the number of output characters used
    return c->length;
}

//---------------------------------------------------------------
// a process which has already opened
// the device file attempts to write to it
static ssize_t device_write( struct file*       file,
        const char __user* buffer,
        size_t             length,
        loff_t*            offset)
{
    if (file->private_data == NULL) {
        // no channel has been set on the file descriptor
        return -EINVAL;
    }

    unsigned int channel_id = (unsigned int) file->private_data;

    if (channel_id == 0) {
        // no channel has been set on the file descriptor
        return -EINVAL;
    }
    if (length <= 0 || length > MAX_MESSAGE_LENGTH) {
        // max message size
        return -EMSGSIZE;
    }

    unsigned long int device_minor = iminor(file->f_path.dentry->d_inode);
    struct device *d = get_device(device_minor);
    struct channel *c = get_channel_from_device_ptr(channel_id, d);

    if (c == NULL) {
        c = create_channel(channel_id, d);
        if (c == NULL) {
            return -ENOMEM;
        }
    }
    else {
        // delete previous message
        if (c->length != 0) {
            c->length = 0;
            kfree(c->message);
        }
    }

    ssize_t i;
    printk("Invoking device_write(%p,%ld)\n", file, length);
    for( i = 0; i < length; ++i ) {
        get_user(c->message[i], &buffer[i]);
    }

    c->length = length;
    // return the number of input characters used
    return length;
}

//----------------------------------------------------------------
static int device_ioctl( struct   file* file,
                          unsigned int   ioctl_command_id,
                          unsigned int  ioctl_param )
{
    // Switch channel id according to the ioctl called
    if( MSG_SLOT_CHANNEL == ioctl_command_id && ioctl_param != 0 ) {
        // Get the parameter given to ioctl by the process
        printk( "Invoking ioctl: setting channel id "
                "to %d\n", ioctl_param);
        file->private_data = (void *) &ioctl_param;
        return SUCCESS;
    }
    return -EINVAL;
}

//==================== DEVICE SETUP =============================

// This structure will hold the functions to be called
// when a process does something to the device we created
struct file_operations Fops = {
        .owner	  = THIS_MODULE,
        .read           = device_read,
        .write          = device_write,
        .open           = device_open,
        .ioctl = device_ioctl,
        .release        = device_release,
};

//---------------------------------------------------------------
// Initialize the module - Register the character device
static int __init simple_init(void)
{
    int rc = -1;
    // init dev struct
    memset( &device_info, 0, sizeof(struct chardev_info) );
    spin_lock_init( &device_info.lock );

    // Register driver capabilities. Obtain major num
    rc = register_chrdev( MAJOR_NUM, DEVICE_RANGE_NAME, &Fops );

    // Negative values signify an error
    if( rc < 0 ) {
        printk( KERN_ALERT "%s registraion failed for  %d\n",
                DEVICE_FILE_NAME, MAJOR_NUM );
        return rc;
    }

    printk( "Registeration is successful. ");
    printk( "If you want to talk to the device driver,\n" );
    printk( "you have to create a device file:\n" );
    printk( "mknod /dev/%s c %d 0\n", DEVICE_FILE_NAME, MAJOR_NUM );
    printk( "You can echo/cat to/from the device file.\n" );
    printk( "Dont forget to rm the device file and "
            "rmmod when you're done\n" );

    // init device list head
    INIT_LIST_HEAD(&device_list_head);

    return 0;
}

//---------------------------------------------------------------
static void __exit si           mple_cleanup(void)
{
    // Unregister the device
    // Should always succeed
    unregister_chrdev(MAJOR_NUM, DEVICE_RANGE_NAME);
    delete_all_devices();
}

//---------------------------------------------------------------
module_init(simple_init);
module_exit(simple_cleanup);

//========================= END OF FILE =========================