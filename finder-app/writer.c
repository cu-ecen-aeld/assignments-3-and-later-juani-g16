#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <syslog.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

int main(int argc, char *argv[])
{
    openlog(NULL, 0, LOG_USER);
    if (argc != 3)
    {
        syslog(LOG_ERR, "Invalid number of args: %d (must be 2)\n", argc);
        return 1;
    }

    int fileDesc;
    ssize_t bytes_written;
    const char *writefile = argv[1];
    const char *text = argv[2];
    fileDesc = creat(writefile, 0644);

    if (fileDesc == -1)
    {
        syslog(LOG_ERR, "Error creating/opening the file: %s\n", strerror(errno));
        return 1;
    }

    bytes_written = write(fileDesc, text, strlen(text));

    if (bytes_written == -1)
    {
        syslog(LOG_ERR, "Error writing to file: %s\n", strerror(errno));
        close(fileDesc);
        return 1;
    }

    if (close(fileDesc) == -1)
    {
        syslog(LOG_DEBUG, "Error closing file: %s\n", strerror(errno));
        return 1;
    }

    syslog(LOG_DEBUG, "Writing %s to %s", text, writefile);

    return 0;
}