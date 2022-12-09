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
#include <linux/slab.h>

MODULE_LICENSE("GPL");


//Our custom definitions of IOCTL operations
#include "message_slot.h"

struct channel {
    unsigned int channel_id;
    char* message;
    ssize_t length;
    struct list_head channel_list ;
};

struct device {
    unsigned long int device_minor;
    struct list_head channel_list_head;
    struct list_head device_list ;
};

struct channel *get_channel_from_device_ptr(unsigned int channel_id, struct device * d);
struct device *get_device(unsigned long int device_minor);
struct channel *get_channel(unsigned int channel_id, unsigned long int device_minor);
void delete_device(unsigned long int device_minor);
void delete_device_from_ptr(struct device *d);
void delete_all_channels(struct list_head channel_list_head);
void delete_all_devices(void);
int create_device(unsigned long int device_minor);

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
    return get_channel_from_device_ptr(channel_id, d);
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
        list_del(&entry->channel_list);
        // removing message from memory
        kfree(entry->message);
        // removing channel struct from memory
        kfree(entry);
    }
}

int create_device(unsigned long int device_minor) {
    // if device already exists no need for that
    struct device *d = get_device(device_minor);
    if (d != NULL) {
        return SUCCESS;
    }
    // TODO: check if device* or simply device is the correct one
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
    // TODO: check if channel* or simply channel is the correct one
    struct channel* c = (struct channel *)kmalloc(sizeof(struct channel), GFP_KERNEL);
    if (c == NULL) {
        return NULL;
    }
    c->channel_id = channel_id;
    list_add(&c->channel_list, &d->channel_list_head); // add channel to channel list
    return c;
}

void delete_all_devices(void) {
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
    unsigned long int minor;
    int status;
    minor = iminor(inode);
    status = create_device(minor);
    if (status == SUCCESS) {
        return SUCCESS;
    }
    return status;
}

//---------------------------------------------------------------
static int device_release( struct inode* inode,
                           struct file*  file)
{
    unsigned long int minor;
    // deleting all device channels
    minor = iminor(inode);
    delete_device(minor);
    return SUCCESS;
}

//---------------------------------------------------------------
// a process which has already opened
// the device file attempts to read from it
static ssize_t device_read( struct file* file,
                            char __user* buffer,
                            ssize_t       length,
                            loff_t*      offset )
{

    ssize_t i;
    unsigned int channel_id;
    unsigned long int device_minor;
    struct device *d;
    struct channel *c;

    if (file->private_data == NULL) {
        // no channel has been set on the file descriptor
        return -EINVAL;
    }

    channel_id = *(unsigned int *) file->private_data;

    if (channel_id == 0) {
        // no channel has been set on the file descriptor
        return -EINVAL;
    }

    device_minor = iminor(file->f_path.dentry->d_inode);
    d = get_device(device_minor);
    c = get_channel_from_device_ptr(channel_id, d);

    if (c == NULL || c->length != 0) {
        // no message in channel
        return -EWOULDBLOCK;
    }

    if (c->length > length) {
        // the buffer provided is too small
        return -ENOSPC;
    }

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
        ssize_t             length,
        loff_t*            offset)
{
    unsigned int channel_id;
    unsigned long int device_minor;
    struct device *d;
    struct channel *c;
    ssize_t i;

    if (file->private_data == NULL) {
        // no channel has been set on the file descriptor
        return -EINVAL;
    }

    channel_id = *(unsigned int *) file->private_data;

    if (channel_id == 0) {
        // no channel has been set on the file descriptor
        return -EINVAL;
    }
    if (length <= 0 || length > MAX_MESSAGE_LENGTH) {
        // max message size
        return -EMSGSIZE;
    }

    device_minor = iminor(file->f_path.dentry->d_inode);
    d = get_device(device_minor);
    c = get_channel_from_device_ptr(channel_id, d);

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

    for( i = 0; i < length; ++i ) {
        get_user(c->message[i], &buffer[i]);
    }

    c->length = length;
    // return the number of input characters used
    return length;
}

//----------------------------------------------------------------
static long device_ioctl(struct file* file, unsigned int ioctl_command_id, unsigned long ioctl_param) {
    // Switch channel id according to the ioctl called
    if( MSG_SLOT_CHANNEL == ioctl_command_id && ioctl_param != 0 ) {
        // Get the parameter given to ioctl by the process
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
        .unlocked_ioctl = device_ioctl,
        .release        = device_release,
};

//---------------------------------------------------------------
// Initialize the module - Register the character device
static int __init simple_init(void)
{
    int rc = -1;

    // Register driver capabilities. Obtain major num
    rc = register_chrdev( MAJOR_NUM, DEVICE_RANGE_NAME, &Fops );

    // Negative values signify an error
    if( rc < 0 ) {
        printk( KERN_ALERT "%s registraion failed for  %d\n",
                DEVICE_FILE_NAME, MAJOR_NUM );
        return rc;
    }

    // init device list head
    INIT_LIST_HEAD(&device_list_head);

    return 0;
}

//---------------------------------------------------------------
static void __exit simple_cleanup(void)
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
