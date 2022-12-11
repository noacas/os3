#include "message_slot.h"

#include <fcntl.h>      /* open */
#include <unistd.h>     /* exit */
#include <sys/ioctl.h>  /* ioctl */
#include <stdio.h>
#include <stdlib.h>
#include <linux/ioctl.h>
#include <linux/types.h>
#include <string.h>

static char* INVALID_INPUT_ERROR_MESSAGE = "usage: message_reader <file> <channel_id>";

int main(int argc, char *argv[])
{
    static char the_message[MAX_MESSAGE_LENGTH];
    int ret_val, file_desc;
    unsigned long int channel_id;

    if (argc != 3) {
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

    ret_val = read(  file_desc, &the_message, MAX_MESSAGE_LENGTH );
    if (ret_val >= 0) {
        if (write(STDOUT_FILENO, the_message, ret_val) != ret_val) {
            perror("Error writing message to stdout: ");
            exit(1);
        }
    }
    else {
        perror("Error reading from channel: ");
        exit(1);
    }

    close(file_desc);
    return 0;
}
