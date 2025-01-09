#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <fcntl.h>
#include <syslog.h>
#include <signal.h>
#include <stdbool.h>
#include <arpa/inet.h>

#define SERVER_PORT "9000"
#define FILEPATH "/var/tmp/aesdsocketdata"
#define BUFFSIZE 100 * 1024

bool caught_signal = false;

static void signal_handler(int signal_number)
{
    int errno_saved = errno;

    switch (signal_number)
    {
    case SIGINT:
    case SIGTERM:
        caught_signal = true;
        break;
    default:
        break;
    }
    errno = errno_saved;
}

static int init_socket()
{
    int socketfd, reuseval = 1;
    struct addrinfo hints, *results, *record;

    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    if ((getaddrinfo(NULL, SERVER_PORT, &hints, &results)) != 0)
    {
        syslog(LOG_ERR, "Failed to translate client socket.");
        return -1;
    }

    printf("Client socket translated.\n");

    // loop through all the results and bind to the first we can
    for (record = results; record != NULL; record = record->ai_next)
    {
        if ((socketfd = socket(record->ai_family, record->ai_socktype, record->ai_protocol)) == -1)
        {
            syslog(LOG_ERR, "Error creating socket: %s.", strerror(errno));
            continue;
        }

        if (setsockopt(socketfd, SOL_SOCKET, SO_REUSEADDR, &reuseval, sizeof(int)) == -1)
        {
            syslog(LOG_ERR, "Error with setsockopt: %s.", strerror(errno));
            return -1;
        }

        if (bind(socketfd, record->ai_addr, record->ai_addrlen) == -1)
        {
            close(socketfd);
            syslog(LOG_ERR, "Error with bind: %s.", strerror(errno));
            continue;
        }

        break;
    }

    freeaddrinfo(results);

    if (record == NULL)
    {
        fprintf(stderr, "server: failed to bind \n");
        return -1;
    }

    return socketfd;
}

int main(int argc, char const *argv[])
{

    int socketfd, acceptedfd, datafd;
    char *buffer;
    char ipaddress[INET_ADDRSTRLEN];
    struct sockaddr cli_addr;
    socklen_t clilen;
    ssize_t bytes_received, bytes_written, bytes_sent;
    ssize_t total_bytes = 0;
    struct sigaction new_action;
    pid_t daemonpid;

    openlog(NULL, 0, LOG_USER);

    memset(&new_action, 0, sizeof(struct sigaction));
    new_action.sa_handler = signal_handler;
    if (sigaction(SIGTERM, &new_action, NULL) != 0)
    {
        printf("Error %d (%s) registering for SIGTERM", errno, strerror(errno));
    }
    if (sigaction(SIGINT, &new_action, NULL) != 0)
    {
        printf("Error %d (%s) registering for SIGINT", errno, strerror(errno));
    }

    buffer = (char *)calloc(BUFFSIZE, sizeof(char));
    if (buffer == NULL)
    {
        syslog(LOG_ERR, "Error allocating memory: %s.", strerror(errno));
    }

    datafd = open(FILEPATH, O_RDWR | O_APPEND | O_CREAT, 0644);
    if (datafd < 0)
    {
        syslog(LOG_ERR, "Error open file: %s.", strerror(errno));
        return -1;
    }

    if ((socketfd = init_socket()) == -1)
    {
        return -1;
    }

    if (argc >= 2 && argv[1] != NULL && strcmp(argv[1], "-d") == 0)
    {
        syslog(LOG_INFO, "Running program as daemon");

        daemonpid = daemon(0, 0);
        if (daemonpid == -1)
        {
            syslog(LOG_ERR, "Error running program as daemon: %s.", strerror(errno));
            return -1;
        }
    }

    if (listen(socketfd, 10) == -1)
    {
        syslog(LOG_ERR, "Error with listen: %s.", strerror(errno));
        return -1;
    }

    while (1)
    {
        clilen = sizeof cli_addr;
        acceptedfd = accept(socketfd, &cli_addr, &clilen);
        if (acceptedfd == -1)
        {
            if (caught_signal)
            {
                close(datafd);
                close(socketfd);
                close(acceptedfd);
                free(buffer);
                remove(FILEPATH);
                syslog(LOG_DEBUG, "Caught signal, exiting");
                break;
            }
            else
            {
                syslog(LOG_ERR, "Error accepting connection: %s.", strerror(errno));
                return -1;
            }
        }

        inet_ntop(AF_INET, cli_addr.sa_data, ipaddress, INET_ADDRSTRLEN);
        syslog(LOG_INFO, "Accepted connection from %s", ipaddress);

        while ((bytes_received = read(acceptedfd, buffer, BUFFSIZE - 1)) > 0)
        {
            if (bytes_received == -1)
            {
                syslog(LOG_ERR, "Error receiving data from client: %s.", strerror(errno));
                return -1;
            }

            if (buffer[bytes_received - 1] == '\n')
            {
                bytes_written = write(datafd, buffer, bytes_received);
                if (bytes_written == -1)
                {
                    syslog(LOG_ERR, "Error writing local file: %s.", strerror(errno));
                    return -1;
                }
                lseek(datafd, 0, SEEK_SET);
                memset(buffer, 0, BUFFSIZE);
                total_bytes += bytes_written;
                bytes_received = read(datafd, buffer, total_bytes);
                if (bytes_received == -1)
                {
                    syslog(LOG_ERR, "Error reading local file: %s.", strerror(errno));
                    return -1;
                }
                bytes_sent = write(acceptedfd, buffer, bytes_received);
                if (bytes_sent == -1)
                {
                    syslog(LOG_ERR, "Error reading file: %s.", strerror(errno));
                    return -1;
                }
                syslog(LOG_INFO, "Closed connection from %s", ipaddress);
                close(acceptedfd);
                break;
            }
        }
    }

    return 0;
}
