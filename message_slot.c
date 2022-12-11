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
    unsigned long int channel_id;
    char* message;
    ssize_t length;
    struct list_head channel_list ;
};

struct message_slot {
    unsigned long int device_minor;
    struct list_head channel_list_head;
    struct list_head message_slot_list ;
};

struct file_data {
    struct channel *current_channel;
    struct message_slot *message_slot;
};

struct channel *get_channel_from_message_slot_ptr(unsigned long int channel_id, struct message_slot *message_slot);
void delete_message_slot_from_ptr(struct message_slot *message_slot);
void delete_all_channels(struct list_head channel_list_head);
void delete_all_message_slots(void);
int create_message_slot(unsigned long int device_minor, struct file *file);
struct message_slot *get_message_slot(unsigned long int device_minor);

static struct list_head message_slot_list_head;

//================== HELPER FUNCTIONS ===========================

struct channel *get_channel_from_message_slot_ptr(unsigned long int channel_id, struct message_slot *message_slot) {
    struct channel  *entry = NULL;
    list_for_each_entry ( entry , & message_slot->channel_list_head, channel_list )
    {
        if (entry->channel_id == channel_id)
            return entry;
    }
    printk("could not find channel %lu from message_slot ptr %p\n", channel_id, message_slot);
    return NULL;
}

void delete_message_slot_from_ptr(struct message_slot *m) {
    printk("delete all message_slot's channels\n");
    delete_all_channels(m->channel_list_head);
    printk("delete message_slot from message_slot list\n");
    list_del(&m->message_slot_list);
    printk("delete message_slot struct from memory\n");
    kfree(m);
}

void delete_all_channels(struct list_head channel_list_head) {
    struct channel  *entry, *temp = NULL ;
    list_for_each_entry_safe ( entry , temp, &channel_list_head, channel_list )
    {
        // removing message from memory
        if (entry->length > 0) {
            kfree(entry->message);
        }
        // removing channel from list
        list_del(&entry->channel_list);
        // removing channel struct from memory
        kfree(entry);
    }
}

struct message_slot *get_message_slot(unsigned long int device_minor) {
    struct message_slot  *entry = NULL;
    list_for_each_entry ( entry , & message_slot_list_head, message_slot_list )
    {
        if (entry->device_minor == device_minor)
            return entry;
    }
    printk("could not find message_slot %ld\n", device_minor);
    return NULL;
}

int create_message_slot(unsigned long int device_minor, struct file *file) {
    struct file_data* file_data;
    printk("creating message_slot for minor %lu\n", device_minor);
    // if message_slot already exists no need for that
    struct message_slot *m = get_message_slot(device_minor);
    if (m == NULL) {
        m = (struct message_slot *) kmalloc(sizeof(struct message_slot), GFP_KERNEL);
        if (m == NULL) {
            printk("failed allocating memory to create message_slot\n");
            return -ENOMEM;
        }
    }
    m->device_minor = device_minor;
    INIT_LIST_HEAD(&m->channel_list_head); // init channel list
    list_add(&m->message_slot_list, &message_slot_list_head); // add message_slot to message_slot list
    printk("created message_slot for minor %lu successfully\n", device_minor);

    printk("creating file_data for new file\n");
    file_data = (struct file_data*) kmalloc(sizeof(struct file_data), GFP_KERNEL);
    file_data->message_slot=m;
    file_data->current_channel=NULL;
    file->private_data = (void*)file_data;
    return SUCCESS;
}

struct channel* create_channel(unsigned long int channel_id, struct message_slot *m) {
    struct channel* c = (struct channel *)kmalloc(sizeof(struct channel), GFP_KERNEL);
    if (c == NULL) {
        printk("failed allocating memory to create channel\n");
        return NULL;
    }
    c->channel_id = channel_id;
    c->message = 0;
    list_add(&c->channel_list, &m->channel_list_head); // add channel to channel list
    printk("created channel for channel id %lu for message_slot ptr %p successfully\n", channel_id, m);
    return c;
}

void delete_all_message_slots(void) {
    struct message_slot *entry, *temp = NULL ;
    printk("starting to delete all message_slots\n");
    list_for_each_entry_safe ( entry , temp, &message_slot_list_head, message_slot_list )
    {
        delete_message_slot_from_ptr(entry);
    }
    printk("finished deleting all message slots\n");
}


//================== DEVICE FUNCTIONS ===========================
static int device_open( struct inode* inode,
                        struct file*  file )
{
    unsigned long int minor;
    int status;
    minor = iminor(inode);
    printk("opening message_slot for minor %lu\n", minor);
    status = create_message_slot(minor, file);
    if (status == SUCCESS) {
        printk("opened device for minor %lu successfully\n", minor);
        return SUCCESS;
    }
    printk("failed opening device for minor %lu\n", minor);
    return status;
}

//---------------------------------------------------------------
static int device_release( struct inode* inode, struct file*  file) {
    unsigned long int minor;
    minor = iminor(inode);
    printk("realising device for minor %lu\n", minor);
    kfree(file->private_data);
    printk("realised device for minor %lu\n", minor);
    return SUCCESS;
}

//---------------------------------------------------------------
// a process which has already opened
// the device file attempts to read from it
static ssize_t device_read( struct file* file, char __user* buffer, size_t length, loff_t* offset ) {
    ssize_t i;
    struct channel *c;
    struct file_data *file_data;
    unsigned long int channel_id, device_minor;

    printk("trying to read from message_slot\n");

    if (file->private_data == NULL) {
        // no message_slot has been set on the file descriptor
        printk("no message_slot has been set on the file descriptor\n");
        return -EINVAL;
    }

    file_data = (struct file_data*) file->private_data;

    if (file_data == NULL || file_data->current_channel == NULL || file_data->current_channel->channel_id == 0) {
        // no channel has been set on the file descriptor
        printk("no channel has been set on the file descriptor\n");
        return -EINVAL;
    }

    c = file_data->current_channel;
    channel_id = c->channel_id;
    device_minor = file_data->message_slot->device_minor;

    printk("reading from message_slot with minor %lu for channel %lu\n", device_minor, channel_id);

    if (c->length != 0) {
        // no message in channel
        printk("no message in channel for message_slot with minor %lu channel %lu\n", device_minor, channel_id);
        return -EWOULDBLOCK;
    }

    if (c->length > length) {
        // the buffer provided is too small
        printk("the buffer provided is too small for device minor %lu channel %lu\n", device_minor, channel_id);
        return -ENOSPC;
    }

    printk("writing message to buffer\n");
    for( i = 0; i < c->length; ++i ) {
        if (put_user(c->message[i], &buffer[i]) != 0) {
            printk("failed writing message to buffer\n");
            return -EIO;
        }
    }

    printk("read message of length %ld for message_slot with minor %lu channel %lu\n", c->length, device_minor, channel_id);

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
    unsigned long int channel_id;
    unsigned long int device_minor;
    struct channel *c;
    struct file_data *file_data;
    ssize_t i;
    char temp_buffer[MAX_MESSAGE_LENGTH];

    printk("trying to write to device\n");

    if (file->private_data == NULL) {
        // no message_slot has been set on the file descriptor
        printk("no message_slot has been set on the file descriptor\n");
        return -EINVAL;
    }

    file_data = (struct file_data*) file->private_data;

    if (file_data->current_channel == NULL || file_data->message_slot) {
        // no message_slot has been set on the file descriptor
        printk("no message_slot has been set on the file descriptor\n");
        return -EINVAL;
    }

    c = file_data->current_channel;
    channel_id = c->channel_id;
    device_minor = file_data->message_slot->device_minor;

    if (channel_id == 0) {
        // no channel has been set on the file descriptor
        printk("no channel has been set on the file descriptor\n");
        return -EINVAL;
    }
    if (length <= 0 || length > MAX_MESSAGE_LENGTH) {
        // max message size
        printk("max message size\n");
        return -EMSGSIZE;
    }
    if (buffer == NULL) {
        // buffer is empty
        printk("buffer is empty\n");
        return -EINVAL;
    }

    // delete previous message
    if (c->length != 0) {
        printk("delete previous message\n");
        c->length = 0;
        kfree(c->message);
    }

    printk("reading message from buffer\n");
    for( i = 0; i < length; ++i ) {
        if (get_user(temp_buffer[i], &buffer[i]) != 0) {
            printk("failed reading message from buffer\n");
            return -EIO;
        }
    }

    c->message = (char *)kmalloc(sizeof(char) * (length), GFP_KERNEL);
    if (c->message == NULL) {
        printk("failed allocating memory for message\n");
        return -ENOMEM;
    }

    printk("writing message from temp buffer to message slot\n");
    for( i = 0; i < length; ++i ) {
        c->message[i] = temp_buffer[i];
    }

    c->length = length;
    printk("wrote to device message of length %ld\n", length);
    // return the number of input characters used
    return length;
}

//----------------------------------------------------------------
static long device_ioctl(struct file* file, unsigned int ioctl_command_id, unsigned long ioctl_param) {
    struct message_slot *m;
    struct channel *c;
    struct file_data *file_data;
    unsigned long int channel_id;
    // Switch channel according to the ioctl called
    printk("ioctl was invoked\n");
    if( MSG_SLOT_CHANNEL != ioctl_command_id || ioctl_param == 0 ) {
        printk("failed in ioctl for incorrect input\n");
        return -EINVAL;
    }

    // Get the parameter given to ioctl by the process
    channel_id = ioctl_param;
    file_data = (struct file_data*) file->private_data;
    if (file_data == NULL || file_data->message_slot == NULL) {
        printk("file_data is not set for file descriptor\n");
        return -EINVAL;
    }
    if (file_data->current_channel == NULL || file_data->current_channel->channel_id != channel_id) {
        m = file_data->message_slot;
        c = get_channel_from_message_slot_ptr(channel_id, m);
        if (c == NULL) {
            printk("no channel has been created on this message_slot for this channel %lu\n", channel_id);
            c = create_channel(channel_id, m);
            if (c == NULL) {
                printk("failed to create channel for this message_slot for this channel %lu\n", channel_id);
                return -ENOMEM;
            }
        }
        file_data->current_channel=c;
    }
    printk("ioctl was invoked successfully\n");
    return SUCCESS;
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
        printk( KERN_ALERT "%s registration failed for %d\n",
                DEVICE_FILE_NAME, MAJOR_NUM );
        return rc;
    }

    printk("Registration is successful. ");

    // init message_slot list head
    INIT_LIST_HEAD(&message_slot_list_head);

    return 0;
}

//---------------------------------------------------------------
static void __exit simple_cleanup(void)
{
    // Unregister the device
    // Should always succeed
    unregister_chrdev(MAJOR_NUM, DEVICE_RANGE_NAME);
    printk("deleting all message_slots in cleanup. ");
    delete_all_message_slots();
    printk("finished deleting all message_slots in cleanup. ");
}

//---------------------------------------------------------------
module_init(simple_init);
module_exit(simple_cleanup);

//========================= END OF FILE =========================
