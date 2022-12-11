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

