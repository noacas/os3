#ifndef CHARDEV_H
#define CHARDEV_H

#include <linux/ioctl.h>
#include <linux/types.h>   /* for linked list */

// The major device number.
// We don't rely on dynamic registration
// any more. We want ioctls to know this
// number at compile time.
//#define MAJOR_NUM 235
#define MAJOR_NUM 235

// Set the channel of the device driver
#define MSG_SLOT_CHANNEL _IOW(MAJOR_NUM, 0, unsigned int)

#define DEVICE_RANGE_NAME "message_slot"
#define MAX_MESSAGE_LENGTH 128
#define DEVICE_FILE_NAME "slot"
#define SUCCESS 0
#define ERROR -1

#endif

struct channel {
    unsigned int channel_id;
    char* message;
    size_t length;
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

