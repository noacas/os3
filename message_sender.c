#include "message_slot.h"

#include <fcntl.h>      /* open */
#include <unistd.h>     /* exit */
#include <sys/ioctl.h>  /* ioctl */
#include <stdio.h>
#include <stdlib.h>
#include <linux/ioctl.h>
#include <linux/types.h>
#include <string.h>

static char* INVALID_INPUT_ERROR_MESSAGE = "usage: message_sender <file> <channel_id> <message>";

int main(int argc, char *argv[])
{
    int ret_val, file_desc;
    unsigned long int channel_id;

    if (argc != 4) {
        write(STDERR_FILENO, INVALID_INPUT_ERROR_MESSAGE, strlen(INVALID_INPUT_ERROR_MESSAGE));
        exit(1);
    }

    file_desc = open(argv[1], O_RDWR );
    if( file_desc < 0 ) {
        perror("Error opening file: ");
        exit(1);
    }

    channel_id = atoi(argv[2]);

    ret_val = ioctl( file_desc, MSG_SLOT_CHANNEL, channel_id);
    if (ret_val < 0) {
        perror("Error changing channel: ");
        exit(1);
    }

    ret_val = write( file_desc, argv[3], strlen(argv[3]));
    if (ret_val < 0) {
        perror("Error writing to channel: ");
        exit(1);
    }

    close(file_desc);
    return 0;
}

